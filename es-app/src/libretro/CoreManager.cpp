//  SPDX-License-Identifier: MIT
//
//  games247
//  CoreManager.cpp
//
//  Finds and installs libretro cores for the built-in player.
//

#include "libretro/CoreManager.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/PlatformUtil.h"
#include "utils/StringUtil.h"

#include <regex>

namespace
{
    const char* coreExtension()
    {
#if defined(_WIN64)
        return ".dll";
#elif defined(__APPLE__)
        return ".dylib";
#else
        return ".so";
#endif
    }
} // namespace

namespace CoreManager
{
    std::string coreNameFromCommand(const std::string& command)
    {
        // Multi-file content (subsystems) isn't supported by the built-in player yet.
        if (command.find("--subsystem") != std::string::npos)
            return "";

        static const std::regex pattern {
            R"(%EMULATOR_RETROARCH%.*\s-L\s+"?%CORE_RETROARCH%[\\/]([A-Za-z0-9_]+_libretro)\.(dll|dylib|so)\b)"};
        std::smatch match;
        if (std::regex_search(command, match, pattern))
            return match[1].str();
        return "";
    }

    std::string coresDirectory()
    {
        return Utils::FileSystem::getAppDataDirectory() + "/cores";
    }

    std::string coreFileName(const std::string& coreName)
    {
        return coreName + coreExtension();
    }

    std::string findInstalledCore(const std::string& coreName)
    {
        const std::string ownCore {coresDirectory() + "/" + coreFileName(coreName)};
        if (Utils::FileSystem::isRegularFile(ownCore))
            return ownCore;

#if defined(__APPLE__)
        // Reuse cores the user already has in RetroArch.
        const std::string retroArchCore {Utils::FileSystem::getHomePath() +
                                         "/Library/Application Support/RetroArch/cores/" +
                                         coreFileName(coreName)};
        if (Utils::FileSystem::isRegularFile(retroArchCore))
            return retroArchCore;
#endif
        return "";
    }

    bool isFtpOnly(const std::string& coreName)
    {
        return coreName == "eden_libretro" || coreName == "lichii_libretro";
    }

    std::string downloadURL(const std::string& coreName)
    {
        if (isFtpOnly(coreName))
            return "";
        const std::string base {"https://buildbot.libretro.com/nightly/"};
#if defined(_WIN64)
        return base + "windows/x86_64/latest/" + coreFileName(coreName) + ".zip";
#elif defined(__APPLE__) && defined(__aarch64__)
        return base + "apple/osx/arm64/latest/" + coreFileName(coreName) + ".zip";
#elif defined(__APPLE__)
        return base + "apple/osx/x86_64/latest/" + coreFileName(coreName) + ".zip";
#elif defined(__linux__) && defined(__x86_64__)
        return base + "linux/x86_64/latest/" + coreFileName(coreName) + ".zip";
#else
        static_cast<void>(base);
        return "";
#endif
    }

    std::string indexURL()
    {
        const std::string url {downloadURL("core")};
        const size_t slash {url.rfind('/')};
        return slash == std::string::npos ? "" : url.substr(0, slash + 1) + ".index-extended";
    }

    int extractArchive(const std::string& archivePath, const std::string& directory)
    {
        Utils::FileSystem::createDirectory(directory);

        // Windows 10 and later and macOS both ship bsdtar, which can extract zip and 7z files.
#if defined(_WIN64)
        const char* systemRoot {getenv("SystemRoot")};
        const std::string tar {std::string {systemRoot ? systemRoot : "C:\\Windows"} +
                               "\\System32\\tar.exe"};
        const std::string command {"\"" + tar + "\" -xf \"" +
                                   Utils::String::replace(archivePath, "/", "\\") + "\" -C \"" +
                                   Utils::String::replace(directory, "/", "\\") + "\""};
        return Utils::Platform::runSystemCommand(Utils::String::stringToWideString(command),
                                                 true);
#else
        const std::string command {"/usr/bin/tar -xf " +
                                   Utils::FileSystem::getEscapedPath(archivePath) + " -C " +
                                   Utils::FileSystem::getEscapedPath(directory)};
        return Utils::Platform::runSystemCommand(command, true);
#endif
    }

    bool installFromZip(const std::string& zipPath,
                        const std::string& coreName,
                        std::string& error)
    {
        const std::string unpackDirectory {coresDirectory() + "/unpacking"};
        Utils::FileSystem::removeDirectory(unpackDirectory, true);
        const int result {extractArchive(zipPath, unpackDirectory)};
        Utils::FileSystem::removeFile(zipPath);

        const std::string unpacked {unpackDirectory + "/" + coreFileName(coreName)};
        if (result != 0 || !Utils::FileSystem::isRegularFile(unpacked)) {
            Utils::FileSystem::removeDirectory(unpackDirectory, true);
            error = "couldn't unpack the core (" + std::to_string(result) + ")";
            LOG(LogError) << "CoreManager: " << error << " from " << zipPath;
            return false;
        }
        const bool installed {installFile(unpacked, coreName, error)};
        Utils::FileSystem::removeDirectory(unpackDirectory, true);
        return installed;
    }

    bool installFile(const std::string& path, const std::string& coreName, std::string& error)
    {
        const std::string directory {coresDirectory()};
        Utils::FileSystem::createDirectory(directory);
        const std::string corePath {directory + "/" + coreFileName(coreName)};
        // The old core is moved aside first, as Windows can't rename over a file, and put
        // back if the new one can't take its place. renameFile() returns true when it fails.
        const std::string oldCore {corePath + ".old"};
        Utils::FileSystem::removeFile(oldCore);
        const bool hadCore {Utils::FileSystem::isRegularFile(corePath)};
        if (hadCore && Utils::FileSystem::renameFile(corePath, oldCore, true)) {
            Utils::FileSystem::removeFile(path);
            error = "couldn't replace the core";
            LOG(LogError) << "CoreManager: couldn't move the old core " << corePath << " aside";
            return false;
        }
        if (Utils::FileSystem::renameFile(path, corePath, true)) {
            if (hadCore)
                Utils::FileSystem::renameFile(oldCore, corePath, true);
            Utils::FileSystem::removeFile(path);
            error = "couldn't replace the core";
            LOG(LogError) << "CoreManager: couldn't move " << path << " to " << corePath;
            return false;
        }
        Utils::FileSystem::removeFile(oldCore);
        LOG(LogInfo) << "CoreManager: installed " << corePath;
        return true;
    }
} // namespace CoreManager
