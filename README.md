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

- `crates/ue4ssl-dll`: Rust entry point for the core `UE4SSL.dll`.
- `crates/ue4ssl-runtime`, `crates/ue4ssl-abi`: Rust runtime logic, host API, and generated C/C++ ABI headers.
- `crates/ue4ssl-support`: shared native support libraries plus the vendored UE4SSL and MProgram C++ boundary sources; builds `ue4ssl_native_support_cpp.lib` and `ue4ssl_core_cpp.lib`.
- `crates/ue4ssl-unreal-support`: vendored Unreal, Constructs, and Function sources; builds `ue4ssl_unreal_cpp.lib`.
- `crates/ue4ssl-hook`: Rust implementation of UE4SSHook plus C++ compatibility headers.
- `crates/ue4ssl-javascript*` and `crates/ue4ssl-lua*`: script engine runtimes and VM support crates.
- `Mods/*/native`: native mods that are part of the Cargo workspace.
- `crates/xtask`: build, package, install, proxy, and ABI synchronization commands.

The old root-level `UE4SSL/` and `deps/first/` source directories are no longer used. Their contents now live under `crates/` as support or native crates. `crates/ue4ssl-unreal-support/vendor/Unreal` is a normal vendored source tree, not a Git submodule.

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

Cross-compile the core DLL plus Lua/JavaScript script engine DLLs from macOS:

```sh
cargo ue4ssl-build --target x86_64-pc-windows-msvc
```

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

The package step copies the core DLL, script engine DLLs, native mod DLLs, PDBs, and any configured resources according to the artifact metadata embedded in `crates/xtask`.

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
