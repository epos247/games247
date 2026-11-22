//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  PlatformUtil.h
//
//  Platform utility functions.
//

#ifndef ES_CORE_UTILS_PLATFORM_UTIL_H
#define ES_CORE_UTILS_PLATFORM_UTIL_H

#include <string>

#if defined(_WIN64)
#include <windows.h>
#endif

namespace Utils
{
    namespace Platform
    {
        enum QuitMode {
            QUIT = 0,
            REBOOT = 1,
            POWEROFF = 2
        };

        int runRebootCommand();
        int runPoweroffCommand();
        int runSuspendCommand();

        // Uses UTF-8 for Unix and UTF-16 for Windows.
        int runSystemCommand(const std::string& cmdUtf8, bool blocking);
        int runSystemCommand(const std::wstring& cmdUtf16, bool blocking);

        int launchGameUnix(const std::string& cmdUtf8,
                           const std::string& startDirectory,
                           bool runInBackground);
        int launchGameWindows(const std::wstring& cmdUtf16,
                              const std::wstring& startDirectory,
                              bool runInBackground,
                              bool hideWindow);

        unsigned int getTaskbarState();
        void hideTaskbar();
        void revertTaskbarState(unsigned int& state);

        // Clean, normal shutdown.
        int quitES(QuitMode mode = QuitMode::QUIT);
        void processQuitMode();

        // Immediately shut down the application as cleanly as possible.
        void emergencyShutdown();

        // games247: while a game runs in the built-in player the process gets above normal
        // priority (Windows), so other programs' work doesn't hold up its frames.
        void setGamePriority(bool playing);
        // games247: call at the start of a background thread (FTP listing, cloud saves). On
        // Windows its processor, disk and memory priority are lowered, so its work can't
        // make a running game stutter.
        void setBackgroundThread();
        // games247: logs where a crash or abort happened (games247, a core or a driver), as
        // Windows only shows the app closing. Call at startup, and again after loading or
        // unloading a libretro core, as a core can put its own crash handler in place (which
        // would crash too once the core is unloaded). Returns true if something else had
        // replaced it. The context (such as the core that ran last) goes in the crash line.
        bool installCrashLogger();
        void setCrashContext(const std::string& context);

        static QuitMode sQuitMode = QuitMode::QUIT;
        // This is simply to get rid of a GCC false positive -Wunused-variable compiler warning.
        static QuitMode sQuitModeDummy = sQuitMode;

    } // namespace Platform

} // namespace Utils

#endif // ES_CORE_UTILS_PLATFORM_UTIL_H
