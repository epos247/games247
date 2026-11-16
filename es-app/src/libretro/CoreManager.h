//  SPDX-License-Identifier: MIT
//
//  games247
//  CoreManager.h
//
//  Finds libretro cores for the built-in player, and downloads missing ones from the
//  official libretro build server (buildbot.libretro.com) so RetroArch isn't required.
//

#ifndef GAMES247_CORE_MANAGER_H
#define GAMES247_CORE_MANAGER_H

#include <string>

namespace CoreManager
{
    // If a raw (unexpanded) launch command from es_systems.xml runs a RetroArch core, for
    // example "%EMULATOR_RETROARCH% -L %CORE_RETROARCH%\vice_x64sc_libretro.dll %ROM%",
    // returns the core's base name ("vice_x64sc_libretro"). Otherwise returns "".
    std::string coreNameFromCommand(const std::string& command);

    // games247's own core folder: <app data>/cores.
    std::string coresDirectory();

    // The core's file name on this platform, e.g. "vice_x64sc_libretro.dll".
    std::string coreFileName(const std::string& coreName);

    // Path to an installed copy of the core, or "" if it isn't installed. Looks in games247's
    // core folder first, then (on macOS) in RetroArch's core folder.
    std::string findInstalledCore(const std::string& coreName);

    // Download address of the zipped core on the libretro build server, or "" if this platform
    // isn't supported.
    std::string downloadURL(const std::string& coreName);

    // Address of the build server's list of this platform's cores (.index-extended), or "".
    std::string indexURL();

    // Extracts a zip or 7z archive into directory (which is created if needed) with the
    // system's bsdtar. Returns the tool's exit code, 0 on success.
    int extractArchive(const std::string& archivePath, const std::string& directory);

    // Extracts a downloaded core zip into the core folder. It's unpacked into a folder of its
    // own first, so a core already there is only replaced by one that unpacked. Returns false
    // and sets error if the core file isn't there afterwards.
    bool installFromZip(const std::string& zipPath,
                        const std::string& coreName,
                        std::string& error);
} // namespace CoreManager

#endif // GAMES247_CORE_MANAGER_H
