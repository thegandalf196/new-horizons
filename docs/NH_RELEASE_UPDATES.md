# Optional release notification

On Windows x64 and desktop Linux x64, the ordinary main menu checks the public
GitHub `thegandalf196/new-horizons` latest-release endpoint once per game session.
The request is unauthenticated: no profile, save, account token or workstation
path is sent. GitHub necessarily receives normal connection information, including
the source IP address. HTTPS certificate verification remains enabled.

The background request has a five-second deadline and a one-MiB response limit.
Offline operation, rate limits, missing releases and malformed responses are
silent. There is no startup wait. A prompt appears only at the main menu, never
over an active game, when a newer stable **product** version has an uploaded,
nonempty player archive for the current platform. Source-only releases do not
produce an offer. The engine revision is not used as the product version.

Accepting opens the official release page in the system browser. It does not
download, install, overwrite files, restart the game or modify saves. Declining
dismisses the offer. To disable checks, set `general.updateChecks` to `false`
in the user's settings JSON; the default is `true`.

Windows uses the system WinHTTP library. Linux requires libcurl with asynchronous
DNS support and the system trust store; providers without bounded DNS support
silently skip the check. Linux builders need the libcurl development package.
Linux player-package dependency and notice audits must cover the actual provider
and its transitive dependencies before publication. Other platforms skip checks.

The CPU-only `nhReleaseUpdatePolicyTest` target tests version, publication,
platform-asset, URL and malformed-response decisions without SDL or networking.
`nhReleaseUpdateTransportProbe` tests pre-cancellation offline; its explicit
`--live` argument makes one request to the fixed public endpoint and prints only
status, response length and elapsed time. A zero status is not proof of connectivity.
