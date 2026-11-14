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

#ifndef GAMES247_FTP_STORAGE_H
#define GAMES247_FTP_STORAGE_H

#include <functional>
#include <map>
#include <string>
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

    static FtpStorage& getInstance();

    // The account from the settings.
    static Account accountFromSettings();
    // True when FTP storage is switched on and an address has been entered.
    static bool isEnabled();

    // Lists the system folders on the server and the games in each one. Called once each time
    // the systems are loaded. Clears everything if FTP storage is switched off.
    void refresh();

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

    // The address of a file or folder on the server. Each part of the path is URL-encoded.
    static std::string buildURL(const Account& account,
                                const std::vector<std::string>& parts,
                                bool isFolder);

private:
    FtpStorage() = default;

    // Lists the names in a folder on the server (files and folders alike).
    static bool listFolder(const Account& account,
                           const std::string& url,
                           std::vector<std::string>& names,
                           std::string& error);

    // Folder name in lower case -> file names in that folder.
    std::map<std::string, std::vector<std::string>> mFolders;
    // Real (not lower-cased) folder names on the server.
    std::map<std::string, std::string> mFolderNames;
    // Local path -> URL of games that haven't been downloaded yet.
    std::map<std::string, std::string> mRemoteGames;
    Account mAccount;
};

#endif // GAMES247_FTP_STORAGE_H
