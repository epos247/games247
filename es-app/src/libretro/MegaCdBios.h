//  SPDX-License-Identifier: MIT
//
//  games247
//  MegaCdBios.h
//
//  The BIOS for Mega-CD (Sega CD) games in Genesis Plus GX and PicoDrive. Both load the BIOS
//  of the game's region from the bios folder: bios_CD_U.bin (USA), bios_CD_E.bin (Europe)
//  or bios_CD_J.bin (Japan), and can't start a CD game without it. Cartridge games don't need
//  one. BIOS files under other names (us_scd1_9210.bin, Mega-CD BIOS (Europe).bin...) are
//  recognised by their checksum or their name and copied to those names.
//

#ifndef GAMES247_MEGA_CD_BIOS_H
#define GAMES247_MEGA_CD_BIOS_H

#include <string>
#include <vector>

namespace MegaCdBios
{
    // True for a Mega-CD game (a disc image, or a game in a Mega-CD folder) in Genesis Plus
    // GX or PicoDrive.
    bool isMegaCdGame(const std::string& corePath, const std::string& romPath);

    // Copies BIOS files found under other names to the names the cores look for. Files
    // already there are left alone.
    void identifyRoms(const std::string& biosDirectory);

    // The BIOS the game needs if it isn't in the folder. For a game of unknown region, all
    // three names when none is there.
    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath);

    // Files in the FTP storage's bios folder that could be Mega-CD BIOS files and aren't in
    // the local folder yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles);
} // namespace MegaCdBios

#endif // GAMES247_MEGA_CD_BIOS_H
