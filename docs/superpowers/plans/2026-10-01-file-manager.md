# FileManager Implementation Plan

> For agentic workers: use superpowers:executing-plans to implement task by task.

**Goal:** Complete a safe local file organizer with SQLite metadata queries.
**Architecture:** Keep scanning, classification, movement and database storage
separate. Main only parses commands and coordinates these modules.
**Tech Stack:** C++17, CMake, SQLite, CTest.
**Spec:** `docs/superpowers/specs/2026-10-01-file-manager-design.md`

## Global Constraints

- C++17; existing src/inclu layout and Allman formatting.
- One-level scan; unknown files retained; preview default; never overwrite.
- No global installs, personal paths, generated databases or commits.

## Review Focus

- Existing category path is a regular file: preserve sources, report failure.
- Destination symlinks: skip rather than follow or replace them.
- SQLite filename itself has a recognized extension: exclude it and sidecars.
- Repeated scans: replace root snapshot without dropping other roots.
- Database errors before apply: prevent movement; after apply: report partial state.

## Task 1: Safe organizing core

Files: src/FileScanner.cpp, src/FileClassifier.cpp, src/FileMove.cpp;
matching headers; tests/FileScanner_test.cpp, FileClassifier_test.cpp,
FileMove_test.cpp, TestSupport.h; CmakeLists.txt.

Interfaces: `scan() const -> vector<fs::path>`;
`classify(const vector<fs::path>&) const -> map<string, vector<fs::path>>`;
`categoryFor(const fs::path&) -> string`;
`move(groups, root, dryRun=false) const -> vector<MoveResult>` where results
contain source, destination, MoveStatus (Planned/Moved/Skipped/Failed), message.

- [x] Write tests for uppercase extensions and collision preservation; run baseline
  and confirm missing behavior. Add scanner and mover edge tests.
- [x] Implement validation, sorted scanning, classification and safe move reports.
- [x] Build and run core CTest tests; confirm all pass.

## Task 2: SQLite snapshots

Files: inclu/FileDatabase.h, src/FileDatabase.cpp, tests/FileDatabase_test.cpp.
Interfaces: FileRecord metadata; QueryFilter category/extension/name;
`FileDatabase(path, readOnly=false)`;
`replaceSnapshot(root, records)`; `query(filter={}) -> vector<FileRecord>`;
`readRecord(path, root) -> FileRecord`.

- [x] Write failing tests for persistence, Unicode and SQL-safe literal filtering,
  repeated snapshots, independent roots and invalid database paths.
- [x] Link installed SQLite or pinned official source; implement RAII, prepared
  statements and transactional root snapshots.
- [x] Build and run database and core tests.

## Task 3: CLI and documentation

Files: src/main.cpp, tests/Cli_test.cmake, README.md, .gitignore.
Interfaces: `scan <directory> [--db path]`, `organize <directory> [--apply]
[--db path]`, `query [--db path] [--category value] [--extension value]
[--name substring]`, `--help`.

- [x] Write CLI integration tests and demonstrate missing commands fail.
- [x] Implement argument validation, Unicode entry point, preview/apply, database
  exclusion, actual-path indexing and nonzero error exits.
- [x] Document commands, build dependencies, metadata semantics and limitations.
- [x] Run Debug and Release builds and full CTest; inspect diff and finish locally.

## Execution record

Ruling: User asked to continue directly; design and plan are recorded without
additional approval gates. Work stays in the existing workspace to preserve
the user's main.cpp edit. No automatic commit, worktree or external publication.

Task 1: complete. Baseline failed uppercase classification and collision retention;
core suites passed after implementation.
Task 2: complete. Database API was missing before implementation; persistence,
literal filtering and transactional rollback tests passed after implementation.
Task 3: complete. Baseline CLI ignored arguments; new end-to-end workflow passed.

Final independent review: no Critical findings in the MSVC workflow. Added MinGW
Unicode startup linkage; that toolchain remains untested. Added CLI Unicode
organization, category junction rejection and deterministic post-move SQLite
failure tests. Recovery-message regression failed before the fix and passed after.
CMake subprocess output now explicitly decodes UTF-8; the old default garbled
Unicode test output even though the CLI/database paths were correct.

Final verification: Debug and Release builds succeeded; full CTest passed 5/5
in each configuration. Symbolic-link fixtures were skipped because the current
Windows token lacks creation privilege; the junction integration case executed.
