//  SPDX-License-Identifier: MIT
//
//  games247
//  AtariTos.cpp
//
//  The TOS ROM for the Atari ST core (Hatari), see AtariTos.h.
//

#include "libretro/AtariTos.h"

#include "FtpStorage.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <fstream>

namespace
{
    // A file name that could be a TOS ROM: tos104uk.img, etos256us.img, TOS 1.04.rom...
    bool looksLikeTos(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        return lower.find("tos") != std::string::npos &&
               (extension == ".img" || extension == ".rom" || extension == ".bin");
    }

    // TOS ROMs are 192, 256, 512 or 1024 KB and start with a BRA instruction (0x60).
    bool isTosRom(const std::string& path)
    {
        const long size {Utils::FileSystem::getFileSize(path)};
        if (size != 196608 && size != 262144 && size != 524288 && size != 1048576)
            return false;
#if defined(_WIN64)
        std::ifstream file {Utils::String::stringToWideString(path).c_str(), std::ios::binary};
#else
        std::ifstream file {path, std::ios::binary};
#endif
        char first {0};
        return file.read(&first, 1) && static_cast<unsigned char>(first) == 0x60;
    }
} // namespace

namespace AtariTos
{
    bool isAtariCore(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("hatari", 0) == 0 && name.rfind("hatarib", 0) != 0;
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        const std::string target {biosDirectory + "/" + TOS_FILE};
        if (Utils::FileSystem::exists(target))
            return;
        std::vector<std::string> candidates;
        for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
            if (Utils::FileSystem::isRegularFile(path) &&
                looksLikeTos(Utils::FileSystem::getFileName(path)) && isTosRom(path))
                candidates.emplace_back(path);
        }
        if (candidates.empty())
            return;
        std::sort(candidates.begin(), candidates.end());
        // copyFile() returns true when it fails.
        if (Utils::FileSystem::copyFile(candidates.front(), target, false))
            LOG(LogError) << "AtariTos: couldn't copy " << candidates.front() << " to " << target;
        else
            LOG(LogInfo) << "AtariTos: copied " << Utils::FileSystem::getFileName(candidates.front())
                         << " to " << TOS_FILE;
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory)
    {
        if (Utils::FileSystem::isRegularFile(biosDirectory + "/" + TOS_FILE))
            return {};
        return {TOS_FILE};
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory)
    {
        std::vector<std::string> candidates;
        if (!FtpStorage::isEnabled())
            return candidates;
        const std::vector<std::string>* files {FtpStorage::getInstance().getFiles("bios")};
        if (files == nullptr)
            return candidates;
        for (const std::string& file : *files) {
            if (looksLikeTos(file) && !Utils::FileSystem::exists(biosDirectory + "/" + file))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace AtariTos
