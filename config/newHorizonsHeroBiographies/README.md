# New Horizons hero biography decisions

These nine files record the reviewed biography decision for all 144 standard
faction heroes. A string is an independently reviewed New Horizons rewrite.
`null` means the installed original remains authoritative and the module emits
no override for that hero. Near-verbatim copyedits also inherit the installed
text rather than placing purchaser prose in the repository.

The decisions began from the supplied `New_Horizons_Hero_Redesign_Workbook.md`,
but that workbook's proposed Skills, perks, classes, spells, specialties and
armies are outside this biography-only pass. Those mechanical proposals must
not be activated from this directory.

Use `tools/review-new-horizons-hero-biographies.py` with explicit `--data-root`
and `--workbook` paths to generate a side-by-side report under `build/`. The
report compares the installed `HEROBIOS.TXT`, the workbook draft and these
decisions.
`tools/update-new-horizons-module.py` emits only non-null entries as leaf-only
`texts.biography` patches. It must never import workbook mechanics or replace a
map-authored custom biography. The purchaser-supplied original biographies must
never be copied into the repository.
