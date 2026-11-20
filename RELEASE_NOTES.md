# games247 release notes

What's new in each release, newest first. The update prompt shows the sections for every
release newer than the installed one, and each GitHub release uses its own section as its
description.

Add a `## <version>` section (the tag without the `v`) before tagging a release. Each line
starting with `- ` is one note; keep them short and written for players.

## 4.0.0-beta.37
- Nintendo Switch 2: a new system (roms folder switch2) that runs games in the built-in player with the Lichii core, taken from the FTP storage's cores folder (lichii_libretro.dll or lichii_libretro.dll.zip). Lichii needs your prod.keys in bios\lichii\keys, and firmware for most games.
- Nintendo Switch 2: when the Lichii core isn't in the FTP storage, or can't run the game, the game opens in standalone Lichii (Emulators\lichii\lichii.exe). "Lichii (Standalone)" can also be chosen as the emulator.

## 4.0.0-beta.36
- Nintendo Switch: games can run in the built-in player with the Eden core, taken from the FTP storage's cores folder (eden_libretro.dll or eden_libretro.dll.zip). Eden needs your prod.keys in bios\eden\keys, and firmware for most games.
- Nintendo Switch: when the Eden core isn't in the FTP storage, or can't run the game, the game opens in standalone Eden as before. "Eden (Standalone)" can still be chosen as the emulator.

## 4.0.0-beta.35
- Emulator cores can be kept in a "cores" folder in the FTP storage, next to the system folders, under their usual file name, as they are or zipped (for example eden_libretro.dll or eden_libretro.dll.zip). games247 downloads a core from there first, before libretro's servers, so cores that aren't on libretro's servers can be used. When the file there changes, games247 updates the core before the next game that uses it.

## 4.0.0-beta.34
- New option for VRR (FreeSync or G-Sync) displays: Other Settings > Variable refresh rate (VRR) for games. While a game runs in the built-in player, VSync is turned off and each game frame is shown exactly once, as soon as it's ready, so the display refreshes at the game's own rate (60, 50...) for smoother movement and quicker controls. Menus keep VSync. Leave it off on displays without VRR, where the picture would tear. FreeSync must be on in AMD Software.

## 4.0.0-beta.33
- PlayStation 3: games run in the built-in player with the RPCS3 core, downloaded the first time like other cores. Games can be disc folders (with PS3_DISC.SFB inside, for example "Game.ps3"), installed game folders, ISOs or .pkg files.
- PlayStation 3: the console's firmware (PS3UPDAT.PUP) is needed. If it isn't in the bios folder or the FTP storage's bios folder, games247 offers to download it from Sony (about 200 MB). The first game then takes a minute or two while the firmware is installed.
- PlayStation 3: the first time a game runs it is slow and stutters while RPCS3 prepares it; a note on the screen says so. There are no save states for PS3 games.
- BIOS files in folders inside the FTP storage's bios folder are now found up to three folders down, such as bios/pcsx2/bios/scph70004.bin for the PS2.

## 4.0.0-beta.32
- SNES: the border crop also crops black bars at the sides, as in Star Fox.

## 4.0.0-beta.31
- SNES: the border crop also works when the bars at the top and bottom are different heights, as on Street Fighter II's fighter select screen and fights. The log notes the bars it finds.

## 4.0.0-beta.30
- SNES: the black bars some games draw above and below the picture during play (Street Fighter II and other fighting games) are cropped and the picture is stretched taller to fill their space. It goes back to normal on screens that use the full height.

## 4.0.0-beta.29
- ZX Spectrum: each time a game starts you choose the machine to run it on: Spectrum +2 (the default), 128K or 48K. Tape games then ask for the loading speed as before.

## 4.0.0-beta.28
- Emulator cores update themselves. games247 checks the libretro build server for newer builds when it starts, and when you start a game whose core has one, it downloads the update first ("UPDATING EMULATOR ..."). Press B to skip the update; if the download fails, the game starts with the core you already have. Turn it off in Other Settings > Update emulator cores automatically.
- Xbox: leaving a game no longer waits 30 seconds for a save state, which the Xbox core can't make, and Xbox games no longer offer to resume from one.
- Xbox: black borders around the picture are cropped, as for N64, Saturn and PS2.
- When a core refuses to start a game, the message gives the core's reason. The Xbox core can only run one game until games247 is restarted.

## 4.0.0-beta.27
- Console game lists show one entry for the regional versions of a game, such as "Sonic the Hedgehog (E)", "(USA)" and "(J)". The list shows your region's version, and starting the game lets you choose another. Discs, betas, prototypes and demos stay separate entries. Turn it off in Other Settings > Show regional versions as one game.

## 4.0.0-beta.26
- Xbox: the screen no longer freezes on the loading screen when a game starts. The xemu core now draws with OpenGL; on Windows with AMD graphics its Vulkan renderer froze the whole games247 screen while the game ran behind it. Vulkan can still be chosen with `xemu_renderer = "vulkan"` in your own copy of xemu_libretro.opt in cores/options.

## 4.0.0-beta.25
- Xbox: the picture no longer stops on AMD graphics while the game carries on. The xemu core's Vulkan "fast paths", which break on AMD, are switched off.
- Cloud saves: when a game's state can't be saved, an older state file is no longer uploaded in its place.

## 4.0.0-beta.24
- Arcade game lists show each game once instead of every version of it. Starting a game with several versions on disk (World, USA, Japan...) asks which one to play. Switch it off with "Show arcade clones as one game" in the menu's Other Settings.

## 4.0.0-beta.23
- PlayStation 2 games run in games247 with the LRPS2 core. Put a PS2 BIOS from your own console in the bios folder (any name) or on the FTP storage; games247 finds it and offers to download it.
- PlayStation 2 games fill the screen: black borders are cropped automatically.
- Analog sticks: pushing a stick all the way up or left no longer flips it the other way in some games (Xbox 360).

## 4.0.0-beta.22
- Sega Saturn: games247 checks for the Saturn BIOS and offers to download it from the FTP storage's bios folder. BIOS files under any name are recognised.
- Sega Saturn games fill the screen: black borders around the picture are cropped automatically, as for the N64.

## 4.0.0-beta.21
- Xbox and Xbox 360: games247 no longer waits on the core's picture while the core is still drawing, which could freeze the game on a black screen and then close games247.
- If games247 ever closes unexpectedly, the log file says where it happened.

## 4.0.0-beta.20
- Game Boy games show the green of the original Game Boy screen instead of grey.
- PlayStation: games247 checks for the BIOS and offers to download it from the FTP storage's bios folder. BIOS files under any name are recognised.
- Xbox, Amiga, Atari ST and PlayStation: BIOS files on the FTP storage are found in folders inside the bios folder (such as bios/xemu) and straight after they're uploaded.

## 4.0.0-beta.19
- N64 and other 3D games: saving or loading a state no longer makes the picture disappear.

## 4.0.0-beta.18
- Original Xbox games run in games247 with the xemu core. Put your Xbox BIOS and MCPX boot ROM in the bios folder (any name) or on the FTP storage; the blank hard disk is downloaded for you.
- Xbox 360 games run in games247 with the Xenia Edge core (needs a Vulkan graphics card).
- The built-in player supports Vulkan cores.
- Save states and game saves sync between computers through the FTP storage.
- The update prompt shows what's new.
- Handhelds (Game Boy, Game Gear, Lynx and more) get an LCD screen filter with the colours and motion blur of the real screens. Switch it off in the pause menu.

## 4.0.0-beta.17
- Amstrad CPC games in zip files start automatically instead of stopping at BASIC.
- Atari ST: joypad buttons match the C64 and Amiga (A fire, B up, Y space, X autofire).
- Atari ST: the artificial floppy drive noise is off.

## 4.0.0-beta.16
- Atari ST: games247 checks for the TOS ROM and offers to download it from the FTP storage's bios folder.
- Amiga: X is the second fire button.

## 4.0.0-beta.15
- Amiga: choose the Amiga 1200, 600, 500+ or 500 when a game starts.
- Amiga: the Kickstart ROMs are checked and can be downloaded from the FTP storage.
- Amiga: C64-style joypad controls.

## 4.0.0-beta.14
- Amstrad CPC: C64-style controls, border cropping and a choice of fast or real-speed tape loading.

## 4.0.0-beta.13
- ZX Spectrum: C64-style controls and the border shows while a tape loads.
- Updates install with one press.

## 4.0.0-beta.12
- N64: black borders are cropped automatically.

## 4.0.0-beta.11
- FTP storage: the game list loads from a saved listing and is checked in the background, with retries and a cloud symbol for games not downloaded yet.

## 4.0.0-beta.10
- N64 on Windows: fixed graphics cores that need a particular OpenGL version.
