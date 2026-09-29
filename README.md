# UE4SS-Lite

UE4SS-Lite is a friendly, high-performance hook framework for integrating with Unreal Engine games. It is designed for building scripting runtimes, native mods, and game-specific extensions on top of a Rust-first architecture, with C++ kept as a compatibility and engine-interop layer.

## Features

- Builds the core runtime DLL: `UE4SSL.dll`.
- Builds script engine mods: `UE4SSL.JavaScript` and `UE4SSL.Lua`.
- Provides `xtask` commands for build, package, install, proxy generation, and ABI header synchronization.

## Requirements

- Windows.
- Visual Studio 2022 17.9 or newer.
- MSVC 19.39 / toolset 14.39 or newer.
- Rust stable toolchain.

## Repository Layout

`crates/` contains the core, shared support libraries, and build tools. Script engines and their dedicated VM dependencies live under `scripts/`; user Mods live under `Mods/`. Native source lives under the package that builds it:

| Package | Responsibility |
|---|---|
| `ue4ssl-abi` | Cross-language contracts and C/C++ ABI header generation. |
| `ue4ssl-runtime` | Mod discovery, state, event queues and lifecycle execution. |
| `ue4ssl-platform` | Rust input, logging and file services; builds the matching `native/Input` and `native/DynamicOutput` adapters. Shared foundational headers live in `native/Common`. |
| `ue4ssl-dll` | Core `UE4SSL.dll` assembly, startup and shutdown; owns and builds the core C++ boundary under `native/UE4SSL`. |
| `ue4ssl-unreal-support` | Unreal object/layout/version/native-call adaptation under `vendor/Unreal`; this is a normal vendored tree, not a Git submodule. |
| `ue4ssl-object-searcher` | Native object-search implementations and `Unreal/ObjectSearch` public headers. Consumes Unreal types; the core DLL links its archive alongside Unreal support. |
| `ue4ssl-hook` | Detour/IAT and instruction-address helpers, plus their C++ compatibility headers. |
| `patternsleuth-scanner` | Byte-pattern and cross-reference scanning algorithms. |
| `patternsleuth` | Image/process analysis and address resolvers. |
| `patternsleuth-bind` | Scanning C ABI and its `native/SinglePassSigScanner` adapter; does not assemble the runtime or Hook library. |
| `ue4ssl-lua` | Lua Mod lifecycle and Unreal/script bindings; produces the Lua plugin DLL. |
| `ue4ssl-lua-support` | LuaRaw VM and LuaMadeSimple native dependencies. |
| `ue4ssl-javascript` | JavaScript Mod lifecycle and Unreal/script bindings; produces the JS plugin DLL. |
| `ue4ssl-javascript-support` | QuickJS native dependency. |
| `ue4ssl-proxy` | Original-DLL export forwarding and core loading. |
| `ue4ssl-build` | Shared SDK include capabilities, core import-library linking, ABI-header synchronization, and native Mod compilation. |
| `xtask` | Core-first build ordering, unified Mod discovery/selection, generated Mod workspaces, packaging, installation, and proxy commands. |

```text
scripts/
  ue4ssl-lua/                 Lua engine plugin
  ue4ssl-lua-support/         LuaRaw and LuaMadeSimple
  ue4ssl-javascript/          JavaScript engine plugin
  ue4ssl-javascript-support/  QuickJS
```

Keep additional engines and their dedicated dependencies under `scripts/`. For build and packaging integration, register their Cargo packages in the root workspace and their plugin artifacts in `SCRIPT_ENGINE_ARTIFACTS` in `crates/xtask/src/native.rs`. Package names, build commands, and deployed `mods/UE4SSL.Lua` / `mods/UE4SSL.JavaScript` directories are unchanged by this source layout.

`Mods/` supports two build backends: existing Cargo workspace packages such as `ue4ssl-paksync`, and native sources such as `CPP_MeowChat/native` built through generated Cargo workspaces. Both use the same Mod listing, selection, and packaging rules.

Build scripts depend on `ue4ssl-build`; they do not include shared Rust source by relative path. SDK include helpers separate platform, Unreal/core SDK, scanner, and ObjectSearcher headers. Unreal SDK consumers receive both generated ABI roots; scanner and ObjectSearcher dependencies are selected explicitly. The platform service does not compile the core or scanning adapter. DLL assembly owns whole-archive linking of the separately built components.

The frozen baseline documents (`migration-baseline.md`, `script-api-baseline.md`, and the baseline evidence in `known-defects.md`), `docs/baseline-*`, and `docs/migration-validation.json` are historical records. Their old source paths and hashes are intentionally retained, not rewritten to imply that the baseline used the current directory layout.

## Building

Development build:

```powershell
cargo ue4ssl-build
```

Release build:

```powershell
cargo ue4ssl-build --profile release
```

Build only the native support crates:

```powershell
cargo ue4ssl-build-native-support
```

Synchronize generated C++ ABI headers:

```powershell
cargo ue4ssl-sync-abi
```

Check or build the core DLL directly:

```powershell
cargo check -p ue4ssl-dll
cargo build -p ue4ssl-dll
```

Cross-compile the core Windows DLL from macOS:

```sh
rustup target add x86_64-pc-windows-msvc
cargo ue4ssl-build --target x86_64-pc-windows-msvc --core-only
```

Cross-compile the core DLL, Lua/JavaScript script engines, and all discovered Mods from macOS:

```sh
cargo ue4ssl-build --target x86_64-pc-windows-msvc
```

Mods are discovered from direct subdirectories of `Mods/`:

- A Mod with `Cargo.toml` must be a root workspace member with a `cdylib` target. Cargo metadata supplies its package and DLL target names; no `xtask` artifact registration is needed.
- A Mod without `Cargo.toml` is built from `native/cpp/` in a generated workspace, without a root workspace entry.
- Optional `mod.json` supplies the logical/deployed `name` (otherwise the directory name) and `resources`. The conventional `resources/` directory overlays the deployed Mod directory; other resource roots retain their basename. PakSync declares `{"name":"UE4SSL.PakSync","resources":["config"]}`, producing `mods/UE4SSL.PakSync/config/paksync.ini`.

List both backends, build all Mods, or select one by its logical name (case-insensitive; `--mod` can be repeated):

```sh
cargo ue4ssl-mods list
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mods-only
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mod CPP_MeowChat
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mod UE4SSL.PakSync
```

The default build includes the core, script engines, and both Mod backends. `--mods-only` and `--mod` skip script engines but still build the core first, so `UE4SSL.dll.lib` is current before linking Mods. `--core-only` excludes all engines and Mods. Do not build the core and dependent plugins in one unordered `cargo build -p ...` invocation. Shared build helpers track the core import library so changes trigger dependent relinking.

The cross-compile path keeps the Windows/MSVC ABI and produces Windows DLLs for validation on a Windows game install. Configure a Windows SDK/MSVC CRT provider such as `cargo-xwin`/`xwin`, or provide equivalent `clang-cl`, `lld-link`, `llvm-lib`, Windows SDK, UCRT, and MSVC CRT paths in the environment.

For the first cross-compile phase, keep Unreal-facing C++ ABI, hook trampolines, and code that depends on C++ class layout in C++. Good candidates for later Rust migration are build glue, artifact/path handling, small Win32 FFI helpers, and metadata plumbing that does not participate in Unreal C++ ABI boundaries.

## Packaging and Install

Create a staged UE4SS layout:

```powershell
cargo ue4ssl-package
cargo ue4ssl-package --profile release
```

Package output is written to:

```text
target/package/<debug|release>/ue4ss/
```

When `--target` is provided, package output is written to:

```text
target/package/<target-triple>/<debug|release>/ue4ss/
```

Install into a game directory:

```powershell
cargo ue4ssl-install --destination "<Game>/Binaries/Win64/ue4ss"
cargo ue4ssl-install --profile release --destination "<Game>/Binaries/Win64/ue4ss"
```

The package step copies the core DLL, script engines, both Mod backends, PDBs, enabled markers, and configured resources. `--mods-only` stages all user Mods; `--mod <name>` stages only selected Mods, without the core or script engines. Unless `--no-build` is used, the core is still built first as a link prerequisite.

```powershell
cargo ue4ssl-package --mod UE4SSL.PakSync
cargo ue4ssl-install --mod UE4SSL.PakSync --destination "<Game>/Binaries/Win64/ue4ss"
```

PakSync's deployment script additionally copies the core and preserves its game-specific working-directory handling:

```powershell
.\Mods\ue4ssl-paksync\deploy.ps1 -Profile Release -Destination "<Game>/Binaries/Win64/ue4ss" -PreserveConfig
```

The script invokes `xtask package --mod UE4SSL.PakSync` from the repository root, then deploys its staged files. `-SkipBuild` passes `--no-build` (it still runs packaging); `-Target <triple>` selects cross-compiled artifacts. `-NoPrune` retains the script's optional-artifact/script-engine directories; `-PreserveConfig` retains an existing `paksync.ini`. All paths are resolved independently of the caller's working directory.

## Proxy DLL

By default, proxy generation targets `C:\Windows\System32\dwmapi.dll`.

```powershell
cargo ue4ssl-build-proxy
cargo ue4ssl-build-proxy --proxy-path "<path-to-system-dll>"
cargo ue4ssl-package-proxy --profile release --proxy-path "<path-to-system-dll>"
cargo ue4ssl-install-proxy --destination "<Game>/Binaries/Win64" --profile release --proxy-path "<path-to-system-dll>"
```

Proxy packages are written to:

```text
target/package/<debug|release>/proxy/
```

## Development Notes

- Keep `Cargo.lock` checked in. This repository builds native DLL artifacts, so the lockfile is part of the reproducible Windows/MSVC baseline.
- After changing native includes, vendor sources, or ABI generation, run `cargo check -p ue4ssl-dll`.
- After changing support crate link behavior, run `cargo build -p ue4ssl-dll` to verify the `/WHOLEARCHIVE` link path.
- After changing script engine or mod include paths, run `cargo ue4ssl-build --profile dev` to cover the full runtime build.
- The Unreal vendor tree is no longer a submodule. Treat updates to `crates/ue4ssl-unreal-support/vendor/Unreal` as normal source changes.

## Credits

UE4SS-Lite is based on the UE4SS ecosystem. See the upstream [UE4SS contributors](https://github.com/UE4SS-RE/RE-UE4SS/graphs/contributors) for the original project history.
