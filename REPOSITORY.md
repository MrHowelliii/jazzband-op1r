# Jazz Band source repository

This repository starts with the existing version 1.5 testing baseline. Migration
does not change the plugin, companion or sound packs, and does not certify their
behavior on hardware. See `baseline-v1.5.json` for exact artifact hashes.

The editable companion is `Jazz Band.html` plus `companion-core.js`; regenerate
the standalone HTML with `python3 build_companion.py`. Plugin sources, host tests
and build documentation are under `jazzband-op1r/`.

Existing backups, local toolchains, generated host output and personal libraries
remain on the computer and are excluded from Git. Teenage Engineering firmware
must not be committed. Sample recipes and credits are included; prepared packs
and compiled plugins belong in release attachments rather than source history.
The two small plugin templates are build inputs and are intentionally tracked.

The original installable files and a source snapshot are preserved in the local
ignored `releases/v1.5-testing/` directory. The baseline manifest describes their
relationship without asserting that local source was used to build a binary.
Before publishing a stable release, verify the matching build and hardware tests.

For future changes, create a branch, keep source and documentation together, run
the relevant checks, and publish a release tied to its source commit. GitHub
Actions (`.github/workflows/build.yml`) builds `jazzband.op1p`, `jbsongs.op1p`
and `jbpack.op1p` with op1r's SDK and toolchain on every push, runs the PC tests,
and attaches the plugin files to the run. Preserve
sample attribution and upstream notices. See `README.md` and
`jazzband-op1r/README.md` for existing usage and build details.
