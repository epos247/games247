//  SPDX-License-Identifier: MIT
//
//  games247
//  NeoGeoBios.cpp
//
//  The BIOS for the Neo Geo core Geolith, see NeoGeoBios.h.
//

#include "libretro/NeoGeoBios.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

namespace
{
    constexpr char AES_FILE[] {"aes.zip"};
    constexpr char MVS_FILE[] {"neogeo.zip"};
    constexpr char CDZ_FILE[] {"neocdz.zip"};
    constexpr char CD_FILE[] {"neocd.zip"};

    const char* const ALL_FILES[] {AES_FILE, MVS_FILE, CDZ_FILE, CD_FILE};

    bool hasFile(const std::string& biosDirectory, const char* file)
    {
        return Utils::FileSystem::isRegularFile(biosDirectory + "/" + file);
    }

    // Geolith plays CD images in Neo Geo CD mode.
    bool isCdGame(const std::string& romPath)
    {
        const std::string extension {
            Utils::String::toLower(Utils::FileSystem::getExtension(romPath))};
        return extension == ".chd" || extension == ".cue";
    }

    // The zip a cartridge game needs with this system type: aes.zip for the default (the home
    // console), neogeo.zip for the MVS and the Universe BIOS.
    const char* cartridgeFile(const std::string& systemType)
    {
        return systemType == "mvs" || systemType == "uni" ? MVS_FILE : AES_FILE;
    }
} // namespace

namespace NeoGeoBios
{
    bool isGeolithCore(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("geolith", 0) == 0;
    }

    void identifyRoms(const std::string& biosDirectory, const std::string& romPath)
    {
        const std::string gameDirectory {Utils::FileSystem::getParent(romPath)};
        for (const std::string& directory :
             {gameDirectory, Utils::FileSystem::getParent(gameDirectory)}) {
            if (directory.empty() || !Utils::FileSystem::isDirectory(directory))
                continue;
            // Names are matched without regard to case: neogeo.zip may be NEOGEO.ZIP.
            for (const std::string& path : Utils::FileSystem::getDirContent(directory)) {
                if (!Utils::FileSystem::isRegularFile(path))
                    continue;
                const std::string name {
                    Utils::String::toLower(Utils::FileSystem::getFileName(path))};
                for (const char* file : ALL_FILES) {
                    if (name != file || hasFile(biosDirectory, file))
                        continue;
                    Utils::FileSystem::createDirectory(biosDirectory);
                    const std::string target {biosDirectory + "/" + file};
                    // copyFile() returns true when it fails.
                    if (Utils::FileSystem::copyFile(path, target, false)) {
                        LOG(LogError) << "NeoGeoBios: couldn't copy " << path << " to "
                                      << target;
                    }
                    else {
                        LOG(LogInfo) << "NeoGeoBios: copied " << path << " to the bios folder";
                    }
                }
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath,
                                          const std::string& systemType,
                                          const std::string& cdSystemType)
    {
        std::vector<std::string> missing;
        if (isCdGame(romPath)) {
            // Every CD system needs neocdz.zip; the front and top loaders also need neocd.zip.
            if ((cdSystemType == "cd_front" || cdSystemType == "cd_top") &&
                !hasFile(biosDirectory, CD_FILE))
                missing.emplace_back(CD_FILE);
            if (!hasFile(biosDirectory, CDZ_FILE))
                missing.emplace_back(CDZ_FILE);
            return missing;
        }
        if (hasFile(biosDirectory, cartridgeFile(systemType)))
            return {};
        // The Universe BIOS is only in neogeo.zip.
        if (systemType != "uni" && hasFile(biosDirectory, AES_FILE))
            return {};
        if (hasFile(biosDirectory, MVS_FILE))
            return {};
        if (systemType == "uni")
            return {MVS_FILE};
        return {cartridgeFile(systemType), systemType == "mvs" ? AES_FILE : MVS_FILE};
    }

    std::string systemTypeOverride(const std::string& biosDirectory,
                                   const std::string& romPath,
                                   const std::string& systemType)
    {
        if (isCdGame(romPath) || systemType == "uni" ||
            hasFile(biosDirectory, cartridgeFile(systemType)))
            return {};
        if (systemType == "mvs")
            return hasFile(biosDirectory, AES_FILE) ? "aes" : "";
        return hasFile(biosDirectory, MVS_FILE) ? "mvs" : "";
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(file))};
            for (const char* bios : ALL_FILES) {
                if (name == bios && !hasFile(biosDirectory, bios))
                    candidates.emplace_back(file);
            }
        }
        return candidates;
    }
} // namespace NeoGeoBios
