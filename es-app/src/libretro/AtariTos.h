//  SPDX-License-Identifier: MIT
//
//  games247
//  AtariTos.h
//
//  The TOS ROM for the Atari ST core (Hatari). Hatari only starts with a TOS ROM named tos.img
//  in the bios folder, so a TOS ROM under another name (tos104uk.img, for instance) is copied
//  to that name.
//

#ifndef GAMES247_ATARI_TOS_H
#define GAMES247_ATARI_TOS_H

#include <string>
#include <vector>

namespace AtariTos
{
    // The file Hatari needs.
    constexpr char TOS_FILE[] {"tos.img"};

    // True for the Hatari core (not Hatari B, which has a TOS built in).
    bool isAtariCore(const std::string& corePath);

    // If there's no tos.img, copies the first TOS ROM in the folder to that name.
    void identifyRoms(const std::string& biosDirectory);

    // {"tos.img"} if it isn't in the folder, otherwise nothing.
    std::vector<std::string> missingFiles(const std::string& biosDirectory);

    // Files in the FTP storage's bios folder that could be TOS ROMs and aren't in the local
    // folder yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory);
} // namespace AtariTos

#endif // GAMES247_ATARI_TOS_H
