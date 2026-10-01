# Windows GUI design

The user wants to operate the existing file organizer through a desktop GUI.
Implement a native Chinese Win32 application, FileManagerGui.exe, alongside the
existing CLI. Reuse C++17 core modules and SQLite; do not install a GUI framework.

- Three pages: organize files, query indexed records, usage help.
- Folder picker and editable Unicode path; selectable index database. Database
  defaults beside the GUI executable, independently of the launch directory.
- Organize page shows category counts, file names, sizes, planned destinations
  and per-file outcomes. Preview has no filesystem writes.
- Apply is enabled only after a successful preview and processes exactly that
  batch. Changing folder/database invalidates the preview. If a previewed file
  has changed or vanished, abort before moving anything and require a new preview.
- Unknown files remain; existing destinations are skipped, never replaced.
  No recursive scanning. Indexing without moving is a separate explicit action.
- Query page uses category, extension and literal filename-substring filters.
- Filesystem and database work runs in a worker thread; controls prevent duplicate
  operations. Closing during a job waits for completion without abandoning moves.
- Preserve per-file outcomes if post-move indexing fails. Show both the partial
  result and recovery instructions, including separate scans of category folders.
- Native common dialogs, keyboard tab navigation, resizable window and per-monitor
  DPI scaling. No moves on launch or navigation.
- Tests: workflow safety/metadata tests and controlled hidden-window integration
  exercising preview, apply, query, stale-state invalidation, Unicode and errors.
  Capture a generated preview image and inspect it before delivery.
- Keep CLI behavior and tests passing. Leave changes local for the user's review.
