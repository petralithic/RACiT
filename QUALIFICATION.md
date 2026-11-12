# Vulkan loader include-order qualification preparation

This local qualification note is separate from the source review branch `review/macos-vulkan-stdio-2124`, whose proposed head remains `0a3a13e1ac84f2979889e966ede1b500cc36ed97`. The corresponding update review head is `21911c8ff61425d67dbfe53acebbca9fe5b3bb1e` on `review/update120-vulkan-stdio-2124`.

The actual source correction moves the existing `stdio.h` include above the Win32 loader shim. Its `dlerror` function calls `snprintf` before the original include location. Windows CI found the missing declaration while compiling the public update backend. This change imports the declaration before use on Windows and also moves the common include earlier during Mac preprocessing. The shader strings, queue code, audio code and generated game sources retain their existing bytes.

Both public patched SDKs contain exactly the same backend blob `e000e2826d594b223c0203b533256efafbbc6fe7`. The prior blob is `594ba74`. Actual SDK differences contain one inserted line and one deleted line in `libs/video/rsx_vulkan_backend.c`. The larger serialized patch difference updates surrounding hunk context and blob metadata.

Fresh application to pinned SDK `5244db92669b54831c6d3ebb7a96d58c4dbfa6df` reproduces Mac tree `51c020e825425a583c7699e147bfdacc8a12facb` and update tree `31ffe3540adcb6af213687e7a7b494165a0a1b54`. Prior trees are respectively `0220f15970ebfa5b55bf7f2a4969cd0e049919d0` and `ff9fc8ee0586bc7c3ad8475e6896e6f960bdc9e4`. The current private Mac qualification SDK also includes the menu features from the latter tree. Applying only this backend correction there preserves those additional features.

## Minimal cached Mac build

Run after the owned games and prior build jobs have closed. Keep the existing source, SDK and build-directory paths. Both inspected private backend files have prior SHA-256 `9c2743e873b8c6efeb943b487628efb4e3c4168791a65bd163ea1d14b1bbe6b5`. Verify that preimage, move the include in those two files, then require Git blob `e000e2826d594b223c0203b533256efafbbc6fe7` from each `git hash-object` call. Preserve the current private SDK's other changes and index until its owner creates the next checkpoint.

The files to update are `upstream-work/macos2012/sdk/libs/video/rsx_vulkan_backend.c` under the owning project and `upstream-update2021/sdk/libs/video/rsx_vulkan_backend.c` under the session scratch. The update cache's `native-build` directory contains logs. Its actual CMake build directory is reached through `source/out/BCUS98124-1.20/build`.

The root agent can run these two normal cached CMake commands through the existing queue. Each builds the changed backend object and links the player while reusing the other objects.

```sh
P="$HOME/Projects/rccit-recomp"
S="$HOME/Library/Caches/AgentWork/rccit/01a15473-3898-77f0-be69-faef100bf5c5"
agentq run --project "$P" --session 01a15473-3898-77f0-be69-faef100bf5c5 -- cmake --build "$S/upstream-macos2012/build" --parallel 2
agentq run --project "$P" --session 01a15473-3898-77f0-be69-faef100bf5c5 -- cmake --build "$S/upstream-update2021/source/out/BCUS98124-1.20/build" --parallel 2
```

The existing CMake cache source directories and SDK links were inspected. Generation and the earlier shader/object checks can retain their prior evidence. At preparation time, these Mac backend builds and the fixed-head Windows backend compilation remain pending. The root agent owns their actual execution, output hashes, resulting SDK checkpoints and publication decision.

All temporary SDK reconstruction files from preparing the source fixes were removed. Existing qualification checkouts, build objects and original dependency indices were preserved by the preparation work.
