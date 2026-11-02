# Documentation policy

A reader should find the right document in two steps (category index, then topic),
and every document should stay short enough to read in one go.

## Layout
```
docs/index.md                      categories, one line each
docs/<category>/index.md           every topic of the category, 2-3 sentences each
docs/<category>/<topic>.md         one topic
docs/<category>/<topic>/index.md   a topic that grew too large becomes a subdirectory with its own index
```
- `docs/` root holds only `index.md` and category directories; every directory has an `index.md`.
- Every `.md` file of a directory is listed in that directory's `index.md` (and every subdirectory too), with a summary
  that says what a reader finds there, not just the title. Update the index in the same change as the topic.
- Names are lowercase with hyphens (`world-hashes.md`). Categories are broad areas (`build`, `testing`, `game`,
  `formats`); create a new category only when at least two topics belong to it.
- **English only**, including headings, tables and examples.

## Size
- A topic file stays under about **300 lines / 20 kB**. When it grows past that, split it: turn `<topic>.md` into
  `<topic>/index.md` plus part files, each part self-contained and listed in the new index.
- Prefer current facts over history. Dates and who-did-what belong in commit bodies.

## Content rules
- Describe current contracts, formats and behavior of **our** code. Name structures and formats rather than
  memory addresses. Explain imported formats through the converter's input and output contracts.
- A doc moves together with its code: update it in the same change when a module is refactored or replaced.
- Link relatively (`../testing/test-runner.md`); `python tools/check_doc_links.py` checks links and anchors.

## Lint
`python tools/check_docs.py` checks the layout rules above: only `index.md` and directories at the root, an `index.md` in
every directory, every file and subdirectory listed in its index, no index entry without a file, and the size limit
(reported as a warning). Run it with `check_doc_links.py` before committing docs; CI will run both.
