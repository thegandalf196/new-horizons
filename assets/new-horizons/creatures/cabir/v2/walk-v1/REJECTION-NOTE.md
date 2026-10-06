# Cabir walk atlas generation attempts

No usable walk-atlas master was selected in this bounded task. Both generated
PNG candidates and their exact prompts are retained in this directory; hashes,
reference roles, raw-alpha bounds, alpha>=16 subject bounds, and generation IDs
are in `generation-metadata.json`.

The target was a 2048×512 sheet of four 512×512 cells, with approximately
350px visible bodies and feet at y=430. Both candidates returned 2172×724
(3:1), divided into four equal 543×724 columns. This layout difference alone
is not treated as fatal. For candidate 2, alpha>=16 subject bounds show a
consistent visible height of 490/490/490/491px and feet bottoms at 601/598/
601/599; the varying raw-alpha bounds come from faint alpha debris, not proven
body-scale or baseline drift. The main subject is still substantially larger
and lower than the requested guide measurements, and the contact poses do not
read clearly as opposite lead-leg phases. Candidate 1 also has unclear contact
alternation and its main subject is roughly 540px tall.

The referenced Cabir v2 standing master remains provisional and is not
user-approved. The prompts are preserved verbatim, including their use of the
word “approved”; that wording does not change the art's status. No pixels were
altered, no per-frame crop/warp/resize was applied, and no third generation
was made. These are rejected drafts, not a validated cycle or runtime asset.
