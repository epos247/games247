//  SPDX-License-Identifier: MIT
//
//  games247
//  PsxBios.cpp
//
//  The BIOS for the PlayStation core (Beetle PSX), see PsxBios.h.
//

#include "libretro/PsxBios.h"

#include "libretro/GameRegion.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/MathUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <map>

namespace
{
    using Region = GameRegion;

    struct KnownBios {
        Region region;
        const char* md5;
    };

    // PlayStation BIOS dumps by region (from DuckStation's list of known BIOSes). The first
    // of each region is the one Beetle PSX asks for; the others work too, with a warning in
    // the core's log.
    const KnownBios KNOWN_BIOSES[] {
        {Region::USA, "490f666e1afb15b7362b406ed1cea246"}, // SCPH-5501, 5503, 7003 (v3.0)
        {Region::EUROPE, "32736f17079d0b2b7024407c39bd3050"}, // SCPH-5502, 5552 (v3.0)
        {Region::JAPAN, "8dd7d5296a650fac7319bce665a6a53c"}, // SCPH-5500 (v3.0)
        {Region::USA, "1e68c231d0896b7eadcad1d7d8e76129"}, // SCPH-7001, 7501, 9001... (v4.1)
        {Region::USA, "924e392ed05558ffdb115408c263dccf"}, // SCPH-1001 (v2.2)
        {Region::USA, "da27e8b6dab242d8f91a9b25d80c63b8"}, // SCPH-1001 (v2.1)
        {Region::USA, "dc2b9bf8da62ec93e868cfd29f0d067d"}, // SCPH-1001 (v2.0)
        {Region::USA, "9a09ab7e49b422c007e6d54d7c49b965"}, // SCPH-101 (v4.4)
        {Region::USA, "6e3735ff4c7dc899ee98981385f6f3d0"}, // SCPH-101 (v4.5)
        {Region::EUROPE, "b9d9a0286c33dc6b7237bb13cd46fdee"}, // SCPH-7002, 7502, 9002 (v4.1)
        {Region::EUROPE, "e2110b8a2b97a8e0b857a45d32f7e187"}, // SCPH-1002 (v2.2)
        {Region::EUROPE, "417b34706319da7cf001e76e40136c23"}, // SCPH-1002 (v2.1)
        {Region::EUROPE, "54847e693405ffeb0359c6287434cbef"}, // SCPH-1002 (v2.0)
        {Region::EUROPE, "b10f5e0e3d9eb60e5159690680b1e774"}, // SCPH-102 (v4.4)
        {Region::EUROPE, "de93caec13d1a141a40a79f5c86168d6"}, // SCPH-102 (v4.5)
        {Region::JAPAN, "8e4c14f567745eff2f0408c8129f72a6"}, // SCPH-7000, 7500, 9000 (v4.0)
        {Region::JAPAN, "57a06303dfa9cf9351222dfcbb4a29d9"}, // SCPH-5000 (v2.2)
        {Region::JAPAN, "cba733ceeff5aef5c32254f1d617fa62"}, // SCPH-3500 (v2.1)
        {Region::JAPAN, "8abc1b549a4a80954addc48ef02c4521"}, // SCPH-100 (v4.3)
        {Region::JAPAN, "849515939161e62f6b866f6853006780"}, // SCPH-3000 (v1.1)
        {Region::JAPAN, "239665b1a3dade1b5a52c06338011044"}, // SCPH-1000 (v1.0)
    };

    // The size of a PlayStation BIOS.
    constexpr long BIOS_SIZE {524288};

    const char* fileFor(Region region)
    {
        return region == Region::USA    ? PsxBios::USA_FILE :
               region == Region::EUROPE ? PsxBios::EUROPE_FILE :
                                          PsxBios::JAPAN_FILE;
    }

    bool hasFile(const std::string& biosDirectory, Region region)
    {
        return Utils::FileSystem::isRegularFile(biosDirectory + "/" + fileFor(region));
    }

    // A name the FTP storage's BIOS file could have: scph5502.bin, SCPH-1001.BIN,
    // ps-30e.bin, PSX BIOS (Europe).bin...
    bool looksLikePsxBios(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        if (extension != ".bin" && extension != ".rom" && extension != ".img")
            return false;
        for (const char* part : {"scph", "psx", "ps1", "ps-", "playstation", "dtl-h"}) {
            if (lower.find(part) != std::string::npos)
                return true;
        }
        return false;
    }
} // namespace

namespace PsxBios
{
    bool isPsxCore(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("mednafen_psx", 0) == 0;
    }

    std::string fileForGame(const std::string& romPath)
    {
        const std::vector<Region> regions {
            regionsInFileName(Utils::FileSystem::getFileName(romPath))};
        return regions.empty() ? std::string {} : std::string {fileFor(regions.front())};
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        if (hasFile(biosDirectory, Region::USA) && hasFile(biosDirectory, Region::EUROPE) &&
            hasFile(biosDirectory, Region::JAPAN))
            return;

        // Every BIOS-sized file in the folder, by its place in KNOWN_BIOSES.
        std::map<size_t, std::string> found;
        for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
            if (!Utils::FileSystem::isRegularFile(path) ||
                Utils::FileSystem::getFileSize(path) != BIOS_SIZE)
                continue;
            const std::string md5 {Utils::Math::md5Hash(path, true)};
            for (size_t i {0}; i < sizeof(KNOWN_BIOSES) / sizeof(KNOWN_BIOSES[0]); ++i) {
                if (md5 == KNOWN_BIOSES[i].md5 && found.find(i) == found.end())
                    found[i] = path;
            }
        }

        for (Region region : {Region::USA, Region::EUROPE, Region::JAPAN}) {
            if (hasFile(biosDirectory, region))
                continue;
            // The map is ordered, so the preferred version of the region comes first.
            for (const auto& [index, path] : found) {
                if (KNOWN_BIOSES[index].region != region)
                    continue;
                const std::string target {biosDirectory + "/" + fileFor(region)};
                // copyFile() returns true when it fails.
                if (Utils::FileSystem::copyFile(path, target, false)) {
                    LOG(LogError) << "PsxBios: couldn't copy " << path << " to " << target;
                }
                else {
                    LOG(LogInfo) << "PsxBios: copied " << Utils::FileSystem::getFileName(path)
                                 << " to " << fileFor(region);
                }
                break;
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& romPath)
    {
        std::vector<Region> regions {regionsInFileName(Utils::FileSystem::getFileName(romPath))};
        const bool knownRegion {!regions.empty()};
        if (!knownRegion)
            regions = {Region::USA, Region::EUROPE, Region::JAPAN};
        for (Region region : regions) {
            if (hasFile(biosDirectory, region))
                return {};
        }
        if (knownRegion)
            return {fileFor(regions.front())};
        return {USA_FILE, EUROPE_FILE, JAPAN_FILE};
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                            const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            // The file's own name: it may be in a folder inside the bios folder.
            const std::string name {Utils::FileSystem::getFileName(file)};
            if (looksLikePsxBios(name) && !Utils::FileSystem::exists(biosDirectory + "/" + name))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace PsxBios
