# games247 fork notes

## Git remotes

- `upstream`: https://gitlab.com/es-de/emulationstation-de.git (ES-DE)
- Work happens on the `games247` branch. To pull in new ES-DE changes:

  ```bash
  git fetch upstream && git merge upstream/master
  ```

## Rebrand status

Done:
- App name, window title, menu strings, log messages and command-line help say games247.
- Translations (`locale/po/*.po`, `locale/es-de.pot`) were updated to match, so translated menus keep working.
- User data directory is `~/.games247` (iOS: `Documents/games247`, Linux system data: `share/games247`).
- Build targets and packages are named `games247`. macOS bundle ID: `com.games247.app`.
- New splash wordmark at `resources/graphics/splash.svg`.
- New app icon. The master artwork is `es-app/assets/com.games247.app.svg`; `games247.icns` (macOS), `games247.ico` (Windows) and `resources/graphics/window_icon_256.png` are generated from it with `tools/icons/generate_icons.sh`.
- The application updater is off by default (`ApplicationUpdaterFrequency` = `never`). Its URL points to a placeholder, `https://updates.games247.example/latest_release.json`.

Still to do:
- **Scraper credentials**: `es-app/src/scrapers/ScreenScraper.h` and `GamesDBJSONScraperResources.cpp` contain ES-DE's own developer keys for ScreenScraper and TheGamesDB. Register games247's own developer accounts and replace them before you distribute builds.
- **Website and updater**: replace the `games247.example` placeholders with your real domain once you have one.
- **Android / iOS** packaging was not rebranded (Android app ID constant only).
- **Docs** (`*.md` user guides) still describe ES-DE.

## Building and running on macOS

One-time setup: Xcode Command Line Tools, Homebrew, then:

```bash
brew install clang-format cmake pkg-config meson nasm yasm
tools/macOS_dependencies_setup.sh
tools/macOS_dependencies_build.sh
```

Build, bundle and launch:

```bash
cmake . && make -j8 && cmake --install . --prefix "$PWD/dist/games247.app/Contents/Resources" && open dist/games247.app
```

Always run it from the `.app` bundle. Run as a bare `./games247` binary, it has no Info.plist, and macOS kills it when it checks Bluetooth status.

## Built-in player (beta)

games247 can run RetroArch's emulator cores (libretro cores) inside its own window instead of launching RetroArch.

**How it works**
- When a game's launch command is a RetroArch command (`RetroArch -L <core> <rom>`), games247 loads that same core itself (`es-app/src/guis/GuiLibretroPlayer.cpp`, hooked in `FileData::launchGame()`).
- If the core can't run in the built-in player, games247 falls back to launching RetroArch as before. That includes cores that need Vulkan, Direct3D or old-style (OpenGL 2) rendering, and multi-disc subsystem content.
- **GPU cores** (OpenGL, for example Mupen64Plus-Next for the N64) get their own OpenGL context on games247's window, as in RetroArch, so they can't disturb games247's drawing. The context is the kind the core asks for: a core profile context for modern OpenGL, or a compatibility profile one for older OpenGL (the Windows build of Mupen64Plus-Next asks for this; `LibretroCore::createHWContext`). They draw into a framebuffer that games247 creates; each frame is copied back and shown like a software core's, so screen modes, the CRT filter and the pause menu all work (`LibretroCore::acceptHardwareRender`, `createHWFramebuffer`, `readHWFrame`).
- **N64 border crop**: N64 games often draw inside a black border that differs from game to game. For N64 cores (Mupen64Plus-Next, ParaLLEl N64) the player looks at every sixth frame and crops off any edge that has stayed black since the game started (`GuiLibretroPlayer::updateAutoCrop`). The area kept only grows, so nothing the game draws is cut off, and black frames (fades, loading screens) are ignored. An edge is only cropped once the game fills at least 70% of the frame that way, so a small logo at the start isn't zoomed in on. The picture keeps its pixel shape, so the aspect ratio follows the crop.
- **Errors**: if the built-in player can't run a game and RetroArch isn't installed either, the error shown is the built-in player's reason rather than "couldn't find emulator RetroArch".
- **Analog sticks**: both sticks are passed to cores that read them (N64 and similar). For those cores the left stick no longer also acts as the D-pad.
- The core loader is `es-app/src/libretro/LibretroCore.cpp`. It uses `libretro.h`, the MIT-licensed libretro API header, and no RetroArch code.
- RetroArch doesn't need to be installed. The first time a system needs a core, games247 downloads it from libretro's build server (`buildbot.libretro.com`) into `~/.games247/cores`. On macOS, cores already installed in RetroArch are reused.
- BIOS files go in `~/.games247/bios`. On macOS, RetroArch's `system` folder is used if it exists. Battery saves go to `~/.games247/saves`.
- Turn it on or off in **Main menu → Other settings → Play RetroArch cores inside games247 (beta)**. It's on by default.

**Controls**
- Keyboard (RetroArch's default layout): arrows = D-pad, Z = B, X = A, A = Y, S = X, Q/W = L/R, Enter = Start, Right Shift = Select. Computer cores (C64, Amiga and others) also get the full keyboard.
- Controllers use your games247 controller mapping. The first controller to press a button is player 1.
- Pause menu: **View + Menu** (Back + Start) together on a controller, or **Escape**. It has Resume, Save state, Load state, Screen, CRT filter, Restart game and Quit game. Save states are stored in `~/.games247/saves`.
- Screen options (press A, or left/right, on the entry): **Fit to screen** keeps the game's shape, **Stretch to screen** fills the whole display, **Pixel perfect** scales by whole numbers so every game pixel is the same size. **CRT filter** adds scanlines, a phosphor mask and a slight glow to a flat picture that fills the same area as without it (`resources/shaders/glsl/crt.glsl`, games247's own shader). Both choices are remembered for all games.
- Zipped and 7z games: cores that can't open archives themselves (for example Genesis Plus GX for Master System and Mega Drive) get the game unpacked to `<games247 folder>/tmp/player_content` first, as RetroArch does. The folder is cleared when the game closes.
- Core options: games247's own defaults live in `resources/libretro/<core>.opt` (RetroArch format, `key = "value"`). A file with the same name in `<games247 folder>/cores/options/` overrides them. The C64 (VICE) layout, by Xbox label: A fire, B joystick up, Y space bar, X swaps the joystick between port 2 and port 1. VICE also crops the C64 borders automatically (`vice_crop = "auto"`).

**Tested** (macOS, Apple Silicon): VICE x64sc C64 core with two .prg games. Video and aspect ratio, keyboard joystick input (including taps shorter than a frame), PAL 50 Hz timing (measured 50.03–50.23 fps against a 50.12 target), steady audio buffering, exit, and relaunching in the same session.

**Not tested yet**: game controllers (including the analog sticks), the RetroArch fallback, and a commercial N64 game with the GLideN64 renderer. The GPU path was tested on macOS with a small OpenGL test core and with Mupen64Plus-Next running test programs.

**Not built yet**: Vulkan cores, a core options menu, rewind and fast-forward.

**Licensing**: cores keep their own licenses. Loading cores that the user installed is fine. If games247 ever bundles cores, check each one: several (Genesis Plus GX, the original Snes9x core and the older MAME versions) forbid commercial use.

## FTP storage

Games can be kept on an FTP server instead of (or as well as) the local ROM folder. The FTP folder holds one folder per system, named like the local ones (`c64`, `amiga`, `nes`, `snes`...).

- **Settings**: *Main menu > FTP storage* has an on/off switch, the address (`ftp.example.com`, `192.168.1.10:2121`, `ftp://...` or `ftps://...`), username, password (masked), path, *Accept self-signed certificates* (for home NAS boxes using FTPS) and *Test connection*. Saving changes reloads the game lists. The password is saved in plain text in `es_settings.xml`, like the scraper password.
- **Listing**: each time the systems load, games247 lists the FTP folder's system folders and the games in each (`FtpStorage::refresh`, called from `SystemData::loadConfig`). Games with a matching file extension that aren't on this computer are added to the game list next to the local games (`SystemData::addFtpGames`). If the server has games for a system that has no local folder, the folder is created so the system shows up. Their gamelist.xml entries (scraped data, favourites) are kept even though the file isn't local yet.
- **Saved listing**: the listing is saved in `ftp_cache.txt` in the games247 folder, with the server, username and path it belongs to. On the next start the saved listing is used straight away, so startup doesn't wait for the server, and the server is checked in the background on its own thread (`FtpStorage::startBackgroundCheck`). One connection is reused for every folder. When the check finishes and something has changed, the new listing is saved and new games are added to the open game lists, the "all games" collection and the system view's game count (`SystemData::addNewFtpGames`, called from `ViewController::update` once no menu, popup or game is open), with a popup saying how many. A folder for a system that isn't loaded yet shows after a restart. Games removed from the server stay in the list until the next start (downloading one says it's not found). If the check fails, the saved listing is kept. The first start, or a change of server, username or path, lists the server straight away as before.
- **Paths** can be typed with backslashes (`\Games\ROMs`) as well as slashes.
- **Cloud symbol**: in the game list, games that are only on the FTP server have a cloud (Font Awesome `\uf0c2`, `ViewController::CLOUD_CHAR`) before their name, or `~` when the theme uses ASCII indicators (`GamelistBase::populateList`). The list is refreshed after a download, so the cloud goes away.
- **Launching**: picking an FTP game shows a download screen with a progress bar (`GuiFtpDownloader`); B cancels. The game is downloaded to `<ROM folder>/<system>/<file>.part`, renamed when complete, and then launched the normal way. From then on it's an ordinary local game.
- **Connection**: libcurl. FTPS is used when the server offers it, otherwise plain FTP. Servers that take a while to wake up (a sleeping NAS, for example) get a 20 second connection timeout and 30 seconds to answer. If the server can't be reached, or a download breaks off, games247 tries twice more, after 3 and then 5 seconds (`RETRY_DELAYS`); a wrong password or a missing file isn't retried. B still cancels a download while it waits. If the server still can't be reached, games247 starts with the local games only.
- Only the top level of each system folder is listed (no subfolders). Games aren't removed from the computer when they're removed from the server.

**Tested** on macOS against a local test FTP server: listing, merging with local games, a system that only exists on the server, download with progress, cancel, launch in the built-in player, Test connection, and starting with the server down. **Not tested yet**: a real FTPS server, Windows.

## Neon theme and transitions

- **Neon** (`themes/neon-247`) is the only theme. The other ES-DE themes aren't installed, and the theme options (theme, variant, colour scheme, font size, aspect ratio, transitions, language, variant triggers and the theme downloader) are gone from *UI Settings*. Saved theme settings from older releases are ignored (`Settings::loadFile`). It's a synthwave design: a dusk sky with a striped sun, neon mountains and an animated grid floor. In the game list the selected game plays on a retro TV, with colour bars when there's no screenshot. System logos, console pictures and system information in `themes/neon-247/system` come from Linear (MIT). The artwork is generated by `tools/neon-theme/`. Font: Orbitron (OFL).
- **Transitions**: scrolling between consoles or game lists is instant (no fade to black). Going between the console carousel and a game list uses the CRT transition below.
- **GIF animations**: a GIF is decoded once and its frames are shared by every element playing it (`GIFAnimComponent::loadFrameCache`, up to 96 MB per animation). Upstream decodes every frame again each time it's drawn, which made the menus stutter with Neon's floor in every system.
- **Neon menus**: every menu, popup and dialog uses the Neon colours (`ViewController::setMenuColors`; the dark/light colour scheme option is gone). Panels are a dark purple gradient with a pink-to-cyan neon edge and a soft glow (`BackgroundComponent::renderNeonPanel`, also used by the built-in player's pause menu), and the selected row is a translucent pink-to-cyan bar. Titles use Orbitron, other menu text uses Rajdhani (OFL, `FONT_PATH_*` in `Font.h`).
- **Background music**: tracks in `resources/music` (and any the user puts in `<games247 folder>/music`) play in random order in the menus (`MusicManager`, decoded with FFmpeg and mixed by `AudioManager`). The music fades out over 1.2 seconds when a game starts, comes back when it ends, and plays quieter while a video has sound. *Sound settings* has an on/off switch and a volume slider. Bundled: five Ocean Software loader tunes, supplied by the project owner.
- **CRT transitions**: a theme can set `<fadeStyle>crt</fadeStyle>` in `capabilities.xml`. Fade transitions between views are then drawn as an old TV switching channels: the picture squeezes into a glowing line, then a dot (`ViewController::renderCRTTransition`). Fades within the system view keep their normal look.
- **Launch screen**: the "Launching game" dialog is replaced by a full-screen loading screen for all themes (`GuiLaunchScreen`). The screen darkens over scanlines, a neon line sweeps out, the marquee and game name fade in with a glow, and NOW LOADING (Monoton font, OFL) blinks below.

## Windows releases and auto-update

**Builds**: every push to the `games247` branch runs the "Windows build" workflow on GitHub Actions. It produces a test installer you can download from the run's page (Artifacts → `games247-windows-installer`).

**Releasing a version**: push a tag that starts with `v`:

```bash
git tag v4.0.0-beta.1 && git push origin v4.0.0-beta.1
```

The workflow stamps that version and an increasing build number into the app. It then publishes a GitHub release with the installer (`games247-<version>-setup.exe`) and the update feed (`latest_release.json`, with MD5 and SHA-256 checksums).

**Auto-update**: on Windows, games247 checks `https://github.com/epos247/games247/releases/latest/download/latest_release.json` at startup. When the release's build number is higher, it offers **Update**. That downloads the installer, checks the MD5, runs it silently, closes games247, and the installer relaunches it. Users get no admin prompt and don't have to pick any files.

**Installer**: `tools/windows/games247.nsi`, a per-user install in `%LOCALAPPDATA%\Programs\games247`. It adds Start menu and desktop shortcuts and an Installed apps entry. Uninstalling keeps `%USERPROFILE%\.games247` (settings, saves, cores).

**Not done yet**: code signing. Unsigned installers show a "Windows protected your PC" SmartScreen warning (More info → Run anyway). The workflow has a `Sign` step ready for when there's a certificate.
