//  SPDX-License-Identifier: MIT
//
//  games247
//  CoreUpdates.h
//
//  Finds newer builds of the libretro cores games247 has downloaded. At startup the libretro
//  build server's list of cores (.index-extended: build date, checksum and file name of each
//  zip) is downloaded in the background. A core has an update when the server's zip has a
//  different checksum from the one it was installed from; for cores installed before this was
//  recorded, when the server's build is from a later day than the core file.
//
//  When FTP storage is on, its cores folder is checked too (see FtpCores.h). A core that's
//  there has an update when the file there has changed since it was installed, and a core
//  installed from the FTP storage is never replaced by the build server's.
//

#ifndef GAMES247_CORE_UPDATES_H
#define GAMES247_CORE_UPDATES_H

#include <string>

namespace CoreUpdates
{
    // Downloads the build server's list of cores, and checks the FTP storage's cores folder,
    // on a thread of its own, unless the "CoreUpdates" setting is off. Call once at startup.
    void startCheck();

    // Waits for the check's thread. Call before shutting down.
    void stop();

    // Whether the server has a newer build of a core in games247's core folder. False while
    // the list is still downloading, when it couldn't be downloaded, and for cores games247
    // didn't download (such as RetroArch's).
    bool hasUpdate(const std::string& coreName);

    // Records that the core in games247's core folder is the server's current build, so it
    // isn't offered again. Call after downloading it. ftpVersion is FtpCores::version() for a
    // core downloaded from the FTP storage, "" for one from the build server.
    void recordInstalled(const std::string& coreName, const std::string& ftpVersion = "");
} // namespace CoreUpdates

#endif // GAMES247_CORE_UPDATES_H
