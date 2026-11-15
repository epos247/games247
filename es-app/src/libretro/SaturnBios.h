//  SPDX-License-Identifier: MIT
//
//  games247
//  SaturnBios.h
//
//  The BIOS for the Sega Saturn core (Beetle Saturn). Beetle Saturn loads the BIOS of the
//  game's region from the bios folder: mpr-17933.bin (USA and Europe) or sega_101.bin
//  (Japan), and can't start without it. BIOS files under other names are recognised by their
//  checksum and copied to those names.
//

#ifndef GAMES247_SATURN_BIOS_H
#define GAMES247_SATURN_BIOS_H

#include <string>
#include <vector>

namespace SaturnBios
{
    constexpr char WESTERN_FILE[] {"mpr-17933.bin"};
    constexpr char JAPAN_FILE[] {"sega_101.bin"};

    // True for Beetle Saturn (mednafen_saturn).
    bool isSaturnCore(const std::string& corePath);

    // Copies BIOS files found under other names to the names Beetle Saturn looks for. Files
    // already there are left alone.
    void identifyRoms(const std::string& biosDirectory);

    // The BIOS the game needs if it isn't in the folder. For a game of unknown region, both
    // names when neither is there.
    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath);

    // Files in the FTP storage's bios folder that could be Saturn BIOS files and aren't in the
    // local folder yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles);
} // namespace SaturnBios

#endif // GAMES247_SATURN_BIOS_H
