//  SPDX-License-Identifier: MIT
//
//  games247
//  SaturnBios.cpp
//
//  The BIOS for the Sega Saturn core (Beetle Saturn), see SaturnBios.h.
//

#include "libretro/SaturnBios.h"

#include "Log.h"
#include "libretro/GameRegion.h"
#include "utils/FileSystemUtil.h"
#include "utils/MathUtil.h"
#include "utils/StringUtil.h"

namespace
{
    struct KnownBios {
        bool japan;
        const char* md5;
    };

    // Saturn BIOS dumps (from libretro's BIOS list). The first of each is the one Beetle
    // Saturn is meant to use.
    const KnownBios KNOWN_BIOSES[] {
        {false, "3240872c70984b6cbfda1586cab68dbe"}, // mpr-17933, USA and Europe (v1.00a)
        {true, "85ec9ca47d8f6807718151cbcca8b964"}, // sega_101, Japan (v1.01)
        {true, "af5828fdff51384f99b3c4926be27762"}, // sega_100, Japan (v1.00)
    };

    // The size of a Saturn BIOS.
    constexpr long BIOS_SIZE {524288};

    const char* fileFor(bool japan)
    {
        return japan ? SaturnBios::JAPAN_FILE : SaturnBios::WESTERN_FILE;
    }

    bool hasFile(const std::string& biosDirectory, bool japan)
    {
        return Utils::FileSystem::isRegularFile(biosDirectory + "/" + fileFor(japan));
    }

    // A name the FTP storage's BIOS file could have: mpr-17933.bin, sega_101.bin,
    // saturn_bios.bin, Sega Saturn BIOS (USA).bin...
    bool looksLikeSaturnBios(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        if (extension != ".bin" && extension != ".rom")
            return false;
        for (const char* part : {"saturn", "sega_10", "mpr-179", "mpr17"}) {
            if (lower.find(part) != std::string::npos)
                return true;
        }
        return false;
    }
} // namespace

namespace SaturnBios
{
    bool isSaturnCore(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("mednafen_saturn", 0) == 0;
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        if (hasFile(biosDirectory, false) && hasFile(biosDirectory, true))
            return;

        for (const bool japan : {false, true}) {
            if (hasFile(biosDirectory, japan))
                continue;
            // The preferred dump first.
            std::string source;
            size_t sourceIndex {sizeof(KNOWN_BIOSES) / sizeof(KNOWN_BIOSES[0])};
            for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
                if (!Utils::FileSystem::isRegularFile(path) ||
                    Utils::FileSystem::getFileSize(path) != BIOS_SIZE)
                    continue;
                const std::string md5 {Utils::Math::md5Hash(path, true)};
                for (size_t i {0}; i < sourceIndex; ++i) {
                    if (KNOWN_BIOSES[i].japan == japan && md5 == KNOWN_BIOSES[i].md5) {
                        source = path;
                        sourceIndex = i;
                        break;
                    }
                }
            }
            if (source.empty())
                continue;
            const std::string target {biosDirectory + "/" + fileFor(japan)};
            // copyFile() returns true when it fails.
            if (Utils::FileSystem::copyFile(source, target, false)) {
                LOG(LogError) << "SaturnBios: couldn't copy " << source << " to " << target;
            }
            else {
                LOG(LogInfo) << "SaturnBios: copied " << Utils::FileSystem::getFileName(source)
                             << " to " << fileFor(japan);
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath)
    {
        const std::vector<GameRegion> regions {
            regionsInFileName(Utils::FileSystem::getFileName(romPath))};
        if (regions.empty()) {
            if (hasFile(biosDirectory, false) || hasFile(biosDirectory, true))
                return {};
            return {WESTERN_FILE, JAPAN_FILE};
        }
        for (const GameRegion region : regions) {
            if (hasFile(biosDirectory, region == GameRegion::JAPAN))
                return {};
        }
        return {fileFor(regions.front() == GameRegion::JAPAN)};
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            // The file's own name: it may be in a folder inside the bios folder.
            const std::string name {Utils::FileSystem::getFileName(file)};
            if (looksLikeSaturnBios(name) &&
                !Utils::FileSystem::exists(biosDirectory + "/" + name))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace SaturnBios
