# Draft: move the runtime to ps3recomp v0.19.0

**Status:** both versions build, load Zolar Forest from slot 2 and pass the
150 s rehearsal offscreen. FPS against mac-perf is in `docs/mac-perf-log.md`.

## What moved

- **Runtime pin:** `5244db9` (v0.13.0) → `de4f0903` (master, v0.19.0 plus
  README/CONTRIBUTORS). Both setup scripts point at the new pin.
- **Runtime patch:** regenerated with the BUILDING.md recipe as
  `git diff --ignore-cr-at-eol de4f0903 racit/v019 | tr -d '\r'`. It applies to
  `de4f0903` with `--ignore-whitespace`. It contains five commits from
  petralithic/ps3recomp `racit/v019`:
  1. Our old patch file, rebased onto v0.19. That took 593 conflict hunks in
     52 files.
  2. The mac-perf live-tree work, which was never saved into the patch file
     (about 2.3k diff lines in 29 files: NEON SPU helpers, the
     `--fx-blocks`/`--st-fuse` lifter flags, walker wake, the yield fix).
     The committed `perf/mac-rsx-walker` can't lift its SPU jobs without it.
  3. `extract_spu_images.py` now runs under `python -I`.
  4. `sys_mapper_base`: a port can choose where mapper regions start.
  5. Immediate-mode draws go only through upstream's `immediate_vertex`
     hook (the load-hang fix, below).
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

| Version | Build | Main menu offscreen | Zolar Forest from slot 2 | 150 s rehearsal |
| --- | --- | --- | --- | --- |
| 1.00 | passes | yes | loads | passes, saves unchanged |
| 1.20 | passes | yes | loads | passes, saves unchanged |

Hooks in both lifts match mac-perf (`racit_camera_take` in 1 / 20 files,
`racit_publish_menu_keyboard` and `racacit_front_end_cards` in 1 each).

## The load hang (fixed)

It wasn't SPURS. The level did load: the game ran its level frames behind the
LOADING screen but never drew the post-process chain or the final composite
into the display buffers, so the last loading image stayed up.

Those passes are immediate-mode quads. Our merge kept RACiT's immediate-mode
emulation (vertices gathered into an inline stream at END) next to upstream's
new `immediate_vertex` sink. Every immediate draw then reached the draw engine
as both immediate and inline geometry, and `sink_end` drops mixed groups. The
fix runs the emulation only for a sink without the hook. It's a bug in our
merge, not in upstream, so it isn't an upstream PR candidate.

Found by tracing targets per frame (`RSX_ENG_TRACE_TARGETS=1`) on both builds:
v0.19 frames stopped after the 960x704 lighting pass, mac-perf ones went on
through bloom, luminance and two draws into the display buffer.
