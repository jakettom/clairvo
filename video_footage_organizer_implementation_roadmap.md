# Implementation Roadmap
## Video Footage Organizer for macOS

**Document version:** 1.0  
**Purpose:** Turn the SRS into an executable engineering plan  
**Recommended architecture:** Native macOS UI + C++ core + SQLite

---

# 1. Implementation Strategy

The application should be developed from the inside out.

Do **not** begin by building the complete UI.

The recommended sequence is:

```text
Core Data Model
      ↓
Persistence
      ↓
Filesystem + Import
      ↓
Metadata
      ↓
Commands / Undo
      ↓
Organization Engine
      ↓
Review / Apply
      ↓
Native UI
      ↓
Canvas
      ↓
Performance / Hardening
```

This order ensures that the UI becomes a client of a stable application model rather than becoming the application's architecture.

---

# 2. Recommended Technology Stack

## 2.1 Core

```text
Language: C++
Standard: C++20 or newer
Build: CMake
```

C++ should own:

- project model;
- commands;
- organization engine;
- metadata abstraction;
- filesystem operations;
- change model;
- validation;
- persistence interfaces.

## 2.2 Database

```text
SQLite
```

Use a small database abstraction rather than spreading SQL through the application.

## 2.3 macOS UI

Preferred architecture:

```text
Swift / SwiftUI
        │
        ▼
C++ Core
```

SwiftUI provides:

- native macOS appearance;
- menus;
- windows;
- drag/drop;
- accessibility;
- native controls.

AppKit can be introduced where SwiftUI does not provide sufficient control.

## 2.4 Interop

Keep the Swift/C++ boundary narrow.

Prefer a C-compatible or Objective-C++ bridge rather than exposing a large collection of C++ templates/classes directly to Swift.

Conceptually:

```text
Swift UI
   ↓
Application Bridge
   ↓
C++ Application API
   ↓
C++ Core
```

---

# 3. Repository Layout

Recommended starting repository:

```text
VideoOrganizer/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── core/
│   ├── project/
│   ├── model/
│   ├── commands/
│   ├── organization/
│   ├── metadata/
│   ├── filesystem/
│   ├── validation/
│   └── undo/
│
├── database/
│   ├── sqlite/
│   ├── migrations/
│   └── schema/
│
├── media/
│   ├── extraction/
│   ├── hashing/
│   └── formats/
│
├── app/
│   ├── application/
│   └── bridge/
│
├── macos/
│   ├── VideoOrganizerApp/
│   ├── Views/
│   ├── ViewModels/
│   ├── Models/
│   └── Resources/
│
└── tests/
    ├── core/
    ├── database/
    ├── filesystem/
    ├── media/
    └── organization/
```

The exact directory structure can change; the separation of responsibilities should not.

---

# 4. Phase 0 — Development Environment

## Goal

Create a buildable application skeleton before implementing product functionality.

## Tasks

- Create Git repository.
- Configure CMake.
- Select C++ standard.
- Create macOS application target.
- Configure Debug/Release builds.
- Add unit testing framework.
- Add SQLite dependency.
- Establish formatting/linting.
- Establish basic CI if desired.

## Deliverable

A blank application that builds and runs on the target Mac.

## Exit Criteria

```text
cmake configure → success
build → success
tests → success
application launches → success
```

---

# 5. Phase 1 — Domain Model

## Goal

Implement the application's conceptual model without a UI.

## Core objects

Implement:

```cpp
Project
Folder
Clip
Tag
Metadata
OrganizationConfig
Change
```

Potential IDs:

```cpp
using ProjectId = UUID;
using FolderId = UUID;
using ClipId = UUID;
using TagId = UUID;
using ChangeId = UUID;
```

Do not use filenames as object identity.

Do not use file hashes as Clip IDs.

## Folder model

Use:

```text
Folder
    id
    project_id
    parent_id
    name
    order_index
```

## Clip model

Use approximately:

```text
Clip
    id
    project_id
    folder_id
    file_path
    original_filename
    title
    file_hash
    status
    organization_override
```

## Exit Criteria

The model can represent:

```text
Project
├── Folder
│   ├── Folder
│   └── Clip
└── Clip
```

and enforce one-folder-per-clip.

---

# 6. Phase 2 — Database Layer

## Goal

Persist the domain model.

## Tables

Initial schema:

```text
projects
folders
clips
raw_metadata
normalized_metadata
tags
clip_tags
organization_configs
changes
```

Potential future tables:

```text
apply_operations
schema_migrations
```

## Tasks

- Create schema.
- Create migration mechanism.
- Implement repositories.
- Implement transactions.
- Add indexes.
- Add foreign-key constraints.

## Important indexes

Likely:

```text
folders(project_id, parent_id)
clips(project_id, folder_id)
clips(project_id, file_path)
clips(file_hash)
metadata(clip_id, category)
tags(project_id, name)
```

## Exit Criteria

A project can be:

```text
created
saved
closed
reopened
```

with identical logical state.

---

# 7. Phase 3 — Project File

## Goal

Create the user-facing `.project` mechanism.

Example:

```text
/Footage/
    MyDocumentary.project
```

## Tasks

- Create project.
- Store root path.
- Associate database with project.
- Add schema version.
- Validate root path when opening.
- Protect project file from being interpreted as footage.

## Exit Criteria

The user can create:

```text
MyDocumentary.project
```

and reopen it after restarting the application.

---

# 8. Phase 4 — Filesystem Abstraction

## Goal

Create a safe abstraction around physical filesystem operations.

Implement operations approximately equivalent to:

```cpp
exists(path)
createDirectory(path)
moveFile(source, destination)
renameFile(path, name)
listDirectory(path)
fileInfo(path)
```

## Rules

The filesystem layer must:

- detect conflicts;
- report errors;
- never silently overwrite;
- distinguish missing sources from permission failures;
- avoid UI dependencies.

## Testing

Use temporary directories.

Test:

```text
create
move
rename
missing source
existing destination
invalid destination
permission failure
```

## Exit Criteria

Filesystem behavior can be tested without launching the UI.

---

# 9. Phase 5 — Media Import

## Goal

Turn a real footage directory into Clip objects.

## Pipeline

```text
Directory
   ↓
Recursive scanner
   ↓
Supported-file filter
   ↓
Clip creation
   ↓
Database
```

## Tasks

- Define supported extensions.
- Implement recursive scanner.
- Add asynchronous job queue.
- Create Clip records.
- Preserve original filename.
- Store physical path.
- Generate file hash.

## Important design choice

Import should preserve existing physical directory structure rather than automatically reorganizing it.

## Exit Criteria

Given:

```text
Footage/
├── A/
│   ├── C001.MOV
│   └── C002.MOV
└── B/
    └── C003.MOV
```

the database contains:

```text
A/
    C001
    C002

B/
    C003
```

without changing the disk.

---

# 10. Phase 6 — Metadata Extraction

## Goal

Extract and normalize camera/file metadata.

## Pipeline

```text
Video file
    ↓
Metadata extractor
    ↓
Raw metadata
    ↓
Normalizer
    ↓
Normalized metadata
```

## Initial normalized categories

Implement:

```text
Camera
Resolution
Frame Rate
Codec
Duration
Location
Time
Lens
Author/User
Orientation
```

The extractor should gracefully handle absent metadata.

## Missing values

Use an explicit representation rather than dropping the field.

For example:

```text
camera = UNKNOWN
```

## Exit Criteria

A set of test video files from different cameras/containers can be inspected using the same application-level metadata categories.

---

# 11. Phase 7 — Application Command Layer

## Goal

Make all mutations explicit commands.

Examples:

```text
CreateFolderCommand
RenameFolderCommand
MoveFolderCommand
DeleteFolderCommand
MoveClipCommand
RenameClipCommand
AddTagCommand
RemoveTagCommand
```

Each command should conceptually support:

```cpp
execute()
undo()
description()
```

## Why this comes before the UI

The tree and canvas should invoke commands rather than directly modifying model state.

This creates:

```text
UI → Command → Model → Database
```

instead of:

```text
UI → random state mutation
```

## Exit Criteria

Every core editing operation can be performed without a UI.

---

# 12. Phase 8 — Undo System

## Goal

Provide reliable virtual-state Undo.

## Implementation

Maintain an in-memory command stack:

```text
Executed commands
        ↓
Undo stack
```

Potentially:

```text
Undo
Redo
```

although only Undo is required initially.

## Important rule

Undo operates on the logical project state.

It does not automatically imply filesystem rollback after Apply.

## Exit Criteria

Test:

```text
Create folder
Rename folder
Move clip
Rename clip
Delete folder
Add tag
```

and verify each can be undone.

---

# 13. Phase 9 — Automatic Organization Engine

## Goal

Implement the application's primary algorithmic feature.

## Input

```text
Project state
+
Normalized metadata
+
Organization configuration
+
Manual overrides
```

## Output

```text
Organization Proposal
+
Change list
```

## Configuration

Example:

```text
criteria:
    Camera
    Resolution

naming:
    {folder}_{number}
```

## Algorithm

1. Identify clips eligible for automatic organization.
2. Exclude/preserve manual overrides.
3. Read selected metadata.
4. Group by first criterion.
5. Recursively group by subsequent criteria.
6. Create virtual folder nodes.
7. Generate clip titles.
8. Resolve collisions.
9. Produce explicit Change objects.
10. Return proposal.

## Exit Criteria

Given deterministic metadata input, the engine produces deterministic organization output.

---

# 14. Phase 10 — Manual Override System

## Goal

Ensure automatic organization does not overwrite deliberate human decisions.

## State

A clip can be:

```text
AUTOMATIC
MANUAL_OVERRIDE
```

## Example

Automatic:

```text
Sony A7IV/
    4K/
        C001
```

User moves it to:

```text
Interviews/
    John/
        C001
```

The Clip becomes:

```text
MANUAL_OVERRIDE
```

## Reset

Implement:

```text
Reset to Automatic Organization
```

which clears the override and allows the organization engine to reconsider the clip.

## Exit Criteria

Regenerating automatic organization does not move manually overridden clips unless the user resets them.

---

# 15. Phase 11 — Naming Engine

## Goal

Generate predictable filenames.

## MVP variables

Start with a minimal set:

```text
{folder}
{number}
{original}
```

Add others only when required.

Potential future variables:

```text
{camera}
{resolution}
{date}
```

## Collision algorithm

For:

```text
Campus_{number}
```

produce:

```text
Campus_001.MOV
Campus_002.MOV
Campus_003.MOV
```

Never overwrite.

## Exit Criteria

Generated filenames are:

- valid;
- deterministic;
- unique within their destination;
- previewable before Apply.

---

# 16. Phase 12 — Change Model

## Goal

Represent exactly what Apply will do.

Example:

```text
CREATE_FOLDER
    destination = /Interviews

CREATE_FOLDER
    destination = /Interviews/John

MOVE_CLIP
    source = /C001.MOV
    destination = /Interviews/John/John_001.MOV
```

## Change states

Consider:

```text
PENDING
VALID
APPLIED
FAILED
```

## Exit Criteria

The complete filesystem operation set can be reconstructed from the Change list.

---

# 17. Phase 13 — Preflight Validation

## Goal

Find preventable errors before touching the filesystem.

Checks:

```text
source exists
destination valid
destination not unexpectedly occupied
folder path valid
project file protected
no contradictory changes
no illegal hierarchy
```

## Output

Example:

```text
Preflight

✓ 42 moves
✓ 3 folders
✓ 42 filenames
✗ 1 destination collision
```

Apply should be blocked until blocking errors are resolved.

---

# 18. Phase 14 — Apply Engine

## Goal

Execute approved filesystem changes safely.

## Recommended process

```text
Pending Changes
      ↓
Preflight
      ↓
Persist operation plan
      ↓
Execute one operation
      ↓
Record result
      ↓
Execute next
      ↓
Reconcile database
```

## Important

Do not assume filesystem operations are globally transactional.

Use an operation journal/result model.

## Result

```text
APPLIED
PARTIALLY_APPLIED
FAILED
```

## Exit Criteria

A test containing both successful and intentionally failing operations results in:

- correct physical files;
- correct database state;
- detailed report.

---

# 19. Phase 15 — Missing File Detection

## Goal

Detect when a file expected by the project is no longer present.

## Trigger points

Potentially:

- project open;
- explicit Refresh;
- before Apply;
- periodic/background verification.

## MVP

If expected path does not exist:

```text
Clip.status = MISSING
```

No automatic search.

## Exit Criteria

Moving a file externally causes the corresponding Clip to become Missing.

---

# 20. Phase 16 — Initial Native UI

## Goal

Create a usable macOS application around the completed core.

Recommended top-level UI:

```text
Toolbar
├── Project
├── Search
├── Import
├── Organize
├── Undo
└── Review / Apply

Main window
├── Tree
├── Content / Canvas
└── Inspector
```

## First UI milestone

Implement only:

- project open/create;
- tree;
- basic selection;
- inspector;
- import progress.

Do not build the canvas first.

---

# 21. Phase 17 — Tree View

## Goal

Make the tree the authoritative interactive editor.

## Features

- folder expansion;
- clip selection;
- multi-selection;
- drag/drop;
- rename;
- create folder;
- delete folder;
- move clips;
- context menus.

## Architecture

Tree actions should call:

```text
View
  ↓
ViewModel
  ↓
Application Command
  ↓
Core
```

not mutate the database directly.

---

# 22. Phase 18 — Inspector

## Goal

Expose clip/folder properties.

For Clip:

```text
Title
Original Filename
Status
Folder
Tags

Metadata:
Camera
Resolution
FPS
Codec
Location
Duration
...
```

Clearly separate:

```text
Editable organizational data
```

from:

```text
Read-only metadata
```

---

# 23. Phase 19 — Tags

## Goal

Expose the user-semantic layer.

Features:

- create tag;
- rename tag;
- delete tag;
- add tags to clip;
- remove tags;
- bulk tagging.

Example:

```text
C001
Tags:
    Interview
    John
    Good Take
```

---

# 24. Phase 20 — Organization UI

## Goal

Expose the automatic organization engine.

UI:

```text
ORGANIZE FOOTAGE

Available Metadata

[x] Camera
[ ] Location
[x] Resolution
[ ] Frame Rate
[ ] Codec

Order

1. Camera
2. Resolution

Naming

{folder}_{number}

[Preview Organization]
```

## Interaction

The user selects criteria and clicks Preview.

The UI receives a proposal.

The proposal changes the virtual model only.

---

# 25. Phase 21 — Review Changes UI

## Goal

Make filesystem consequences understandable.

Display:

```text
Folders
+ Interviews
+ Interviews/John

Moves
C001.MOV → Interviews/John/

Renames
C001.MOV → John_001.MOV
```

Provide:

```text
Cancel
Undo
Apply
```

---

# 26. Phase 22 — Canvas

## Goal

Provide a visual representation of the same hierarchy.

## Critical rule

The canvas is not a second data model.

It consumes the same project tree.

```text
Project Model
   ├── Tree View
   └── Canvas View
```

## MVP canvas

Start simple:

- folder nodes;
- clip nodes;
- parent-child relationships;
- pan/zoom;
- selection;
- drag/drop.

Sophisticated layout algorithms can be added later.

---

# 27. Phase 23 — Search

## Goal

Provide basic project navigation.

MVP search fields:

```text
Title
Original Filename
Folder
Tag
Camera
Metadata values
```

Do not build semantic AI search yet.

---

# 28. Phase 24 — Performance

Once functionality works end-to-end, optimize.

## Background jobs

Use a task system for:

```text
directory scanning
metadata extraction
hashing
missing-file checks
```

## Database

Use transactions for bulk import.

Avoid:

```text
INSERT
COMMIT
INSERT
COMMIT
```

for every file.

Prefer batched transactions.

## UI

Use lazy/virtualized rendering for large trees.

## Exit Criteria

Test with increasingly large synthetic projects.

Potential stress levels:

```text
1,000 clips
10,000 clips
50,000 clips
100,000+ clips
```

The actual supported target can be established through profiling.

---

# 29. Phase 25 — Reliability and Crash Recovery

## Goal

Make Apply robust enough for real footage.

Implement:

```text
Apply Journal
Operation ID
Operation status
Source
Destination
Result
Error
```

Potential sequence:

```text
OP-001 PENDING
OP-001 SUCCESS

OP-002 PENDING
OP-002 FAILED
```

If the application crashes:

```text
Reopen project
    ↓
Detect unfinished Apply
    ↓
Inspect journal
    ↓
Reconcile filesystem
    ↓
Resume/recover/report
```

---

# 30. Phase 26 — Testing

## Unit tests

Test:

- IDs;
- folder hierarchy;
- clip membership;
- tags;
- metadata normalization;
- naming;
- collision handling;
- organization grouping;
- manual overrides;
- commands;
- undo.

## Integration tests

Test:

```text
Import → Database
Metadata → Organization
Organization → Changes
Changes → Apply
Apply → Database
```

## Filesystem tests

Use temporary directories for:

- normal moves;
- renames;
- collisions;
- missing files;
- permission errors;
- partial failure;
- nested folders.

## UI tests

Test:

- drag/drop;
- rename;
- folder creation;
- deletion prompt;
- Undo;
- organization preview;
- review;
- Apply.

---

# 31. Phase 27 — Packaging

## Goal

Produce a normal macOS application.

Tasks:

- app bundle;
- application icon;
- project file association;
- Finder integration;
- code signing;
- hardened runtime;
- notarization;
- installer/distribution strategy.

The exact distribution method can be selected later.

---

# 32. Recommended Development Milestones

## Milestone A — Core Prototype

Includes:

```text
C++
SQLite
Project
Folder
Clip
Basic persistence
```

Success condition:

> A project can be created and reopened.

---

## Milestone B — Real Footage Ingestion

Includes:

```text
Recursive scan
Video detection
Hashing
Metadata extraction
```

Success condition:

> A real camera directory becomes a populated project.

---

## Milestone C — Virtual Organizer

Includes:

```text
Folders
Clip moves
Renames
Tags
Undo
```

Success condition:

> The entire organization workflow works without touching disk.

---

## Milestone D — Automatic Organization

Includes:

```text
Metadata criteria
Hierarchy generation
Naming templates
Manual overrides
Change model
```

Success condition:

> The application can propose a meaningful hierarchy.

---

## Milestone E — Filesystem Apply

Includes:

```text
Preflight
Apply
Error reporting
Partial success
Missing files
```

Success condition:

> The application can safely reorganize real footage.

---

## Milestone F — Production UI

Includes:

```text
Native macOS interface
Tree
Inspector
Organization panel
Review panel
```

Success condition:

> A user can operate the complete system without developer tooling.

---

## Milestone G — Visual Canvas

Includes:

```text
Canvas
Interactive hierarchy
Tree/canvas synchronization
```

Success condition:

> The project can be understood and edited through either representation.

---

## Milestone H — Hardening

Includes:

```text
Performance
Crash recovery
Large projects
Packaging
```

Success condition:

> The application is suitable for real-world use.

---

# 33. Suggested Class Architecture

A starting point:

```text
core/
├── model/
│   ├── Project
│   ├── Folder
│   ├── Clip
│   ├── Tag
│   ├── Metadata
│   └── OrganizationConfig
│
├── commands/
│   ├── Command
│   ├── CreateFolderCommand
│   ├── MoveClipCommand
│   ├── RenameClipCommand
│   ├── RenameFolderCommand
│   ├── DeleteFolderCommand
│   └── TagCommands
│
├── organization/
│   ├── OrganizationEngine
│   ├── OrganizationProposal
│   ├── NamingEngine
│   └── CollisionResolver
│
├── filesystem/
│   ├── FileSystem
│   ├── FileOperation
│   ├── ApplyEngine
│   └── PreflightValidator
│
├── metadata/
│   ├── MetadataExtractor
│   ├── MetadataNormalizer
│   └── MetadataRegistry
│
└── undo/
    └── UndoManager
```

---

# 34. Application Service Layer

Rather than exposing the database directly to the UI, provide application-level services.

Potential services:

```text
ProjectService
ImportService
ClipService
FolderService
TagService
OrganizationService
ReviewService
ApplyService
SearchService
```

Example:

```cpp
class OrganizationService {
public:
    OrganizationProposal preview(
        ProjectId,
        OrganizationConfig
    );

    void acceptProposal(
        ProjectId,
        OrganizationProposal
    );
};
```

---

# 35. Swift/C++ Boundary

Keep the bridge centered around application operations.

Avoid exposing:

```cpp
std::vector<InternalNode>
std::unordered_map<...>
template-heavy types
```

directly to Swift.

Prefer an interface conceptually like:

```text
ProjectBridge
    openProject()
    createProject()
    import()
    moveClip()
    renameClip()
    organize()
    undo()
    review()
    apply()
```

The Swift UI should not need to know how SQLite works.

---

# 36. Data Flow

## Import

```text
User
 ↓
UI
 ↓
ImportService
 ↓
Filesystem Scanner
 ↓
Metadata Extractor
 ↓
Normalizer
 ↓
Hashing
 ↓
SQLite
 ↓
UI refresh
```

## Organization

```text
User
 ↓
Organization UI
 ↓
OrganizationService
 ↓
OrganizationEngine
 ↓
Proposal
 ↓
Project Model
 ↓
Review UI
```

## Apply

```text
User
 ↓
Review UI
 ↓
ApplyService
 ↓
Preflight
 ↓
Filesystem Operations
 ↓
Operation Journal
 ↓
Database Reconciliation
 ↓
Result UI
```

---

# 37. First Vertical Slice

Before building the entire product, implement one complete thin slice:

```text
Create project
    ↓
Import one video
    ↓
Extract camera metadata
    ↓
Display clip
    ↓
Choose Camera as organization criterion
    ↓
Generate folder
    ↓
Review move
    ↓
Apply
```

For example:

```text
Before:

Footage/
    C001.MOV
    Project.project

After:

Footage/
    Sony A7IV/
        C001.MOV
    Project.project
```

If this works end-to-end, the architecture is validated.

Only then expand the number of metadata categories and UI features.

---

# 38. Recommended Build Order for the Actual Code

A practical order for implementation is:

```text
01. CMake project
02. UUID / ID utilities
03. Domain model
04. SQLite schema
05. Repository layer
06. Project create/open
07. Filesystem abstraction
08. Recursive scanner
09. Hashing
10. Metadata extraction
11. Metadata normalization
12. Command system
13. Undo
14. Organization engine
15. Naming engine
16. Change model
17. Preflight
18. Apply engine
19. Missing-file detection
20. SwiftUI shell
21. Project tree
22. Inspector
23. Tags
24. Organization panel
25. Review panel
26. Canvas
27. Search
28. Performance optimization
29. Crash recovery
30. Packaging
```

---

# 39. What Not to Build Early

Avoid spending early development time on:

```text
AI
Thumbnails
Advanced playback
Semantic search
Fancy canvas physics
Cloud synchronization
Duplicate detection
Geocoding
Complex animation
```

until the core:

```text
Import → Organize → Review → Apply
```

pipeline is stable.

The central product risk is not UI polish; it is correctness of the project model and filesystem synchronization.

---

# 40. Engineering Priorities

Priority order should be:

### Priority 1 — Data correctness

The application must never lose track of a Clip.

### Priority 2 — Filesystem safety

The application must never silently overwrite or delete footage.

### Priority 3 — Organization correctness

The generated hierarchy must correspond exactly to the selected criteria.

### Priority 4 — Human control

Manual changes must survive automatic organization.

### Priority 5 — Persistence

The project must reliably reopen in the same state.

### Priority 6 — Performance

Large projects must remain usable.

### Priority 7 — UI polish

The final application should feel native and refined.

---

# 41. Definition of Done for MVP

The MVP is complete when the following scenario works reliably:

```text
User has 5,000 raw video files.

        ↓

Creates a project.

        ↓

Application scans the directory.

        ↓

Metadata is extracted.

        ↓

User chooses:

    Camera
    Resolution

        ↓

Application proposes hierarchy.

        ↓

User moves several clips manually.

        ↓

Manual decisions become overrides.

        ↓

User adds semantic tags.

        ↓

User defines:

    {folder}_{number}

        ↓

Application generates names.

        ↓

User opens Review.

        ↓

Every filesystem operation is visible.

        ↓

Preflight succeeds.

        ↓

User selects Apply.

        ↓

Files are physically moved/renamed.

        ↓

No file is overwritten.

        ↓

One operation fails.

        ↓

Application reports the failure.

        ↓

Database reflects actual results.

        ↓

User reopens project.

        ↓

Project accurately represents filesystem.
```

That is the core product.

---

# 42. Post-MVP Roadmap

Once the MVP is stable:

## Version 1.1

Potentially:

- thumbnails;
- richer search;
- better canvas;
- saved organization presets;
- persistent undo;
- Locate File;
- duplicate detection.

## Version 1.2

Potentially:

- audio waveform previews;
- lightweight playback;
- temporal grouping;
- geocoding;
- more advanced metadata rules.

## Version 2+

Potentially:

```text
AI analysis
    ↓
People / Objects / Scenes / Speech
    ↓
Semantic metadata
    ↓
Natural-language search
    ↓
AI-assisted organization
```

AI should augment the existing system rather than replace the deterministic metadata/organization layer.

---

# 43. Engineering Principle

The most important implementation rule is:

> **Never let the UI become the source of truth.**

The source of truth should be the application/domain model, persisted through SQLite.

The UI should be a client of that model.

Likewise:

> **Never let an automatic organization algorithm directly manipulate the filesystem.**

It should generate a proposal.

And:

> **Never treat a successful command as proof that the physical filesystem changed.**

Only confirmed filesystem results should update the persisted physical state.

The resulting architecture is:

```text
                 ┌──────────────┐
                 │   macOS UI   │
                 └──────┬───────┘
                        │
                 Application API
                        │
              ┌─────────▼─────────┐
              │   Domain Model    │
              └─────────┬─────────┘
                        │
        ┌───────────────┼────────────────┐
        │               │                │
        ▼               ▼                ▼
    SQLite       Organization       Filesystem
                   Engine             Engine
        │               │                │
        └───────────────┼────────────────┘
                        │
                  Change / Review
                        │
                       Apply
```

This should be the foundation on which the rest of the product is built.
