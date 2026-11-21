//  SPDX-License-Identifier: MIT
//
//  games247
//  SwitchFiles.h
//
//  The keys and firmware for the Switch cores (Eden and Lichii). Each core reads them from
//  its own folder in the bios folder: bios/eden or bios/lichii, holding
//    keys/prod.keys (and title.keys)
//    nand/system/Contents/registered/<32 hex digits>.nca (the firmware)
//  When FTP storage is on, the same folder in the FTP storage's bios folder (bios/eden,
//  bios/lichii) is copied down before a game starts: files that aren't on this computer, or
//  are a different size, are downloaded. Nothing on this computer is deleted.
//

#ifndef GAMES247_SWITCH_FILES_H
#define GAMES247_SWITCH_FILES_H

#include "FtpStorage.h"

#include <atomic>
#include <string>
#include <vector>

namespace SwitchFiles
{
    // "eden" or "lichii" for the Switch cores, otherwise empty.
    std::string folderFor(const std::string& corePath);

    struct Download {
        std::string name;
        std::string url;
        std::string localPath;
        long long size;
    };

    // Lists the folder in the FTP storage's bios folder and returns the files to download
    // into biosDirectory/folder. No such folder on the server isn't an error: downloads is
    // empty. Runs on the calling thread; cancel stops it when set.
    bool plan(const FtpStorage::Account& account,
              const std::string& biosDirectory,
              const std::string& folder,
              std::vector<Download>& downloads,
              std::string& error,
              const std::atomic<bool>* cancel);
} // namespace SwitchFiles

#endif // GAMES247_SWITCH_FILES_H
