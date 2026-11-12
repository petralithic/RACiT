# Windows menu qualification progress

- [x] Focused source-only Windows job prepared locally for exact menu/Mac integration head `d1d7aa6c80d1fcc42831a7fc143ef7db65ee767b`.
- [x] Actual Windows component compilation and focused menu checks passed for the exact feature head in run `43159298548`.

The standard Windows job compiled the three filesystem hooks and actual menu/archive sources, and passed read-only archive selection, protected receiver rejection and caller-context restoration checks. It used the existing pinned tooling and official zlib setup pin with synthetic component inputs. Windows gameplay and actual Scaleform execution remain unverified. The feature branch and an unsubmitted review form have been prepared by the parent owner. [Operational handoff](HANDOFF.md).

## **2026-11-12 00:24 -05:00 — Exact menu components pass on Windows**

The hosted Windows run completed successfully at CI commit `f12e5b680ace9a7a0d5e8093055496cc5f61271a`, checking feature source `d1d7aa6c80d1fcc42831a7fc143ef7db65ee767b`. Actual filesystem-hook objects compiled, the menu component executable passed its assertions, and both Python source files passed syntax checks. The parent published the exact feature as `feat/menu-keyboard-prompts` and opened a prefilled review form without submitting a PR. This result covers the source components exercised by the job. Windows game linking, visible gameplay and Scaleform execution remain open. [Successful run](https://github.com/petralithic/RACiT/actions/runs/43159298548).

## 2026-11-11 — Focused menu check prepared

The new branch retains existing qualification code and selects only the new menu component. Unaffected input and shader checks are omitted. The integration feature commit is included as a second parent solely to make its immutable source reachable to the runner. Its tree is archived privately by the job. No feature source is merged into the qualification tree, and the default branch remains unchanged.

## 2026-11-12 00:06 -05:00 — Source review corrects component prerequisites

The fixture now obtains each filesystem adapter's flag values from that adapter's own source or header. The three existing implementations use different creation/truncation/append values, so sharing one header's constants would misinterpret the component results. The PPU filesystem object also receives a source-only include adapter for its generated-header name, backed by the real SDK context header. Python source parsing and diff checks pass. Windows compilation and execution remain pending with the same exact feature head.
