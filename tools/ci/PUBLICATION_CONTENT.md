# Publication content check

This check detects two specific forms of accidentally published conversation
material. It complements the retired-path export policy and instruction-home-path
check; it is not a general privacy, credential, or historical-removal verdict.

Use a verified absolute Python interpreter in `NH_PYTHON`:

```sh
"$NH_PYTHON" -B -m unittest tools.tests.test_publication_content
"$NH_PYTHON" -B tools/ci/publication_content.py --root . --staged
"$NH_PYTHON" -B tools/ci/publication_content.py --root . --revision "$NH_REVISION"
```

`--staged` inspects all selected index objects, including unchanged entries, not
working-tree copies. `--revision` inspects one explicitly selected commit tree.
Choose exactly one mode. Neither command scans every historical commit.
The existing Repository contracts workflow checks its exact checkout commit.

## Detection and inspection boundaries

- R1 detects populated adjacent user/assistant messages in the supported raw
  role envelopes, or a populated conversation block containing both roles.
- R2 detects structured session identifiers with populated role/content messages
  in JSON, TOML, or embedded strict JSON.
- Free-form copied conversations, arbitrary generation prompts, private design,
  and credentials are not comprehensively detected. Review them separately.

The reader selects declared text suffixes and special filenames in
`git_publication_blobs.py`. Other entries are counted outside scope; Gitlinks
are counted as external and their repositories are not inspected. Selected
symlinks or unsupported modes, unreadable objects, non-UTF-8 text, NUL-containing
text, oversized selected blobs, changed indexes, and empty inspection scope
fail rather than being silently accepted. Selected blobs are limited to 16 MiB;
structured traversal and framing candidates have separate bounded limits.

Git object replacement and lazy fetching are disabled for every reader command.
A missing local object or unsupported Git option is an inspection error, not
permission to fetch it or retry with weaker protections. The workflow checks
these Git options explicitly before inspecting its target.

Output reports rule names, relative paths, line numbers, scope counts, and
inspection errors without quoting matched content. Exit status is zero for no
findings within the declared scope, one for a content finding, and two for an
inspection error. A zero result does not establish that publication is safe.
