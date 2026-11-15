//  SPDX-License-Identifier: MIT
//
//  games247
//  CloudSaves.cpp
//
//  Save states and game saves synced through the FTP storage, see CloudSaves.h.
//

#include "CloudSaves.h"

#include "FtpStorage.h"
#include "Log.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
    constexpr char SAVES_FOLDER[] {"saves"};

    // The record of what's on the server as of this computer's last sync: one line per file,
    // "<system folder>/<file name>\t<size>\t<time>".
    std::mutex recordMutex;

    std::string recordPath()
    {
        return Utils::FileSystem::getAppDataDirectory() + "/cloud_saves.txt";
    }

    std::map<std::string, FtpStorage::FileInfo> loadRecords()
    {
        std::map<std::string, FtpStorage::FileInfo> records;
        std::ifstream file {recordPath()};
        std::string line;
        while (std::getline(file, line)) {
            const std::vector<std::string> parts {
                Utils::String::delimitedStringToVector(line, "\t")};
            if (parts.size() != 3)
                continue;
            FtpStorage::FileInfo info;
            info.exists = true;
            try {
                info.size = std::stoll(parts[1]);
                info.time = std::stoll(parts[2]);
            }
            catch (...) {
                continue;
            }
            records[parts[0]] = info;
        }
        return records;
    }

    void saveRecords(const std::map<std::string, FtpStorage::FileInfo>& records)
    {
        std::ofstream file {recordPath(), std::ios::trunc};
        for (const auto& [key, info] : records)
            file << key << "\t" << info.size << "\t" << info.time << "\n";
    }

    std::string recordKey(const std::string& systemFolder, const std::string& fileName)
    {
        return Utils::String::toLower(systemFolder) + "/" + fileName;
    }

    void setRecord(const std::string& key, const FtpStorage::FileInfo* info)
    {
        std::lock_guard<std::mutex> lock {recordMutex};
        std::map<std::string, FtpStorage::FileInfo> records {loadRecords()};
        if (info != nullptr && info->exists)
            records[key] = *info;
        else
            records.erase(key);
        saveRecords(records);
    }

    // True when the server's file is the one this computer last uploaded or downloaded. A
    // server that doesn't give modification times can't tell, so its files always count as
    // changed (save states of a game are usually all the same size).
    bool matchesRecord(const std::string& key, const FtpStorage::FileInfo& info)
    {
        if (info.time < 0)
            return false;
        std::lock_guard<std::mutex> lock {recordMutex};
        const std::map<std::string, FtpStorage::FileInfo> records {loadRecords()};
        const auto it = records.find(key);
        return it != records.cend() && it->second == info;
    }

    std::string remoteName(const std::string& romPath, const char* extension)
    {
        return Utils::FileSystem::getStem(romPath) + extension;
    }

    std::string remoteURL(const FtpStorage::Account& account,
                          const std::string& systemFolder,
                          const std::string& fileName)
    {
        return FtpStorage::buildURL(account, {SAVES_FOLDER, systemFolder, fileName}, false);
    }

    // Downloads to a temporary file first, so a broken download doesn't replace a good file.
    bool downloadFile(const FtpStorage::Account& account,
                      const std::string& systemFolder,
                      const std::string& fileName,
                      const std::string& localPath,
                      std::string& error)
    {
        FtpStorage::FileInfo info;
        const std::string url {remoteURL(account, systemFolder, fileName)};
        if (!FtpStorage::fileInfo(account, url, info, error))
            return false;
        if (!info.exists) {
            error = "the cloud save isn't on the server any more";
            return false;
        }
        const std::string partPath {localPath + ".part"};
        if (!FtpStorage::download(
                account, url, partPath, [](long long, long long) { return true; }, error))
            return false;
        Utils::FileSystem::removeFile(localPath);
        if (Utils::FileSystem::renameFile(partPath, localPath, true)) {
            Utils::FileSystem::removeFile(partPath);
            error = "couldn't save the file on this computer";
            return false;
        }
        setRecord(recordKey(systemFolder, fileName), &info);
        LOG(LogInfo) << "CloudSaves: downloaded " << systemFolder << "/" << fileName;
        return true;
    }

    // Uploads run one at a time on a worker thread.
    struct Upload {
        FtpStorage::Account account;
        std::string systemFolder;
        // Local path -> name on the server.
        std::vector<std::pair<std::string, std::string>> files;
    };

    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<Upload> queue;
    bool uploading {false};
    bool stopping {false};
    std::thread worker;

    void runUpload(const Upload& upload)
    {
        for (const auto& [localPath, fileName] : upload.files) {
            const std::string url {remoteURL(upload.account, upload.systemFolder, fileName)};
            const std::string key {recordKey(upload.systemFolder, fileName)};
            std::string error;
            if (!FtpStorage::upload(upload.account, localPath, url, error)) {
                LOG(LogWarning) << "CloudSaves: couldn't upload " << upload.systemFolder << "/"
                                << fileName << ": " << error;
                continue;
            }
            // What the server now has, so this computer doesn't download it again.
            FtpStorage::FileInfo info;
            if (FtpStorage::fileInfo(upload.account, url, info, error))
                setRecord(key, &info);
            else
                setRecord(key, nullptr);
            LOG(LogInfo) << "CloudSaves: uploaded " << upload.systemFolder << "/" << fileName;
        }
    }

    void workerLoop()
    {
        std::unique_lock<std::mutex> lock {queueMutex};
        while (true) {
            queueChanged.wait(lock, [] { return stopping || !queue.empty(); });
            if (queue.empty())
                return;
            const Upload upload {queue.front()};
            queue.pop_front();
            uploading = true;
            lock.unlock();
            runUpload(upload);
            lock.lock();
            uploading = false;
            queueChanged.notify_all();
        }
    }

    // Waits until nothing is queued or uploading, at most timeoutSeconds.
    bool waitForUploads(int timeoutSeconds)
    {
        std::unique_lock<std::mutex> lock {queueMutex};
        return queueChanged.wait_for(lock, std::chrono::seconds(timeoutSeconds),
                                     [] { return queue.empty() && !uploading; });
    }
} // namespace

namespace CloudSaves
{
    bool isEnabled()
    {
        return FtpStorage::isEnabled() && Settings::getInstance()->getBool("FTPCloudSaves");
    }

    bool check(const std::string& systemFolder,
               const std::string& romPath,
               const std::string& statePath,
               CheckResult& result,
               std::string& error,
               const std::atomic<bool>& cancel)
    {
        result = CheckResult {};
        // The last game's files may still be on their way up.
        for (int i {0}; i < 120 && !waitForUploads(1); ++i) {
            if (cancel) {
                error = "cancelled";
                return false;
            }
        }

        const FtpStorage::Account account {FtpStorage::accountFromSettings()};
        const std::string stateName {remoteName(romPath, ".state")};
        const std::string saveRAMName {remoteName(romPath, ".srm")};

        FtpStorage::FileInfo stateInfo;
        if (!FtpStorage::fileInfo(account, remoteURL(account, systemFolder, stateName),
                                  stateInfo, error))
            return false;
        FtpStorage::FileInfo saveRAMInfo;
        if (!FtpStorage::fileInfo(account, remoteURL(account, systemFolder, saveRAMName),
                                  saveRAMInfo, error))
            return false;

        if (cancel) {
            error = "cancelled";
            return false;
        }
        result.hasState = stateInfo.exists;
        result.stateNeedsDownload =
            stateInfo.exists && (!Utils::FileSystem::isRegularFile(statePath) ||
                                 !matchesRecord(recordKey(systemFolder, stateName), stateInfo));

        if (saveRAMInfo.exists &&
            !matchesRecord(recordKey(systemFolder, saveRAMName), saveRAMInfo)) {
            const std::string path {Utils::FileSystem::getParent(statePath) + "/" +
                                    Utils::FileSystem::getStem(romPath) + ".cloud.srm"};
            std::string downloadError;
            if (downloadFile(account, systemFolder, saveRAMName, path, downloadError) &&
                !cancel)
                result.downloadedSaveRAM = path;
            else
                LOG(LogWarning) << "CloudSaves: couldn't download " << systemFolder << "/"
                                << saveRAMName << ": " << downloadError;
        }
        LOG(LogInfo) << "CloudSaves: " << systemFolder << "/" << stateName
                     << (result.hasState ? (result.stateNeedsDownload ? " is new on the server" :
                                                                        " is up to date") :
                                           " isn't on the server");
        return true;
    }

    bool downloadState(const std::string& systemFolder,
                       const std::string& romPath,
                       const std::string& statePath,
                       std::string& error)
    {
        return downloadFile(FtpStorage::accountFromSettings(), systemFolder,
                            remoteName(romPath, ".state"), statePath, error);
    }

    void queueUpload(const std::string& systemFolder,
                     const std::string& romPath,
                     const std::string& statePath,
                     const std::string& saveRAMPath)
    {
        Upload upload;
        upload.account = FtpStorage::accountFromSettings();
        upload.systemFolder = systemFolder;
        if (!statePath.empty() && Utils::FileSystem::isRegularFile(statePath))
            upload.files.emplace_back(statePath, remoteName(romPath, ".state"));
        if (!saveRAMPath.empty() && Utils::FileSystem::isRegularFile(saveRAMPath))
            upload.files.emplace_back(saveRAMPath, remoteName(romPath, ".srm"));
        if (upload.files.empty())
            return;

        std::lock_guard<std::mutex> lock {queueMutex};
        if (stopping)
            return;
        queue.push_back(std::move(upload));
        if (!worker.joinable())
            worker = std::thread {workerLoop};
        queueChanged.notify_all();
    }

    void shutdown(int timeoutSeconds)
    {
        if (!worker.joinable())
            return;
        const bool finished {waitForUploads(timeoutSeconds)};
        {
            std::lock_guard<std::mutex> lock {queueMutex};
            stopping = true;
            queue.clear();
        }
        queueChanged.notify_all();
        if (finished) {
            worker.join();
        }
        else {
            LOG(LogWarning) << "CloudSaves: quitting before the uploads finished";
            worker.detach();
        }
    }
} // namespace CloudSaves
