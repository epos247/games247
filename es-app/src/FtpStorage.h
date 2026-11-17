//  SPDX-License-Identifier: MIT
//
//  games247
//  FtpStorage.h
//
//  Games stored on an FTP server. The FTP folder holds one folder per system, named like the
//  local ROM folders (c64, amiga, nes, snes...). When the systems load, the games in those
//  folders are added to the game lists next to the local games. Launching one of them
//  downloads it into the matching local ROM folder first (see GuiFtpDownloader), after which
//  it's an ordinary local game.
//
//  FTPS is used when the server offers it, otherwise plain FTP.
//
//  The listing is saved in ftp_cache.txt in the games247 folder. When games247 starts, the
//  saved listing is used straight away and the server is checked in the background; any new
//  games are added to the game lists once the check is done (see SystemData::addNewFtpGames).
//

#ifndef GAMES247_FTP_STORAGE_H
#define GAMES247_FTP_STORAGE_H

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class FtpStorage
{
public:
    struct Account {
        std::string host;
        std::string username;
        std::string password;
        std::string path;
        bool allowSelfSigned {false};
    };

    // What's on the server: one entry per folder.
    struct Listing {
        // Folder name in lower case -> file names in that folder.
        std::map<std::string, std::vector<std::string>> folders;
        // Real (not lower-cased) folder names on the server.
        std::map<std::string, std::string> folderNames;
        bool operator==(const Listing& other) const
        {
            return folders == other.folders && folderNames == other.folderNames;
        }
    };

    static FtpStorage& getInstance();
    ~FtpStorage() { stopBackgroundCheck(); }

    // The account from the settings.
    static Account accountFromSettings();
    // True when FTP storage is switched on and an address has been entered.
    static bool isEnabled();

    // Called once each time the systems are loaded. Uses the saved listing if there is one
    // for this account and checks the server in the background, otherwise lists the server
    // straight away. Clears everything if FTP storage is switched off.
    void refresh();

    // Called every frame. Once the background check has finished and found changes, the new
    // listing replaces the old one (and is saved), and this returns true with the folders the
    // old listing didn't have. Returns false otherwise.
    bool takeChangedListing(std::vector<std::string>& newFolders);
    // Stops the background check, if one is running. Quick, as the check notices within a
    // second or so.
    void stopBackgroundCheck();

    // The file names in the FTP folder for a system, or nullptr if the server has no such
    // folder. The folder name is the last part of the system's local ROM path, compared
    // without regard to case.
    const std::vector<std::string>* getFiles(const std::string& folderName) const;

    // Remembers that a game in the game lists is only on the FTP server so far.
    void addRemoteGame(const std::string& localPath,
                       const std::string& folderName,
                       const std::string& fileName);
    // True if the game at this local path is on the FTP server and hasn't been downloaded yet.
    bool isRemoteGame(const std::string& localPath) const;
    // The address to download a remote game from.
    std::string getRemoteURL(const std::string& localPath) const;
    // The address of a file in one of the server's folders, such as the bios folder.
    std::string getFileURL(const std::string& folderName, const std::string& fileName) const;
    // Called once a game has been downloaded.
    void forgetRemoteGame(const std::string& localPath);

    // Connects and lists the FTP folder. On success, message says how many system folders
    // were found, otherwise it holds the error.
    static bool testConnection(const Account& account, std::string& message);

    // Downloads a file to a local path. progress is called now and then with the bytes so far
    // and the total (0 if unknown); returning false from it cancels the download. Runs on the
    // calling thread.
    static bool download(const Account& account,
                         const std::string& url,
                         const std::string& localPath,
                         const std::function<bool(long long, long long)>& progress,
                         std::string& error);

    // A file on the server: whether it's there, its size and its modification time (seconds
    // since 1970, or -1 if the server doesn't say).
    struct FileInfo {
        bool exists {false};
        long long size {-1};
        long long time {-1};
        bool operator==(const FileInfo& other) const
        {
            return exists == other.exists && size == other.size && time == other.time;
        }
    };
    // Looks a file up on the server. A missing file (or folder) isn't an error: info.exists
    // is false and this returns true. Runs on the calling thread.
    static bool fileInfo(const Account& account,
                         const std::string& url,
                         FileInfo& info,
                         std::string& error);

    // Uploads a local file, replacing the file on the server and creating missing folders.
    // Runs on the calling thread.
    static bool upload(const Account& account,
                       const std::string& localPath,
                       const std::string& url,
                       std::string& error);

    // Lists the server's bios folder now (not the saved listing, so files uploaded since
    // games247 started are seen), and the folders inside it one level down. files gets each
    // file's path inside the bios folder, such as "scph5502.bin" or "xemu/mcpx_1.0.bin", and
    // serverFolder the bios folder's name on the server (bios, BIOS...). Runs on the calling
    // thread; cancel (optional) stops it when set.
    static bool listBiosFolder(const Account& account,
                               std::vector<std::string>& files,
                               std::string& serverFolder,
                               std::string& error,
                               const std::atomic<bool>* cancel);

    // Lists the files in one of the server's top folders now (not the saved listing), such as
    // the cores folder. name is compared without regard to case; serverFolder gets the name
    // on the server. A missing folder isn't an error: files is empty and this returns true.
    // Runs on the calling thread; cancel (optional) stops it when set.
    static bool listTopFolder(const Account& account,
                              const std::string& name,
                              std::vector<std::string>& files,
                              std::string& serverFolder,
                              std::string& error,
                              const std::atomic<bool>* cancel);

    // The address of a file or folder on the server. Each part of the path is URL-encoded.
    static std::string buildURL(const Account& account,
                                const std::vector<std::string>& parts,
                                bool isFolder);

private:
    FtpStorage() = default;

    // Lists the names in a folder on the server (files and folders alike). Reusing the same
    // curl handle reuses the connection, so the server isn't logged in to for every folder.
    // cancel (optional) stops the listing when set.
    static bool listFolder(void* curlHandle,
                           const Account& account,
                           const std::string& url,
                           std::vector<std::string>& names,
                           std::string& error,
                           const std::atomic<bool>* cancel);
    // Lists the system folders on the server and the games in each one.
    static bool fetchListing(const Account& account,
                             Listing& listing,
                             std::string& error,
                             const std::atomic<bool>* cancel);

    static std::string getCachePath();
    static bool loadCache(const Account& account, Listing& listing);
    static void saveCache(const Account& account, const Listing& listing);

    struct BackgroundCheck {
        std::thread thread;
        std::atomic<bool> cancel {false};
        std::atomic<bool> done {false};
        bool succeeded {false};
        Listing listing;
        std::string error;
    };
    void startBackgroundCheck();

    Listing mListing;
    std::unique_ptr<BackgroundCheck> mCheck;
    // Local path -> URL of games that haven't been downloaded yet.
    std::map<std::string, std::string> mRemoteGames;
    Account mAccount;
};

#endif // GAMES247_FTP_STORAGE_H
