//  SPDX-License-Identifier: MIT
//
//  games247
//  XboxBios.h
//
//  The system files for the original Xbox core (xemu). xemu only starts with three files in
//  the xemu folder inside the bios folder: the MCPX boot ROM (mcpx_1.0.bin), the flash BIOS
//  (Complex_4627v1.03.bin) and a hard disk image (xbox_hdd.qcow2). Boot ROMs and BIOSes under
//  other names, or straight in the bios folder, are copied to those names. The hard disk image
//  can be xemu's copyright-free blank one (HDD_IMAGE_URL).
//

#ifndef GAMES247_XBOX_BIOS_H
#define GAMES247_XBOX_BIOS_H

#include <string>
#include <vector>

namespace XboxBios
{
    constexpr char MCPX_FILE[] {"mcpx_1.0.bin"};
    constexpr char BIOS_FILE[] {"Complex_4627v1.03.bin"};
    constexpr char HDD_FILE[] {"xbox_hdd.qcow2"};
    // xemu's blank, formatted hard disk image with no copyrighted content (a 68 KB zip).
    constexpr char HDD_IMAGE_URL[] {
        "https://github.com/xemu-project/xemu-hdd-image/releases/download/1.0/"
        "xbox_hdd.qcow2.zip"};

    // True for the xemu and ParaLLEl Xbox cores.
    bool isXboxCore(const std::string& corePath);

    // <bios folder>/xemu, where the core looks for its files.
    std::string xemuDirectory(const std::string& biosDirectory);

    // Copies boot ROMs, BIOSes and hard disk images found under other names, or in the bios
    // folder itself, to the names xemu looks for. Files already there are left alone.
    void identifyRoms(const std::string& biosDirectory);

    // The files of MCPX_FILE, BIOS_FILE and HDD_FILE that aren't in the xemu folder.
    std::vector<std::string> missingFiles(const std::string& biosDirectory);

    // Files in the FTP storage's bios folder that could be Xbox system files and aren't in
    // the local folders yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                            const std::vector<std::string>& serverFiles);
} // namespace XboxBios

#endif // GAMES247_XBOX_BIOS_H
