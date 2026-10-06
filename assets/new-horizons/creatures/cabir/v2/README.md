# Cabir standing draft v2

User-requested rougher, darker Heroes III-style revision, created with the
built-in image generator using the HoMM3 Art workflow. The exact edit prompt
is retained in `standing-master.prompt.txt`. The approved v1 standing master
was the edit target; this version preserves its creature identity and pose.

The master is preserved unchanged as 1323x1189 RGBA. The existing mechanical
Cabir exporter reduces its alpha-bounded figure to a 64x60 native preview;
the 256x240 preview is nearest-neighbor enlargement. The comparison sheet
shows v1 on the left and v2 on the right, at native size and enlarged 4x.

Status: provisional visual draft, installed only in the separate static
in-game preview `build/nh-cabir-ingame-preview-v2.0iKT62/Play-Cabir-Preview.sh`.
It has no complete battle animations, upgraded variant or portrait set. Both
Gremlin identities temporarily use one standing sprite in all32 groups.
Neither the normal playable snapshot nor the isolated v1 Cabir preview has
been replaced by this draft. The exporter requires explicit `--version v2`;
its default remains v1. Native pixel checks and bounded headless loading are
not actual rendered approval.

Reproduce the previews:

```sh
/usr/bin/python3 tools/export_new_horizons_cabir_preview.py \
  --input assets/new-horizons/creatures/cabir/v2/standing-master.png \
  --output-dir assets/new-horizons/creatures/cabir/v2/previews --height 60
```
