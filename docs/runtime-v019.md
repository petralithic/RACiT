# Draft: move the runtime to ps3recomp v0.19.0

**Status: not ready.** Both versions build, boot and reach the main menu
offscreen, but loading Zolar Forest from slot 2 never finishes on either one.
Don't open a PR until that's fixed.

## What moved

- **Runtime pin:** `5244db9` (v0.13.0) → `de4f0903` (master, v0.19.0 plus
  README/CONTRIBUTORS). Both setup scripts point at the new pin.
- **Runtime patch:** regenerated with the BUILDING.md recipe as
  `git diff --ignore-cr-at-eol de4f0903 racit/v019 | tr -d '\r'`. It applies to
  `de4f0903` with `--ignore-whitespace`. It contains four commits from
  petralithic/ps3recomp `racit/v019`:
  1. Our old patch file, rebased onto v0.19. That took 593 conflict hunks in
     52 files.
  2. The mac-perf live-tree work, which was never saved into the patch file
     (about 2.3k diff lines in 29 files: NEON SPU helpers, the
     `--fx-blocks`/`--st-fuse` lifter flags, walker wake, the yield fix).
     The committed `perf/mac-rsx-walker` can't lift its SPU jobs without it.
  3. `extract_spu_images.py` now runs under `python -I`.
  4. `sys_mapper_base`: a port can choose where mapper regions start.
- **`patch_lift.py`:** the frame-gate anchor now accepts the v0.19 lifter's
  CR test (`>> 25) & 1`) and `ppc_rlw64`.
- **`versions/BCUS98124-1.20-hooks.json`:** all 109 hooks were re-anchored on
  the v0.19 lifter output, with new before/after hashes. The new 1.20 lift has
  exactly the same hook calls as mac-perf's.
- **`game/main.cpp`:** sets `sys_mapper_base = 0x20000000`. v0.19 put the
  title's 256 MB heap at 0x40000000, while the GCM IO, write-watch and
  prompt-scan code all expect it at 0x20000000.

## Where we took upstream (it already fixes the same thing)

- Log caps (`PS3_LOG_FIRST`).
- SPU exact float math (we kept round-toward-zero on by default).
- The reservation re-check under the lock-line lock.
- 64-bit cellFs seek.
- The lwmutex fast path.
- The sys_memory allocator (it reuses freed blocks).
- Thread slot handling (non-joinable threads, main stack info).
- One host clock for mftb, system time and timers (#540).
- NV3089 engine readback and resync label replay.
- The cellSysutil, cellFs and sysPrxForUser TLS fixes.
- The D3D12 backend (Windows).
- **The SPURS kernel:** upstream's per-SPU workers replaced our persistent
  lanes and our HLE kernel. This is the main suspect for the load hang (see
  below).

## Where we kept ours

- The Vulkan backend, whole. Upstream's batch, present split and letterbox
  path don't merge with ours.
- Draw-engine features both sides had built: poly offset, colour snapshot,
  texture cache.
- Our cellPad hot-plug model.
- Trophies and save paths.
- Collision PUT hook semantics.
- The SPU stack pool and the semaphore ACQUIRE grace.

## Tested

| Version | Build | Main menu offscreen | Zolar Forest from slot 2 |
| --- | --- | --- | --- |
| 1.00 | passes | yes | stuck on LOADING (over 5 min, 60 fps loading screen) |
| 1.20 | passes | yes | stuck on LOADING (170 s) |

- The mac-perf build `b7d4b9a` loads Zolar Forest in under 2.5 minutes on
  the same route, and the rehearsal passes.
- Owner saves were unchanged on every run.

## The open blocker

During the hang:

- The main thread is still running frames, and the SPURS workers are running
  WWS and igPm jobs.
- No PPU thread is stuck in a syscall.

Restoring our old SPURS kernel on top of v0.19 (branch
`racit/v019-spurs-ours`) crashes at the first WWS policy-module DMA, because
it no longer matches the merged SPU runtime.

Next steps:

- Diff the SPURS job and event-flag traffic against the mac-perf build during
  the load.
- Or port our lane and WakeUp semantics onto upstream's kernel.

FPS has not been measured.
