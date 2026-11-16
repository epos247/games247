//  SPDX-License-Identifier: MIT
//
//  games247
//  Ps2Bios.cpp
//
//  The BIOS for the PlayStation 2 core (LRPS2), see Ps2Bios.h.
//

#include "libretro/Ps2Bios.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace
{
    // The sizes LRPS2 accepts.
    constexpr long MIN_SIZE {4 * 1024 * 1024};
    constexpr long MAX_SIZE {8 * 1024 * 1024};

    // A PS2 BIOS starts with a ROM directory, whose first entries are RESET, ROMDIR and
    // EXTINFO, followed later by ROMVER. Looking for those in the first megabyte tells a BIOS
    // from any other file of the same size.
    bool isPs2Bios(const std::string& path)
    {
        if (!Utils::FileSystem::isRegularFile(path))
            return false;
        const long size {Utils::FileSystem::getFileSize(path)};
        if (size < MIN_SIZE || size > MAX_SIZE)
            return false;
#if defined(_WIN64)
        std::ifstream file {Utils::String::stringToWideString(path).c_str(), std::ios::binary};
#else
        std::ifstream file {path, std::ios::binary};
#endif
        std::vector<char> data(1024 * 1024);
        file.read(data.data(), static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<size_t>(file.gcount()));
        const auto contains = [&data](const char* text, size_t length) {
            return std::search(data.begin(), data.end(), text, text + length) != data.end();
        };
        return contains("RESET\0\0\0\0\0", 10) && contains("ROMDIR", 6) && contains("ROMVER", 6);
    }

    bool hasBios(const std::string& directory)
    {
        for (const std::string& path : Utils::FileSystem::getDirContent(directory)) {
            if (isPs2Bios(path))
                return true;
        }
        return false;
    }

    // A name the FTP storage's BIOS file could have: SCPH-70012.bin, scph39001.bin,
    // ps2-0230e-20040614.bin, PS2 BIOS (Europe).bin...
    bool looksLikePs2Bios(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        if (extension != ".bin" && extension != ".rom")
            return false;
        for (const char* part : {"ps2", "playstation 2", "playstation2"}) {
            if (lower.find(part) != std::string::npos)
                return true;
        }
        // SCPH and DTL-H model numbers: PS2 ones have five digits (SCPH-70012), PS1 ones
        // fewer (SCPH-1001, scph5502).
        for (const char* model : {"scph", "dtl-h"}) {
            const size_t start {lower.find(model)};
            if (start == std::string::npos)
                continue;
            size_t digits {0};
            for (size_t i {start + std::strlen(model)}; i < lower.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(lower[i])))
                    ++digits;
                else if (digits > 0 || (lower[i] != '-' && lower[i] != '_' && lower[i] != ' '))
                    break;
            }
            if (digits >= 5)
                return true;
        }
        return false;
    }
} // namespace

namespace Ps2Bios
{
    bool isPs2Core(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("pcsx2_libretro", 0) == 0;
    }

    std::string coreBiosDirectory(const std::string& biosDirectory)
    {
        return biosDirectory + "/pcsx2/bios";
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        const std::string target {coreBiosDirectory(biosDirectory)};
        if (hasBios(target))
            return;
        Utils::FileSystem::createDirectory(target);
        for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
            if (!isPs2Bios(path))
                continue;
            const std::string copy {target + "/" + Utils::FileSystem::getFileName(path)};
            // copyFile() returns true when it fails.
            if (Utils::FileSystem::copyFile(path, copy, false)) {
                LOG(LogError) << "Ps2Bios: couldn't copy " << path << " to " << copy;
            }
            else {
                LOG(LogInfo) << "Ps2Bios: copied " << Utils::FileSystem::getFileName(path)
                             << " to " << target;
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory)
    {
        if (hasBios(coreBiosDirectory(biosDirectory)))
            return {};
        return {"a PS2 BIOS (such as SCPH-70012.bin)"};
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            // The file's own name: it may be in a folder inside the bios folder.
            const std::string name {Utils::FileSystem::getFileName(file)};
            if (looksLikePs2Bios(name) &&
                !Utils::FileSystem::exists(biosDirectory + "/" + name) &&
                !Utils::FileSystem::exists(coreBiosDirectory(biosDirectory) + "/" + name))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace Ps2Bios
