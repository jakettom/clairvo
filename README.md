# Clairvo — Video Footage Organizer

A non-destructive, metadata-aware footage organizer for macOS (Apple Silicon), implementing the MVP described in
[`video_footage_organizer_spec.md`](video_footage_organizer_spec.md),
[`video_footage_organizer_SRS.md`](video_footage_organizer_SRS.md) and
[`video_footage_organizer_implementation_roadmap.md`](video_footage_organizer_implementation_roadmap.md).

> The computer handles objective information; the human supplies semantic context.

Import a folder of raw clips → metadata is extracted and normalized → choose criteria (Camera, Resolution, …) to get a
proposed hierarchy → refine it by hand (folders, renames, tags) → **Review** every filesystem operation → **Apply**.
Nothing on disk changes before Apply. Files are never deleted or overwritten.

## Building

Requirements: macOS 14+, CMake ≥ 3.26, Ninja, Xcode Command Line Tools (Swift 5.9+). Configuring downloads GoogleTest
and nlohmann/json once.

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build            # or ./build/tests/clairvo_tests
open build/macos/Clairvo.app
```

With full Xcode installed you can also generate an Xcode project: `cmake -S . -B build-xcode -G Xcode`.
(Only the Ninja build has been verified so far; Xcode isn't installed on the development machine.)

Options: `-DCLAIRVO_BUILD_APP=OFF` (core + tests only), `-DCLAIRVO_BUILD_TESTS=OFF`.

### Sample footage and the CLI

```sh
swift tools/make-sample-footage.swift ~/Desktop/SampleFootage     # small real H.264 clips with camera/GPS metadata
./build/clairvo-cli create ~/Desktop/SampleFootage "Documentary"  # create project + import
./build/clairvo-cli ~/Desktop/SampleFootage/Documentary.project \
    '{"cmd":"organize","criteria":["camera","resolution"],"namingTemplate":"{folder}_{number}"}'
./build/clairvo-cli ~/Desktop/SampleFootage/Documentary.project '{"cmd":"pendingChanges"}'
./build/clairvo-cli ~/Desktop/SampleFootage/Documentary.project '{"cmd":"apply"}'
```

`clairvo-cli` runs the same JSON commands the UI sends to the core (see `app/bridge/Bridge.cpp` for the full list).
Don't run it against a project that is open in the app at the same time.

## Using the app

| Area | What it does |
|---|---|
| **Tree** (left) | The authoritative editor: expand, multi-select, drag clips/folders onto folders, ⏎ or double-click to rename, ⌫ to delete a folder / remove clips, right-click for the context menu (Move to, Add/Remove Tag, Reset to Automatic, Remove from Project, Reveal in Finder). Badges: green ⊕ = folder will be created, blue → = pending move/rename, purple ✋ = manual override, orange ⚠ = missing file. |
| **Canvas / Clips** (center) | Canvas: the same hierarchy drawn as a tree (double-click a folder to expand, drag nodes onto folders, pinch/zoom). Clips: sortable table of the selected folder or search results. |
| **Inspector** (right) | Editable title, tags and organization state, kept separate from read-only metadata (normalized categories + raw keys). Multi-selection allows bulk tagging and moves. |
| **Organize** (wand) | Pick and order metadata criteria, set a naming template with live preview, *Preview Organization*, *Apply Naming Only*, or *Reset Organization*. |
| **Review** (checklist) | Preflight checks plus every folder creation, move and rename. *Apply…* is enabled only when preflight passes; the result sheet lists each ✓/✗ operation with its reason. |
| **Undo** | ⌘Z / ⇧⌘Z for every virtual edit (session only). Apply is not undoable. |
| **Check Files** | ⌘R re-checks the disk and marks moved-away files as Missing. |

## Architecture

```
macos/          SwiftUI + AppKit (NSOutlineView tree) — displays state, sends commands
   │  JSON over a narrow C API (app/bridge/include/vo_bridge.h), events via callback
app/            ProjectSession (write-through persistence, undo, events), ImportService,
                SearchService, JsonCodec, Bridge/dispatcher, clairvo-cli
core/           model (ProjectModel + deltas), commands, undo, organization (engine, naming),
                changes (diff), filesystem (FileSystem, Preflight, ApplyEngine, missing detection)
database/       SQLite wrapper, schema + migrations, ProjectStore (+ apply journal)
media/          scanner, supported formats, fingerprinting, AVFoundation extractor (.mm)
tests/          GoogleTest: unit, filesystem (temp dirs, fault injection), database, media, integration, stress
```

**Commands and undo.** Every primitive model mutation records a before/after *delta*. A command is a validated sequence
of mutations (`core/commands/EditCommands.cpp`, `core/organization/OrganizationEngine.cpp`); its deltas give it
execute/undo semantics, are what the `UndoManager` reverts/reapplies, and are exactly what `ProjectStore` persists in one
SQLite transaction. A command that throws is rolled back.

**Virtual vs physical.** `clips.file_path` and `folders.physical_path` are the *confirmed* state on disk; a clip's
`folder_id` + `title` (and folder names/parents) are the *intended* state. Pending changes are **derived** by diffing
the two (`core/changes/ChangeSet.cpp`), so Review always shows exactly what Apply will do, and Apply updates the
physical fields only after each operation is confirmed.

## Design decisions (where the spec left options open)

- **Project file**: `<Name>.project` in the footage root *is* the SQLite database (rollback journal, so no lingering
  side files). Schema version is stored in `PRAGMA user_version` and the `meta` table; migrations run `v1 → vN`, and newer
  files are refused. The root is always the folder containing the project file, so moving the whole footage folder works.
- **IDs**: random UUIDv4; file hash is only a fingerprint (SHA-256 of size + first/last 4 MiB, fast on large footage).
- **Supported media**: `.mov`, `.mp4`, `.m4v`. Hidden files, symlinks, `*.project`, and package directories
  (`.fcpbundle`, `.app`, …) are skipped.
- **Metadata**: AVFoundation extracts QuickTime/MPEG-4/user-data metadata and track format info; the normalizer maps
  camera-specific keys (e.g. `com.apple.quicktime.location.ISO6709` and `GPSLatitude`/`GPSLongitude`) onto Time, Location,
  Camera, Lens, Resolution, Frame Rate, Codec, Author, Duration, Orientation. Locations stay raw coordinates (no geocoding).
- **Missing metadata** groups into `Unknown <Category>` folders; clips never disappear from a proposal.
- **Grouping values**: Frame Rate `29.97 fps`, Time by date, Location rounded to ~100 m, Duration buckets
  (`Under 1 min`, `1-5 min`, `5-15 min`, `Over 15 min`).
- **Overrides** are two flags: *placement* (set by manual moves, including moving/renaming a containing folder) and *title*
  (set by manual renames). Organize only re-places clips without a placement override; the naming template only renames
  clips without a title override, so a manually placed clip still gets `John_001` (spec §70). *Reset to Automatic*
  clears both.
- **Organize** prunes automatic/imported folders it leaves empty (never manually created ones). *Reset Organization*
  returns the virtual hierarchy to the current layout on disk (zero pending changes); it is undoable.
- **Naming**: `{folder} {number} {number:N} {original} {camera} {resolution} {date}`; `{number}` is per folder, in
  recording-time order, skipping names already used in the folder or occupied on disk by non-project files. Extensions are
  always preserved. Manual renames that collide get ` (2)`. Name comparisons are case-insensitive (APFS default).
- **Import** only accepts folders inside the project root (a clip belongs to a project by physical location). Existing
  subdirectories become folders. Import and Apply clear the undo history because the physical baseline changed.
- **Apply** order: folder renames/moves and creations top-down, then clip moves. Moves use `renamex_np(RENAME_EXCL)` so an
  existing file can never be replaced; swaps and case-only renames go through a staging name. Each operation is journaled
  as PENDING before running and committed together with its model update afterwards; on open, interrupted operations are
  reconciled against the disk and reported. Afterwards, emptied directories that no longer correspond to a folder are
  removed with `rmdir` (only if empty apart from a Finder `.DS_Store`), so media can never be deleted.
- **Folder deletion** offers *Move clips to parent* / *Remove clips from project* / Cancel; neither touches files.

## Testing

`ctest` runs 67 tests, including: the spec §61 organization example and determinism; override preservation and reset;
undo/redo of every command; persistence round-trips; schema-version refusal; preflight blocking an occupied destination;
swap cycles and case-only renames; real `chmod` permission failures and injected failures producing `PARTIALLY_APPLIED`
with a database that matches the disk; crash-journal recovery; missing-file detection; AVFoundation extraction from a
generated H.264 movie; the C bridge end to end; the roadmap §41 "Definition of Done" scenario; and a 10,000-clip
organize/diff stress test.

## Out of scope (per the spec)

Video editing/playback, thumbnails, AI analysis, semantic search, duplicate detection, relinking/Locate File, geocoding,
multiple open projects, persistent undo, filesystem rollback after Apply.
