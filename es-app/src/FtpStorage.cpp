//  SPDX-License-Identifier: MIT
//
//  games247
//  FtpStorage.cpp
//
//  Games stored on an FTP server, see FtpStorage.h.
//

#include "FtpStorage.h"

#include "Log.h"
#include "Settings.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <curl/curl.h>

#include <cstdio>
#include <memory>

namespace
{
    // How long to wait for the server before giving up, in seconds. Listing happens while
    // games247 starts, so an unreachable server mustn't hold things up for long.
    constexpr long CONNECT_TIMEOUT {8};
    constexpr long RESPONSE_TIMEOUT {15};

    struct CurlHandle {
        CURL* handle {curl_easy_init()};
        ~CurlHandle()
        {
            if (handle != nullptr)
                curl_easy_cleanup(handle);
        }
    };

    size_t appendToString(char* data, size_t size, size_t count, void* userData)
    {
        static_cast<std::string*>(userData)->append(data, size * count);
        return size * count;
    }

    size_t writeToFile(char* data, size_t size, size_t count, void* userData)
    {
        return std::fwrite(data, size, count, static_cast<FILE*>(userData)) * size;
    }

    struct ProgressData {
        const std::function<bool(long long, long long)>* callback;
    };

    int progressCallback(
        void* userData, curl_off_t total, curl_off_t downloaded, curl_off_t, curl_off_t)
    {
        const ProgressData* data {static_cast<ProgressData*>(userData)};
        // Returning non-zero makes curl abort the transfer.
        return (*data->callback)(downloaded, total) ? 0 : 1;
    }

    // Login, encryption and timeouts shared by every request.
    void setCommonOptions(CURL* handle, const FtpStorage::Account& account, const std::string& url)
    {
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        if (!account.username.empty()) {
            curl_easy_setopt(handle, CURLOPT_USERNAME, account.username.c_str());
            curl_easy_setopt(handle, CURLOPT_PASSWORD, account.password.c_str());
        }
        // Encrypt the connection (FTPS) when the server supports it.
        curl_easy_setopt(handle, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_TRY));
        if (account.allowSelfSigned) {
            curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
        }
#if defined(USE_BUNDLED_CERTIFICATES)
        curl_easy_setopt(handle, CURLOPT_CAINFO,
                         ResourceManager::getInstance()
                             .getResourcePath(":/certificates/curl-ca-bundle.crt")
                             .c_str());
#endif
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
        curl_easy_setopt(handle, CURLOPT_SERVER_RESPONSE_TIMEOUT, RESPONSE_TIMEOUT);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        // Only ftp:// and ftps:// addresses are allowed.
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "ftp,ftps");
#else
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_FTP | CURLPROTO_FTPS);
#endif
    }

    std::string describeError(CURLcode code, const char* details)
    {
        switch (code) {
            case CURLE_LOGIN_DENIED:
                return "wrong username or password";
            case CURLE_COULDNT_RESOLVE_HOST:
                return "couldn't find the server";
            case CURLE_COULDNT_CONNECT:
                return "couldn't connect to the server";
            case CURLE_OPERATION_TIMEDOUT:
                return "the server didn't answer";
            case CURLE_REMOTE_ACCESS_DENIED:
            case CURLE_REMOTE_FILE_NOT_FOUND:
                return "folder or file not found on the server";
            case CURLE_PEER_FAILED_VERIFICATION:
                return "the server's certificate couldn't be verified";
            default:
                break;
        }
        if (details != nullptr && details[0] != '\0')
            return details;
        return curl_easy_strerror(code);
    }
} // namespace

FtpStorage& FtpStorage::getInstance()
{
    static FtpStorage instance;
    return instance;
}

FtpStorage::Account FtpStorage::accountFromSettings()
{
    Account account;
    Settings* settings {Settings::getInstance()};
    account.host = Utils::String::trim(settings->getString("FTPHost"));
    account.username = settings->getString("FTPUsername");
    account.password = settings->getString("FTPPassword");
    account.path = Utils::String::trim(settings->getString("FTPPath"));
    account.allowSelfSigned = settings->getBool("FTPAllowSelfSigned");
    return account;
}

bool FtpStorage::isEnabled()
{
    return Settings::getInstance()->getBool("FTPEnabled") &&
           !Utils::String::trim(Settings::getInstance()->getString("FTPHost")).empty();
}

std::string FtpStorage::buildURL(const Account& account,
                                 const std::vector<std::string>& parts,
                                 bool isFolder)
{
    std::string url {account.host};
    if (url.find("://") == std::string::npos)
        url = "ftp://" + url;
    while (!url.empty() && url.back() == '/')
        url.pop_back();

    // The path from the settings, then the extra parts, each URL-encoded.
    std::vector<std::string> segments;
    for (const std::string& segment : Utils::String::delimitedStringToVector(account.path, "/")) {
        if (!segment.empty())
            segments.emplace_back(segment);
    }
    for (const std::string& part : parts)
        segments.emplace_back(part);

    CurlHandle curl;
    for (const std::string& segment : segments) {
        char* escaped {curl.handle == nullptr ? nullptr :
                                                curl_easy_escape(curl.handle, segment.c_str(),
                                                                 static_cast<int>(segment.size()))};
        url.append("/").append(escaped != nullptr ? escaped : segment);
        if (escaped != nullptr)
            curl_free(escaped);
    }
    if (isFolder)
        url.append("/");
    return url;
}

bool FtpStorage::listFolder(const Account& account,
                            const std::string& url,
                            std::vector<std::string>& names,
                            std::string& error)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    std::string listing;
    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(curl.handle, account, url);
    curl_easy_setopt(curl.handle, CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &listing);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);

    const CURLcode result {curl_easy_perform(curl.handle)};
    if (result != CURLE_OK) {
        error = describeError(result, errorBuffer);
        return false;
    }

    names.clear();
    size_t start {0};
    while (start < listing.size()) {
        size_t end {listing.find('\n', start)};
        if (end == std::string::npos)
            end = listing.size();
        std::string name {listing.substr(start, end - start)};
        start = end + 1;
        if (!name.empty() && name.back() == '\r')
            name.pop_back();
        // Some servers give the whole path rather than just the name.
        const size_t slash {name.find_last_of('/')};
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        if (!name.empty() && name != "." && name != "..")
            names.emplace_back(name);
    }
    return true;
}

void FtpStorage::refresh()
{
    mFolders.clear();
    mFolderNames.clear();
    mRemoteGames.clear();

    if (!isEnabled())
        return;

    mAccount = accountFromSettings();
    LOG(LogInfo) << "FtpStorage: listing games on the FTP server";

    std::vector<std::string> folders;
    std::string error;
    if (!listFolder(mAccount, buildURL(mAccount, {}, true), folders, error)) {
        LOG(LogWarning) << "FtpStorage: couldn't list the FTP folder: " << error;
        return;
    }

    for (const std::string& folder : folders) {
        // Files in the top folder can't be games as they don't belong to a system. Listing a
        // file as a folder fails, so those are skipped below.
        if (Utils::FileSystem::getExtension(folder).size() > 1 &&
            Utils::FileSystem::getExtension(folder).size() <= 5)
            continue;
        std::vector<std::string> files;
        if (!listFolder(mAccount, buildURL(mAccount, {folder}, true), files, error))
            continue;
        const std::string key {Utils::String::toLower(folder)};
        mFolders[key] = files;
        mFolderNames[key] = folder;
    }

    LOG(LogInfo) << "FtpStorage: found " << mFolders.size() << " system folders";
}

const std::vector<std::string>* FtpStorage::getFiles(const std::string& folderName) const
{
    const auto it = mFolders.find(Utils::String::toLower(folderName));
    return it == mFolders.cend() ? nullptr : &it->second;
}

void FtpStorage::addRemoteGame(const std::string& localPath,
                               const std::string& folderName,
                               const std::string& fileName)
{
    const auto it = mFolderNames.find(Utils::String::toLower(folderName));
    const std::string& serverFolder {it == mFolderNames.cend() ? folderName : it->second};
    mRemoteGames[localPath] = buildURL(mAccount, {serverFolder, fileName}, false);
}

bool FtpStorage::isRemoteGame(const std::string& localPath) const
{
    return mRemoteGames.find(localPath) != mRemoteGames.cend() &&
           !Utils::FileSystem::exists(localPath);
}

std::string FtpStorage::getRemoteURL(const std::string& localPath) const
{
    const auto it = mRemoteGames.find(localPath);
    return it == mRemoteGames.cend() ? "" : it->second;
}

void FtpStorage::forgetRemoteGame(const std::string& localPath) { mRemoteGames.erase(localPath); }

bool FtpStorage::testConnection(const Account& account, std::string& message)
{
    if (account.host.empty()) {
        message = "no FTP address entered";
        return false;
    }

    std::vector<std::string> names;
    if (!listFolder(account, buildURL(account, {}, true), names, message))
        return false;

    // Count the entries that look like folders (no file extension).
    int folders {0};
    for (const std::string& name : names) {
        if (Utils::FileSystem::getExtension(name).size() <= 1)
            ++folders;
    }
    message = std::to_string(folders) + (folders == 1 ? " folder found" : " folders found");
    return true;
}

bool FtpStorage::download(const Account& account,
                          const std::string& url,
                          const std::string& localPath,
                          const std::function<bool(long long, long long)>& progress,
                          std::string& error)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(localPath));
#if defined(_WIN64)
    FILE* file {_wfopen(Utils::String::stringToWideString(localPath).c_str(), L"wb")};
#else
    FILE* file {std::fopen(localPath.c_str(), "wb")};
#endif
    if (file == nullptr) {
        error = "couldn't create the file on this computer";
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE] {};
    ProgressData progressData {&progress};
    setCommonOptions(curl.handle, account, url);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl.handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFODATA, &progressData);
    // Give up if the download stalls for 30 seconds.
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_TIME, 30L);

    const CURLcode result {curl_easy_perform(curl.handle)};
    const bool written {std::fclose(file) == 0};

    if (result != CURLE_OK || !written) {
        Utils::FileSystem::removeFile(localPath);
        if (result == CURLE_ABORTED_BY_CALLBACK)
            error = "cancelled";
        else if (result != CURLE_OK)
            error = describeError(result, errorBuffer);
        else
            error = "couldn't save the file on this computer";
        return false;
    }
    return true;
}
