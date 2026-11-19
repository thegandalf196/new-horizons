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
