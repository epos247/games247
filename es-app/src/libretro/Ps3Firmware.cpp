//  SPDX-License-Identifier: MIT
//
//  games247
//  Ps3Firmware.cpp
//
//  The PS3 system software for the PlayStation 3 core (RPCS3), see Ps3Firmware.h.
//

#include "libretro/Ps3Firmware.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <curl/curl.h>

#include <cstdio>
#include <sstream>

namespace
{
    // Sony's list of the current PS3 system software, with the address of its PS3UPDAT.PUP.
    // Sony's servers only answer plain HTTP properly (their HTTPS certificate isn't one curl
    // trusts); the file is signed by Sony, and RPCS3 checks that when it installs it.
    constexpr const char* UPDATE_LIST_URL {
        "http://fus01.ps3.update.playstation.net/update/ps3/list/us/ps3-updatelist.txt"};

    // The folders the core looks in, in its order.
    std::vector<std::string> pupFolders(const std::string& biosDirectory)
    {
        const std::string rpcs3 {Ps3Firmware::rpcs3Directory(biosDirectory)};
        return {rpcs3, biosDirectory, rpcs3 + "/firmware"};
    }

    size_t writeToString(char* data, size_t size, size_t count, void* userData)
    {
        static_cast<std::string*>(userData)->append(data, size * count);
        return size * count;
    }

    size_t writeToFile(char* data, size_t size, size_t count, void* userData)
    {
        return std::fwrite(data, size, count, static_cast<FILE*>(userData)) * size;
    }

    struct Progress {
        std::atomic<long long>* downloaded;
        std::atomic<long long>* total;
        const std::atomic<bool>* cancel;
    };

    int onProgress(void* userData, curl_off_t total, curl_off_t downloaded, curl_off_t, curl_off_t)
    {
        const Progress* progress {static_cast<Progress*>(userData)};
        *progress->downloaded = static_cast<long long>(downloaded);
        if (total > 0)
            *progress->total = static_cast<long long>(total);
        // Returning non-zero makes curl stop.
        return *progress->cancel ? 1 : 0;
    }

    CURL* newHandle(const std::string& url)
    {
        CURL* handle {curl_easy_init()};
        if (handle == nullptr)
            return nullptr;
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "games247");
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 30L);
        // Give up if nothing arrives for a minute.
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
        return handle;
    }

    // The PS3UPDAT.PUP address in Sony's update list:
    // "Dest=84;ImageVersion=...;SystemSoftwareVersion=4.9300;CDN=http://.../PS3UPDAT.PUP;..."
    std::string pupURL(const std::string& list)
    {
        std::istringstream lines {list};
        std::string line;
        while (std::getline(lines, line)) {
            const size_t start {line.find("CDN=")};
            if (start == std::string::npos)
                continue;
            const size_t end {line.find(';', start)};
            const std::string url {line.substr(start + 4, end == std::string::npos ?
                                                              std::string::npos :
                                                              end - start - 4)};
            if (Utils::String::toUpper(Utils::FileSystem::getFileName(url)) ==
                Ps3Firmware::PUP_FILE)
                return url;
        }
        return "";
    }
} // namespace

namespace Ps3Firmware
{
    bool isPs3Core(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        return name.rfind("rpcs3_libretro", 0) == 0;
    }

    std::string rpcs3Directory(const std::string& biosDirectory)
    {
        return biosDirectory + "/rpcs3";
    }

    bool isInstalled(const std::string& biosDirectory)
    {
        // What the core itself checks.
        return Utils::FileSystem::isRegularFile(rpcs3Directory(biosDirectory) +
                                                "/dev_flash/sys/external/liblv2.sprx");
    }

    std::string findPup(const std::string& biosDirectory)
    {
        for (const std::string& folder : pupFolders(biosDirectory)) {
            if (!Utils::FileSystem::isDirectory(folder))
                continue;
            for (const std::string& path : Utils::FileSystem::getDirContent(folder)) {
                if (Utils::String::toUpper(Utils::FileSystem::getFileName(path)) == PUP_FILE &&
                    Utils::FileSystem::isRegularFile(path))
                    return path;
            }
        }
        return "";
    }

    std::vector<std::string> missingFiles(const std::string& biosDirectory)
    {
        if (isInstalled(biosDirectory) || !findPup(biosDirectory).empty())
            return {};
        return {PUP_FILE};
    }

    bool needsInstall(const std::string& biosDirectory)
    {
        return !isInstalled(biosDirectory) && !findPup(biosDirectory).empty();
    }

    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                           const std::vector<std::string>& serverFiles)
    {
        std::vector<std::string> candidates;
        for (const std::string& file : serverFiles) {
            const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(file))};
            if (Utils::FileSystem::getExtension(name) != ".pup" ||
                name.find("ps3") == std::string::npos)
                continue;
            if (!Utils::FileSystem::exists(biosDirectory + "/" +
                                           Utils::FileSystem::getFileName(file)))
                candidates.emplace_back(file);
            // One system software file is enough.
            break;
        }
        return candidates;
    }

    bool downloadFromSony(const std::string& biosDirectory,
                          std::atomic<long long>& downloaded,
                          std::atomic<long long>& total,
                          const std::atomic<bool>& cancel,
                          std::string& error)
    {
        downloaded = 0;
        total = 0;

        // Where the current version is.
        std::string list;
        {
            CURL* handle {newHandle(UPDATE_LIST_URL)};
            if (handle == nullptr) {
                error = "couldn't start the download";
                return false;
            }
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeToString);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, &list);
            const CURLcode result {curl_easy_perform(handle)};
            curl_easy_cleanup(handle);
            if (result != CURLE_OK) {
                error = curl_easy_strerror(result);
                return false;
            }
        }
        const std::string url {pupURL(list)};
        if (url.empty()) {
            error = "Sony's update list has no PS3UPDAT.PUP";
            return false;
        }
        LOG(LogInfo) << "Ps3Firmware: downloading " << url;

        const std::string folder {rpcs3Directory(biosDirectory)};
        Utils::FileSystem::createDirectory(folder);
        const std::string path {folder + "/" + PUP_FILE};
        const std::string partPath {path + ".part"};
#if defined(_WIN64)
        FILE* file {_wfopen(Utils::String::stringToWideString(partPath).c_str(), L"wb")};
#else
        FILE* file {std::fopen(partPath.c_str(), "wb")};
#endif
        if (file == nullptr) {
            error = "couldn't write " + partPath;
            return false;
        }
        Progress progress {&downloaded, &total, &cancel};
        CURL* handle {newHandle(url)};
        CURLcode result {CURLE_FAILED_INIT};
        if (handle != nullptr) {
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeToFile);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, file);
            curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, onProgress);
            curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progress);
            result = curl_easy_perform(handle);
            curl_easy_cleanup(handle);
        }
        const bool written {std::fclose(file) == 0};
        // renameFile() returns true when it fails.
        if (result != CURLE_OK || !written ||
            Utils::FileSystem::renameFile(partPath, path, true)) {
            Utils::FileSystem::removeFile(partPath);
            error = cancel ? "cancelled" :
                    result != CURLE_OK ? std::string {curl_easy_strerror(result)} :
                                         "couldn't save the download";
            return false;
        }
        LOG(LogInfo) << "Ps3Firmware: saved " << path;
        return true;
    }

    std::string contentPath(const std::string& folder)
    {
        for (const char* name : {"PS3_DISC.SFB", "PARAM.SFO", "PS3_GAME/PARAM.SFO"}) {
            const std::string path {folder + "/" + name};
            if (Utils::FileSystem::isRegularFile(path))
                return path;
            // Other filesystems than Windows' and macOS's are case-sensitive.
            const std::string lower {folder + "/" + Utils::String::toLower(name)};
            if (Utils::FileSystem::isRegularFile(lower))
                return lower;
        }
        return "";
    }
} // namespace Ps3Firmware
