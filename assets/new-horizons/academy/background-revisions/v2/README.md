# Academy town background — duplicate hall correction

Status: Provisional artwork, installed in source; playable delivery and user
visual acceptance remain pending.

The supplied town landscape painted a hall in the same area as the separate
Village Hall building layer. This produced a second visible roof behind the
foreground building. The built-in HoMM3 Art edit removes only the painted hall,
retaining the neighboring house and landscape. The exact prompt is retained in
`landscape.prompt.txt`; the generated master is `landscape-master.png`.

The mechanical exporter resizes the master to 800×374 and copies only the
reviewed rectangle `(0,254)-(177,335)` onto the retained authored native
landscape. Every pixel outside that rectangle is unchanged. This is a bounded
export from original image-generation work, not procedural repainting.

The retained baseline and earlier hall revision are not overwritten. No hall
placement, sprite, click/hover mask, gameplay rule or layer order changes.
Comparisons include a native registered VillageHall-stage composition; these
are explicitly mechanical composites, not game screenshots.

Run `/usr/bin/python3 tools/export_new_horizons_academy_background_v2.py --check`
from the repository to verify the pinned exports and comparisons. The importer
accepts only exact baseline or revised bytes. Twenty focused Academy checks and
independent review pass. Full original-handoff archive reimport is unverified
because the archive is unavailable. Earlier supplied-art rights remain in
effect; this revision assigns no new license to that material.
