//  SPDX-License-Identifier: MIT
//
//  games247
//  Ps3Firmware.h
//
//  The PS3 system software for the PlayStation 3 core (RPCS3). The core needs Sony's
//  PS3UPDAT.PUP, which it installs into rpcs3/dev_flash inside the bios folder the first time
//  it starts (taking a minute or two). It looks for the file in rpcs3, in the bios folder
//  itself and in rpcs3/firmware, whatever the case of its name. Sony gives the file away on
//  its update server, so games247 can download it from there as well as from the FTP storage.
//
//  Games: a disc dump folder is loaded by its PS3_DISC.SFB and an installed game by its
//  PARAM.SFO (see contentPath()); ISOs and .pkg files are loaded as they are.
//

#ifndef GAMES247_PS3_FIRMWARE_H
#define GAMES247_PS3_FIRMWARE_H

#include <atomic>
#include <string>
#include <vector>

namespace Ps3Firmware
{
    constexpr const char* PUP_FILE {"PS3UPDAT.PUP"};

    // True for RPCS3 (rpcs3_libretro).
    bool isPs3Core(const std::string& corePath);

    // <bios folder>/rpcs3, where the core keeps the installed system software, games and saves.
    std::string rpcs3Directory(const std::string& biosDirectory);

    // Whether the core has installed the system software.
    bool isInstalled(const std::string& biosDirectory);

    // The PS3UPDAT.PUP the core will find, or "".
    std::string findPup(const std::string& biosDirectory);

    // What's missing, for messages: empty when the system software is installed or its
    // PS3UPDAT.PUP is there to be installed.
    std::vector<std::string> missingFiles(const std::string& biosDirectory);

    // True when the core will install the system software when it starts.
    bool needsInstall(const std::string& biosDirectory);

    // Files in the FTP storage's bios folder that could be the PS3 system software.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles);

    // Downloads PS3UPDAT.PUP from Sony's update server into the core's folder (about 200 MB).
    // Blocking, for a thread of its own: progress gets the bytes so far and the total, and
    // cancel stops it. Returns false and sets error if it didn't work.
    bool downloadFromSony(const std::string& biosDirectory,
                          std::atomic<long long>& downloaded,
                          std::atomic<long long>& total,
                          const std::atomic<bool>& cancel,
                          std::string& error);

    // For a game that's a folder: the file the core loads, PS3_DISC.SFB for a disc dump or
    // PARAM.SFO for an installed game. "" if the folder has neither.
    std::string contentPath(const std::string& folder);
} // namespace Ps3Firmware

#endif // GAMES247_PS3_FIRMWARE_H
