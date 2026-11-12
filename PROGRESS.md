# Windows menu qualification progress

- [x] Focused source-only Windows job prepared locally for exact menu/Mac integration head `d1d7aa6c80d1fcc42831a7fc143ef7db65ee767b`.
- [ ] Actual Windows component compilation and focused menu checks. The workflow has not been pushed or run.

The job compiles the three filesystem hooks and actual menu/archive sources, and checks read-only archive selection, protected receiver rejection and caller-context restoration. It reuses the existing pinned standard Windows runner tooling and official zlib setup pin. Synthetic component inputs contain no game assets. Windows gameplay and actual Scaleform execution remain unverified. The parent workflow owner will review the local commit before deciding on push and execution. [Operational handoff](HANDOFF.md).

## 2026-11-11 — Focused menu check prepared

The new branch retains existing qualification code and selects only the new menu component. Unaffected input and shader checks are omitted. The integration feature commit is included as a second parent solely to make its immutable source reachable to the runner. Its tree is archived privately by the job. No feature source is merged into the qualification tree, and the default branch remains unchanged.

## 2026-11-12 00:06 -05:00 — Source review corrects component prerequisites

The fixture now obtains each filesystem adapter's flag values from that adapter's own source or header. The three existing implementations use different creation/truncation/append values, so sharing one header's constants would misinterpret the component results. The PPU filesystem object also receives a source-only include adapter for its generated-header name, backed by the real SDK context header. Python source parsing and diff checks pass. Windows compilation and execution remain pending with the same exact feature head.
