# Bootstrap version alignment plan (RaftCore + RaftCLI)

_Status: PLANNED 2026-08-09. Context: CMP0169 FetchContent deprecation fix made
in RaftCore scripts (uncommitted); RaftCLI scaffolds projects with a stale
pinned bootstrap._

## Problem

1. `raft new` scaffolds a project `CMakeLists.txt` with a **hardcoded**
   `BOOTSTRAP_URL` pointing at the RaftCore **v1.37.1** release asset
   (current RaftCore is v1.52.0) — see
   `RaftCLI/raft_templates/CMakeLists.txt`.
2. The scaffolded `features.cmake` pins `RaftCore@{{raft_core_git_tag}}` with
   default **`main`** (floating). So new projects run a 15-release-old
   bootstrap script that chains into scripts from RaftCore `main` — a version
   skew that produced the deprecated `FetchContent_Populate` warnings
   (CMP0169) on CMake ≥ 3.30 and could produce worse mismatches later.
3. The downloaded bootstrap is fetched on *every* configure and there is no
   way to test local bootstrap changes without cutting a release (discovered
   during PlaneRadar development; PlaneRadar's CMakeLists now has a local
   override — see step 4).

## Fix already made (needs release)

`scripts/RaftBootstrap.cmake` and `scripts/RaftProject.cmake`:
`FetchContent_Populate(<name>)` (deprecated, CMP0169) replaced with
`FetchContent_Declare(... SOURCE_SUBDIR _raft_fetch_only_no_subdir_)` +
`FetchContent_MakeAvailable()`. The non-existent `SOURCE_SUBDIR` is the
documented pattern for download-only (no `add_subdirectory()`) — required
because Raft components join the build via `EXTRA_COMPONENT_DIRS`, not as
CMake subdirectories. Verified against a full PlaneRadar build (CMake 4.0.3,
IDF 6.0.2): zero warnings, identical component resolution.

## Plan

### 1. RaftCore: release the fix

- [x] Commit the two script changes (37c0776)
- [x] Push and cut release **v1.52.1** (published 2026-08-09)
- [x] Release must include the `RaftBootstrap.cmake` asset — via the new
      GitHub Action (step 2); v1.37.1's asset was uploaded manually and no
      workflow existed in this repo before

### 2. RaftCore: GitHub Action to attach RaftBootstrap.cmake to every release

Add `.github/workflows/release-assets.yml`:

```yaml
name: Attach release assets
on:
  release:
    types: [published]
permissions:
  contents: write
jobs:
  attach-bootstrap:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ github.event.release.tag_name }}
      - name: Upload RaftBootstrap.cmake to the release
        run: gh release upload "$TAG" scripts/RaftBootstrap.cmake --clobber
        env:
          GH_TOKEN: ${{ github.token }}
          TAG: ${{ github.event.release.tag_name }}
```

- [x] Add the workflow (16d2732) (trigger on `release: published` so it also
      works for releases created from the GitHub UI; `--clobber` makes re-runs
      safe)
- [x] Verified on v1.52.1: asset uploaded by the action, hash-identical to the
      tagged `scripts/RaftBootstrap.cmake`

### 3. RaftCLI template: bootstrap version follows the RaftCore tag (DECIDED)

Governing rule — never break a working pinned build:

- **`RaftCore@<tag>` in features.cmake** → bootstrap from that same tag:
  `releases/download/<tag>/RaftBootstrap.cmake`. Full pinning of both stages.
- **`RaftCore@main`** → bootstrap from `releases/latest/download/...`. The
  user has already opted out of reproducibility (RaftCore and its stage-2
  scripts float), so a floating stage-1 adds no new risk and avoids the
  stale-pin mismatch hazard.

Implementation:

- [x] Template `CMakeLists.txt` parses the tag from `features.cmake` at
      configure time (`file(STRINGS)` + regex on `RaftCore@`), so
      `features.cmake` stays the single source of truth — no second version
      baked into CMakeLists that can go stale when the user edits the tag
- [x] **Download guard**: URL stamp file - re-download only when missing or
      the pinned version changed; failed download keeps the previous copy
      (warning) or fatal-errors only when no copy exists
- [ ] Caveat: tag-derived asset URLs only exist for releases with the asset
      (≥ v1.37.1, and reliably only once step 2's action is in place)
- [x] Note in RaftCLI README: no per-release RaftCLI change needed any more —
      the URL is derived, not pinned in the template

### 3b. Future RaftCore improvement: stage-1 shim (not urgent)

The downloaded bootstrap is already only "stage 1" (systype detection +
component fetch; it then defers to `${raftcore_SOURCE_DIR}/scripts/...`).
Shrink it further to a trivial shim that parses the tag, fetches RaftCore and
includes the fetched copy's own bootstrap (with a recursion guard) — then the
downloaded file almost never changes and version matching evaporates. Do at a
natural release boundary.

### 4. RaftCLI template: local bootstrap override (optional but recommended)

Add the pattern proven in PlaneRadar's CMakeLists.txt to the template:

```cmake
if(EXISTS "${CMAKE_SOURCE_DIR}/raftdevlibs/RaftCore/scripts/RaftBootstrap.cmake")
    include("${CMAKE_SOURCE_DIR}/raftdevlibs/RaftCore/scripts/RaftBootstrap.cmake")
else()
    # _raft_bootstrap_url derived from the RaftCore@<tag> in features.cmake
    # (tag -> that release's asset; main -> releases/latest) - see step 3
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")
        file(DOWNLOAD ${_raft_bootstrap_url} "${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")
    endif()
    include("${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")
endif()
```

The bootstrap already prefers `raftdevlibs/<lib>` for the *libraries*; this
extends the same dev workflow to the bootstrap script itself. No cost for
normal users (fallback identical).

### 5. Existing projects

- [x] PlaneRadar: CMakeLists rewritten with the same derived-URL + local
      override + download guard logic as the template (build verified; parse
      logic unit-tested for both `main` and pinned-tag cases)
- [ ] Other robdobsn Raft apps: bump on next touch (one-line change; only
      cosmetic urgency — warnings, not breakage, until CMake removes CMP0169
      OLD behaviour)

### 6. Verification

- [ ] Scaffold a throwaway project with the updated RaftCLI (`raft new`),
      build for esp32 and esp32p4, confirm: no CMP0169/deprecation warnings,
      bootstrap version in build log matches the pin, components fetch and
      link
- [x] Tag parse logic unit-tested (PlaneRadar `main` and synthetic pinned
      `v1.52.1` both resolve correctly)
- [x] Delete-build-folder rebuild of PlaneRadar with `raftdevlibs` removed:
      bootstrap downloaded from the v1.52.1 release via `releases/latest`
      (asset download count confirmed), zero deprecation warnings, full build
      succeeded (2026-08-09). Gotcha discovered: open editor tabs can
      resurrect a skeleton `raftdevlibs` — the local-override EXISTS check
      could be hardened to verify Phase2 script presence too
- [ ] Offline reconfigure test (network disabled) to prove the download guard
      keeps an existing build working
