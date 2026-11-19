# Documentation replacement

**Status: replacement manual implemented and technically checked; consolidated
reader acceptance remains pending.**

The current manual is in `docs/`; the historical manual remains editable in
`docs-history/`. `make docs` builds the current manual in `docs/_build/html`;
`make docs-serve` serves it locally. `make docs-site` builds both books and
assembles them in `docs/_build/site`; `make docs-full` rebuilds both strictly
before assembly. The old draft commands have been removed.

The {download}`compatibility manifest <../coverage/compatibility.json>`
preserves 1,306 original page/fragment targets.
Retired pages route to the historical manual. Current pages retain their own
anchors and route obsolete fragments to the archive; ordinary links remain
available without JavaScript. The
{project}`assembler <scripts/assemble_docs.py>` refuses missing archived targets.
The [authoring guide](../development/documentation.md#publication-and-source-archives)
links the build and packaging owners.

The [closing review](../review.md) separates technical verification from reader
acceptance. The [publication checklist](publication.md) owns hosting, rights,
CI and launch qualification. No website is published by these local commands.
