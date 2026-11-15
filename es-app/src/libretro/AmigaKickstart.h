//  SPDX-License-Identifier: MIT
//
//  games247
//  AmigaKickstart.h
//
//  Kickstart ROMs for the Amiga core (PUAE). Each Amiga model needs its own Kickstart in the
//  player's bios folder. PUAE looks for them under fixed names (kick34005.A500 and so on, or
//  the Amiga Forever and TOSEC names), so ROMs under any other name are recognised by their
//  contents and copied to the name PUAE looks for. Amiga Forever's encrypted ROMs are decoded
//  with its rom.key, which must be in the same folder.
//

#ifndef GAMES247_AMIGA_KICKSTART_H
#define GAMES247_AMIGA_KICKSTART_H

#include <string>
#include <vector>

namespace AmigaKickstart
{
    // True for the PUAE core.
    bool isAmigaCore(const std::string& corePath);

    // The model PUAE will run: the chosen one (a puae_model value) unless that's empty or
    // "auto", otherwise what PUAE picks itself: CD32 for compact discs, A1200 for hard drives
    // and WHDLoad games, A500 for floppy disks.
    std::string modelFor(const std::string& romPath, const std::string& chosenModel);

    // The Kickstart files a model needs, under the names PUAE looks for.
    std::vector<std::string> filesFor(const std::string& model);

    // Copies Kickstart ROMs in the folder that have other names, or are encrypted, to the
    // names PUAE looks for. Files already there are left alone.
    void identifyRoms(const std::string& biosDirectory);

    // The files of filesFor(model) that aren't in the folder.
    std::vector<std::string> missingFiles(const std::string& biosDirectory,
                                          const std::string& model);

    // Files in the FTP storage's bios folder that could be Kickstart ROMs (or Amiga Forever's
    // rom.key) and aren't in the local folder yet.
    std::vector<std::string> ftpCandidates(const std::string& biosDirectory,
                                            const std::vector<std::string>& serverFiles);
} // namespace AmigaKickstart

#endif // GAMES247_AMIGA_KICKSTART_H
