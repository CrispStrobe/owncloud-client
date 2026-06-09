# ownCloud Desktop Client — Delta Sync Fork

> **This is a fork of [owncloud/client](https://github.com/owncloud/client) that adds block-level delta sync.**
>
> When the [CrispCloud Delta Sync](https://github.com/CrispStrobe/crispcloud-delta-sync) server app is installed on your ownCloud 10, this client uploads only the 4 MB blocks that actually changed — instead of re-uploading entire files.
>
> **Branch:** `delta-sync` | **Status:** CI-verified on Linux, Windows, macOS | **[Download binaries](https://github.com/CrispStrobe/owncloud-client/releases/tag/delta-sync-latest)**

## Download

Pre-built binaries are available from the [Releases page](https://github.com/CrispStrobe/owncloud-client/releases/tag/delta-sync-latest):

- **Linux:** AppImage (x86_64)
- **Windows:** Installer/EXE (x86_64, MSVC 2022)
- **macOS:** DMG (ARM64)

These are built automatically from the `delta-sync` branch on every push.

## What's different from upstream

| Feature | Upstream | This fork |
|---------|----------|-----------|
| Large file upload | Full re-upload or TUS | Block-level delta (Adler-32 + SHA-256 per 4 MB block) |
| Settings toggle | N/A | General Settings > "Enable block-level delta sync for large files" |
| Activity display | Standard sync message | Appends "Delta sync: 2/125 blocks, 98.4% bandwidth saved" |
| Logging | N/A | Full category logging under `owncloud.sync.propagator.upload.delta` |
| Fallback | N/A | Graceful fallback to TUS or simple PUT if server app unavailable |
| File shrink | Re-upload corrupts tail | Finalize sends `?size=N`; server truncates to exact new size |

### Requirements

- **Server:** Install the [crispcloud_delta](https://github.com/CrispStrobe/crispcloud-delta-sync) app — PHP for ownCloud 10.11+, or the [Go sidecar](https://github.com/CrispStrobe/crispcloud-delta-sync/tree/main/ocis) for oCIS v5+
- **File size:** Delta sync activates for files >= 10 MB
- **Note:** ownCloud Infinite Scale (oCIS) requires the Go sidecar extension instead of the PHP app

### Files changed

- `src/libsync/propagateuploaddelta.h/.cpp` — new `PropagateUploadFileDelta` class
- `src/libsync/capabilities.h/.cpp` — `deltaSyncAvailable()` capability check
- `src/libsync/configfile.h/.cpp` — `deltaSyncEnabled()` settings toggle
- `src/libsync/owncloudpropagator.cpp` — integration into upload job creation
- `src/gui/generalsettings.ui/.cpp` — settings checkbox
- `src/gui/protocolitem.cpp` — activity display with savings info
- `test/testdeltasync.cpp` — 11 unit tests (Adler-32 correctness, block map construction, diff logic, JSON parsing)

---

[![Build Status](https://drone.owncloud.com/api/badges/owncloud/client/status.svg)](https://drone.owncloud.com/owncloud/client) [![Build Status](https://github.com/owncloud/client/workflows/ownCloud%20CI/badge.svg)](https://github.com/owncloud/client/actions)

## Introduction

The ownCloud Desktop Client is a tool to synchronize files from ownCloud Server
with your computer.

## Download

### Binary packages

- Refer to the download page https://owncloud.com/desktop-app/

### Source code

The ownCloud Desktop Client is developed in Git. Since Git makes it easy to
fork and improve the source code and to adapt it to your need, many copies
can be found on the Internet, in particular on GitHub. However, the
authoritative repository maintained by the developers is located at
https://github.com/owncloud/client.

### Building from source

This project uses CMake as build system. Please refer to the cmake documentation
for general instructions on how to use CMake: https://cmake.org/documentation/.

Further more as meta build system KDE Craft is used. Please refer to the KDE Craft
documentation for instructions on how to use Craft: https://community.kde.org/Craft.

For easy to use instructions on how to build the ownCloud Desktop Client please have 
a look at https://github.com/owncloud/ownbuild.

## Reporting issues and contributing

If you find any bugs or have any suggestion for improvement, please
file an issue at https://github.com/owncloud/client/issues. Do not
contact the authors directly by mail, as this increases the chance
of your report being lost.

If you created a patch, please submit a [Pull
Request](https://github.com/owncloud/client/pulls). For non-trivial
patches, we need you to sign the [Contributor
Agreement](https://owncloud.com/contribute/join-the-development/contributor-agreement/) before
we can accept your patch.


## Maintainers and Contributors

ownCloud Desktop Client is developed by the ownCloud community and [receives
patches from a variety of authors](https://github.com/owncloud/client/graphs/contributors).

Past maintainers:

- Markus Goetz <guruz@owncloud.com>
- Olivier Goffart <ogoffart@owncloud.com>
- Christian Kamm <mail@ckamm.de>
- Thomas Müller <thomas.mueller@owncloud.com>
- Klaas Freitag <freitag@owncloud.com>
- Daniel Molkentin <daniel@molkentin.de>
- Andreas Schneider <asn@cryptomilk.org>

## License

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    for more details.

