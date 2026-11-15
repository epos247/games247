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

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <thread>

namespace
{
    // How long to wait for the server before giving up, in seconds. Some servers (a NAS that
    // has gone to sleep, for example) take several seconds to wake up and answer.
    constexpr long CONNECT_TIMEOUT {20};
    constexpr long RESPONSE_TIMEOUT {30};
    // If the server can't be reached, try again after these pauses (in seconds): three tries
    // in all. Listing happens while games247 starts, so these are kept short.
    constexpr int RETRY_DELAYS[] {3, 5};

    // Failures that may go away if we try again, such as a server that's still waking up.
    // A wrong password or a missing file won't, so those aren't retried.
    bool isTemporaryFailure(CURLcode code)
    {
        switch (code) {
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_FTP_WEIRD_SERVER_REPLY:
            case CURLE_FTP_WEIRD_PASV_REPLY:
            case CURLE_FTP_CANT_GET_HOST:
            case CURLE_FTP_ACCEPT_FAILED:
            case CURLE_FTP_ACCEPT_TIMEOUT:
            case CURLE_PARTIAL_FILE:
            case CURLE_GOT_NOTHING:
            case CURLE_SEND_ERROR:
            case CURLE_RECV_ERROR:
            case CURLE_SSL_CONNECT_ERROR:
                return true;
            default:
                return false;
        }
    }

    // Waits before the next try. Returns false if keepWaiting() says to stop (a cancel).
    bool waitBeforeRetry(int seconds, const std::function<bool()>& keepWaiting)
    {
        for (int i {0}; i < seconds * 10; ++i) {
            if (keepWaiting && !keepWaiting())
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

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

    size_t discard(char*, size_t size, size_t count, void*) { return size * count; }

    size_t readFromFile(char* data, size_t size, size_t count, void* userData)
    {
        return std::fread(data, size, count, static_cast<FILE*>(userData));
    }

    FILE* openFile(const std::string& path, bool write)
    {
#if defined(_WIN64)
        return _wfopen(Utils::String::stringToWideString(path).c_str(), write ? L"wb" : L"rb");
#else
        return std::fopen(path.c_str(), write ? "wb" : "rb");
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
    // Windows users may type the path with backslashes (\Games\ROMs).
    const std::string path {Utils::String::replace(account.path, "\\", "/")};
    for (const std::string& segment : Utils::String::delimitedStringToVector(path, "/")) {
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

namespace
{
    // Stops a listing when the background check is cancelled. libcurl calls this about once
    // a second, also while it's connecting.
    int cancelCallback(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        const std::atomic<bool>* cancel {static_cast<const std::atomic<bool>*>(userData)};
        return (cancel != nullptr && *cancel) ? 1 : 0;
    }

    constexpr const char* CACHE_HEADER {"games247 ftp cache 1"};
} // namespace

bool FtpStorage::listFolder(void* curlHandle,
                            const Account& account,
                            const std::string& url,
                            std::vector<std::string>& names,
                            std::string& error,
                            const std::atomic<bool>* cancel)
{
    CURL* handle {static_cast<CURL*>(curlHandle)};
    std::string listing;
    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(handle, account, url);
    curl_easy_setopt(handle, CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &listing);
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, cancel == nullptr ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, cancelCallback);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, cancel);

    CURLcode result {curl_easy_perform(handle)};
    for (const int delay : RETRY_DELAYS) {
        if (result == CURLE_OK || !isTemporaryFailure(result))
            break;
        LOG(LogWarning) << "FtpStorage: " << describeError(result, errorBuffer)
                        << ", trying again in " << delay << " seconds";
        if (!waitBeforeRetry(delay, [cancel]() { return cancel == nullptr || !*cancel; })) {
            result = CURLE_ABORTED_BY_CALLBACK;
            break;
        }
        listing.clear();
        errorBuffer[0] = '\0';
        result = curl_easy_perform(handle);
    }
    if (result != CURLE_OK) {
        error = result == CURLE_ABORTED_BY_CALLBACK ? "cancelled" :
                                                      describeError(result, errorBuffer);
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

bool FtpStorage::listBiosFolder(const Account& account,
                                std::vector<std::string>& files,
                                std::string& serverFolder,
                                std::string& error,
                                const std::atomic<bool>* cancel)
{
    files.clear();
    serverFolder.clear();
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    std::vector<std::string> topNames;
    if (!listFolder(curl.handle, account, buildURL(account, {}, true), topNames, error, cancel))
        return false;
    for (const std::string& name : topNames) {
        if (Utils::String::toLower(name) == "bios") {
            serverFolder = name;
            break;
        }
    }
    if (serverFolder.empty()) {
        error = "there's no bios folder on the FTP storage";
        return false;
    }

    std::vector<std::string> names;
    if (!listFolder(curl.handle, account, buildURL(account, {serverFolder}, true), names, error,
                    cancel))
        return false;
    for (const std::string& name : names) {
        // Names without a file extension may be folders, such as the xemu folder the Xbox
        // files go in locally. Listing a file as a folder fails, so then it's a file.
        const std::string extension {Utils::FileSystem::getExtension(name)};
        if (extension.size() > 1 && extension.size() <= 5) {
            files.emplace_back(name);
            continue;
        }
        std::vector<std::string> subNames;
        std::string subError;
        if (listFolder(curl.handle, account, buildURL(account, {serverFolder, name}, true),
                       subNames, subError, cancel)) {
            for (const std::string& subName : subNames)
                files.emplace_back(name + "/" + subName);
        }
        else if (subError == "cancelled") {
            error = subError;
            return false;
        }
        else {
            files.emplace_back(name);
        }
    }
    LOG(LogInfo) << "FtpStorage: " << files.size() << " file(s) in the FTP folder "
                 << serverFolder;
    return true;
}

bool FtpStorage::fetchListing(const Account& account,
                              Listing& listing,
                              std::string& error,
                              const std::atomic<bool>* cancel)
{
    // One handle for every folder, so the connection is reused.
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    std::vector<std::string> folders;
    if (!listFolder(curl.handle, account, buildURL(account, {}, true), folders, error, cancel))
        return false;

    listing = Listing {};
    for (const std::string& folder : folders) {
        if (cancel != nullptr && *cancel) {
            error = "cancelled";
            return false;
        }
        // Files in the top folder can't be games as they don't belong to a system. Listing a
        // file as a folder fails, so those are skipped below.
        if (Utils::FileSystem::getExtension(folder).size() > 1 &&
            Utils::FileSystem::getExtension(folder).size() <= 5)
            continue;
        std::vector<std::string> files;
        std::string folderError;
        if (!listFolder(curl.handle, account, buildURL(account, {folder}, true), files,
                        folderError, cancel)) {
            if (folderError == "cancelled") {
                error = folderError;
                return false;
            }
            continue;
        }
        const std::string key {Utils::String::toLower(folder)};
        listing.folders[key] = files;
        listing.folderNames[key] = folder;
    }
    return true;
}

std::string FtpStorage::getCachePath()
{
    return Utils::FileSystem::getAppDataDirectory() + "/ftp_cache.txt";
}

bool FtpStorage::loadCache(const Account& account, Listing& listing)
{
#if defined(_WIN64)
    std::ifstream stream {Utils::String::stringToWideString(getCachePath()).c_str()};
#else
    std::ifstream stream {getCachePath()};
#endif
    if (!stream.is_open())
        return false;

    // The saved listing only counts for the same server, user and folder.
    std::string line;
    if (!std::getline(stream, line) || line != CACHE_HEADER)
        return false;
    if (!std::getline(stream, line) ||
        line != "account\t" + account.host + "\t" + account.username + "\t" + account.path)
        return false;

    listing = Listing {};
    std::string key;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, 7, "folder\t") == 0) {
            const std::string folder {line.substr(7)};
            key = Utils::String::toLower(folder);
            listing.folderNames[key] = folder;
            listing.folders[key];
        }
        else if (line.compare(0, 5, "file\t") == 0 && !key.empty()) {
            listing.folders[key].emplace_back(line.substr(5));
        }
    }
    return true;
}

void FtpStorage::saveCache(const Account& account, const Listing& listing)
{
#if defined(_WIN64)
    std::ofstream stream {Utils::String::stringToWideString(getCachePath()).c_str(),
                          std::ios::binary};
#else
    std::ofstream stream {getCachePath(), std::ios::binary};
#endif
    if (!stream.is_open()) {
        LOG(LogWarning) << "FtpStorage: couldn't save the FTP listing to \"" << getCachePath()
                        << "\"";
        return;
    }
    stream << CACHE_HEADER << "\n";
    stream << "account\t" << account.host << "\t" << account.username << "\t" << account.path
           << "\n";
    for (const auto& folder : listing.folders) {
        const auto name = listing.folderNames.find(folder.first);
        stream << "folder\t" << (name != listing.folderNames.cend() ? name->second : folder.first)
               << "\n";
        for (const std::string& file : folder.second)
            stream << "file\t" << file << "\n";
    }
}

void FtpStorage::refresh()
{
    stopBackgroundCheck();
    mListing = Listing {};
    mRemoteGames.clear();

    if (!isEnabled())
        return;

    mAccount = accountFromSettings();

    // Use the saved listing straight away and check the server while games247 is running.
    if (loadCache(mAccount, mListing)) {
        LOG(LogInfo) << "FtpStorage: using the saved listing (" << mListing.folders.size()
                     << " folders), checking the FTP server in the background";
        startBackgroundCheck();
        return;
    }

    // Nothing saved yet (or a different account), so list the server now.
    LOG(LogInfo) << "FtpStorage: listing games on the FTP server";
    std::string error;
    if (!fetchListing(mAccount, mListing, error, nullptr)) {
        LOG(LogWarning) << "FtpStorage: couldn't list the FTP folder: " << error;
        mListing = Listing {};
        return;
    }
    saveCache(mAccount, mListing);
    LOG(LogInfo) << "FtpStorage: found " << mListing.folders.size() << " system folders";
}

void FtpStorage::startBackgroundCheck()
{
    mCheck = std::make_unique<BackgroundCheck>();
    BackgroundCheck* check {mCheck.get()};
    const Account account {mAccount};
    check->thread = std::thread([check, account]() {
        check->succeeded = fetchListing(account, check->listing, check->error, &check->cancel);
        check->done = true;
    });
}

void FtpStorage::stopBackgroundCheck()
{
    if (mCheck == nullptr)
        return;
    mCheck->cancel = true;
    if (mCheck->thread.joinable())
        mCheck->thread.join();
    mCheck.reset();
}

bool FtpStorage::takeChangedListing(std::vector<std::string>& newFolders)
{
    if (mCheck == nullptr || !mCheck->done)
        return false;

    mCheck->thread.join();
    std::unique_ptr<BackgroundCheck> check {std::move(mCheck)};

    if (!check->succeeded) {
        LOG(LogWarning) << "FtpStorage: couldn't check the FTP server, keeping the saved "
                           "listing: "
                        << check->error;
        return false;
    }
    if (check->listing == mListing) {
        LOG(LogInfo) << "FtpStorage: the FTP server hasn't changed since last time";
        return false;
    }

    newFolders.clear();
    for (const auto& folder : check->listing.folders) {
        if (mListing.folders.find(folder.first) == mListing.folders.cend())
            newFolders.emplace_back(folder.first);
    }
    mListing = std::move(check->listing);
    saveCache(mAccount, mListing);
    LOG(LogInfo) << "FtpStorage: the FTP server has changed, updating the game lists";
    return true;
}

const std::vector<std::string>* FtpStorage::getFiles(const std::string& folderName) const
{
    const auto it = mListing.folders.find(Utils::String::toLower(folderName));
    return it == mListing.folders.cend() ? nullptr : &it->second;
}

void FtpStorage::addRemoteGame(const std::string& localPath,
                               const std::string& folderName,
                               const std::string& fileName)
{
    mRemoteGames[localPath] = getFileURL(folderName, fileName);
}

std::string FtpStorage::getFileURL(const std::string& folderName,
                                   const std::string& fileName) const
{
    const auto it = mListing.folderNames.find(Utils::String::toLower(folderName));
    const std::string& serverFolder {it == mListing.folderNames.cend() ? folderName : it->second};
    return buildURL(mAccount, {serverFolder, fileName}, false);
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

    CurlHandle curl;
    if (curl.handle == nullptr) {
        message = "curl_easy_init failed";
        return false;
    }
    std::vector<std::string> names;
    if (!listFolder(curl.handle, account, buildURL(account, {}, true), names, message, nullptr))
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
    FILE* file {openFile(localPath, true)};
    if (file == nullptr) {
        error = "couldn't create the file on this computer";
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE] {};
    ProgressData progressData {&progress};
    setCommonOptions(curl.handle, account, url);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl.handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl.handle, CURLOPT_XFERINFODATA, &progressData);
    // Give up if the download stalls for 30 seconds.
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_TIME, 30L);

    // If the server can't be reached or the download breaks off, start again from the
    // beginning after a pause. The progress callback is asked during the pause too, so
    // cancelling still works.
    curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, file);
    CURLcode result {curl_easy_perform(curl.handle)};
    for (const int delay : RETRY_DELAYS) {
        if (result == CURLE_OK || !isTemporaryFailure(result))
            break;
        LOG(LogWarning) << "FtpStorage: " << describeError(result, errorBuffer)
                        << ", trying the download again in " << delay << " seconds";
        std::fclose(file);
        file = nullptr;
        if (!waitBeforeRetry(delay, [&progress]() { return progress(0, 0); })) {
            result = CURLE_ABORTED_BY_CALLBACK;
            break;
        }
        file = openFile(localPath, true);
        if (file == nullptr)
            break;
        errorBuffer[0] = '\0';
        curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, file);
        result = curl_easy_perform(curl.handle);
    }
    const bool written {file != nullptr && std::fclose(file) == 0};

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

bool FtpStorage::fileInfo(const Account& account,
                          const std::string& url,
                          FileInfo& info,
                          std::string& error)
{
    info = FileInfo {};
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(curl.handle, account, url);
    // SIZE and MDTM only, no download.
    curl_easy_setopt(curl.handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_FILETIME, 1L);
    // curl reports the size and time as header lines, which would otherwise go to stdout.
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);

    CURLcode result {curl_easy_perform(curl.handle)};
    for (const int delay : RETRY_DELAYS) {
        if (result == CURLE_OK || !isTemporaryFailure(result))
            break;
        if (!waitBeforeRetry(delay, nullptr))
            break;
        errorBuffer[0] = '\0';
        result = curl_easy_perform(curl.handle);
    }
    // No such file, or no such folder (the server refuses to change into it).
    if (result == CURLE_REMOTE_FILE_NOT_FOUND || result == CURLE_REMOTE_ACCESS_DENIED ||
        result == CURLE_FTP_COULDNT_RETR_FILE)
        return true;
    if (result != CURLE_OK) {
        error = describeError(result, errorBuffer);
        return false;
    }
    curl_off_t size {-1};
    if (curl_easy_getinfo(curl.handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size) != CURLE_OK)
        size = -1;
    curl_off_t time {-1};
    if (curl_easy_getinfo(curl.handle, CURLINFO_FILETIME_T, &time) != CURLE_OK)
        time = -1;
    info.exists = true;
    info.size = static_cast<long long>(size);
    info.time = static_cast<long long>(time);
    return true;
}

bool FtpStorage::upload(const Account& account,
                        const std::string& localPath,
                        const std::string& url,
                        std::string& error)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    FILE* file {openFile(localPath, false)};
    if (file == nullptr) {
        error = "couldn't open the file on this computer";
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(curl.handle, account, url);
    curl_easy_setopt(curl.handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_READFUNCTION, readFromFile);
    curl_easy_setopt(curl.handle, CURLOPT_READDATA, file);
    curl_easy_setopt(curl.handle, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(Utils::FileSystem::getFileSize(localPath)));
    curl_easy_setopt(curl.handle, CURLOPT_FTP_CREATE_MISSING_DIRS,
                     static_cast<long>(CURLFTP_CREATE_DIR_RETRY));
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);
    // Give up if the upload stalls for 30 seconds.
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_LOW_SPEED_TIME, 30L);

    CURLcode result {curl_easy_perform(curl.handle)};
    for (const int delay : RETRY_DELAYS) {
        if (result == CURLE_OK || !isTemporaryFailure(result))
            break;
        LOG(LogWarning) << "FtpStorage: " << describeError(result, errorBuffer)
                        << ", trying the upload again in " << delay << " seconds";
        if (!waitBeforeRetry(delay, nullptr))
            break;
        std::rewind(file);
        errorBuffer[0] = '\0';
        result = curl_easy_perform(curl.handle);
    }
    std::fclose(file);
    if (result != CURLE_OK) {
        error = describeError(result, errorBuffer);
        return false;
    }
    return true;
}
