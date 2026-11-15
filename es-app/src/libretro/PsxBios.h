//  SPDX-License-Identifier: MIT
//
//  games247
//  PsxBios.h
//
//  The BIOS for the PlayStation core (Beetle PSX). Beetle PSX looks in the bios folder for the
//  BIOS of the game's region: scph5501.bin (USA), scph5502.bin (Europe) or scph5500.bin
//  (Japan). Without it, it starts with the free OpenBIOS, which some games don't work with.
//  BIOS files under other names are recognised by their checksum and copied to those names;
//  another BIOS version of the same region is used when the preferred one isn't there.
//

#ifndef GAMES247_PSX_BIOS_H
#define GAMES247_PSX_BIOS_H

#include <string>
#include <vector>

namespace PsxBios
{
    constexpr char USA_FILE[] {"scph5501.bin"};
    constexpr char EUROPE_FILE[] {"scph5502.bin"};
    constexpr char JAPAN_FILE[] {"scph5500.bin"};

    // True for the Beetle PSX cores (mednafen_psx and mednafen_psx_hw).
    bool isPsxCore(const std::string& corePath);

    // The BIOS file a game needs, from the region in its file name ("(Europe)", "(USA)",
    // "(Japan)" and the like), or an empty string when the name doesn't say.
    std::string fileForGame(const std::string& romPath);

    // Copies BIOS files found under other names to the names Beetle PSX looks for. Files
    // already there are left alone.
    void identifyRoms(const std::string& biosDirectory);

    // The BIOS the game needs if it isn't in the folder. For a game of unknown region, all
    // three names when none of them is there.
    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath);

    // Files in the FTP storage's bios folder that could be PlayStation BIOS files and aren't
    // in the local folder yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                            const std::vector<std::string>& serverFiles);
} // namespace PsxBios

#endif // GAMES247_PSX_BIOS_H
