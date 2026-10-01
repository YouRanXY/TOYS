# FileManager completion design

The user requested autonomous completion of this personal C++ learning project.
The result is a small local CLI for classifying files, safely organizing them,
and persisting/querying metadata with SQLite. No GUI or recursive organization.

- C++17; retain `src/` and `inclu/` and four-space, Allman formatting.
- Scan one directory level, deterministically; reject missing/non-directory roots.
  Ignore symlinks and subdirectories. Match extensions case-insensitively.
- Keep Images, Code, Text; unknown files stay in place (metadata category Other).
- `FileManager organize <directory>` previews without writing files or a database.
  `--apply` explicitly moves. `scan <directory>` only indexes metadata.
  `query` reads the index, optionally filtering category, extension and filename.
- Category destinations must be real directories under the root. Never overwrite
  an existing file, directory or dangling symlink. Report per-file failures and
  continue; return nonzero on errors or skipped collisions during apply.
- SQLite belongs in FileDatabase, outside scanner/classifier/mover. Persist path,
  root, filename, normalized extension, category, byte size, modification ticks,
  and index time. Replace one root's snapshot transactionally on scan. After
  organize, record actual paths including failed/unknown files. Index represents
  the last successful operation, not a live or recursive filesystem view.
- Database defaults to `filemanager.db` in the working directory; allow `--db`.
  Exclude the active database and its sidecars from scanning and movement.
  Open/validate database before apply; failure prevents any movement. Filesystem
  moves and SQLite commits are separate: report index failures honestly; no rollback.
- Windows Unicode paths use wide command-line arguments and UTF-8 SQLite strings.
- Prefer installed SQLite >= 3.24; otherwise fetch a pinned official amalgamation
  with SHA-256 verification. No global dependency installation.
- CTest tests under `tests/`, all temporary fixtures inside generated build output.
  Cover recognized/unknown extensions, case, collisions, category files, missing
  roots, non-recursion, Unicode, SQL bindings, snapshot refresh, CLI preview/apply.
- Keep existing user edits; leave results uncommitted for review. No personal paths
  or generated metadata in tracked files.
