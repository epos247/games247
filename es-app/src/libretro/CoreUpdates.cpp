//  SPDX-License-Identifier: MIT
//
//  games247
//  CoreUpdates.cpp
//
//  Finds newer builds of downloaded libretro cores, see CoreUpdates.h.
//

#include "libretro/CoreUpdates.h"

#include "HttpReq.h"
#include "Log.h"
#include "Settings.h"
#include "libretro/CoreManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <SDL2/SDL_timer.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
    struct Build {
        std::string date; // "2026-11-16"
        std::string crc; // "ee9c49e2"
    };

    // How long the list may take to download.
    constexpr unsigned int MAX_DOWNLOAD_MS {30000};

    std::mutex sMutex;
    // The server's builds by core name ("xemu_libretro"), once downloaded.
    std::map<std::string, Build> sBuilds;
    bool sListReady {false};
    // The checksum each core was installed from, from core_versions.txt.
    std::map<std::string, std::string> sInstalled;
    bool sInstalledLoaded {false};

    std::unique_ptr<std::thread> sThread;
    std::atomic<bool> sStop {false};

    std::string versionsFile()
    {
        return CoreManager::coresDirectory() + "/core_versions.txt";
    }

    // Call with sMutex held.
    void loadInstalled()
    {
        if (sInstalledLoaded)
            return;
        sInstalledLoaded = true;
        std::ifstream file {versionsFile()};
        std::string name;
        std::string crc;
        while (file >> name >> crc)
            sInstalled[name] = crc;
    }

    // Call with sMutex held.
    void saveInstalled()
    {
        Utils::FileSystem::createDirectory(CoreManager::coresDirectory());
        std::ofstream file {versionsFile(), std::ios::trunc};
        for (const auto& [name, crc] : sInstalled)
            file << name << " " << crc << "\n";
    }

    // The day a file was last changed, as "2026-11-16" (UTC, like the server's dates).
    std::string fileDate(const std::string& path)
    {
        std::error_code error;
#if defined(_WIN64)
        const std::filesystem::path filePath {Utils::String::stringToWideString(path)};
#else
        const std::filesystem::path filePath {path};
#endif
        const auto written = std::filesystem::last_write_time(filePath, error);
        if (error)
            return "";
        const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            written - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        const std::time_t time {std::chrono::system_clock::to_time_t(systemTime)};
        char date[16] {};
        std::strftime(date, sizeof(date), "%Y-%m-%d", std::gmtime(&time));
        return date;
    }

    void parseList(const std::string& content)
    {
        // "2026-11-16 ee9c49e2 xemu_libretro.dll.zip"
        const std::string suffix {CoreManager::coreFileName("") + ".zip"};
        std::map<std::string, Build> builds;
        std::istringstream lines {content};
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream fields {line};
            Build build;
            std::string file;
            if (!(fields >> build.date >> build.crc >> file))
                continue;
            if (file.size() <= suffix.size() ||
                file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            builds[file.substr(0, file.size() - suffix.size())] = build;
        }
        LOG(LogInfo) << "CoreUpdates: the build server lists " << builds.size() << " cores";
        std::unique_lock<std::mutex> lock {sMutex};
        sBuilds = std::move(builds);
        sListReady = true;
    }

    void checkThread(std::string url)
    {
        auto request = std::make_unique<HttpReq>(url, false);
        const unsigned int start {SDL_GetTicks()};
        while (!sStop && SDL_GetTicks() - start < MAX_DOWNLOAD_MS) {
            const HttpReq::Status status {request->status()};
            if (status == HttpReq::REQ_SUCCESS) {
                parseList(request->getContent());
                return;
            }
            if (status != HttpReq::REQ_IN_PROGRESS) {
                LOG(LogWarning) << "CoreUpdates: couldn't download the list of cores: "
                                << request->getErrorMsg();
                return;
            }
            SDL_Delay(20);
        }
        if (!sStop) {
            LOG(LogWarning) << "CoreUpdates: the list of cores took too long to download";
        }
    }
} // namespace

namespace CoreUpdates
{
    void startCheck()
    {
        if (sThread || !Settings::getInstance()->getBool("CoreUpdates"))
            return;
        const std::string url {CoreManager::indexURL()};
        if (url.empty())
            return;
        LOG(LogInfo) << "CoreUpdates: checking for core updates";
        sThread = std::make_unique<std::thread>(checkThread, url);
    }

    void stop()
    {
        sStop = true;
        if (sThread) {
            sThread->join();
            sThread.reset();
        }
    }

    bool hasUpdate(const std::string& coreName)
    {
        if (!Settings::getInstance()->getBool("CoreUpdates"))
            return false;
        const std::string corePath {CoreManager::coresDirectory() + "/" +
                                    CoreManager::coreFileName(coreName)};
        if (!Utils::FileSystem::isRegularFile(corePath))
            return false;

        std::unique_lock<std::mutex> lock {sMutex};
        if (!sListReady)
            return false;
        const auto build = sBuilds.find(coreName);
        if (build == sBuilds.end())
            return false;
        loadInstalled();
        const auto installed = sInstalled.find(coreName);
        if (installed != sInstalled.end())
            return installed->second != build->second.crc;

        // Installed before checksums were recorded: newer if the server built it on a later
        // day than the file's. Otherwise it's taken to be this build from now on.
        const std::string date {fileDate(corePath)};
        if (!date.empty() && date < build->second.date) {
            LOG(LogInfo) << "CoreUpdates: " << coreName << " is from " << date
                         << ", the server's from " << build->second.date;
            return true;
        }
        sInstalled[coreName] = build->second.crc;
        saveInstalled();
        return false;
    }

    void recordInstalled(const std::string& coreName)
    {
        std::unique_lock<std::mutex> lock {sMutex};
        loadInstalled();
        const auto build = sBuilds.find(coreName);
        // When the list isn't there yet, the core is compared by date next time.
        if (build != sBuilds.end())
            sInstalled[coreName] = build->second.crc;
        else
            sInstalled.erase(coreName);
        saveInstalled();
    }
} // namespace CoreUpdates
