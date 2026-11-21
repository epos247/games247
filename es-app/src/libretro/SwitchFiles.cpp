//  SPDX-License-Identifier: MIT
//
//  games247
//  SwitchFiles.cpp
//
//  The keys and firmware for the Switch cores, see SwitchFiles.h.
//

#include "libretro/SwitchFiles.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

namespace SwitchFiles
{
    std::string folderFor(const std::string& corePath)
    {
        const std::string name {Utils::String::toLower(Utils::FileSystem::getFileName(corePath))};
        if (name.rfind("eden_libretro", 0) == 0)
            return "eden";
        if (name.rfind("lichii_libretro", 0) == 0)
            return "lichii";
        return "";
    }

    bool plan(const FtpStorage::Account& account,
              const std::string& biosDirectory,
              const std::string& folder,
              std::vector<Download>& downloads,
              std::string& error,
              const std::atomic<bool>* cancel)
    {
        downloads.clear();

        // The bios folder and the core's folder in it, whatever their case on the server.
        std::vector<std::string> biosNames;
        std::string serverBios;
        if (!FtpStorage::listTopFolder(account, "bios", biosNames, serverBios, error, cancel))
            return false;
        if (serverBios.empty())
            return true;
        std::string serverFolder;
        for (const std::string& name : biosNames) {
            if (Utils::String::toLower(name) == folder) {
                serverFolder = name;
                break;
            }
        }
        if (serverFolder.empty())
            return true;

        std::vector<FtpStorage::TreeEntry> entries;
        bool exists {false};
        if (!FtpStorage::listTree(account,
                                  FtpStorage::buildURL(account, {serverBios, serverFolder}, true),
                                  entries, exists, error, cancel))
            return false;

        const std::string localRoot {biosDirectory + "/" + folder};
        for (const FtpStorage::TreeEntry& entry : entries) {
            if (entry.isFolder)
                continue;
            std::vector<std::string> parts {Utils::String::delimitedStringToVector(entry.path, "/")};
            if (parts.empty())
                continue;
            // The cores only open the key files by their lower-case names.
            std::string localPath {entry.path};
            const std::string fileName {parts.back()};
            const std::string lowerName {Utils::String::toLower(fileName)};
            if (Utils::FileSystem::getExtension(lowerName) == ".keys" ||
                lowerName == "key_retail.bin")
                localPath = localPath.substr(0, localPath.size() - fileName.size()) + lowerName;
            localPath = localRoot + "/" + localPath;

            if (Utils::FileSystem::isRegularFile(localPath) &&
                FtpStorage::localFileSize(localPath) == entry.size)
                continue;
            std::vector<std::string> urlParts {serverBios, serverFolder};
            urlParts.insert(urlParts.end(), parts.cbegin(), parts.cend());
            downloads.emplace_back(Download {fileName, FtpStorage::buildURL(account, urlParts, false),
                                             localPath, entry.size});
        }
        LOG(LogInfo) << "SwitchFiles: " << entries.size() << " entries in the FTP storage's "
                     << serverBios << "/" << serverFolder << " folder, " << downloads.size()
                     << " to download";
        return true;
    }
} // namespace SwitchFiles
