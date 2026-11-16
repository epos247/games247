//  SPDX-License-Identifier: MIT
//
//  games247
//  Ps2Bios.h
//
//  The BIOS for the PlayStation 2 core (LRPS2). LRPS2 looks in pcsx2/bios inside the bios
//  folder and uses any PS2 BIOS it finds there, whatever its name; without one it can't start.
//  PS2 BIOS files straight in the bios folder are copied there, so it doesn't matter which of
//  the two folders they're put in.
//

#ifndef GAMES247_PS2_BIOS_H
#define GAMES247_PS2_BIOS_H

#include <string>
#include <vector>

namespace Ps2Bios
{
    // True for LRPS2 (pcsx2_libretro).
    bool isPs2Core(const std::string& corePath);

    // <bios folder>/pcsx2/bios, where the core looks for the BIOS.
    std::string coreBiosDirectory(const std::string& biosDirectory);

    // Copies PS2 BIOS files from the bios folder to the core's folder when that has none.
    void identifyRoms(const std::string& biosDirectory);

    // What's missing, for messages: empty when the core's folder has a PS2 BIOS.
    std::vector<std::string> missingFiles(const std::string& biosDirectory);

    // Files in the FTP storage's bios folder that could be PS2 BIOS files and aren't in the
    // local folders yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles);
} // namespace Ps2Bios

#endif // GAMES247_PS2_BIOS_H
