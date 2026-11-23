//  SPDX-License-Identifier: MIT
//
//  games247
//  NeoGeoBios.h
//
//  The BIOS for the Neo Geo core Geolith, which loads it from the bios folder and can't start
//  without it: aes.zip for the home console (its default), neogeo.zip for the arcade machine
//  (MVS) and the Universe BIOS, and neocdz.zip (plus neocd.zip for the front and top loaders)
//  for Neo Geo CD games. These are the same zips MAME and FinalBurn Neo use, so they're often
//  next to the games, from where they're copied to the bios folder.
//

#ifndef GAMES247_NEOGEO_BIOS_H
#define GAMES247_NEOGEO_BIOS_H

#include <string>
#include <vector>

namespace NeoGeoBios
{
    // True for Geolith.
    bool isGeolithCore(const std::string& corePath);

    // Copies the BIOS zips found in the game's folder (or the folder above it) to the bios
    // folder. Files already there are left alone.
    void identifyRoms(const std::string& biosDirectory, const std::string& romPath);

    // The BIOS the game needs that isn't in the folder. systemType and cdSystemType are the
    // core's geolith_system_type and geolith_cd_system_type options (empty for the defaults).
    // A cartridge game is happy with either aes.zip or neogeo.zip, since the system type can
    // be switched to the one that's there (see systemTypeOverride()).
    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath,
                                          const std::string& systemType,
                                          const std::string& cdSystemType);

    // The geolith_system_type to use instead of the chosen one when only the other BIOS is
    // there ("mvs" when there's neogeo.zip but no aes.zip, and the other way round), or empty.
    std::string systemTypeOverride(const std::string& biosDirectory,
                                   const std::string& romPath,
                                   const std::string& systemType);

    // The Neo Geo BIOS zips in the FTP storage's bios folder that aren't in the local folder.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles);
} // namespace NeoGeoBios

#endif // GAMES247_NEOGEO_BIOS_H
