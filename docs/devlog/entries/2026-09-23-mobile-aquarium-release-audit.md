# 2026-09-23 — Mobile native aquarium and pre-release branch audit

## Implemented

- Android imports six original resident models from the approved Blender source,
  with closed backs, articulated working/attention poses, reactive fish and a
  smaller wandering snail. Export and asset tests prevent other Blender scenes
  from leaking a default cube into the model.
- Shared generated native foreground budget: eight residents, selected and
  awaiting sessions first, with every session retained in the full roster.
  This does not infer parent/child relationships from matching project names.
- Apple reserves scene space for attention and timeline panels; native viewing
  hides the timeline. Labels gain unlit backing, and wide viewports adjust the
  camera. The existing dashboard preference and default remain unchanged.
- Android limits the native render surface and shadow map, removes expensive
  optional effects and samples power/thermal state every two seconds. Dashboard
  text remains at display resolution; the separate e-ink path is preserved.

## Measured validation

- Lenovo tablet, thermal status 3 in both samples: native SurfaceFlinger cadence
  8.380 → 29.836 fps; main UI 13.211 → 58.522 fps; render-engine average about
  76 → 11.135 ms. The user confirmed movement was clearly smoother. These are
  device measurements, not a universal or normal-temperature 60 fps guarantee.
- Full TypeScript build/typecheck and Vitest: 4,671 passed, two skipped.
  Protocol regeneration left no tracked drift.
- Android: 409 unit tests and final release APK/AAB build passed. iOS development
  build passed; Apple targeted native tests: 36 passed. Documentation, catalog,
  devlog and token synchronization gates passed. Design lint reports the same
  92 working-checkout findings (89 tracked baseline plus three generated-output
  findings), with no newly introduced finding. Final visual/archive release
  gates remain; development compilation is not App Store validation.

## Local branch audit before Apple delivery

- Fetched/pruned origin and inspected every local branch and both worktrees.
  The shared root was clean and its master behind origin; it was not switched
  or reset. The release branch includes all of origin/master with 23 additional
  commits before this mobile commit, including two not yet pushed.
- Removed ten inactive local branches only after proving ancestry or exact
  patch equivalence against origin/master. A verified Git bundle of all local
  branches/tags, a binary working diff, untracked-file archive and branch SHA
  inventory were retained outside the repository before removal.
- Retained the unmerged commercialization research branch. Its dated findings
  were not adopted as current release policy. No stash or abandoned worktree
  needed cleanup.
- Preserved the modified dark-garden Blender source and its backup separately
  from the release staging set. Object names, transforms, mesh topology and
  material assignments matched the committed scene in a 511-object comparison;
  this is not proof of equality of every Blender property. Android assets were
  exported from the committed source, not the unreviewed local binary.
- Open PR #363 is an independent Node Claude-recovery fix with green CI. It is
  not a prerequisite for the native Apple apps and was not silently folded into
  this release. PR #362 remains the current device/dashboard work.

## Delivery state

Android 1.5.0 (20) is installed for tablet verification; iOS/macOS source version
is 1.5.0. Store upload/submission has not occurred for this preparation. The user
requested local cleanup before proceeding with Apple delivery. Preserve that
ordering, complete final visual and Release archive checks, and then record each
store's actual upload/submission/live state independently. No additional ESP32
or deck firmware deployment is needed solely for these native app UI changes.
