# Wisp handoff v3 provenance and integration note

The prompts and export metadata here accompany the user-supplied, newly authored
Wisp art. Runtime PNGs are copied byte-for-byte into `Mods/new-horizons/Images/`;
review GIFs and contact sheets are not installed.

The installed projectile descriptors intentionally expose only group 0, with its
nine directional frames. In this renderer group 1 is reserved for the mirrored
left-facing projectile path, so using supplied flicker phases as groups 1-3 would
collide with direction handling. All supplied `bolt-phase-00` through
`bolt-phase-03` PNGs remain installed unchanged as source material; the extra
phases are not runtime animation groups until a compatible use is specified.
