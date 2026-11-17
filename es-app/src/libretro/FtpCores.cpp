//  SPDX-License-Identifier: MIT
//
//  games247
//  FtpCores.cpp
//
//  libretro cores from the FTP storage, see FtpCores.h.
//

#include "libretro/FtpCores.h"

#include "FtpStorage.h"
#include "Log.h"
#include "libretro/CoreManager.h"
#include "utils/StringUtil.h"

namespace FtpCores
{
    std::string fileInListing(const std::string& coreName, const std::vector<std::string>& files)
    {
        const std::string plain {Utils::String::toLower(CoreManager::coreFileName(coreName))};
        const std::string zipped {plain + ".zip"};
        std::string found;
        for (const std::string& file : files) {
            const std::string name {Utils::String::toLower(file)};
            if (name == zipped)
                return file;
            if (name == plain)
                found = file;
        }
        return found;
    }

    bool find(const std::string& coreName,
              Core& core,
              std::string& error,
              const std::atomic<bool>* cancel)
    {
        core = Core {};
        error.clear();
        if (!FtpStorage::isEnabled())
            return false;
        const FtpStorage::Account account {FtpStorage::accountFromSettings()};
        std::vector<std::string> files;
        std::string serverFolder;
        if (!FtpStorage::listTopFolder(account, FOLDER, files, serverFolder, error, cancel))
            return false;
        const std::string file {fileInListing(coreName, files)};
        if (file.empty())
            return false;

        core.url = FtpStorage::buildURL(account, {serverFolder, file}, false);
        core.zipped = Utils::String::toLower(file).size() > 4 &&
                      Utils::String::toLower(file).compare(file.size() - 4, 4, ".zip") == 0;
        FtpStorage::FileInfo info;
        if (!FtpStorage::fileInfo(account, core.url, info, error) || !info.exists) {
            if (error.empty())
                error = "couldn't look at " + file;
            return false;
        }
        core.size = info.size;
        core.time = info.time;
        LOG(LogInfo) << "FtpCores: " << coreName << " is in the FTP storage as " << serverFolder
                     << "/" << file;
        return true;
    }

    std::string version(const Core& core)
    {
        return "ftp:" + std::to_string(core.size) + ":" + std::to_string(core.time);
    }

    bool install(const std::string& coreName,
                 const Core& core,
                 std::atomic<long long>& downloaded,
                 std::atomic<long long>& total,
                 const std::atomic<bool>& cancel,
                 std::string& error)
    {
        downloaded = 0;
        total = core.size > 0 ? core.size : 0;
        const std::string path {CoreManager::coresDirectory() + "/" +
                                CoreManager::coreFileName(coreName) +
                                (core.zipped ? ".zip" : ".download")};
        const auto progress = [&](long long done, long long size) {
            downloaded = done;
            if (size > 0)
                total = size;
            return !cancel;
        };
        if (!FtpStorage::download(FtpStorage::accountFromSettings(), core.url, path, progress,
                                  error))
            return false;
        if (cancel) {
            error = "cancelled";
            return false;
        }
        return core.zipped ? CoreManager::installFromZip(path, coreName, error) :
                             CoreManager::installFile(path, coreName, error);
    }
} // namespace FtpCores
