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
- **Bundled themes** (`themes/*-es-de`) keep their names for now. Check their individual licenses before you rebrand them.
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
- If the core can't run in the built-in player, games247 falls back to launching RetroArch as before. That includes cores needing GPU rendering (N64, PSP, Dreamcast and similar) and multi-disc subsystem content.
- The core loader is `es-app/src/libretro/LibretroCore.cpp`. It uses `libretro.h`, the MIT-licensed libretro API header, and no RetroArch code.
- RetroArch doesn't need to be installed. The first time a system needs a core, games247 downloads it from libretro's build server (`buildbot.libretro.com`) into `~/.games247/cores`. On macOS, cores already installed in RetroArch are reused.
- BIOS files go in `~/.games247/bios`. On macOS, RetroArch's `system` folder is used if it exists. Battery saves go to `~/.games247/saves`.
- Turn it on or off in **Main menu → Other settings → Play RetroArch cores inside games247 (beta)**. It's on by default.

**Controls**
- Keyboard (RetroArch's default layout): arrows = D-pad, Z = B, X = A, A = Y, S = X, Q/W = L/R, Enter = Start, Right Shift = Select. Computer cores (C64, Amiga and others) also get the full keyboard.
- Controllers use your games247 controller mapping. The first controller to press a button is player 1.
- Pause menu: **View + Menu** (Back + Start) together on a controller, or **Escape**. It has Resume, Save state, Load state, Screen, CRT filter, Restart game and Quit game. Save states are stored in `~/.games247/saves`.
- Screen options (press A, or left/right, on the entry): **Fit to screen** keeps the game's shape, **Stretch to screen** fills the whole display, **Pixel perfect** scales by whole numbers so every game pixel is the same size. **CRT filter** adds a curved tube, scanlines, a phosphor mask and a slight glow (`resources/shaders/glsl/crt.glsl`, games247's own shader). Both choices are remembered for all games.

**Tested** (macOS, Apple Silicon): VICE x64sc C64 core with two .prg games. Video and aspect ratio, keyboard joystick input (including taps shorter than a frame), PAL 50 Hz timing (measured 50.03–50.23 fps against a 50.12 target), steady audio buffering, exit, and relaunching in the same session.

**Not tested yet**: game controllers, the RetroArch fallback for GPU-rendered cores, and cores other than VICE.

**Not built yet**: GPU-rendered (hardware) cores, a pause/in-game menu, save states, core options, shaders, rewind, fast-forward, analog sticks, and Windows/Linux support. Linux should mostly work as written; Windows needs `LoadLibrary` instead of `dlopen`.

**Licensing**: cores keep their own licenses. Loading cores that the user installed is fine. If games247 ever bundles cores, check each one: several (Genesis Plus GX, the original Snes9x core and the older MAME versions) forbid commercial use.

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
