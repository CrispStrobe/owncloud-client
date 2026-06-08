/*
 * Copyright (C) 2026 CrispCloud Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Block-level delta sync upload: compute Adler-32 + SHA-256 block maps
 * and upload only changed blocks via the crispcloud_delta server app.
 *
 * Requires the crispcloud_delta ownCloud app to be installed.
 * Falls back to normal upload if the app is not detected.
 */

#pragma once

#include "propagateuploadcommon.h"

#include <QCryptographicHash>

namespace OCC {

Q_DECLARE_LOGGING_CATEGORY(lcPropagateUploadDelta)

struct BlockSignature {
    int blockIndex = 0;
    qint64 offset = 0;
    qint64 size = 0;
    quint32 weakHash = 0;
    QByteArray strongHash;
};

struct BlockMap {
    QString filePath;
    qint64 totalSize = 0;
    qint64 blockSize = 0;
    int blockCount = 0;
    QVector<BlockSignature> signatures;
    QString etag;
};

class PropagateUploadFileDelta : public PropagateUploadCommon
{
    Q_OBJECT

public:
    PropagateUploadFileDelta(OwncloudPropagator *propagator, const SyncFileItemPtr &item);

    void doStartUpload() override;

public slots:
    void abort(PropagatorJob::AbortType abortType) override;

private slots:
    void slotStatusCheckFinished();
    void slotBlockMapFetched();
    void slotBlockUploaded();
    void slotFinalizeFinished();

private:
    static quint32 adler32(const QByteArray &data);
    BlockMap computeLocalBlockMap(const QString &filePath, qint64 blockSize);
    static BlockMap parseServerBlockMap(const QByteArray &json);
    static QVector<int> findChangedBlocks(const BlockMap &local, const BlockMap &remote);
    void fallbackToNormalUpload();
    void uploadNextBlock();

    static constexpr qint64 DefaultBlockSize = 4 * 1024 * 1024;
    static constexpr qint64 MinDeltaSyncSize = 10 * 1024 * 1024;

    QString _deltaAppBase;
    BlockMap _localBlockMap;
    BlockMap _remoteBlockMap;
    QVector<int> _changedBlocks;
    int _currentBlockIndex = 0;
    bool _deltaAvailable = false;
};

} // namespace OCC
