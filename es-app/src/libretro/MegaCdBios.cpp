//  SPDX-License-Identifier: MIT
//
//  games247
//  MegaCdBios.cpp
//
//  The BIOS for Mega-CD games, see MegaCdBios.h.
//

#include "libretro/MegaCdBios.h"

#include "Log.h"
#include "libretro/GameRegion.h"
#include "utils/FileSystemUtil.h"
#include "utils/MathUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>

namespace
{
    struct RegionFile {
        GameRegion region;
        const char* file;
        // The dump libretro lists for this name.
        const char* md5;
    };

    const RegionFile FILES[] {
        {GameRegion::USA, "bios_CD_U.bin", "2efd74e3232ff260e371b99f84024f7f"},
        {GameRegion::EUROPE, "bios_CD_E.bin", "e66fa1dc5820d254611fdcdba0662372"},
        {GameRegion::JAPAN, "bios_CD_J.bin", "278a9397d192149e84e820ac621a8edd"},
    };

    // The size of a Mega-CD BIOS.
    constexpr long BIOS_SIZE {131072};

    const RegionFile& fileFor(GameRegion region)
    {
        for (const RegionFile& file : FILES) {
            if (file.region == region)
                return file;
        }
        return FILES[0];
    }

    bool hasFile(const std::string& biosDirectory, GameRegion region)
    {
        return Utils::FileSystem::isRegularFile(biosDirectory + "/" + fileFor(region).file);
    }

    // A name a Mega-CD BIOS could have: us_scd1_9210.bin, eu_mcd1_9210.bin, bios_CD_J.bin,
    // Sega CD BIOS (USA).bin, Mega-CD Model 2 (Europe).bin...
    bool looksLikeMegaCdBios(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        if (extension != ".bin" && extension != ".rom" && extension != ".md")
            return false;
        for (const char* part : {"scd", "mcd", "segacd", "sega cd", "sega-cd", "megacd",
                                 "mega cd", "mega-cd", "bios_cd"}) {
            if (lower.find(part) != std::string::npos)
                return true;
        }
        return false;
    }

    // The region a BIOS file's name gives, from words such as us, usa, (U), eu, europe, jp,
    // japan. Returns false when it doesn't say.
    bool regionInName(const std::string& fileName, GameRegion& region)
    {
        const std::string lower {Utils::String::toLower(Utils::FileSystem::getStem(fileName))};
        std::vector<std::string> words;
        std::string word;
        for (const char c : lower + " ") {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                word += c;
            }
            else if (!word.empty()) {
                words.emplace_back(word);
                word.clear();
            }
        }
        for (const std::string& w : words) {
            if (w == "u" || w == "us" || w == "usa" || w == "america")
                region = GameRegion::USA;
            else if (w == "e" || w == "eu" || w == "eur" || w == "europe")
                region = GameRegion::EUROPE;
            else if (w == "j" || w == "jp" || w == "jpn" || w == "japan" || w == "asia")
                region = GameRegion::JAPAN;
            else
                continue;
            return true;
        }
        return false;
    }
} // namespace

namespace MegaCdBios
{
    bool isMegaCdGame(const std::string& corePath, const std::string& romPath)
    {
        const std::string core {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        if (core.rfind("genesis_plus_gx", 0) != 0 && core.rfind("picodrive", 0) != 0)
            return false;
        const std::string folder {Utils::String::toLower(
            Utils::FileSystem::getFileName(Utils::FileSystem::getParent(romPath)))};
        if (folder == "megacd" || folder == "megacdjp" || folder == "segacd")
            return true;
        const std::string extension {
            Utils::String::toLower(Utils::FileSystem::getExtension(romPath))};
        for (const char* disc : {".cue", ".chd", ".iso", ".m3u", ".ccd", ".toc"}) {
            if (extension == disc)
                return true;
        }
        return false;
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        bool allThere {true};
        for (const RegionFile& file : FILES)
            allThere = allThere && hasFile(biosDirectory, file.region);
        if (allThere)
            return;

        // Each BIOS-sized file: its checksum and the region its name gives.
        struct Candidate {
            std::string path;
            std::string md5;
            bool hasRegion;
            GameRegion region;
        };
        std::vector<Candidate> candidates;
        for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
            if (!Utils::FileSystem::isRegularFile(path) ||
                Utils::FileSystem::getFileSize(path) != BIOS_SIZE)
                continue;
            Candidate candidate {path, Utils::Math::md5Hash(path, true), false,
                                 GameRegion::USA};
            const std::string name {Utils::FileSystem::getFileName(path)};
            candidate.hasRegion =
                looksLikeMegaCdBios(name) && regionInName(name, candidate.region);
            candidates.emplace_back(candidate);
        }

        for (const RegionFile& file : FILES) {
            if (hasFile(biosDirectory, file.region))
                continue;
            // The dump libretro lists first, then any other version named for the region.
            std::string source;
            for (const Candidate& candidate : candidates) {
                if (candidate.md5 == file.md5) {
                    source = candidate.path;
                    break;
                }
            }
            for (const Candidate& candidate : candidates) {
                if (!source.empty())
                    break;
                if (candidate.hasRegion && candidate.region == file.region)
                    source = candidate.path;
            }
            if (source.empty())
                continue;
            const std::string target {biosDirectory + "/" + file.file};
            // copyFile() returns true when it fails.
            if (Utils::FileSystem::copyFile(source, target, false)) {
                LOG(LogError) << "MegaCdBios: couldn't copy " << source << " to " << target;
            }
            else {
                LOG(LogInfo) << "MegaCdBios: copied " << Utils::FileSystem::getFileName(source)
                             << " to " << file.file;
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath)
    {
        const std::vector<GameRegion> regions {
            regionsInFileName(Utils::FileSystem::getFileName(romPath))};
        if (regions.empty()) {
            for (const RegionFile& file : FILES) {
                if (hasFile(biosDirectory, file.region))
                    return {};
            }
            return {FILES[0].file, FILES[1].file, FILES[2].file};
        }
        for (const GameRegion region : regions) {
            if (hasFile(biosDirectory, region))
                return {};
        }
        return {fileFor(regions.front()).file};
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            // The file's own name: it may be in a folder inside the bios folder.
            const std::string name {Utils::FileSystem::getFileName(file)};
            if (looksLikeMegaCdBios(name) &&
                !Utils::FileSystem::exists(biosDirectory + "/" + name))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace MegaCdBios
