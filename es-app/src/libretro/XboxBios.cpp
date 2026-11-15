//  SPDX-License-Identifier: MIT
//
//  games247
//  XboxBios.cpp
//
//  The system files for the original Xbox core (xemu), see XboxBios.h.
//

#include "libretro/XboxBios.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>

namespace
{
    enum class Kind { NONE, MCPX, BIOS, HDD };

    bool contains(const std::string& text, const char* part)
    {
        return text.find(part) != std::string::npos;
    }

    // What a file could be, from its name: mcpx_1.0.bin, Complex_4627v1.03.bin,
    // xbox_hdd.qcow2 and the like.
    Kind kindFromName(const std::string& fileName)
    {
        const std::string lower {Utils::String::toLower(fileName)};
        const std::string extension {Utils::FileSystem::getExtension(lower)};
        if (extension == ".qcow2" && contains(lower, "hdd"))
            return Kind::HDD;
        if (extension != ".bin" && extension != ".rom" && extension != ".img")
            return Kind::NONE;
        if (contains(lower, "eeprom") || contains(lower, "chihiro") || contains(lower, "hdd"))
            return Kind::NONE;
        if (contains(lower, "mcpx"))
            return Kind::MCPX;
        if (contains(lower, "complex") || contains(lower, "4627") || contains(lower, "xbox"))
            return Kind::BIOS;
        return Kind::NONE;
    }

    // The boot ROM is 512 bytes; flash BIOSes are 256 KB, or 1 MB with it repeated.
    bool sizeFits(Kind kind, long size)
    {
        switch (kind) {
            case Kind::MCPX:
                return size == 512;
            case Kind::BIOS:
                return size == 262144 || size == 1048576;
            case Kind::HDD:
                return size > 0;
            case Kind::NONE:
                break;
        }
        return false;
    }

    const char* targetName(Kind kind)
    {
        return kind == Kind::MCPX ? XboxBios::MCPX_FILE :
               kind == Kind::BIOS ? XboxBios::BIOS_FILE :
                                    XboxBios::HDD_FILE;
    }

    bool hasFile(const std::string& xemuDirectory, Kind kind)
    {
        if (Utils::FileSystem::isRegularFile(xemuDirectory + "/" + targetName(kind)))
            return true;
        // xemu also takes the BIOS under its shorter name.
        return kind == Kind::BIOS &&
               Utils::FileSystem::isRegularFile(xemuDirectory + "/Complex_4627.bin");
    }
} // namespace

namespace XboxBios
{
    bool isXboxCore(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("xemu_libretro", 0) == 0 || name.rfind("parallel_xbox_libretro", 0) == 0;
    }

    std::string xemuDirectory(const std::string& biosDirectory)
    {
        return biosDirectory + "/xemu";
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        const std::string xemu {xemuDirectory(biosDirectory)};
        Utils::FileSystem::createDirectory(xemu);

        for (Kind kind : {Kind::MCPX, Kind::BIOS, Kind::HDD}) {
            if (hasFile(xemu, kind))
                continue;
            std::vector<std::string> candidates;
            for (const std::string& folder : {xemu, biosDirectory}) {
                for (const std::string& path : Utils::FileSystem::getDirContent(folder)) {
                    if (Utils::FileSystem::isRegularFile(path) &&
                        kindFromName(Utils::FileSystem::getFileName(path)) == kind &&
                        sizeFits(kind, Utils::FileSystem::getFileSize(path)))
                        candidates.emplace_back(path);
                }
            }
            if (candidates.empty())
                continue;
            std::sort(candidates.begin(), candidates.end());
            const std::string target {xemu + "/" + targetName(kind)};
            // copyFile() returns true when it fails.
            if (Utils::FileSystem::copyFile(candidates.front(), target, false)) {
                LOG(LogError) << "XboxBios: couldn't copy " << candidates.front() << " to "
                              << target;
            }
            else {
                LOG(LogInfo) << "XboxBios: copied " << candidates.front() << " to xemu/"
                             << targetName(kind);
            }
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory)
    {
        const std::string xemu {xemuDirectory(biosDirectory)};
        std::vector<std::string> missing;
        for (Kind kind : {Kind::MCPX, Kind::BIOS, Kind::HDD}) {
            if (!hasFile(xemu, kind))
                missing.emplace_back(targetName(kind));
        }
        return missing;
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                            const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        const std::string xemu {xemuDirectory(biosDirectory)};
        for (const std::string& file : serverFiles) {
            // The file's own name: it may be in a folder inside the bios folder, such as xemu.
            const std::string name {Utils::FileSystem::getFileName(file)};
            const Kind kind {kindFromName(name)};
            // Only what's still missing, so a big hard disk image isn't fetched again.
            if (kind != Kind::NONE && !hasFile(xemu, kind) &&
                !Utils::FileSystem::exists(biosDirectory + "/" + name))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace XboxBios
