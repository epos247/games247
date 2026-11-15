//  SPDX-License-Identifier: MIT
//
//  games247
//  AmigaKickstart.cpp
//
//  Kickstart ROMs for the Amiga core (PUAE), see AmigaKickstart.h.
//

#include "libretro/AmigaKickstart.h"

#include "FtpStorage.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{
    // Kickstart ROMs by CRC32, with the names PUAE looks for. The checksums are from UAE's ROM
    // list (rommgr.c).
    struct KnownRom {
        uint32_t crc;
        const char* name;
    };
    constexpr KnownRom knownRoms[] {
        {0xa6ce1636, "kick33180.A500"},     // 1.2 (33.180)
        {0xc4f0f55f, "kick34005.A500"},     // 1.3 (34.5)
        {0xc3bdb240, "kick37175.A500"},     // 2.04 (37.175), A500+
        {0x43b0df7b, "kick37350.A600"},     // 2.05 (37.350), A600
        {0xfc24ae0d, "kick40063.A600"},     // 3.1 (40.63), A500/A600/A2000
        {0x1483a091, "kick40068.A1200"},    // 3.1 (40.68), A1200
        {0xd6bae334, "kick40068.A4000"},    // 3.1 (40.68), A4000
        {0x43b6dd22, "kick40068.A4000"},    // 3.1 (40.68), A4000, Cloanto
        {0x1e62d4a5, "kick40060.CD32"},     // CD32 3.1 (40.60)
        {0x87746be2, "kick40060.CD32.ext"}, // CD32 extended ROM
    };

    constexpr size_t ROM_256K {262144};
    constexpr size_t ROM_512K {524288};
    // Amiga Forever's encrypted ROMs start with this, followed by the ROM XORed with rom.key.
    constexpr char CLOANTO_HEADER[] {"AMIROMTYPE1"};
    constexpr size_t CLOANTO_HEADER_SIZE {11};

    uint32_t crc32(const std::vector<unsigned char>& data, size_t size)
    {
        static const std::array<uint32_t, 256> table {[] {
            std::array<uint32_t, 256> values {};
            for (uint32_t i {0}; i < 256; ++i) {
                uint32_t c {i};
                for (int bit {0}; bit < 8; ++bit)
                    c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
                values[i] = c;
            }
            return values;
        }()};
        uint32_t crc {0xffffffffu};
        for (size_t i {0}; i < size; ++i)
            crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
        return crc ^ 0xffffffffu;
    }

    bool readFile(const std::string& path, std::vector<unsigned char>& data)
    {
#if defined(_WIN64)
        std::ifstream file {Utils::String::stringToWideString(path).c_str(), std::ios::binary};
#else
        std::ifstream file {path, std::ios::binary};
#endif
        if (!file)
            return false;
        data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    bool writeFile(const std::string& path, const std::vector<unsigned char>& data, size_t size)
    {
#if defined(_WIN64)
        std::ofstream file {Utils::String::stringToWideString(path).c_str(),
                            std::ios::binary | std::ios::trunc};
#else
        std::ofstream file {path, std::ios::binary | std::ios::trunc};
#endif
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(size));
        return static_cast<bool>(file);
    }

    const char* knownName(uint32_t crc)
    {
        for (const KnownRom& rom : knownRoms) {
            if (rom.crc == crc)
                return rom.name;
        }
        return nullptr;
    }

    bool isCompactDisc(const std::string& extension)
    {
        return extension == ".cue" || extension == ".ccd" || extension == ".chd" ||
               extension == ".iso" || extension == ".nrg" || extension == ".mds";
    }

    bool isHardDrive(const std::string& extension)
    {
        return extension == ".hdf" || extension == ".hdz" || extension == ".lha" ||
               extension == ".slave" || extension == ".info";
    }
} // namespace

namespace AmigaKickstart
{
    bool isAmigaCore(const std::string& corePath)
    {
        return Utils::String::toLower(Utils::FileSystem::getFileName(corePath)).rfind("puae", 0) ==
               0;
    }

    std::string modelFor(const std::string& romPath, const std::string& chosenModel)
    {
        if (!chosenModel.empty() && chosenModel != "auto")
            return chosenModel;
        const std::string extension {
            Utils::String::toLower(Utils::FileSystem::getExtension(romPath))};
        if (isCompactDisc(extension))
            return "CD32";
        if (isHardDrive(extension))
            return "A1200";
        return "A500";
    }

    std::vector<std::string> filesFor(const std::string& model)
    {
        // As in PUAE's own model table (libretro-core.c).
        if (model == "A500PLUS")
            return {"kick37175.A500"};
        if (model == "A600" || model == "A600OG" || model == "A2000")
            return {"kick40063.A600"};
        if (model == "A1200" || model == "A1200OG")
            return {"kick40068.A1200"};
        if (model == "A4030" || model == "A4040")
            return {"kick40068.A4000"};
        if (model == "CD32" || model == "CD32FR")
            return {"kick40060.CD32", "kick40060.CD32.ext"};
        if (model == "CDTV")
            return {"kick34005.A500", "kick34005.CDTV"};
        return {"kick34005.A500"};
    }

    void identifyRoms(const std::string& biosDirectory)
    {
        std::vector<unsigned char> key;
        readFile(biosDirectory + "/rom.key", key);

        for (const std::string& path : Utils::FileSystem::getDirContent(biosDirectory)) {
            if (!Utils::FileSystem::isRegularFile(path))
                continue;
            const long fileSize {Utils::FileSystem::getFileSize(path)};
            const bool encryptedSize {fileSize == static_cast<long>(ROM_256K + CLOANTO_HEADER_SIZE) ||
                                      fileSize == static_cast<long>(ROM_512K + CLOANTO_HEADER_SIZE)};
            if (fileSize != static_cast<long>(ROM_256K) &&
                fileSize != static_cast<long>(ROM_512K) && !encryptedSize)
                continue;
            // Files already named the way PUAE looks for them don't need copying.
            const std::string fileName {Utils::FileSystem::getFileName(path)};
            if (std::any_of(std::cbegin(knownRoms), std::cend(knownRoms),
                            [&fileName](const KnownRom& rom) { return fileName == rom.name; }))
                continue;

            std::vector<unsigned char> data;
            if (!readFile(path, data))
                continue;
            if (encryptedSize) {
                if (std::memcmp(data.data(), CLOANTO_HEADER, CLOANTO_HEADER_SIZE) != 0)
                    continue;
                if (key.empty()) {
                    LOG(LogWarning) << "AmigaKickstart: " << fileName
                                    << " is encrypted and there's no rom.key to decode it";
                    continue;
                }
                data.erase(data.begin(), data.begin() + CLOANTO_HEADER_SIZE);
                for (size_t i {0}; i < data.size(); ++i)
                    data[i] ^= key[i % key.size()];
            }

            const char* name {knownName(crc32(data, data.size()))};
            size_t size {data.size()};
            // A 256 KB ROM saved twice over as 512 KB.
            if (name == nullptr && size == ROM_512K &&
                std::equal(data.begin(), data.begin() + ROM_256K, data.begin() + ROM_256K)) {
                name = knownName(crc32(data, ROM_256K));
                size = ROM_256K;
            }
            if (name == nullptr)
                continue;

            const std::string target {biosDirectory + "/" + name};
            if (Utils::FileSystem::exists(target))
                continue;
            if (writeFile(target, data, size))
                LOG(LogInfo) << "AmigaKickstart: copied " << fileName << " to " << name;
            else
                LOG(LogError) << "AmigaKickstart: couldn't write " << target;
        }
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& model)
    {
        std::vector<std::string> missing;
        for (const std::string& file : filesFor(model)) {
            if (!Utils::FileSystem::isRegularFile(biosDirectory + "/" + file))
                missing.emplace_back(file);
        }
        return missing;
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
            const std::string lower {Utils::String::toLower(file)};
            const bool kickstartLike {
                lower.rfind("kick", 0) == 0 || lower.rfind("amiga-os", 0) == 0 ||
                lower.rfind("amiga-ext", 0) == 0 || lower.find("kickstart") != std::string::npos ||
                lower.rfind("cd32", 0) == 0 || Utils::FileSystem::getExtension(lower) == ".rom" ||
                lower == "rom.key"};
            if (kickstartLike && !Utils::FileSystem::exists(biosDirectory + "/" + file))
                candidates.emplace_back(file);
        }
        return candidates;
    }
} // namespace AmigaKickstart
