# Windows contribution checks

This temporary CI branch checks the five immutable heads listed in `windows.py`. The workflow uses the standard `windows-2022` runner, a 30-minute deadline, serial component compilations and two glslang build workers. Dependency versions match the Windows setup script. Package wheel hashes come from the corresponding PyPI release metadata. Public source repositories use exact commit IDs and Git's object verification.

The check compiles the actual modified C/C++ components and executes focused protected-page, mouse, prompt-atlas, graph and signed-remap regressions. Depth shaders come from the exact water engine and compile through its actual runtime translator with the repository-pinned glslang and Windows toolchain. The graph fixture checks generated drawing coordinates. The atlas fixture uses a small synthetic atlas and the actual codec/helper.

Profile checking compiles the real Python source and verifies that an invalid executable is rejected. Valid game fingerprints and generated PPU callers require local game inputs and remain outside this source-only job. Windows graphics-driver behavior, visible gameplay and full game linking remain unverified by these component checks.

The workflow uploads no artifacts, caches, game assets, saves, screenshots or generated game code. Results remain in the job log and summary. All source/build staging is disposable storage on the finite hosted runner. This qualification branch is independent of the proposed feature PRs.
