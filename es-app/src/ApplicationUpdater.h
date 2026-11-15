//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ApplicationUpdater.h
//
//  Checks for application updates.
//  Used in conjunction with GuiApplicationUpdater.
//

#ifndef ES_APP_APPLICATION_UPDATER_H
#define ES_APP_APPLICATION_UPDATER_H

#include "AsyncHandle.h"
#include "HttpReq.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class ApplicationUpdater : public AsyncHandle
{
public:
    virtual ~ApplicationUpdater();
    static ApplicationUpdater& getInstance();

    void checkForUpdates();
    void updaterThread();
    bool downloadFile();
    void update();
    void parseFile();
    void compareVersions();
    bool getResults();

    struct Package {
        std::string name;
        std::string version;
        std::string filename;
        std::string url;
        std::string md5;
        std::string message;
    };

    const std::string& getResultsString() { return mResults; }
    const Package& getPackageInfo() { return mPackage; }
    // games247: what's new in each release newer than this one, newest first (from the
    // feed's "notes", which come from RELEASE_NOTES.md). Empty if the feed has none.
    const std::string& getReleaseNotes() { return mReleaseNotes; }

private:
    ApplicationUpdater();

    struct Release {
        std::string releaseType;
        std::string version;
        std::string releaseNum;
        std::string androidVersionCode;
        std::string date;
        std::vector<Package> packages;
    };

    enum class PackageType {
        WINDOWS_PORTABLE,
        WINDOWS_INSTALLER,
        MACOS_APPLE,
        MACOS_INTEL,
        LINUX_APPIMAGE,
        LINUX_AARCH64_APPIMAGE,
        LINUX_STEAM_DECK_APPIMAGE,
        UNKNOWN
    };

    PackageType mPackageType;
    Package mPackage;
    std::string mUrl;
    std::string mResults;
    std::string mLogInfo;
    std::string mLogWarning;
    std::string mLogError;
    unsigned int mTimer;
    unsigned int mMaxTime;
    std::atomic<bool> mAbortDownload;
    std::atomic<bool> mApplicationShutdown;
    bool mCheckedForUpdate;
    bool mNewVersion;

    std::unique_ptr<std::thread> mThread;
    std::unique_ptr<HttpReq> mRequest;
    AsyncHandleStatus mStatus;

    Release mStableRelease;
    Release mPrerelease;

    struct ReleaseNote {
        std::string version;
        std::string text;
    };
    std::vector<ReleaseNote> mNotes;
    std::string mReleaseNotes;
};

#endif // ES_APP_APPLICATION_UPDATER_H
