# UE4SS-Lite

UE4SS-Lite 是一个用于集成到 Unreal Engine 游戏中的友好、高性能 hook 框架。它面向脚本运行时、原生 Mod 和游戏定制扩展构建，整体采用 Rust 优先的架构，C++ 主要作为兼容层和引擎互操作桥接层保留。

## 功能

- 构建核心运行时 DLL：`UE4SSL.dll`。
- 构建脚本引擎 Mod：`UE4SSL.JavaScript` 和 `UE4SSL.Lua`。
- 通过 `xtask` 提供 build、package、install、proxy 生成和 ABI 头同步命令。

## 环境要求

- Windows。
- Visual Studio 2022 17.9 或更新版本。
- MSVC 19.39 / toolset 14.39 或更新版本。
- Rust stable 工具链。

## 仓库结构

- `crates/ue4ssl-dll`：核心 `UE4SSL.dll` 的 Rust 入口。
- `crates/ue4ssl-core`、`crates/ue4ssl-host`、`crates/ue4ssl-abi`：Rust 运行时逻辑、宿主 API 和生成的 C/C++ ABI 头。
- `crates/ue4ssl-native-support`：Input、DynamicOutput、Helpers、String、SinglePassSigScanner 等共享 native 支撑库。
- `crates/ue4ssl-unreal-support`：vendored Unreal、Constructs、Function、MProgram 源码；构建 `ue4ssl_unreal_cpp.lib`。
- `crates/ue4ssl-cpp-support`：vendored UE4SSL C++ 边界源码；构建 `ue4ssl_core_cpp.lib`。
- `crates/ue4ss-hook-rs`：UE4SSHook 的 Rust 实现和 C++ 兼容头。
- `crates/ue4ssl-javascript*` 与 `crates/ue4ssl-lua*`：脚本引擎运行时和 VM support crate。
- `Mods/*/native`：纳入 Cargo workspace 的原生 Mod。
- `crates/xtask`：build、package、install、proxy 和 ABI 同步命令。

旧的根目录 `UE4SSL/` 和 `deps/first/` 源码目录已经不再使用。相关内容现在都位于 `crates/` 下的 support 或 native crate 中。`crates/ue4ssl-unreal-support/vendor/Unreal` 是普通 vendor 源码树，不是 Git 子模块。

## 构建

开发构建：

```powershell
cargo ue4ssl-build
```

发布构建：

```powershell
cargo ue4ssl-build --profile release
```

只构建 native support crates：

```powershell
cargo ue4ssl-build-native-support
```

同步生成的 C++ ABI 头：

```powershell
cargo ue4ssl-sync-abi
```

直接检查或构建核心 DLL：

```powershell
cargo check -p ue4ssl-dll
cargo build -p ue4ssl-dll
```

## 打包与安装

生成可安装的 UE4SS 布局：

```powershell
cargo ue4ssl-package
cargo ue4ssl-package --profile release
```

打包输出目录：

```text
target/package/<debug|release>/ue4ss/
```

安装到游戏目录：

```powershell
cargo ue4ssl-install --destination "<Game>/Binaries/Win64/ue4ss"
cargo ue4ssl-install --profile release --destination "<Game>/Binaries/Win64/ue4ss"
```

package 步骤会根据 `crates/ue4ssl-native` 中的 artifact 元数据复制核心 DLL、脚本引擎 DLL、原生 Mod DLL、PDB 和配置的资源目录。

## Proxy DLL

默认情况下，proxy 生成目标是 `C:\Windows\System32\dwmapi.dll`。

```powershell
cargo ue4ssl-build-proxy
cargo ue4ssl-build-proxy --proxy-path "<path-to-system-dll>"
cargo ue4ssl-package-proxy --profile release --proxy-path "<path-to-system-dll>"
cargo ue4ssl-install-proxy --destination "<Game>/Binaries/Win64" --profile release --proxy-path "<path-to-system-dll>"
```

Proxy package 输出目录：

```text
target/package/<debug|release>/proxy/
```

## 开发说明

- 保留并提交 `Cargo.lock`。本仓库构建 native DLL，lockfile 是 Windows/MSVC 可复现构建基线的一部分。
- 修改 native include、vendor 源码或 ABI 生成逻辑后，运行 `cargo check -p ue4ssl-dll`。
- 修改 support crate 链接行为后，运行 `cargo build -p ue4ssl-dll` 验证 `/WHOLEARCHIVE` 链接路径。
- 修改脚本引擎或 Mod include 路径后，运行 `cargo ue4ssl-build --profile dev` 覆盖完整运行时构建。
- Unreal vendor 树已经不是子模块。对 `crates/ue4ssl-unreal-support/vendor/Unreal` 的更新按普通源码变更处理。

## Credits

UE4SS-Lite 基于 UE4SS 生态。原项目历史可参考上游 [UE4SS contributors](https://github.com/UE4SS-RE/RE-UE4SS/graphs/contributors)。
