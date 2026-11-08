# Grand Strategy Style Map: private Mac port

Working copy: `TestHost/Plugins/InteractiveMap`. The original download is untouched. No files from this port are installed into the game or engine. `.gitignore` excludes purchased sources/content, test outputs and logs from accidental repository inclusion; do not force-add or publish this directory.

## Environment

Inspected 2026-11-08: Apple Silicon, Xcode 27.2 (27B52), Apple clang 21.0.0, macOS SDK 27.2. Installed Unreal build is 5.8.4, changelist 58902052. Deployment target: macOS 14.0, arm64 only. No applicable AGENTS.md was found in the workspace, ancestors, or source plugin.

## Reproduction

From this directory:

```sh
bash test-generator.sh > logs/generator-final.log 2>&1
bash build-editor.sh > logs/editor-build.log 2>&1
bash test-metal.sh > logs/metal-stdout.log 2>&1
bash package-runtime.sh > logs/package-runtime.log 2>&1
bash test-packaged.sh > logs/packaged-stdout.log 2>&1
```

The generator script compiles all 14 translation units using C++20, libc++, PIC, hidden visibility and `-arch arm64`, archives with Xcode libtool, verifies architecture with lipo, and removes only its own temporary object directory. The host is disposable; its project-local UBT XML disables adaptive unity and limits parallel compilation to reduce memory pressure. Unreal writes normal user-level logs and caches.

## Changes

- Added Mac to seven module allowlists; retained Win64. Descriptor updated to 5.8.0 (metadata, not compatibility evidence).
- Added the existing StructUtils dependency to the descriptor.
- Platform-specific generator link rules: original `.lib` on Win64, native archive on Mac arm64, explicit error for unsupported architecture/platform or missing archive.
- Corrected local generator include capitalization and CMake thirdParty directory spelling.
- Defined the missing boolean lambda return for an invalid centroid as false, consistent with the neighboring point-query method.
- Removed `#pragma once` from C++ implementation files, which Clang rejects under Unreal's warning-as-error policy.
- Converted UE 5.8 JSON shared-string keys explicitly to FString.
- Corrected GPU readback lock size from element count to byte count.
- Added standalone generator smoke coverage and Unreal automation tests for province lookup and GPU color replacement.
- Enabled `bSupportUVFromHitResults=True` in the disposable host's `TestHost/Config/DefaultEngine.ini`, allowing `FindCollisionUV` hit results to resolve map texture coordinates.

## Verification

- Native arm64 library compilation and standalone FromHeightMap generation: **passed**. Binary input produced 14 colors; tile count, BGRA buffer length and invalid-centroid query were checked (`logs/generator-final.log`).
- Full eight-module Unreal editor build against UE 5.8.4 and PortHost Mac Development arm64 build: **passed** (`logs/editor-build-passed.log`, `logs/runtime-build.log`).
- Unreal Metal automation: **both tests passed** (`logs/metal-tests.log`): ColorReplacement GPU dispatch/readback on RHI Metal, and ProvinceLookup. The standalone `xcrun metal --version` diagnostic still reports the optional Metal Toolchain missing; Unreal's engine test ran successfully without installing it.
- Packaged Mac recook/stage/package after enabling hit-result UVs: **passed**, AutomationTool `ExitCode=0` (`logs/package-runtime.log`). Staged app: `TestHost/Saved/StagedBuilds/Mac/PortHost.app`.
- Packaged offscreen run: **passed the runtime warning check**. It ran about 900 frames, with no `FindCollisionUV` / Support-UV warning and no `Error:` or `Fatal` (`logs/packaged-stdout.log`). The process exited successfully after `PreExit Game` and shader-library close; this build did not emit a final `LogExit` line.
- Windowed packaged interaction: **passed**. One click on the visible map logged `Clicked on Tile ID: 193`, `Color searched: (R=17,G=156,B=102,A=255)`, and `UVs: X=0.486 Y=0.914` (`logs/packaged-click-stdout.log`). This proves the click lookup on the sample map resolves a nonzero UV and non-default province color.
- Preservation: all 303 recorded original files still match SHA256; copied content, resources, shaders and licenses match originals (`logs/preservation-check.log`).

## Existing limitations

`MapModeGen::FromMask` is declared but not implemented by `Map::GenerateMap`; using it leaves no lookup map. Tectonic-height generation does not populate its elevation array (implementation is commented out) and ignores requested dimensions. These upstream paths were not replaced or disabled. Windows support is preserved in configuration but has not been rebuilt or run here. Intel macOS is unsupported by the arm64-only archive. The compute-shader multi-pass `StartPixel` gap is not covered by the small GPU automation test. No source or assets were uploaded.
