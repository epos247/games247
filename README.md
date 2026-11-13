# games247

games247 is a frontend for browsing and launching games from your multi-platform collection.

It is an enhanced fork of [ES-DE](https://gitlab.com/es-de/emulationstation-de) (EmulationStation Desktop Edition). games247 is an independent project and is not affiliated with or endorsed by ES-DE or Northwestern Software AB.

## What's different from ES-DE

- New name and branding: games247 app name, splash screen, window title and package names.
- User data lives in the hidden folder `~/.games247` instead of `~/ES-DE`.
- The built-in application updater is off by default and no longer checks ES-DE's release server.

The planned enhancements are tracked in [FORK.md](FORK.md).

## Building

The build steps are the same as upstream ES-DE, except the output binary is `games247` (or `games247.app` on macOS). See [INSTALL-DEV.md](INSTALL-DEV.md).

## License and credits

games247 is released under the MIT license, the same as ES-DE. See [LICENSE](LICENSE). The upstream copyright notices are kept as the license requires.

Huge thanks to the ES-DE team and the EmulationStation contributors. The original ES-DE README is in [README-ES-DE.md](README-ES-DE.md), and third-party credits are in [CREDITS.md](CREDITS.md) and [licenses/](licenses/).
