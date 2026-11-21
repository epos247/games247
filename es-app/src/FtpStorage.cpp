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
#include "utils/PlatformUtil.h"
#include "utils/StringUtil.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
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

    struct UploadProgressData {
        const std::function<bool(long long, long long)>* callback;
    };

    int uploadProgressCallback(
        void* userData, curl_off_t, curl_off_t, curl_off_t total, curl_off_t uploaded)
    {
        const UploadProgressData* data {static_cast<UploadProgressData*>(userData)};
        return (*data->callback)(uploaded, total) ? 0 : 1;
    }

    // Splits a file's address into its folder's address (ending in /) and the file name, which
    // isn't URL-encoded.
    void splitFileURL(CURL* handle, const std::string& url, std::string& folderURL,
                      std::string& fileName)
    {
        const size_t slash {url.find_last_of('/')};
        folderURL = url.substr(0, slash + 1);
        int length {0};
        char* decoded {curl_easy_unescape(handle, url.c_str() + slash + 1,
                                          static_cast<int>(url.size() - slash - 1), &length)};
        fileName = decoded != nullptr ? std::string(decoded, static_cast<size_t>(length)) : "";
        curl_free(decoded);
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
                            const std::atomic<bool>* cancel,
                            std::string* rawListing,
                            int* resultCode)
{
    CURL* handle {static_cast<CURL*>(curlHandle)};
    std::string listing;
    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(handle, account, url);
    curl_easy_setopt(handle, CURLOPT_DIRLISTONLY, rawListing == nullptr ? 1L : 0L);
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
    if (resultCode != nullptr)
        *resultCode = static_cast<int>(result);
    if (result != CURLE_OK) {
        error = result == CURLE_ABORTED_BY_CALLBACK ? "cancelled" :
                                                      describeError(result, errorBuffer);
        return false;
    }

    names.clear();
    if (rawListing != nullptr) {
        *rawListing = std::move(listing);
        return true;
    }
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

    // Files in folders inside the bios folder are listed too, a few levels down, as the
    // cores' own folders often are (pcsx2/bios/scph70004.bin, xemu/...).
    constexpr int MAX_DEPTH {3};
    std::function<bool(const std::vector<std::string>&, int)> listInside;
    listInside = [&](const std::vector<std::string>& folder, int depth) {
        std::vector<std::string> names;
        if (!listFolder(curl.handle, account, buildURL(account, folder, true), names, error,
                        cancel))
            return false;
        std::string prefix;
        for (size_t i {1}; i < folder.size(); ++i)
            prefix += folder[i] + "/";
        for (const std::string& name : names) {
            // Names without a file extension may be folders, such as the xemu folder the
            // Xbox files go in locally. Listing a file as a folder fails, so then it's a file.
            const std::string extension {Utils::FileSystem::getExtension(name)};
            if ((extension.size() > 1 && extension.size() <= 5) || depth >= MAX_DEPTH) {
                files.emplace_back(prefix + name);
                continue;
            }
            std::vector<std::string> subFolder {folder};
            subFolder.emplace_back(name);
            const size_t before {files.size()};
            if (!listInside(subFolder, depth + 1)) {
                if (error == "cancelled")
                    return false;
                // Not a folder.
                error.clear();
                files.resize(before);
                files.emplace_back(prefix + name);
            }
        }
        return true;
    };
    if (!listInside({serverFolder}, 1))
        return false;
    LOG(LogInfo) << "FtpStorage: " << files.size() << " file(s) in the FTP folder "
                 << serverFolder;
    return true;
}

bool FtpStorage::listTopFolder(const Account& account,
                               const std::string& name,
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
    const std::string wanted {Utils::String::toLower(name)};
    for (const std::string& topName : topNames) {
        if (Utils::String::toLower(topName) == wanted) {
            serverFolder = topName;
            break;
        }
    }
    if (serverFolder.empty())
        return true;
    return listFolder(curl.handle, account, buildURL(account, {serverFolder}, true), files,
                      error, cancel);
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
        Utils::Platform::setBackgroundThread();
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

namespace
{
    // Downloads one file using a handle that may be reused for the next one, so a game
    // folder's files all go over one connection.
    bool performDownload(CURL* handle,
                         const FtpStorage::Account& account,
                         const std::string& url,
                         const std::string& localPath,
                         const std::function<bool(long long, long long)>& progress,
                         std::string& error)
    {
        Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(localPath));
        FILE* file {openFile(localPath, true)};
        if (file == nullptr) {
            error = "couldn't create the file on this computer";
            return false;
        }

        char errorBuffer[CURL_ERROR_SIZE] {};
        ProgressData progressData {&progress};
        setCommonOptions(handle, account, url);
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 0L);
        curl_easy_setopt(handle, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(handle, CURLOPT_DIRLISTONLY, 0L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeToFile);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progressData);
        // Give up if the download stalls for 30 seconds.
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 30L);

        // If the server can't be reached or the download breaks off, start again from the
        // beginning after a pause. The progress callback is asked during the pause too, so
        // cancelling still works.
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, file);
        CURLcode result {curl_easy_perform(handle)};
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
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, file);
            result = curl_easy_perform(handle);
        }
        const bool written {file != nullptr && std::fclose(file) == 0};
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, nullptr);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, nullptr);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, nullptr);

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

    // Uploads one file using a handle that may be reused for the next one. commands
    // (optional) are sent in the file's folder once it's all there.
    bool performUpload(CURL* handle,
                       const FtpStorage::Account& account,
                       const std::string& localPath,
                       const std::string& url,
                       std::string& error,
                       const std::function<bool(long long, long long)>& progress,
                       struct curl_slist* commands)
    {
        const long long size {FtpStorage::localFileSize(localPath)};
        FILE* file {size < 0 ? nullptr : openFile(localPath, false)};
        if (file == nullptr) {
            error = "couldn't open the file on this computer";
            return false;
        }

        char errorBuffer[CURL_ERROR_SIZE] {};
        setCommonOptions(handle, account, url);
        curl_easy_setopt(handle, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(handle, CURLOPT_DIRLISTONLY, 0L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(handle, CURLOPT_READFUNCTION, readFromFile);
        curl_easy_setopt(handle, CURLOPT_READDATA, file);
        curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
        curl_easy_setopt(handle, CURLOPT_FTP_CREATE_MISSING_DIRS,
                         static_cast<long>(CURLFTP_CREATE_DIR_RETRY));
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
        // Give up if the upload stalls for 30 seconds.
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 30L);

        UploadProgressData progressData {&progress};
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
        if (progress) {
            curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, uploadProgressCallback);
            curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &progressData);
        }
        curl_easy_setopt(handle, CURLOPT_POSTQUOTE, commands);

        CURLcode result {curl_easy_perform(handle)};
        for (const int delay : RETRY_DELAYS) {
            if (result == CURLE_OK || !isTemporaryFailure(result))
                break;
            LOG(LogWarning) << "FtpStorage: " << describeError(result, errorBuffer)
                            << ", trying the upload again in " << delay << " seconds";
            if (!waitBeforeRetry(delay, progress ? [&progress]() { return progress(0, 0); } :
                                                   std::function<bool()> {})) {
                result = CURLE_ABORTED_BY_CALLBACK;
                break;
            }
            std::rewind(file);
            errorBuffer[0] = '\0';
            result = curl_easy_perform(handle);
        }
        std::fclose(file);
        // The handle may be used again after these have gone.
        curl_easy_setopt(handle, CURLOPT_POSTQUOTE, nullptr);
        curl_easy_setopt(handle, CURLOPT_READDATA, nullptr);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, nullptr);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, nullptr);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(handle, CURLOPT_UPLOAD, 0L);
        if (result != CURLE_OK) {
            error = result == CURLE_ABORTED_BY_CALLBACK ? "cancelled" :
                                                          describeError(result, errorBuffer);
            return false;
        }
        return true;
    }
} // namespace

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
    return performDownload(curl.handle, account, url, localPath, progress, error);
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
                        std::string& error,
                        const std::function<bool(long long, long long)>& progress,
                        const std::string& renameTo)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    // Once the file is all there, give it its real name. The commands run in the file's
    // folder. A leading * lets the delete fail, as there may be no file to replace.
    struct curl_slist* commands {nullptr};
    if (!renameTo.empty()) {
        std::string folderURL;
        std::string uploadName;
        splitFileURL(curl.handle, url, folderURL, uploadName);
        commands = curl_slist_append(commands, ("*DELE " + renameTo).c_str());
        commands = curl_slist_append(commands, ("RNFR " + uploadName).c_str());
        commands = curl_slist_append(commands, ("RNTO " + renameTo).c_str());
    }

    const bool succeeded {
        performUpload(curl.handle, account, localPath, url, error, progress, commands)};
    curl_slist_free_all(commands);
    if (!succeeded) {
        // Don't leave a part-uploaded file behind.
        if (!renameTo.empty()) {
            std::string removeError;
            removeFile(account, url, removeError);
        }
        return false;
    }
    return true;
}

bool FtpStorage::removeFile(const Account& account, const std::string& url, std::string& error)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    // QUOTE commands run straight after logging in, before curl changes folder, so the file
    // is given by its path from the login folder, which is how curl reads the address too.
    const size_t scheme {url.find("://")};
    const size_t pathStart {scheme == std::string::npos ? std::string::npos :
                                                          url.find('/', scheme + 3)};
    if (pathStart == std::string::npos || pathStart + 1 >= url.size()) {
        error = "bad address";
        return false;
    }
    int length {0};
    char* decoded {curl_easy_unescape(curl.handle, url.c_str() + pathStart + 1,
                                      static_cast<int>(url.size() - pathStart - 1), &length)};
    const std::string filePath {decoded != nullptr ?
                                    std::string(decoded, static_cast<size_t>(length)) :
                                    ""};
    curl_free(decoded);

    char errorBuffer[CURL_ERROR_SIZE] {};
    setCommonOptions(curl.handle, account, url.substr(0, pathStart + 1));
    struct curl_slist* commands {curl_slist_append(nullptr, ("DELE " + filePath).c_str())};
    curl_easy_setopt(curl.handle, CURLOPT_QUOTE, commands);
    // Just the command, no listing.
    curl_easy_setopt(curl.handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);
    const CURLcode result {curl_easy_perform(curl.handle)};
    curl_slist_free_all(commands);
    if (result != CURLE_OK) {
        error = describeError(result, errorBuffer);
        return false;
    }
    return true;
}

long long FtpStorage::localFileSize(const std::string& path)
{
    std::error_code code;
#if defined(_WIN64)
    const auto size = std::filesystem::file_size(
        Utils::String::stringToWideString(Utils::FileSystem::getGenericPath(path)), code);
#else
    const auto size = std::filesystem::file_size(Utils::FileSystem::getGenericPath(path), code);
#endif
    return code ? -1 : static_cast<long long>(size);
}

void FtpStorage::addToListing(const std::string& folderName, const std::string& fileName)
{
    const std::string key {Utils::String::toLower(folderName)};
    std::vector<std::string>& files {mListing.folders[key]};
    if (std::find(files.cbegin(), files.cend(), fileName) == files.cend())
        files.emplace_back(fileName);
    if (mListing.folderNames.find(key) == mListing.folderNames.cend())
        mListing.folderNames[key] = folderName;
    saveCache(mAccount, mListing);
}

// games247: games that are folders, such as PS3 games.

namespace
{
    // One line of a LIST answer. isFolder is 1 for a folder, 0 for a file and -1 when the
    // line couldn't be read (the name is then the whole line).
    struct ListLine {
        std::string name;
        long long size {-1};
        int isFolder {-1};
    };

    std::vector<std::string> splitWords(const std::string& line, std::vector<size_t>& starts)
    {
        std::vector<std::string> words;
        starts.clear();
        size_t i {0};
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            if (i >= line.size())
                break;
            const size_t start {i};
            while (i < line.size() && line[i] != ' ' && line[i] != '\t')
                ++i;
            starts.emplace_back(start);
            words.emplace_back(line.substr(start, i - start));
        }
        return words;
    }

    bool isNumber(const std::string& word)
    {
        return !word.empty() && std::all_of(word.cbegin(), word.cend(),
                                            [](char c) { return c >= '0' && c <= '9'; });
    }

    // Reads the lines of a LIST answer: the Unix style most servers use
    // (drwxr-xr-x 2 user group 4096 Nov 21 10:15 PS3_GAME) and the Windows (IIS) style
    // (11-21-26  10:15AM       <DIR>          PS3_GAME).
    std::vector<ListLine> parseList(const std::string& listing)
    {
        static const std::vector<std::string> months {"jan", "feb", "mar", "apr", "may", "jun",
                                                      "jul", "aug", "sep", "oct", "nov", "dec"};
        std::vector<ListLine> lines;
        size_t start {0};
        while (start < listing.size()) {
            size_t end {listing.find('\n', start)};
            if (end == std::string::npos)
                end = listing.size();
            std::string line {listing.substr(start, end - start)};
            start = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.compare(0, 6, "total ") == 0)
                continue;

            std::vector<size_t> starts;
            const std::vector<std::string> words {splitWords(line, starts)};
            ListLine entry;

            const char type {line[0]};
            if (words.size() >= 8 && words[0].size() >= 10 &&
                std::string("-dlbcps").find(type) != std::string::npos) {
                // Unix style. The size is the number before the month, the name comes after
                // the day and the time (or year).
                for (size_t i {1}; i + 3 < words.size(); ++i) {
                    if (std::find(months.cbegin(), months.cend(),
                                  Utils::String::toLower(words[i])) == months.cend() ||
                        !isNumber(words[i - 1]))
                        continue;
                    entry.size = std::stoll(words[i - 1]);
                    entry.name = line.substr(starts[i + 3]);
                    entry.isFolder = type == 'd' ? 1 : (type == 'l' ? -1 : 0);
                    break;
                }
                // A link: name -> target.
                const size_t arrow {entry.name.find(" -> ")};
                if (type == 'l' && arrow != std::string::npos)
                    entry.name = entry.name.substr(0, arrow);
            }
            else if (words.size() >= 4 && words[0].size() >= 8 && words[0][2] == '-' &&
                     isNumber(words[0].substr(0, 2))) {
                // Windows style.
                if (words[2] == "<DIR>") {
                    entry.isFolder = 1;
                    entry.name = line.substr(starts[3]);
                }
                else if (isNumber(words[2])) {
                    entry.isFolder = 0;
                    entry.size = std::stoll(words[2]);
                    entry.name = line.substr(starts[3]);
                }
            }
            if (entry.name.empty()) {
                // Some other style. The caller finds out what it is.
                entry.name = line;
                entry.isFolder = -1;
            }
            const size_t slash {entry.name.find_last_of('/')};
            if (slash != std::string::npos)
                entry.name = entry.name.substr(slash + 1);
            if (!entry.name.empty() && entry.name != "." && entry.name != "..")
                lines.emplace_back(entry);
        }
        return lines;
    }

    std::string escapePart(CURL* handle, const std::string& part)
    {
        char* escaped {curl_easy_escape(handle, part.c_str(), static_cast<int>(part.size()))};
        const std::string result {escaped != nullptr ? escaped : part};
        curl_free(escaped);
        return result;
    }

    std::string escapePath(CURL* handle, const std::string& path)
    {
        std::string result;
        for (const std::string& part : Utils::String::delimitedStringToVector(path, "/")) {
            if (!result.empty())
                result.append("/");
            result.append(escapePart(handle, part));
        }
        return result;
    }

    // Folders deeper than this are left out, in case a server lists a folder inside itself.
    constexpr int MAX_TREE_DEPTH {32};
} // namespace

std::string FtpStorage::serverPath(const std::string& url)
{
    const size_t scheme {url.find("://")};
    const size_t pathStart {scheme == std::string::npos ? std::string::npos :
                                                          url.find('/', scheme + 3)};
    if (pathStart == std::string::npos || pathStart + 1 >= url.size())
        return "";
    CurlHandle curl;
    if (curl.handle == nullptr)
        return "";
    int length {0};
    char* decoded {curl_easy_unescape(curl.handle, url.c_str() + pathStart + 1,
                                      static_cast<int>(url.size() - pathStart - 1), &length)};
    std::string path {decoded != nullptr ? std::string(decoded, static_cast<size_t>(length)) :
                                           ""};
    curl_free(decoded);
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

bool FtpStorage::runCommands(const Account& account,
                             const std::vector<std::string>& commands,
                             std::string& error)
{
    if (commands.empty())
        return true;
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    struct curl_slist* list {nullptr};
    for (const std::string& command : commands)
        list = curl_slist_append(list, command.c_str());

    char errorBuffer[CURL_ERROR_SIZE] {};
    // The commands run straight after logging in, so the address is just the server.
    std::string base {buildURL(account, {}, true)};
    const size_t scheme {base.find("://")};
    const size_t pathStart {base.find('/', scheme + 3)};
    if (pathStart != std::string::npos)
        base = base.substr(0, pathStart + 1);
    setCommonOptions(curl.handle, account, base);
    curl_easy_setopt(curl.handle, CURLOPT_QUOTE, list);
    curl_easy_setopt(curl.handle, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, discard);
    // Thousands of commands can take a while.
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl.handle, CURLOPT_ERRORBUFFER, errorBuffer);
    const CURLcode result {curl_easy_perform(curl.handle)};
    curl_slist_free_all(list);
    if (result != CURLE_OK) {
        error = describeError(result, errorBuffer);
        return false;
    }
    return true;
}

bool FtpStorage::listTree(const Account& account,
                          const std::string& url,
                          std::vector<TreeEntry>& entries,
                          bool& exists,
                          std::string& error,
                          const std::atomic<bool>* cancel)
{
    entries.clear();
    exists = false;
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    // Asks the server for one folder's listing. notFolder is set when it isn't a folder (or
    // isn't there): the server won't change into it.
    const auto listOne = [&](const std::string& folderURL, std::vector<ListLine>& lines,
                             bool& notFolder) {
        std::vector<std::string> names;
        std::string raw;
        int code {0};
        notFolder = false;
        if (!listFolder(curl.handle, account, folderURL, names, error, cancel, &raw, &code)) {
            if (code == CURLE_REMOTE_ACCESS_DENIED || code == CURLE_REMOTE_FILE_NOT_FOUND ||
                code == CURLE_FTP_COULDNT_RETR_FILE) {
                notFolder = true;
                error.clear();
                return true;
            }
            return false;
        }
        lines = parseList(raw);
        return true;
    };

    std::function<bool(const std::string&, const std::string&, int)> listInside;
    listInside = [&](const std::string& folderURL, const std::string& prefix, int depth) {
        std::vector<ListLine> lines;
        bool notFolder {false};
        if (!listOne(folderURL, lines, notFolder))
            return false;
        if (notFolder) {
            // The folder that was asked for isn't there, which isn't an error.
            if (depth == 1)
                return true;
            error = "folder not found on the server";
            return false;
        }
        if (depth == 1)
            exists = true;
        for (ListLine& line : lines) {
            if (cancel != nullptr && *cancel) {
                error = "cancelled";
                return false;
            }
            const std::string path {prefix + line.name};
            const std::string childURL {folderURL + escapePart(curl.handle, line.name)};
            if (line.isFolder == -1) {
                // Find out by trying to list it.
                std::vector<ListLine> inside;
                bool childNotFolder {false};
                if (!listOne(childURL + "/", inside, childNotFolder))
                    return false;
                line.isFolder = childNotFolder ? 0 : 1;
                if (!line.isFolder) {
                    FileInfo info;
                    if (!fileInfo(account, childURL, info, error))
                        return false;
                    line.size = info.size;
                }
            }
            if (line.isFolder == 1) {
                entries.emplace_back(TreeEntry {path, -1, true});
                if (depth < MAX_TREE_DEPTH &&
                    !listInside(childURL + "/", path + "/", depth + 1))
                    return false;
            }
            else {
                entries.emplace_back(TreeEntry {path, line.size, false});
            }
        }
        return true;
    };

    return listInside(url, "", 1);
}

std::vector<FtpStorage::TreeEntry> FtpStorage::localTree(const std::string& path)
{
    std::vector<TreeEntry> entries;
    const std::string root {Utils::FileSystem::getGenericPath(path) + "/"};
    for (const std::string& entry : Utils::FileSystem::getDirContent(path, true)) {
        const std::string generic {Utils::FileSystem::getGenericPath(entry)};
        if (generic.compare(0, root.size(), root) != 0)
            continue;
        const std::string relative {generic.substr(root.size())};
        if (Utils::FileSystem::isDirectory(generic))
            entries.emplace_back(TreeEntry {relative, -1, true});
        else
            entries.emplace_back(TreeEntry {relative, localFileSize(generic), false});
    }
    return entries;
}

long long FtpStorage::treeSize(const std::vector<TreeEntry>& entries)
{
    long long total {0};
    for (const TreeEntry& entry : entries) {
        if (!entry.isFolder && entry.size > 0)
            total += entry.size;
    }
    return total;
}

bool FtpStorage::sameTree(const std::vector<TreeEntry>& a, const std::vector<TreeEntry>& b)
{
    std::map<std::string, long long> files;
    for (const TreeEntry& entry : a) {
        if (!entry.isFolder)
            files[entry.path] = entry.size;
    }
    size_t count {0};
    for (const TreeEntry& entry : b) {
        if (entry.isFolder)
            continue;
        const auto it = files.find(entry.path);
        if (it == files.cend() || it->second != entry.size || entry.size < 0)
            return false;
        ++count;
    }
    return count == files.size();
}

bool FtpStorage::removeFolder(const Account& account, const std::string& url, std::string& error)
{
    std::vector<TreeEntry> entries;
    bool exists {false};
    if (!listTree(account, url, entries, exists, error, nullptr))
        return false;
    if (!exists)
        return true;
    const std::string root {serverPath(url)};
    if (root.empty()) {
        error = "bad address";
        return false;
    }
    // The files first, then the folders, deepest first.
    std::vector<std::string> commands;
    for (const TreeEntry& entry : entries) {
        if (!entry.isFolder)
            commands.emplace_back("DELE " + root + "/" + entry.path);
    }
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        if (it->isFolder)
            commands.emplace_back("RMD " + root + "/" + it->path);
    }
    commands.emplace_back("RMD " + root);
    return runCommands(account, commands, error);
}

bool FtpStorage::uploadFolder(const Account& account,
                              const std::string& localPath,
                              const std::string& folderURL,
                              const std::string& name,
                              std::string& error,
                              const std::function<bool(long long, long long)>& progress,
                              const std::atomic<bool>* cancel)
{
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }
    const std::vector<TreeEntry> local {localTree(localPath)};
    const long long total {treeSize(local)};
    const std::string partURL {folderURL + escapePart(curl.handle, name + ".part") + "/"};
    const std::string finalURL {folderURL + escapePart(curl.handle, name) + "/"};

    // What's left of an upload that didn't finish.
    if (!removeFolder(account, partURL, error))
        return false;

    // The folders first, so empty ones are there too. Uploading a file creates the folders
    // it goes in, but not empty ones.
    const std::string partPath {serverPath(partURL)};
    std::vector<std::string> folders {"*MKD " + partPath};
    for (const TreeEntry& entry : local) {
        if (entry.isFolder)
            folders.emplace_back("*MKD " + partPath + "/" + entry.path);
    }
    if (!runCommands(account, folders, error))
        return false;

    long long done {0};
    for (const TreeEntry& entry : local) {
        if (entry.isFolder)
            continue;
        const long long before {done};
        if (!performUpload(
                curl.handle, account, localPath + "/" + entry.path,
                partURL + escapePath(curl.handle, entry.path), error,
                [&progress, before, total](long long uploaded, long long) {
                    return progress(before + uploaded, total);
                },
                nullptr)) {
            if (error != "cancelled")
                error = entry.path + ": " + error;
            std::string removeError;
            removeFolder(account, partURL, removeError);
            return false;
        }
        done += entry.size;
        if (!progress(done, total)) {
            error = "cancelled";
            std::string removeError;
            removeFolder(account, partURL, removeError);
            return false;
        }
    }

    // Only put it in place once the server really has all of it.
    std::vector<TreeEntry> uploaded;
    bool exists {false};
    if (!listTree(account, partURL, uploaded, exists, error, cancel))
        return false;
    if (!exists || !sameTree(local, uploaded)) {
        error = "the uploaded folder isn't complete on the server";
        return false;
    }
    if (!removeFolder(account, finalURL, error))
        return false;
    return runCommands(account, {"RNFR " + partPath, "RNTO " + serverPath(finalURL)}, error);
}

bool FtpStorage::downloadFolder(const Account& account,
                                const std::string& url,
                                const std::string& localPath,
                                const std::function<bool(long long, long long)>& progress,
                                std::string& error,
                                const std::atomic<bool>* cancel)
{
    std::vector<TreeEntry> entries;
    bool exists {false};
    if (!listTree(account, url, entries, exists, error, cancel))
        return false;
    if (!exists) {
        error = NOT_A_FOLDER;
        return false;
    }
    CurlHandle curl;
    if (curl.handle == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    const std::string partPath {localPath + ".part"};
    Utils::FileSystem::removeDirectory(partPath, true);
    Utils::FileSystem::createDirectory(partPath);
    const long long total {treeSize(entries)};
    long long done {0};
    bool succeeded {true};
    for (const TreeEntry& entry : entries) {
        const std::string target {partPath + "/" + entry.path};
        if (entry.isFolder) {
            Utils::FileSystem::createDirectory(target);
            continue;
        }
        const long long before {done};
        if (!performDownload(curl.handle, account, url + escapePath(curl.handle, entry.path),
                             target,
                             [&progress, before, total](long long downloaded, long long) {
                                 return progress(before + downloaded, total);
                             },
                             error)) {
            if (error != "cancelled")
                error = entry.path + ": " + error;
            succeeded = false;
            break;
        }
        done += std::max(0LL, entry.size);
        if (!progress(done, total)) {
            error = "cancelled";
            succeeded = false;
            break;
        }
    }

    // renameFile() returns true when it fails.
    if (succeeded && Utils::FileSystem::renameFile(partPath, localPath, false)) {
        error = "couldn't save the game";
        succeeded = false;
    }
    if (!succeeded)
        Utils::FileSystem::removeDirectory(partPath, true);
    return succeeded;
}
