//  SPDX-License-Identifier: MIT
//
//  games247
//  FtpCores.h
//
//  libretro cores kept in the FTP storage's cores folder, for cores that aren't on the libretro
//  build server or that should be a particular build. The folder sits next to the system
//  folders and holds each core under its usual file name, as it is or zipped:
//  eden_libretro.dll or eden_libretro.dll.zip (.dylib and .so for macOS and Linux). A core
//  that's there is downloaded from the FTP storage rather than the build server, and when the
//  file there changes (its size or date), games247 offers it as a core update.
//

#ifndef GAMES247_FTP_CORES_H
#define GAMES247_FTP_CORES_H

#include <atomic>
#include <string>
#include <vector>

namespace FtpCores
{
    constexpr const char* FOLDER {"cores"};

    struct Core {
        std::string url;
        bool zipped {false};
        long long size {-1};
        long long time {-1};
    };

    // The core's file in a listing of the cores folder: "eden_libretro.dll.zip" or
    // "eden_libretro.dll" (the zip wins), compared without regard to case. "" if it isn't there.
    std::string fileInListing(const std::string& coreName, const std::vector<std::string>& files);

    // Looks for a core in the FTP storage's cores folder. Returns true and fills core when it's
    // there. Returns false when it isn't, or when the server couldn't be asked (then error says
    // why). Runs on the calling thread; cancel (optional) stops it.
    bool find(const std::string& coreName,
              Core& core,
              std::string& error,
              const std::atomic<bool>* cancel);

    // Says which file on the server a core was installed from, for core_versions.txt.
    std::string version(const Core& core);

    // Downloads the core and installs it into games247's core folder. Runs on the calling
    // thread: downloaded and total get the bytes so far and the file's size, and cancel stops
    // it. Returns false and sets error if it didn't work.
    bool install(const std::string& coreName,
                 const Core& core,
                 std::atomic<long long>& downloaded,
                 std::atomic<long long>& total,
                 const std::atomic<bool>& cancel,
                 std::string& error);
} // namespace FtpCores

#endif // GAMES247_FTP_CORES_H
