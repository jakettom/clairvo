# Video Footage Organizer
## Comprehensive Product and Technical Specification

**Status:** Design specification / pre-implementation  
**Target platform:** macOS, Apple Silicon (initially M1)  
**Primary implementation language:** C++  
**UI:** Native macOS-oriented UI; exact framework to be selected  
**Storage:** SQLite-backed project database  
**Application type:** Standalone non-destructive video-footage organization application

---

# 1. Product Overview

## 1.1 Purpose

This application is a desktop tool for organizing large collections of raw video footage before, alongside, or independently of a conventional video editor.

The application is **not a video editor**. Its primary purpose is to help a user transform an initially unstructured collection of video files into a meaningful, hierarchical organization using:

1. File and camera metadata that can be objectively extracted from the files.
2. User-defined organizational information that cannot be inferred reliably from metadata.
3. Automatic organization proposals.
4. Direct human editing and review of those proposals.
5. Explicit application of approved changes to the physical filesystem.

The central design principle is:

> **The computer handles objective information; the human supplies semantic context.**

For example, the application may know that several clips:

- were recorded by Camera A,
- at 3840×2160,
- at 60 fps,
- at particular GPS coordinates,
- at particular times,

but it cannot necessarily know that those clips represent:

- an interview,
- campus B-roll,
- a particular person,
- a specific event,
- a failed take,
- or the best shot.

The user can provide this semantic information through folders, titles, and tags.

---

# 2. Core Product Concept

The application separates **logical organization** from **physical filesystem changes**.

A user may begin with:

```text
/Footage/
    C001.MOV
    C002.MOV
    C003.MOV
    C004.MOV
    MyDocumentary.project
```

The application imports the files and constructs a virtual representation:

```text
MyDocumentary
├── C001.MOV
├── C002.MOV
├── C003.MOV
└── C004.MOV
```

The user can then request automatic organization based on metadata.

The application might propose:

```text
MyDocumentary
├── Sony A7IV
│   └── 3840x2160
│       ├── C001.MOV
│       └── C002.MOV
└── iPhone
    └── 3840x2160
        ├── C003.MOV
        └── C004.MOV
```

The user can then manually modify the proposal:

```text
MyDocumentary
├── Interviews
│   ├── John
│   │   ├── John_001.MOV
│   │   └── John_002.MOV
│   └── Jane
│       └── Jane_001.MOV
└── B-Roll
    └── Campus
        ├── Campus_001.MOV
        └── Campus_002.MOV
```

Only after reviewing the proposed changes does the user select **Apply**.

The application then performs the corresponding filesystem operations.

---

# 3. Design Principles

## 3.1 Non-destructive organization

The application should avoid making filesystem changes until the user explicitly approves them.

The organization process is:

```text
Raw Files
   ↓
Metadata Extraction
   ↓
Automatic Organization Proposal
   ↓
Human Editing
   ↓
Review Changes
   ↓
Apply
   ↓
Filesystem Changes
```

## 3.2 Virtual hierarchy is authoritative

Before Apply, the application's project model is the source of truth.

The user should be able to:

- create folders,
- rename folders,
- delete folders,
- move folders,
- move clips,
- rename clips,
- create/remove tags,
- modify organizational decisions,
- undo organizational operations.

These changes modify the virtual project model rather than the filesystem.

## 3.3 Filesystem changes are explicit

The filesystem should only be changed when the user explicitly invokes Apply.

Apply can:

- create directories,
- move files,
- rename files,
- rename directories where necessary.

Apply does not:

- delete physical media,
- silently overwrite files,
- silently relocate files,
- silently alter camera metadata.

## 3.4 Human decisions override automatic organization

Automatic organization is a proposal rather than an authority.

If the user manually moves or renames a clip, that decision should take precedence over subsequent automatic organization.

A clip can therefore have an organization override.

The user should also be able to explicitly select:

> Reset to Automatic Organization

This removes the manual override and allows the organization engine to reconsider the clip.

---

# 4. Project Model

## 4.1 Project

A **Project** represents one complete collection of footage and its organizational metadata.

A project contains:

- folders,
- clips,
- labels/tags,
- metadata,
- organization configuration,
- organization state,
- change history.

A project corresponds to a physical root directory.

Only one project is open at a time in the initial version.

## 4.2 Project root

The project's root directory is also the master folder from the application's perspective.

The application does **not** rename the root directory.

For example:

```text
/Footage/
```

remains:

```text
/Footage/
```

even if the project itself is called:

```text
My Documentary
```

The project file lives inside the root directory.

Example:

```text
/Footage/
    C001.MOV
    C002.MOV
    MyDocumentary.project
```

The `.project` file is application infrastructure and is **not** represented as a clip or folder in the virtual footage hierarchy.

---

# 5. Project File

The application creates a project file in the footage root.

Example:

```text
/Footage/MyDocumentary.project
```

The project file stores or references the application's database and project configuration.

The exact physical representation can be finalized during implementation. A practical initial architecture is:

```text
MyDocumentary.project
```

as the user-visible project entry point, with a SQLite database associated with it.

The project should be self-contained enough that opening the project later restores:

- the folder hierarchy,
- clips,
- metadata,
- labels/tags,
- organization settings,
- manual overrides,
- project state.

---

# 6. Folder Model

Folders are logical sorting containers for clips.

Folders can contain:

- clips,
- child folders.

There is no intended fixed nesting depth.

Example:

```text
Project
└── Interviews
    └── John
        └── Campus
            └── Exterior
```

The project root itself acts as the top-level folder.

A clip belongs to exactly one folder at a time.

---

# 7. Clip Model

A **Clip** represents one physical video file.

A clip contains a reference to its physical file rather than containing the video data itself.

A clip should have at least:

- internal stable ID,
- project ID,
- current folder ID,
- physical path,
- original filename,
- current title,
- file fingerprint/hash,
- status,
- organization override state,
- extracted metadata.

## 7.1 Stable clip identity

The internal `clip_id` should **not** be the file hash.

Instead:

```text
clip_id
```

is the application's stable database identity.

Separately:

```text
file_hash
```

is a fingerprint of the physical file.

The hash is not currently used for duplicate detection.

Duplicate detection may be added in a future version.

## 7.2 One physical file = one clip

Each distinct physical file imported into the project corresponds to one Clip record.

Two files with identical filenames in different directories are still distinct clips.

Example:

```text
/A/C001.MOV
/B/C001.MOV
```

represent two separate Clip records.

---

# 8. Clip Status

At minimum, a clip can have:

```text
AVAILABLE
MISSING
```

## 8.1 AVAILABLE

The expected physical file exists at the expected location.

## 8.2 MISSING

The physical file is no longer where the project expects it to be.

A file may become missing if the user moves it outside the project's root/focus directory using another application.

For MVP:

- the application detects the missing file,
- marks the clip as Missing,
- does not automatically relocate it,
- does not provide a Locate File feature.

A future version may provide relinking.

---

# 9. Labels and Tags

Labels provide user-supplied semantic organization.

They are distinct from camera/file metadata.

## 9.1 Tags

Tags are controlled vocabulary items created by the user.

Examples:

```text
Interview
B-Roll
Good Take
Bad Take
John
Campus
Exterior
Night
```

A clip can have multiple tags.

Example:

```text
Clip:
    C001.MOV

Tags:
    Interview
    John
    Good Take
```

## 9.2 Titles

Each clip has exactly one title.

The title is generally associated with the organizational naming scheme.

When filesystem changes are accepted, the title becomes the physical filename, subject to extension and collision handling.

For example:

```text
Title:
John_001
```

may become:

```text
John_001.MOV
```

The application should preserve the appropriate file extension unless the user explicitly supports extension changes in a later version.

---

# 10. Metadata System

Metadata is the primary source of automatically available objective organization information.

The application should support multiple cameras and file formats without requiring users to understand camera-specific metadata keys.

## 10.1 Raw metadata

The application should preserve the raw metadata extracted from each file.

Conceptually:

```text
Clip
 └── Raw Metadata
       ├── key
       ├── value
       └── source
```

Examples of raw metadata might include:

```text
GPSLatitude
GPSLongitude
com.apple.quicktime.location.ISO6709
CameraModelName
FrameRate
VideoCodec
ImageWidth
ImageHeight
```

## 10.2 Normalized metadata

Camera and file metadata should also be mapped into standardized application categories.

For example:

```text
Camera A:
    com.apple.quicktime.location.ISO6709
            ↓
    Location

Camera B:
    GPSLatitude
    GPSLongitude
            ↓
    Location

Camera C:
    XMP location information
            ↓
    Location
```

The user should see:

```text
Location
```

rather than needing to know which camera-specific metadata field produced it.

---

# 11. Proposed Standard Metadata Categories

The initial standardized metadata vocabulary may include:

- Time
- Location
- Camera
- Lens
- Resolution
- Frame Rate
- Codec
- Author/User
- Duration
- Orientation

The exact supported set should be determined by the metadata extraction implementation and supported file formats.

The system should be designed so additional categories can be added later without redesigning the project model.

---

# 12. Metadata Immutability

Metadata extracted from the camera/file is treated as immutable from the user's perspective.

The user cannot directly edit:

```text
Camera
Resolution
Frame Rate
Codec
Location
Duration
```

through the organizer.

This maintains a clear distinction:

```text
Metadata = objective source information
Tags = user-supplied organization
Folders = user/automatic hierarchy
Titles = organizational naming
```

If metadata changes in the physical file outside the application, the application can re-scan the file in a future implementation, but the user should not directly overwrite the metadata through the organizer.

---

# 13. Location Handling

Location metadata should initially be represented as raw coordinates.

For example:

```text
42.3736, -71.1097
```

The application should not automatically convert coordinates into named locations such as:

```text
Harvard Square
Cambridge, MA
```

unless a future feature explicitly adds geocoding.

This keeps the MVP independent of external geocoding services.

---

# 14. Time Handling

Time metadata should be available for organization.

However, the MVP does not need sophisticated temporal session detection.

For example, the application does not initially need to automatically infer:

```text
Day 1
Morning
Afternoon
Interview Session
```

from timestamps.

The system should expose time-related metadata where available and leave more sophisticated temporal grouping for later.

---

# 15. Import Workflow

The user should be able to import one or more directories containing video footage.

The application recursively scans supported video files.

Example:

```text
Footage/
├── CameraA/
│   ├── C001.MOV
│   ├── C002.MOV
│   └── C003.MOV
└── CameraB/
    ├── C101.MOV
    └── C102.MOV
```

The application discovers the video files and creates Clip records.

The initial import should:

1. Identify supported video files.
2. Record their physical paths.
3. Generate stable clip IDs.
4. Generate file fingerprints.
5. Extract metadata.
6. Normalize supported metadata.
7. Add clips to the project.
8. Build the initial virtual folder hierarchy.

The application should not automatically reorganize the filesystem during import.

---

# 16. Initial Organization State

After importing raw footage, the initial virtual state should reflect the existing filesystem as closely as practical.

For example:

```text
/Footage/
    C001.MOV
    C002.MOV
    C003.MOV
```

becomes:

```text
Project
├── C001.MOV
├── C002.MOV
└── C003.MOV
```

If the user imports existing subdirectories, those directories can initially be represented as folders.

The initial import should not arbitrarily flatten a user's existing organization.

---

# 17. Automatic Organization

Automatic organization is one of the application's central features.

The user selects metadata dimensions by which the application should construct a hierarchy.

Example:

```text
Camera
Resolution
```

The organization engine might produce:

```text
Sony A7IV
├── 3840x2160
│   ├── C001.MOV
│   └── C002.MOV
└── 1920x1080
    └── C003.MOV

iPhone
└── 3840x2160
    ├── C004.MOV
    └── C005.MOV
```

The organization engine does not immediately create these directories on disk.

It creates a virtual proposal.

---

# 18. Organization Criteria

The organization UI should expose available standardized metadata fields.

Conceptually:

```text
ORGANIZE FOOTAGE

Available Metadata

[x] Camera
[ ] Location
[x] Resolution
[ ] Frame Rate
[ ] Codec
[ ] Lens
[ ] Author
[ ] Duration
[ ] Orientation
```

The user can choose multiple dimensions.

The order matters.

For example:

```text
1. Camera
2. Resolution
3. Frame Rate
```

produces a hierarchy conceptually equivalent to:

```text
Camera
└── Resolution
    └── Frame Rate
        └── Clips
```

The user should be able to reorder the selected criteria.

---

# 19. Missing Metadata

Not every camera or file will provide every metadata category.

The organization engine therefore needs a defined representation for missing values.

A likely initial behavior is:

```text
Unknown
```

For example:

```text
Camera A
├── 3840x2160
└── Unknown

Unknown Camera
├── 3840x2160
└── ...
```

The exact treatment of missing metadata should be finalized during UI/algorithm implementation.

The key requirement is that missing metadata must not cause clips to disappear from the organization proposal.

---

# 20. Organization Naming

Automatic organization may propose both:

1. Folder names.
2. Clip filenames.

The user should be able to specify a naming template.

Example:

```text
{folder}_{number}
```

might produce:

```text
Campus_001.MOV
Campus_002.MOV
Campus_003.MOV
```

The naming system should initially support a small, predictable set of variables.

Potential variables include:

```text
{folder}
{number}
{original}
{camera}
{resolution}
{date}
```

The exact MVP variable set should be finalized before implementation.

---

# 21. Filename Collision Handling

The application must never silently overwrite an existing file.

If a proposed filename already exists, the application should automatically select a non-conflicting name.

Example:

```text
Interview_001.MOV
Interview_002.MOV
Interview_003.MOV
```

If:

```text
Interview_001.MOV
```

already exists, the application might choose:

```text
Interview_002.MOV
```

or the next available number.

Collision resolution should be deterministic and should be shown in the Review Changes interface.

---

# 22. Organization Overrides

Each clip can have an automatic organization result or a manual override.

Conceptually:

```text
organization_source:
    AUTOMATIC
    MANUAL
```

If a user manually moves a clip:

```text
C001.MOV
```

from:

```text
Camera A/
```

to:

```text
Interviews/John/
```

the application records that the user has overridden the automatic decision.

A later automatic organization operation should not casually undo that choice.

---

# 23. Reset to Automatic

The user should be able to select a manually overridden clip or group of clips and choose:

> Reset to Automatic Organization

This:

1. Removes the manual override.
2. Re-evaluates the clip using current organization criteria.
3. Places it according to the current automatic proposal.
4. Generates the corresponding virtual changes.

This gives the user control without permanently disconnecting a clip from automatic organization.

---

# 24. Change Representation

Organization operations should be represented explicitly.

Potential change types:

```text
MOVE_CLIP
MOVE_FOLDER
RENAME_CLIP
RENAME_FOLDER
CREATE_FOLDER
DELETE_FOLDER
```

Potentially later:

```text
CREATE_TAG
DELETE_TAG
CHANGE_TAGS
```

A change should contain enough information to explain and execute it.

Conceptually:

```text
Change
├── id
├── type
├── source
├── destination
├── old_name
├── new_name
├── associated object
└── source of change
```

The source can identify whether the change came from:

```text
AUTOMATIC
MANUAL
```

---

# 25. Review Changes

Before Apply, the user should have access to a Changes/Review interface.

Example:

```text
REVIEW CHANGES

Folders
    + Create Interviews/
    + Create Interviews/John/
    + Create B-Roll/
    + Create B-Roll/Campus/

Moves
    C001.MOV
        / → /Interviews/John/

    C002.MOV
        / → /Interviews/John/

Renames
    C001.MOV → John_001.MOV
    C002.MOV → John_002.MOV
```

The review screen should make the eventual filesystem consequences clear.

---

# 26. Apply Operation

Apply is the boundary between:

```text
logical organization
```

and:

```text
physical filesystem modification
```

When the user selects Apply, the application executes the required operations.

Possible operations:

1. Create directories.
2. Move files.
3. Rename files.
4. Rename directories where necessary.
5. Update the project database to reflect the resulting physical layout.

The `.project` file remains at the project root.

---

# 27. Apply Failure Handling

Apply should not rely on an all-or-nothing filesystem transaction because ordinary filesystem operations do not necessarily provide database-like transactional semantics across all involved filesystems.

The intended behavior is:

> **Partial execution + detailed error report.**

For example:

```text
Apply Results

✓ Created Interviews/
✓ Created Interviews/John/
✓ Moved C001.MOV
✓ Renamed C001.MOV → John_001.MOV
✗ Failed to move C002.MOV
    Reason: Permission denied
✓ Moved C003.MOV

3 operations succeeded
1 operation failed
```

The database should be updated carefully so that it reflects what actually happened rather than what was merely intended.

Failed operations should remain identifiable so the user can resolve them.

---

# 28. No Physical File Deletion in MVP

Deleting a clip from the project does not delete its physical video file.

The application should explicitly distinguish:

```text
Remove from Project
```

from:

```text
Delete File
```

The second operation should not exist in the MVP.

---

# 29. Folder Deletion

If the user deletes a folder containing clips, the application should ask what should happen to those clips logically.

Example:

```text
Delete folder "Interview"?

This folder contains 12 clips.

[Move clips to parent]
[Remove clips from project]
[Cancel]
```

Important:

- "Remove clips from project" does not delete physical files.
- "Move clips to parent" preserves the clips in the project.
- The physical files remain until Apply changes their location.

If the folder contains nested folders, the application should appropriately handle the entire subtree.

---

# 30. Missing Files

If the application expects:

```text
/Footage/Interviews/John/John_001.MOV
```

but the file is no longer there, the clip becomes:

```text
MISSING
```

The application should visually distinguish missing clips.

MVP does not include:

- automatic searching,
- automatic relocation,
- relinking,
- Locate File workflow.

These can be future features.

---

# 31. Undo System

Virtual organization changes should be undoable.

The UI should provide an explicit:

```text
UNDO
```

button.

Keyboard shortcuts such as:

```text
Cmd + Z
```

may also be supported.

The initial undo history does not need to persist across application restarts.

Undo should apply to logical project changes such as:

- move clip,
- move folder,
- rename clip,
- rename folder,
- create folder,
- delete folder,
- tag modifications,
- organization proposal changes.

The exact relationship between Undo and an already-applied filesystem change should be treated separately from ordinary virtual editing.

---

# 32. Apply vs. Undo

Apply should conceptually be treated as a filesystem operation rather than merely another virtual editing operation.

Once physical changes have been made, ordinary in-memory Undo should not be assumed to reverse them.

A future version could provide a filesystem operation history and safe reversal system, but this is not required for MVP.

---

# 33. User Interface

The application should have a native-feeling macOS interface with a dark-mode-oriented aesthetic.

A conceptual layout is:

```text
┌──────────────────────────────────────────────────────────────┐
│ Project    Search    Import    Organize    Undo    Review   │
├───────────────┬───────────────────────────────┬──────────────┤
│               │                               │              │
│ PROJECT TREE  │        MAIN CONTENT           │  INSPECTOR   │
│               │                               │              │
│ Project       │                               │ Metadata     │
│ ├ Interviews  │                               │ Tags         │
│ │ ├ John      │                               │ Filename     │
│ │ └ Jane      │                               │ Status       │
│ └ B-Roll      │                               │              │
│   └ Campus    │                               │              │
│               │                               │              │
└───────────────┴───────────────────────────────┴──────────────┘
```

The exact layout is flexible.

---

# 34. Tree View

The tree view is the authoritative editing interface.

It should support direct interaction.

Users should be able to:

- expand/collapse folders,
- select clips,
- select folders,
- drag clips into folders,
- drag folders into other folders,
- rename folders,
- rename clips,
- create folders,
- delete folders,
- apply tags through an inspector/context menu.

The tree is a direct representation of the project's current virtual hierarchy.

---

# 35. Visual/Canvas View

The same hierarchy should also be visualized as a canvas or graph-like representation.

This is not a second organizational model.

Both views operate on the same underlying project data.

Conceptually:

```text
                    PROJECT
                       │
          ┌────────────┴────────────┐
          │                         │
      INTERVIEWS                  B-ROLL
          │                         │
     ┌────┴────┐                 CAMPUS
     │         │
    JOHN      JANE
```

The canvas should eventually allow interaction such as:

- dragging objects,
- moving clips,
- creating folders,
- rearranging hierarchy,
- selecting objects,
- opening the inspector.

The tree remains the clearest canonical representation, while the canvas provides a spatial overview.

---

# 36. Inspector

Selecting a clip should expose its properties.

Example:

```text
CLIP

Title
John_001

Original Filename
C001.MOV

Status
Available

Folder
Interviews / John

Tags
Interview
John
Good Take

Metadata

Camera
Sony A7IV

Resolution
3840 × 2160

Frame Rate
59.94 fps

Codec
H.264

Location
42.3736, -71.1097

Duration
00:01:24
```

Metadata should be clearly separated from editable organizational properties.

---

# 37. Organization Interface

The organization interface should provide:

```text
ORGANIZE FOOTAGE

Available Metadata

[x] Camera
[ ] Location
[x] Resolution
[ ] Frame Rate
[ ] Codec
[ ] Lens
[ ] Author
[ ] Duration
[ ] Orientation

Organization Order

1. Camera
2. Resolution

Naming Template

{folder}_{number}

[ Preview Organization ]
```

Preview should modify the virtual project state rather than the physical filesystem.

---

# 38. Search

A search system is useful but can remain relatively simple in MVP.

Potential search targets:

- clip title,
- original filename,
- folder name,
- tag,
- camera,
- metadata values.

Search should not require video-content analysis.

Future semantic search can be added when AI/video analysis is introduced.

---

# 39. Selection Model

The UI should support:

- single selection,
- multi-selection,
- folder selection,
- hierarchical selection where appropriate.

Bulk operations should be supported where safe.

For example:

```text
Select 20 clips
→ Add "B-Roll" tag
```

or:

```text
Select 10 clips
→ Move to Campus/
```

---

# 40. Context Menus

Context menus should expose relevant actions.

For a clip:

```text
Rename
Move to...
Add Tag
Remove Tag
Reset to Automatic Organization
Remove from Project
Reveal in Finder
```

For a folder:

```text
Rename
New Folder
Move
Delete
```

"Reveal in Finder" is a useful non-destructive convenience and can be included if straightforward.

---

# 41. Backend Architecture

The GUI should not contain the application's core business logic.

A recommended conceptual architecture is:

```text
┌─────────────────────────────────────┐
│               GUI                   │
│                                     │
│ Tree View                           │
│ Canvas                              │
│ Inspector                           │
│ Organization UI                    │
│ Changes / Review                   │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌─────────────────────────────────────┐
│        Application Layer            │
│                                     │
│ Project Manager                     │
│ Clip Manager                        │
│ Folder Manager                      │
│ Tag Manager                         │
│ Organization Controller             │
│ Change Manager                      │
│ Undo Manager                        │
└──────────────────┬──────────────────┘
                   │
                   ▼
┌─────────────────────────────────────┐
│             Core Engine             │
│                                     │
│ Data Model                          │
│ Organization Engine                 │
│ Metadata Normalization              │
│ Filesystem Operations               │
│ Validation                          │
└───────────────┬─────────────┬───────┘
                │             │
                ▼             ▼
        ┌──────────────┐  ┌──────────────┐
        │    SQLite    │  │  Filesystem  │
        └──────────────┘  └──────────────┘
                ▲
                │
        ┌───────┴────────┐
        │ Metadata       │
        │ Extraction     │
        └────────────────┘
```

---

# 42. Separation of Responsibilities

## GUI

Responsible for:

- displaying state,
- collecting user interaction,
- initiating application commands,
- showing errors,
- rendering the tree,
- rendering the canvas,
- rendering inspectors and review panels.

It should not independently decide how files are moved or how organization is calculated.

## Application layer

Responsible for coordinating operations.

Examples:

```text
CreateFolder()
MoveClip()
RenameClip()
OrganizeProject()
Undo()
ApplyChanges()
```

## Core engine

Responsible for:

- data rules,
- organization algorithms,
- validation,
- filesystem operations,
- metadata normalization,
- project consistency.

## Database layer

Responsible for persistence.

## Filesystem layer

Responsible for safe physical file operations.

---

# 43. C++ Role

C++ is a strong choice for the core application.

It is particularly suitable for:

- project model,
- SQLite integration,
- metadata processing,
- filesystem operations,
- organization algorithms,
- change management,
- future high-performance media analysis.

The entire application does not need to be written in C++.

A practical architecture could use:

```text
C++
    Core engine
    Data model
    Filesystem
    Metadata
    Organization

Native macOS UI framework
    Interface
    Window management
    Drag/drop
    Menus
    macOS integration

Potential future Python/service layer
    AI analysis
    Machine learning
    Experimental algorithms
```

The core should expose a clean API so that the UI implementation is not tightly coupled to internal data structures.

---

# 44. Possible macOS UI Technology

The UI framework should be selected based on desired native integration and development preferences.

Potential options include:

- SwiftUI
- AppKit
- a C++ UI toolkit
- a hybrid C++ core + Swift/SwiftUI frontend

A particularly attractive architecture for a native macOS application is:

```text
Swift / SwiftUI
        │
        ▼
C++ application/core library
        │
        ▼
SQLite + filesystem
```

This allows:

- native macOS UI,
- good drag/drop behavior,
- native menus and dialogs,
- strong macOS integration,

while retaining C++ for the core engineering-heavy components.

The C++/Swift boundary should be kept relatively narrow.

---

# 45. Database

SQLite is the proposed persistence layer.

Reasons:

- local,
- lightweight,
- transactional,
- mature,
- no server required,
- well suited to a desktop application,
- handles potentially large numbers of clips,
- easy to back up with the project.

The application should not require a network connection for normal organization operations.

---

# 46. Proposed Database Schema

The following schema is conceptual and should be refined during implementation.

## PROJECT

```text
PROJECT
-------
id
name
root_path
created_at
updated_at
```

## FOLDER

```text
FOLDER
------
id
project_id
parent_id
name
order_index
```

`parent_id` provides arbitrary folder nesting.

## CLIP

```text
CLIP
----
id
project_id
folder_id
file_hash
filepath
original_filename
title
organization_override
status
created_at
updated_at
```

## METADATA

```text
METADATA
--------
id
clip_id
category
value
source
```

Potentially the normalized and raw metadata layers should eventually be represented separately.

## TAG

```text
TAG
---
id
project_id
name
```

## CLIP_TAG

```text
CLIP_TAG
--------
clip_id
tag_id
```

This provides many-to-many clip/tag relationships.

## ORGANIZATION_CONFIG

```text
ORGANIZATION_CONFIG
-------------------
id
project_id
sort_fields
naming_template
```

The exact representation of `sort_fields` may be normalized into a separate table if necessary.

## CHANGE

```text
CHANGE
------
id
project_id
type
clip_id
folder_id
source
destination
old_name
new_name
source_type
status
```

The exact fields should evolve as filesystem transaction handling is implemented.

---

# 47. Metadata Storage Refinement

The database should distinguish between raw and normalized metadata.

Conceptually:

```text
RAW_METADATA
------------
clip_id
raw_key
raw_value
source
```

and:

```text
NORMALIZED_METADATA
-------------------
clip_id
category
value
```

This provides two benefits:

1. The application can preserve information it does not yet understand.
2. The organization engine can operate on stable standardized categories.

---

# 48. File Hashing

A file fingerprint should be generated during import.

Potential purposes:

- future duplicate detection,
- file identity verification,
- detecting unexpected physical changes,
- verifying that the file being manipulated is the expected file.

The exact hashing algorithm can be chosen during implementation.

The hash should not be used as the Clip's database identity.

---

# 49. Filesystem Abstraction

Filesystem operations should be isolated behind a dedicated interface.

Conceptually:

```cpp
class FileSystem
{
public:
    createDirectory(...);
    moveFile(...);
    renameFile(...);
    exists(...);
    removeDirectory(...);
    getFileInfo(...);
};
```

The core application should interact with this abstraction rather than directly calling filesystem APIs throughout the UI and business logic.

This makes:

- testing easier,
- error handling centralized,
- future filesystem support easier.

---

# 50. Filesystem Safety

The filesystem layer should validate operations before executing them.

Examples:

- destination path is valid,
- source exists,
- destination does not unexpectedly overwrite an existing file,
- source and destination are not identical,
- parent directory exists or can be created,
- permissions permit the operation,
- filename is valid for the target filesystem.

The application should avoid partial operations where a preflight check can catch a problem beforehand.

---

# 51. Apply Preflight

Before Apply, the application should perform a preflight validation.

Potential checks:

```text
✓ All source files still exist
✓ Destination directories are valid
✓ No conflicting filenames
✓ Required directories can be created
✓ Paths are valid
✓ No illegal moves detected
✓ Project file is protected
```

If errors exist, the user should be shown them before physical modifications begin.

Preflight does not eliminate runtime failures, so detailed post-operation reporting is still necessary.

---

# 52. Project Consistency

The database and filesystem can temporarily diverge during an Apply operation.

The application therefore needs an explicit Apply state model.

Conceptually:

```text
PLANNED
   ↓
PRECHECKED
   ↓
APPLYING
   ↓
PARTIALLY_APPLIED / APPLIED
```

After every physical operation, the project state should be updated or the result should be recorded so the application can recover after a crash.

A future implementation may use a persistent operation journal.

---

# 53. Crash Safety

The application should be designed so that a crash during Apply does not leave the database falsely claiming that all operations completed.

A practical approach is:

1. Generate explicit changes.
2. Persist the pending change set.
3. Execute operations one at a time.
4. Record each successful operation.
5. Record failures.
6. Reconcile database state with filesystem state after Apply.

This is especially important for large footage collections.

---

# 54. Project Opening

When a project is opened:

1. Load project database.
2. Validate root path.
3. Check project version/schema.
4. Load folders.
5. Load clips.
6. Load metadata.
7. Load tags.
8. Check filesystem state as needed.
9. Mark missing files.
10. Render project.

The application should not necessarily hash every file every time the project opens; efficient incremental validation should be considered.

---

# 55. Project Versioning

The `.project` format/database should contain a schema version.

Example:

```text
schema_version = 1
```

This allows future application releases to migrate projects.

Potential migration path:

```text
v1 → v2 → v3
```

rather than requiring users to recreate projects.

---

# 56. Performance Goals

The application should be designed for large footage collections.

Potential performance considerations:

- asynchronous directory scanning,
- asynchronous metadata extraction,
- incremental database writes,
- background hashing,
- virtualized tree rendering,
- lazy metadata loading,
- efficient SQLite indexes,
- batched database transactions.

The UI should remain responsive while importing or analyzing files.

---

# 57. Import Progress

Large imports should provide visible progress.

Example:

```text
Importing footage...

1,284 / 4,921 files

Extracting metadata...
```

The UI should distinguish:

```text
Scanning
Metadata extraction
Hashing
Database insertion
```

where practical.

---

# 58. Supported Media

The MVP should focus on video files.

The exact list of extensions should be implementation-defined.

Likely initial formats include common camera formats such as:

```text
.MOV
.MP4
.M4V
```

The architecture should permit additional formats later.

Unsupported files should not be silently imported as clips.

---

# 59. Metadata Extraction Layer

Metadata extraction should be isolated from the application core.

Conceptually:

```text
Video File
    ↓
Metadata Extractor
    ↓
Raw Metadata
    ↓
Normalizer
    ↓
Normalized Metadata
```

The extractor should ideally support common metadata standards and container formats.

A third-party media metadata library can be used rather than implementing every container parser from scratch.

---

# 60. Organization Engine Architecture

The organization engine should be independent of the UI.

Input:

```text
Project
+
Organization Configuration
+
Current Manual Overrides
```

Output:

```text
Organization Proposal
+
Filesystem Changes
```

Conceptually:

```cpp
OrganizationProposal organize(
    const Project& project,
    const OrganizationConfig& config
);
```

The proposal should be deterministic given the same:

- project state,
- metadata,
- configuration,
- overrides.

---

# 61. Organization Algorithm

A basic algorithm can operate as follows:

1. Identify all eligible clips.
2. Read selected normalized metadata.
3. Ignore metadata dimensions explicitly overridden by manual decisions where appropriate.
4. Group clips by first criterion.
5. Within each group, group by second criterion.
6. Continue recursively for each selected criterion.
7. Generate folder names.
8. Generate clip names.
9. Resolve naming collisions.
10. Generate proposed changes.
11. Preserve manually overridden decisions.
12. Return proposal.

Example:

```text
Criteria:
    Camera
    Resolution
```

Input:

```text
C001 → Sony A7IV, 4K
C002 → Sony A7IV, 4K
C003 → Sony A7IV, 1080p
C004 → iPhone, 4K
```

Output:

```text
Sony A7IV/
    3840x2160/
        C001
        C002
    1920x1080/
        C003

iPhone/
    3840x2160/
        C004
```

---

# 62. Organization Proposal vs. Project State

The application should not necessarily maintain multiple saved proposals.

MVP behavior:

- Generate one proposal.
- User edits it.
- User can reset/start over if desired.
- No saved intermediate proposals.
- No multiple proposal comparison.

This keeps the initial implementation simple.

---

# 63. Reset Organization

The user should be able to discard the current proposed organizational arrangement and start again.

This is different from Undo.

Conceptually:

```text
Reset Organization
```

returns the organization proposal to a known baseline rather than traversing individual historical actions.

The exact semantics should be finalized during UI design.

---

# 64. Manual Editing After Automatic Organization

The normal workflow should permit:

```text
Automatic proposal
      ↓
Manual edits
      ↓
Review
      ↓
Apply
```

Manual edits are first-class operations, not exceptions.

The application should not force the user to accept the automatically generated hierarchy.

---

# 65. Future AI Layer

AI analysis is explicitly outside the MVP.

Future analysis could include:

- visual scene detection,
- people recognition,
- object recognition,
- speech-to-text,
- OCR,
- semantic scene descriptions,
- shot quality analysis,
- automatic event detection,
- natural-language search.

The architecture should allow AI-derived information to become another metadata-like layer without replacing the current system.

For example:

```text
Camera Metadata
    ↓
User Tags
    ↓
AI Analysis
```

could all eventually contribute information to organization/search.

---

# 66. Future Semantic Search

A future system could allow queries such as:

```text
"Show clips of John speaking outside on campus."
```

This should be built as an additional search/indexing layer rather than changing the fundamental Clip/Folder model.

---

# 67. Future Duplicate Detection

Duplicate detection is explicitly deferred.

The existing file hash provides a foundation.

Future behavior could identify:

```text
C001.MOV
C017.MOV
```

as identical physical content.

However, MVP should not automatically merge or deduplicate clips.

---

# 68. Future File Relinking

A future version can provide:

```text
Locate Missing File
```

which allows the user to point the application at a new physical location.

Potential matching strategies:

- filename,
- file hash,
- metadata,
- file size,
- timestamps.

None of this is required for MVP.

---

# 69. Future Multi-Project Features

MVP supports one project open at a time.

Projects remain independent.

A file belongs to a project based on its physical location.

The system should not assume that clips are shared between projects.

Future versions could support:

- multiple open projects,
- cross-project search,
- project libraries,
- shared media databases.

These are not MVP requirements.

---

# 70. Example Complete Workflow

## Step 1: Start project

User chooses:

```text
/Footage/
```

Application creates:

```text
/Footage/MyDocumentary.project
```

## Step 2: Scan

Application discovers:

```text
C001.MOV
C002.MOV
C003.MOV
C004.MOV
```

## Step 3: Extract metadata

Example:

```text
C001
Camera: Sony A7IV
Resolution: 3840x2160
FPS: 59.94
Location: 42.37, -71.11

C002
Camera: Sony A7IV
Resolution: 3840x2160
FPS: 59.94

C003
Camera: iPhone
Resolution: 3840x2160
FPS: 30

C004
Camera: iPhone
Resolution: 1920x1080
FPS: 30
```

## Step 4: Choose organization

User selects:

```text
Camera
Resolution
```

## Step 5: Generate proposal

Application proposes:

```text
Sony A7IV/
    3840x2160/
        C001.MOV
        C002.MOV

iPhone/
    3840x2160/
        C003.MOV
    1920x1080/
        C004.MOV
```

## Step 6: Human edits

User changes:

```text
Sony A7IV/3840x2160/C001
```

to:

```text
Interviews/John/C001
```

and tags it:

```text
Interview
John
```

This becomes a manual override.

## Step 7: Rename

User's naming template:

```text
{folder}_{number}
```

generates:

```text
John_001.MOV
```

## Step 8: Review

Changes panel shows:

```text
Create Interviews/
Create Interviews/John/
Move C001.MOV → Interviews/John/
Rename C001.MOV → John_001.MOV
...
```

## Step 9: Apply

The application physically performs the operations.

Final filesystem:

```text
/Footage/
├── Interviews/
│   └── John/
│       └── John_001.MOV
├── iPhone/
│   ├── 3840x2160/
│   │   └── ...
│   └── 1920x1080/
│       └── ...
└── MyDocumentary.project
```

## Step 10: Continue organizing

The project remains open and the application continues tracking the resulting structure.

---

# 71. MVP Feature List

## Required

### Project
- Create/open project
- Project file at footage root
- One project open at a time
- Root directory remains unchanged

### Import
- Recursive video scan
- Clip creation
- File path tracking
- File fingerprint
- Metadata extraction
- Metadata normalization

### Organization
- Metadata-based organization
- Multiple metadata criteria
- Criterion ordering
- Automatic folder hierarchy
- Automatic naming
- Naming template
- Manual overrides

### Editing
- Create folders
- Rename folders
- Delete folders
- Move folders
- Move clips
- Rename clips
- Tags
- Multi-selection
- Undo
- Reset to automatic organization

### Review
- Changes panel
- Proposed folder operations
- Proposed moves
- Proposed renames
- Preflight validation

### Apply
- Create directories
- Move files
- Rename files
- Collision avoidance
- Partial failure handling
- Detailed error report
- Database/filesystem reconciliation

### Status
- Available
- Missing

### UI
- Native-feeling macOS interface
- Dark mode
- Tree view
- Canvas representation
- Inspector
- Organization panel
- Changes/review panel

---

# 72. Explicitly Out of Scope for MVP

The following should not be required for the first implementation:

- Video editing
- Video transcoding
- Advanced playback
- Scrubbing
- AI video analysis
- Object recognition
- Face recognition
- Speech recognition
- Semantic video search
- Duplicate detection/merging
- File deletion
- Automatic file relocation
- Locate File/relinking
- Geocoding
- Multiple simultaneous projects
- Saved proposal history
- Persistent undo history
- Sophisticated temporal session detection
- Cloud synchronization
- Remote collaboration
- Mobile application

---

# 73. Suggested Development Phases

## Phase 1 — Core Data Model

Implement:

- Project
- Folder
- Clip
- Tag
- Metadata
- SQLite persistence

Goal:

> A project can be created, saved, reopened, and displayed.

---

## Phase 2 — Filesystem Scanner

Implement:

- recursive directory scan,
- supported-file detection,
- Clip creation,
- file paths,
- file hash,
- missing-file detection.

Goal:

> The application can ingest a real footage directory.

---

## Phase 3 — Metadata Extraction

Implement:

- raw metadata extraction,
- normalized metadata,
- camera detection,
- resolution,
- frame rate,
- codec,
- location,
- duration,
- other supported categories.

Goal:

> The application understands the objective information available in the footage.

---

## Phase 4 — Tree UI

Implement:

- project tree,
- folders,
- clips,
- selection,
- drag/drop,
- rename,
- create/delete folders,
- move clips.

Goal:

> The user can manually organize footage virtually.

---

## Phase 5 — Tags and Inspector

Implement:

- tag creation,
- tag assignment,
- clip inspector,
- metadata display.

Goal:

> Users can add semantic context.

---

## Phase 6 — Organization Engine

Implement:

- metadata selection,
- ordering,
- grouping,
- folder generation,
- naming templates,
- collision resolution,
- manual override preservation.

Goal:

> The application can automatically propose useful organization.

---

## Phase 7 — Review and Undo

Implement:

- change model,
- explicit Undo,
- Changes panel,
- reset organization,
- manual override state.

Goal:

> The user can safely modify and inspect proposals.

---

## Phase 8 — Apply

Implement:

- preflight,
- filesystem operations,
- collision handling,
- partial failure handling,
- error reports,
- database reconciliation.

Goal:

> The application can safely turn the virtual organization into physical filesystem organization.

---

## Phase 9 — Canvas

Implement:

- alternate hierarchy representation,
- interactive nodes,
- synchronized state with tree view.

Goal:

> The same project can be understood spatially as well as hierarchically.

---

## Phase 10 — Performance and Reliability

Implement:

- background scanning,
- background metadata extraction,
- asynchronous hashing,
- database optimization,
- crash recovery,
- large-project testing.

Goal:

> The application remains usable with very large footage collections.

---

# 74. Testing Strategy

Testing should be divided into several layers.

## Unit tests

Test:

- folder hierarchy,
- clip movement,
- naming,
- collision handling,
- metadata normalization,
- organization algorithm,
- tag operations,
- change generation.

## Filesystem tests

Use temporary directories to test:

- create folder,
- move file,
- rename file,
- collision,
- missing source,
- permission failure,
- partial Apply.

## Database tests

Test:

- project creation,
- persistence,
- reopening,
- migrations,
- relationships,
- rollback behavior.

## Organization tests

Given known metadata:

```text
C001 → Camera A / 4K
C002 → Camera A / 4K
C003 → Camera B / 1080p
```

the expected organization should be deterministic.

## UI tests

Test:

- drag/drop,
- rename,
- multi-selection,
- undo,
- review,
- Apply confirmation.

---

# 75. Key Architectural Invariants

The implementation should preserve the following rules.

### Invariant 1

A Clip has exactly one current folder.

### Invariant 2

A Clip has one stable internal ID independent of its file hash.

### Invariant 3

A file hash is a fingerprint, not the clip's identity.

### Invariant 4

Metadata is immutable through the organizer.

### Invariant 5

Tags are user-controlled and separate from metadata.

### Invariant 6

Manual organization can override automatic organization.

### Invariant 7

Automatic organization does not directly modify the filesystem.

### Invariant 8

Apply is required for physical filesystem changes.

### Invariant 9

Physical file deletion is not part of MVP.

### Invariant 10

The `.project` file remains at the project root.

### Invariant 11

The project root directory is never renamed by the application.

### Invariant 12

The application never silently overwrites an existing file.

### Invariant 13

A missing physical file is represented as a Missing Clip rather than automatically removed.

### Invariant 14

Tree and canvas views represent the same underlying hierarchy.

---

# 76. Conceptual API

The exact API is implementation-dependent, but the application core should expose operations resembling:

```cpp
Project createProject(Path root);

Project openProject(Path projectFile);

void importDirectory(ProjectId project, Path directory);

FolderId createFolder(ProjectId project, FolderId parent, String name);

void renameFolder(FolderId folder, String name);

void moveFolder(FolderId folder, FolderId destination);

void deleteFolder(FolderId folder, DeleteFolderBehavior behavior);

void moveClip(ClipId clip, FolderId destination);

void renameClip(ClipId clip, String title);

void addTag(ProjectId project, String tag);

void addTagToClip(ClipId clip, TagId tag);

OrganizationProposal generateOrganization(
    ProjectId project,
    OrganizationConfig config
);

void applyOrganizationProposal(
    ProjectId project,
    OrganizationProposal proposal
);

void undo(ProjectId project);

ApplyResult applyChanges(ProjectId project);
```

The actual API should be command-oriented enough that both UI and future automation can use the same core functionality.

---

# 77. Command-Based Architecture

A command model is likely useful for the application.

For example:

```text
MoveClipCommand
RenameClipCommand
CreateFolderCommand
DeleteFolderCommand
MoveFolderCommand
AddTagCommand
RemoveTagCommand
```

Each command can provide:

```text
execute()
undo()
describe()
```

This naturally supports the explicit Undo system.

Automatic organization can also generate commands rather than directly mutating state.

---

# 78. Event/Change Notifications

The UI should be able to react when the core model changes.

For example:

```text
FolderCreated
FolderRenamed
ClipMoved
ClipRenamed
TagChanged
MetadataUpdated
ApplyStarted
ApplyFinished
ClipMissing
```

This prevents the GUI from needing to continuously poll the database.

---

# 79. Future Extensibility

The architecture should be intentionally layered.

The long-term conceptual model is:

```text
                 ┌───────────────┐
                 │ User Actions  │
                 └───────┬───────┘
                         │
                 ┌───────▼───────┐
                 │ Project Model │
                 └───────┬───────┘
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
      Metadata        User Tags      AI Analysis
          │              │              │
          └──────────────┼──────────────┘
                         ▼
                Organization Engine
                         │
                         ▼
                  Change Proposal
                         │
                         ▼
                       Review
                         │
                         ▼
                       Apply
                         │
                         ▼
                    Filesystem
```

This allows future capabilities without redesigning the fundamental project model.

---

# 80. Product Philosophy

The application should not attempt to replace the user's understanding of their footage.

Instead, it should reduce the mechanical work required to turn that understanding into a physical organization.

The application knows:

```text
What the file says.
```

The user knows:

```text
What happened.
```

The application's job is to connect those two sources of information.

The most important workflow is therefore:

```text
COMPUTER
Objective metadata
       │
       ▼
Automatic proposal
       │
       ▼
HUMAN
Semantic knowledge
       │
       ▼
Manual refinement
       │
       ▼
Review
       │
       ▼
Apply
       │
       ▼
FILESYSTEM
```

---

# 81. Final MVP Definition

The first usable version should answer one fundamental question:

> Can a user take a large, messy directory of raw video files, have the application understand their available metadata, automatically propose a structured organization, manually refine that structure, review every resulting filesystem change, and safely apply it without losing or overwriting footage?

If yes, the core product has been successfully implemented.

Everything else—including AI analysis, semantic video search, duplicate detection, geocoding, advanced playback, and collaboration—can be layered onto this foundation later.

---

# 82. Open Design Decisions

The following items are intentionally not fully locked down yet.

## UI framework

Candidates:

- SwiftUI
- AppKit
- C++ UI framework
- SwiftUI/AppKit frontend + C++ core

## Exact metadata categories

Initial proposal:

```text
Time
Location
Camera
Lens
Resolution
Frame Rate
Codec
Author/User
Duration
Orientation
```

## Missing metadata behavior

Likely:

```text
Unknown
```

but exact grouping/display semantics remain to be finalized.

## Naming template variables

Potential:

```text
{folder}
{number}
{original}
{camera}
{resolution}
{date}
```

The MVP should keep this deliberately small.

## Apply/Undo relationship

Virtual organization Undo is required.

Filesystem reversal after Apply is not an MVP requirement and should be designed separately.

## Canvas interaction model

The canvas should represent the same hierarchy as the tree, but the precise visual layout and interaction mechanics remain open.

---

# 83. Recommended Initial Repository Structure

A possible implementation structure is:

```text
VideoOrganizer/
├── CMakeLists.txt
├── README.md
│
├── app/
│   ├── main/
│   └── application/
│
├── core/
│   ├── project/
│   ├── clips/
│   ├── folders/
│   ├── tags/
│   ├── metadata/
│   ├── organization/
│   ├── changes/
│   ├── undo/
│   └── filesystem/
│
├── database/
│   ├── schema/
│   ├── migrations/
│   └── sqlite/
│
├── media/
│   ├── metadata/
│   ├── hashing/
│   └── formats/
│
├── ui/
│   ├── tree/
│   ├── canvas/
│   ├── inspector/
│   ├── organization/
│   └── changes/
│
├── tests/
│   ├── core/
│   ├── database/
│   ├── filesystem/
│   ├── organization/
│   └── ui/
│
└── resources/
```

This is only a starting point; the final structure should follow the selected macOS UI architecture.

---

# 84. Summary

The application is a **non-destructive, metadata-aware video footage organizer for macOS**.

Its fundamental architecture is:

```text
Physical Video Files
        │
        ▼
Metadata Extraction
        │
        ▼
Normalized Metadata
        │
        ▼
Organization Engine
        │
        ▼
Virtual Project Hierarchy
        │
        ├───────────────┐
        ▼               ▼
   Tree View        Canvas View
        │               │
        └───────┬───────┘
                ▼
          Human Editing
                │
                ▼
        Change / Review
                │
                ▼
             Apply
                │
                ▼
       Physical Filesystem
```

The application should remain deliberately conservative about destructive operations.

The user should always be able to distinguish:

```text
What the application proposes
```

from:

```text
What the user has decided
```

and from:

```text
What has actually happened on disk
```

That separation is the central architectural principle of the entire application.
