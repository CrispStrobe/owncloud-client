/*
 * Copyright (C) 2026 CrispCloud Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Block-level delta sync upload for ownCloud.
 */

#include "propagateuploaddelta.h"
#include "propagateuploadfile.h"
#include "propagateuploadtus.h"
#include "networkjobs.h"
#include "account.h"
#include "owncloudpropagator.h"
#include "common/syncjournaldb.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QBuffer>

namespace OCC {

Q_LOGGING_CATEGORY(lcPropagateUploadDelta, "nextcloud.sync.propagator.upload.delta", QtInfoMsg)

static constexpr quint32 AdlerMod = 65521;

PropagateUploadFileDelta::PropagateUploadFileDelta(OwncloudPropagator *propagator, const SyncFileItemPtr &item)
    : PropagateUploadCommon(propagator, item)
{
}

quint32 PropagateUploadFileDelta::adler32(const QByteArray &data)
{
    quint32 a = 1, b = 0;
    for (int i = 0; i < data.size(); ++i) {
        a = (a + static_cast<quint8>(data[i])) % AdlerMod;
        b = (b + a) % AdlerMod;
    }
    return (b << 16) | a;
}

BlockMap PropagateUploadFileDelta::computeLocalBlockMap(const QString &filePath, qint64 blockSize)
{
    BlockMap map;
    map.filePath = filePath;
    map.blockSize = blockSize;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcPropagateUploadDelta) << "Cannot open file:" << filePath;
        return map;
    }

    map.totalSize = file.size();
    map.blockCount = map.totalSize == 0
        ? 0
        : static_cast<int>((map.totalSize + blockSize - 1) / blockSize);

    for (int i = 0; i < map.blockCount; ++i) {
        qint64 offset = static_cast<qint64>(i) * blockSize;
        qint64 remaining = qMin(blockSize, map.totalSize - offset);
        QByteArray data = file.read(remaining);
        if (data.size() < remaining) break;

        BlockSignature sig;
        sig.blockIndex = i;
        sig.offset = offset;
        sig.size = data.size();
        sig.weakHash = adler32(data);

        QCryptographicHash sha256(QCryptographicHash::Sha256);
        sha256.addData(data);
        sig.strongHash = sha256.result().toHex();

        map.signatures.append(sig);
    }

    return map;
}

BlockMap PropagateUploadFileDelta::parseServerBlockMap(const QByteArray &json)
{
    BlockMap map;
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull() || !doc.isObject()) return map;

    QJsonObject obj = doc.object();
    map.filePath = obj[QStringLiteral("filePath")].toString();
    map.totalSize = obj[QStringLiteral("totalSize")].toVariant().toLongLong();
    map.blockSize = obj[QStringLiteral("blockSize")].toVariant().toLongLong();
    map.blockCount = obj[QStringLiteral("blockCount")].toInt();
    map.etag = obj[QStringLiteral("etag")].toString();

    QJsonArray sigs = obj[QStringLiteral("signatures")].toArray();
    for (const auto &val : sigs) {
        QJsonObject s = val.toObject();
        BlockSignature sig;
        sig.blockIndex = s[QStringLiteral("blockIndex")].toInt();
        sig.offset = s[QStringLiteral("offset")].toVariant().toLongLong();
        sig.size = s[QStringLiteral("size")].toVariant().toLongLong();
        sig.weakHash = static_cast<quint32>(s[QStringLiteral("weakHash")].toVariant().toULongLong());
        sig.strongHash = s[QStringLiteral("strongHash")].toString().toLatin1();
        map.signatures.append(sig);
    }

    return map;
}

QVector<int> PropagateUploadFileDelta::findChangedBlocks(const BlockMap &local, const BlockMap &remote)
{
    QHash<int, const BlockSignature *> remoteByIndex;
    for (const auto &sig : remote.signatures) {
        remoteByIndex[sig.blockIndex] = &sig;
    }

    QVector<int> changed;
    for (const auto &localSig : local.signatures) {
        auto it = remoteByIndex.find(localSig.blockIndex);
        if (it == remoteByIndex.end()
            || localSig.weakHash != (*it)->weakHash
            || localSig.strongHash != (*it)->strongHash) {
            changed.append(localSig.blockIndex);
        }
    }
    return changed;
}

void PropagateUploadFileDelta::doStartUpload()
{
    if (_fileToUpload._size < MinDeltaSyncSize) {
        qCInfo(lcPropagateUploadDelta) << "File too small for delta sync:" << _fileToUpload._size;
        fallbackToNormalUpload();
        return;
    }

    _deltaAppBase = QStringLiteral("/index.php/apps/crispcloud_delta");

    // Probe the server for the crispcloud_delta app
    auto url = propagator()->account()->url();
    auto statusPath = _deltaAppBase + QStringLiteral("/api/status");

    auto *job = new SimpleNetworkJob(propagator()->account().data(), url, statusPath,
        "GET", QNetworkRequest{}, this);
    connect(job, &SimpleNetworkJob::finishedSignal, this, &PropagateUploadFileDelta::slotStatusCheckFinished);
    job->start();
}

void PropagateUploadFileDelta::slotStatusCheckFinished()
{
    auto *job = qobject_cast<SimpleNetworkJob *>(sender());
    if (!job || job->reply()->error() != QNetworkReply::NoError) {
        qCInfo(lcPropagateUploadDelta) << "Delta sync app not available, falling back";
        fallbackToNormalUpload();
        return;
    }

    int httpCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) {
        fallbackToNormalUpload();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(job->reply()->readAll());
    if (doc.isNull() || doc.object()[QStringLiteral("app")].toString() != QStringLiteral("crispcloud_delta")) {
        fallbackToNormalUpload();
        return;
    }

    _deltaAvailable = true;
    qCInfo(lcPropagateUploadDelta) << "Delta sync app detected, fetching block map for" << _item->_file;

    // Fetch remote block map
    auto url = propagator()->account()->url();
    auto bmPath = _deltaAppBase + QStringLiteral("/api/blockmap/") + _item->_file;

    auto *bmJob = new SimpleNetworkJob(propagator()->account().data(), url, bmPath,
        "GET", QNetworkRequest{}, this);
    connect(bmJob, &SimpleNetworkJob::finishedSignal, this, &PropagateUploadFileDelta::slotBlockMapFetched);
    bmJob->start();
}

void PropagateUploadFileDelta::slotBlockMapFetched()
{
    auto *job = qobject_cast<SimpleNetworkJob *>(sender());
    if (!job) { fallbackToNormalUpload(); return; }

    int httpCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode == 404 || job->reply()->error() != QNetworkReply::NoError || httpCode != 200) {
        qCInfo(lcPropagateUploadDelta) << "Cannot fetch block map, HTTP" << httpCode;
        fallbackToNormalUpload();
        return;
    }

    _remoteBlockMap = parseServerBlockMap(job->reply()->readAll());
    if (_remoteBlockMap.blockCount == 0) { fallbackToNormalUpload(); return; }

    qint64 blockSize = _remoteBlockMap.blockSize > 0 ? _remoteBlockMap.blockSize : DefaultBlockSize;
    _localBlockMap = computeLocalBlockMap(_fileToUpload._path, blockSize);
    if (_localBlockMap.blockCount == 0) { fallbackToNormalUpload(); return; }

    _changedBlocks = findChangedBlocks(_localBlockMap, _remoteBlockMap);

    if (_changedBlocks.isEmpty()) {
        qCInfo(lcPropagateUploadDelta) << "File identical, skipping upload:" << _item->_file;
        finalize();
        return;
    }

    qint64 changedBytes = 0;
    for (int idx : _changedBlocks) {
        if (idx < _localBlockMap.signatures.size()) changedBytes += _localBlockMap.signatures[idx].size;
    }
    double savings = _localBlockMap.totalSize > 0
        ? (1.0 - static_cast<double>(changedBytes) / _localBlockMap.totalSize) * 100.0 : 0.0;

    qCInfo(lcPropagateUploadDelta) << "Delta sync:" << _changedBlocks.size()
                                   << "/" << _localBlockMap.blockCount << "blocks,"
                                   << changedBytes << "bytes (" << QString::number(savings, 'f', 1) << "% savings)";

    _currentBlockIndex = 0;
    uploadNextBlock();
}

void PropagateUploadFileDelta::uploadNextBlock()
{
    if (_currentBlockIndex >= _changedBlocks.size()) {
        // All blocks uploaded — finalize
        auto url = propagator()->account()->url();
        auto finalizePath = _deltaAppBase + QStringLiteral("/api/finalize/") + _item->_file;
        QNetworkRequest req;
        req.setRawHeader("OCS-APIREQUEST", "true");

        auto *finalizeJob = new SimpleNetworkJob(propagator()->account().data(), url, finalizePath,
            "POST", req, this);
        connect(finalizeJob, &SimpleNetworkJob::finishedSignal, this, &PropagateUploadFileDelta::slotFinalizeFinished);
        finalizeJob->start();
        return;
    }

    int blockIdx = _changedBlocks[_currentBlockIndex];
    if (blockIdx >= _localBlockMap.signatures.size()) { fallbackToNormalUpload(); return; }

    const BlockSignature &sig = _localBlockMap.signatures[blockIdx];

    QFile file(_fileToUpload._path);
    if (!file.open(QIODevice::ReadOnly)) { fallbackToNormalUpload(); return; }
    file.seek(sig.offset);
    QByteArray blockData = file.read(sig.size);
    file.close();
    if (blockData.size() != sig.size) { fallbackToNormalUpload(); return; }

    auto url = propagator()->account()->url();
    auto blockPath = _deltaAppBase + QStringLiteral("/api/blocks/") + _item->_file
        + QStringLiteral("?offset=") + QString::number(sig.offset)
        + QStringLiteral("&size=") + QString::number(sig.size);

    QNetworkRequest req;
    req.setRawHeader("Content-Type", "application/octet-stream");
    req.setRawHeader("OCS-APIREQUEST", "true");

    auto *putJob = new SimpleNetworkJob(propagator()->account().data(), url, blockPath,
        "POST", std::move(blockData), req, this);
    connect(putJob, &SimpleNetworkJob::finishedSignal, this, &PropagateUploadFileDelta::slotBlockUploaded);
    putJob->start();
}

void PropagateUploadFileDelta::slotBlockUploaded()
{
    auto *job = qobject_cast<SimpleNetworkJob *>(sender());
    if (!job || job->reply()->error() != QNetworkReply::NoError) {
        fallbackToNormalUpload();
        return;
    }
    int httpCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) { fallbackToNormalUpload(); return; }

    _currentBlockIndex++;
    uploadNextBlock();
}

void PropagateUploadFileDelta::slotFinalizeFinished()
{
    auto *job = qobject_cast<SimpleNetworkJob *>(sender());
    if (!job || job->reply()->error() != QNetworkReply::NoError) {
        fallbackToNormalUpload();
        return;
    }
    int httpCode = job->reply()->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) { fallbackToNormalUpload(); return; }

    qCInfo(lcPropagateUploadDelta) << "Delta sync completed for" << _item->_file;
    finalize();
}

void PropagateUploadFileDelta::fallbackToNormalUpload()
{
    qCInfo(lcPropagateUploadDelta) << "Falling back to normal upload for" << _item->_file;

    PropagateItemJob *job;
    if (propagator()->account()->capabilities().tusSupport().isValid()) {
        job = new PropagateUploadFileTUS(propagator(), _item);
    } else {
        job = new PropagateUploadFile(propagator(), _item);
    }
    job->start();
}

void PropagateUploadFileDelta::abort(PropagatorJob::AbortType abortType)
{
    abortNetworkJobs(abortType,
        [](AbstractNetworkJob *) { return true; });
}

} // namespace OCC
