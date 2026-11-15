//  SPDX-License-Identifier: MIT
//
//  games247
//  CloudSaves.h
//
//  Save states and game saves (save RAM) kept in sync between computers through the FTP
//  storage. Each game's files go to saves/<system folder>/ on the server, named after the
//  game: <game>.state and <game>.srm.
//
//  - When a game in the built-in player is quit, its state is saved and both files are
//    uploaded in the background (queueUpload).
//  - When a game is launched, the server is checked (check). A game save that changed on the
//    server is downloaded and used. If there's a save state, the player asks whether to
//    resume from it or start a new game. The check runs once the core has loaded the game
//    but before its first frame, so a core that can't run here still falls back to RetroArch.
//
//  cloud_saves.txt in the games247 folder records each file's size and time on the server
//  as of this computer's last upload or download of it, so a file is only downloaded when
//  another computer has changed it.
//

#ifndef GAMES247_CLOUD_SAVES_H
#define GAMES247_CLOUD_SAVES_H

#include <atomic>
#include <string>
#include <vector>

namespace CloudSaves
{
    // True when FTP storage and cloud saves are both switched on.
    bool isEnabled();

    struct CheckResult {
        // There's a save state for the game on the server.
        bool hasState {false};
        // The server's save state differs from the one this computer last synced (or there's
        // no local copy), so resuming needs a download.
        bool stateNeedsDownload {false};
        // The game save was downloaded to this file (empty if there was nothing new). The
        // player gives it to the core instead of the local one.
        std::string downloadedSaveRAM;
    };

    // Looks for the game's files on the server and downloads a changed game save. Waits for
    // uploads still in progress first. Slow (network): run it on a worker thread. Returns
    // false with an error if the server couldn't be reached, or with "cancelled" once
    // cancel is set (nothing is downloaded after that).
    bool check(const std::string& systemFolder,
               const std::string& romPath,
               const std::string& statePath,
               CheckResult& result,
               std::string& error,
               const std::atomic<bool>& cancel);

    // Downloads the server's save state to statePath. Slow: run it on a worker thread.
    bool downloadState(const std::string& systemFolder,
                       const std::string& romPath,
                       const std::string& statePath,
                       std::string& error);

    // Uploads the game's state and save RAM files (whichever exist) in the background.
    void queueUpload(const std::string& systemFolder,
                     const std::string& romPath,
                     const std::string& statePath,
                     const std::string& saveRAMPath);

    // Waits for the uploads to finish, at most timeoutSeconds. Called when games247 quits.
    void shutdown(int timeoutSeconds);
} // namespace CloudSaves

#endif // GAMES247_CLOUD_SAVES_H
