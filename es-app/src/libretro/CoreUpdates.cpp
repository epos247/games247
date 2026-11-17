//  SPDX-License-Identifier: MIT
//
//  games247
//  CoreUpdates.cpp
//
//  Finds newer builds of downloaded libretro cores, see CoreUpdates.h.
//

#include "libretro/CoreUpdates.h"

#include "FtpStorage.h"
#include "HttpReq.h"
#include "Log.h"
#include "Settings.h"
#include "libretro/CoreManager.h"
#include "libretro/FtpCores.h"
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
    // Whether the FTP storage's cores folder is checked too, whether that's done, and the
    // version (FtpCores::version) of each installed core that's there.
    bool sFtpEnabled {false};
    bool sFtpReady {false};
    std::map<std::string, std::string> sFtpVersions;

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

    void checkBuildServer(const std::string& url)
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

    // Finds which of the installed cores are in the FTP storage's cores folder, and the
    // size and date of each.
    void checkFtp(const FtpStorage::Account& account)
    {
        std::map<std::string, std::string> versions;
        std::vector<std::string> files;
        std::string serverFolder;
        std::string error;
        if (!FtpStorage::listTopFolder(account, FtpCores::FOLDER, files, serverFolder, error,
                                       &sStop)) {
            LOG(LogWarning) << "CoreUpdates: couldn't list the FTP storage's cores folder: "
                            << error;
        }
        else if (!files.empty()) {
            const std::string suffix {CoreManager::coreFileName("")};
            for (const std::string& path :
                 Utils::FileSystem::getDirContent(CoreManager::coresDirectory())) {
                const std::string name {Utils::FileSystem::getFileName(path)};
                if (sStop)
                    break;
                if (name.size() <= suffix.size() ||
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
                    continue;
                const std::string coreName {name.substr(0, name.size() - suffix.size())};
                const std::string file {FtpCores::fileInListing(coreName, files)};
                if (file.empty())
                    continue;
                FtpCores::Core core;
                core.url = FtpStorage::buildURL(account, {serverFolder, file}, false);
                FtpStorage::FileInfo info;
                if (!FtpStorage::fileInfo(account, core.url, info, error) || !info.exists)
                    continue;
                core.size = info.size;
                core.time = info.time;
                versions[coreName] = FtpCores::version(core);
            }
            LOG(LogInfo) << "CoreUpdates: " << versions.size()
                         << " installed core(s) are in the FTP storage";
        }
        std::unique_lock<std::mutex> lock {sMutex};
        sFtpVersions = std::move(versions);
        sFtpReady = true;
    }

    void checkThread(std::string url, bool ftp, FtpStorage::Account account)
    {
        if (ftp)
            checkFtp(account);
        if (!url.empty() && !sStop)
            checkBuildServer(url);
    }
} // namespace

namespace CoreUpdates
{
    void startCheck()
    {
        if (sThread || !Settings::getInstance()->getBool("CoreUpdates"))
            return;
        const std::string url {CoreManager::indexURL()};
        sFtpEnabled = FtpStorage::isEnabled();
        if (url.empty() && !sFtpEnabled)
            return;
        LOG(LogInfo) << "CoreUpdates: checking for core updates";
        sThread = std::make_unique<std::thread>(checkThread, url, sFtpEnabled,
                                                FtpStorage::accountFromSettings());
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
        loadInstalled();
        const auto installed = sInstalled.find(coreName);
        if (sFtpEnabled) {
            // The FTP storage's cores come first, so wait for its check.
            if (!sFtpReady)
                return false;
            const auto ftp = sFtpVersions.find(coreName);
            if (ftp != sFtpVersions.end()) {
                // A core that came from the build server, or from before versions were
                // recorded, is replaced by the FTP storage's.
                return installed == sInstalled.end() || installed->second != ftp->second;
            }
        }
        // A core from the FTP storage isn't replaced by the build server's, even when the
        // FTP storage can't be reached.
        if (installed != sInstalled.end() && installed->second.rfind("ftp:", 0) == 0)
            return false;

        if (!sListReady)
            return false;
        const auto build = sBuilds.find(coreName);
        if (build == sBuilds.end())
            return false;
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

    void recordInstalled(const std::string& coreName, const std::string& ftpVersion)
    {
        std::unique_lock<std::mutex> lock {sMutex};
        loadInstalled();
        if (!ftpVersion.empty()) {
            sInstalled[coreName] = ftpVersion;
            if (sFtpReady)
                sFtpVersions[coreName] = ftpVersion;
            saveInstalled();
            return;
        }
        const auto build = sBuilds.find(coreName);
        // When the list isn't there yet, the core is compared by date next time.
        if (build != sBuilds.end())
            sInstalled[coreName] = build->second.crc;
        else
            sInstalled.erase(coreName);
        saveInstalled();
    }
} // namespace CoreUpdates
