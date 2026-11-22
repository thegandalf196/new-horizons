# Desktop launcher and updates

The portable Windows and desktop Linux packages open a New Horizons launcher
with the installed version, **Play**, **Check for updates** and **Original
game files**. When an update is available, **Install update** prepares it in
the launcher. The player can start the new version as soon as installation
finishes.

The launchers check the public GitHub latest-release endpoint for
`thegandalf196/new-horizons`. Stable product versions and uploaded player
archives for the current platform determine update eligibility. Update
preparation verifies the archive size and SHA-256 against current GitHub
metadata, then verifies every installed file against the package manifest.
The selected version changes only after the checked client passes its version
probe. Failed downloads, verification or probes leave the installed version
available. Original game files, settings and saves use the existing profile.

The first updater-capable release is installed by extracting its complete
package. Windows uses the root `new-horizons.exe`. Linux uses
`new-horizons-setup` and the GTK 4 desktop libraries from Ubuntu. Direct CLI
arguments keep their setup and automation behavior.

After **Play**, the launcher shows startup status while the game checks or
prepares the NHART cache. The game process owns the artwork work and uses the
same checked cache during play. See the [artwork cache contract](NHART_DELIVERY.md#installed-artwork-preparation).

The launcher's update requests use HTTPS and send no player profile, save,
account token or workstation path. GitHub receives normal connection data such
as the source IP address. Set `general.updateChecks` to `false` in the existing
profile settings to disable the automatic check. **Check for updates** remains
available in the launcher. The standalone engine retains its older browser
notification for direct starts.

## Launcher log

Each launcher window writes one plain-text session log, `launcher.log`, and
keeps the previous session as `launcher.previous.log`. A session stops logging
at 1 MiB. Windows writes it to
`%LOCALAPPDATA%\HeroesIII-NewHorizons\logs\`, beside the stable game logs, for
both tracks. Linux writes it to `~/.local/state/new-horizons/`, or
`$XDG_STATE_HOME/new-horizons/`, because the game profile is created by setup.
A failed update check or start names the log in the launcher status line.

Lines carry the local time in milliseconds, the seconds since that process
opened the log and the writing role: `launcher`, `check` or `install`. The log
records launcher version, OS and runtime versions, launcher hand-over
self-tests, each update-check phase, and each GitHub request. Request lines
contain host, path, status, length, duration, whether a proxy carries the
request, and network or TLS errors. The log also records deadlines and the
phase they stopped, exception types with their causes, child exit codes, and
the game exit code. The home or profile folder appears as `~` or
`%USERPROFILE%`. URL queries, tokens, proxy addresses and the environment are
never written.

The update check has a 45-second deadline. Each GitHub request has its own
20-second bound. **Play** remains available during a check; selecting it stops
the check and starts the game.
