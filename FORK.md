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
- User data directory is `~/games247` (iOS: `Documents/games247`, Linux system data: `share/games247`).
- Build targets and packages are named `games247`. macOS bundle ID: `com.games247.app`.
- New splash wordmark at `resources/graphics/splash.svg`.
- The application updater is off by default (`ApplicationUpdaterFrequency` = `never`). Its URL points to a placeholder, `https://updates.games247.example/latest_release.json`.

Still to do:
- **App icons**: `es-app/assets/games247.icns`, `games247.ico`, `com.games247.app.svg` and `resources/graphics/window_icon_256.png` still contain the ES-DE artwork.
- **Scraper credentials**: `es-app/src/scrapers/ScreenScraper.h` and `GamesDBJSONScraperResources.cpp` contain ES-DE's own developer keys for ScreenScraper and TheGamesDB. Register games247's own developer accounts and replace them before you distribute builds.
- **Website and updater**: replace the `games247.example` placeholders with your real domain once you have one.
- **Bundled themes** (`themes/*-es-de`) keep their names for now. Check their individual licenses before you rebrand them.
- **Android / iOS** packaging was not rebranded (Android app ID constant only).
- **Docs** (`*.md` user guides) still describe ES-DE.
