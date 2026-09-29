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

`crates/` 包含核心、公共支撑库与构建工具；脚本引擎及其专用 VM 依赖位于 `scripts/`，用户 Mod 位于 `Mods/`。原生源码放在实际负责构建它的模块内：

| 模块 | 职责 |
|---|---|
| `ue4ssl-abi` | 跨语言契约、C/C++ ABI 头生成。 |
| `ue4ssl-runtime` | Mod 发现、状态、事件队列和生命周期执行。 |
| `ue4ssl-platform` | Rust 输入、日志和文件服务；编译对应的 `native/Input`、`native/DynamicOutput` 薄适配，基础公共头位于 `native/Common`。 |
| `ue4ssl-dll` | 核心 `UE4SSL.dll` 组装、启动和关闭；拥有并编译 `native/UE4SSL` 下的核心 C++ 边界代码。 |
| `ue4ssl-unreal-support` | `vendor/Unreal` 下的引擎对象、布局、版本及原生调用适配；这是普通 vendor 源码树，不是 Git 子模块。 |
| `ue4ssl-object-searcher` | 原生对象搜索实现及 `Unreal/ObjectSearch` 公共头；消费 Unreal 类型，由核心 DLL 与 Unreal support 一起链接其原生库。 |
| `ue4ssl-hook` | Detour、IAT、指令地址辅助及对应 C++ 兼容头。 |
| `patternsleuth-scanner` | 字节模式与交叉引用扫描算法。 |
| `patternsleuth` | 映像、进程分析及地址解析器。 |
| `patternsleuth-bind` | 扫描 C ABI 与 `native/SinglePassSigScanner` 适配；不负责组装 runtime 或 Hook 库。 |
| `ue4ssl-lua` | Lua Mod 生命周期、Unreal/脚本绑定，生成 Lua 插件 DLL。 |
| `ue4ssl-lua-support` | LuaRaw VM 和 LuaMadeSimple 原生依赖。 |
| `ue4ssl-javascript` | JavaScript Mod 生命周期、Unreal/脚本绑定，生成 JS 插件 DLL。 |
| `ue4ssl-javascript-support` | QuickJS 原生依赖。 |
| `ue4ssl-proxy` | 原 DLL 导出转发与核心加载。 |
| `ue4ssl-build` | 公共 SDK include 能力、核心 import library 链接、ABI 头同步及原生 Mod 编译。 |
| `xtask` | 核心优先的构建顺序、统一 Mod 发现与选择、生成 Mod workspace、打包、安装及 Proxy 命令。 |

```text
scripts/
  ue4ssl-lua/                 Lua 引擎插件
  ue4ssl-lua-support/         LuaRaw 与 LuaMadeSimple
  ue4ssl-javascript/          JavaScript 引擎插件
  ue4ssl-javascript-support/  QuickJS
```

后续引擎及专用依赖统一放入 `scripts/`。接入构建与打包时，在根 workspace 登记 Cargo 包，并在 `crates/xtask/src/native.rs` 的 `SCRIPT_ENGINE_ARTIFACTS` 登记插件产物。此次源码目录调整不改变包名、构建命令或部署目录 `mods/UE4SSL.Lua` / `mods/UE4SSL.JavaScript`。

`Mods/` 支持两种构建后端：`ue4ssl-paksync` 等自带 Cargo workspace 包的 Mod，以及 `CPP_MeowChat/native` 等通过生成 Cargo workspace 构建的原生源码 Mod。两种后端使用同一套列表、选择与打包规则。

各构建脚本依赖 `ue4ssl-build` 复用工具，不通过相对路径包含公共 Rust 源码。SDK include 辅助函数区分平台、Unreal/核心 SDK、扫描器与 ObjectSearcher；Unreal SDK 消费者获得两组生成 ABI 头，扫描器和 ObjectSearcher 依赖显式添加。平台服务不编译核心或扫描适配；核心 DLL 负责组装并 whole-archive 链接各自归属的原生库。

冻结基线文档（`migration-baseline.md`、`script-api-baseline.md` 及 `known-defects.md` 中的基线证据）、`docs/baseline-*` 和 `docs/migration-validation.json` 是历史记录，其中旧源码路径与哈希有意保留，不改写成当前目录，以免伪造基线来源。

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

在 macOS 上交叉编译核心 Windows DLL：

```sh
rustup target add x86_64-pc-windows-msvc
cargo ue4ssl-build --target x86_64-pc-windows-msvc --core-only
```

在 macOS 上交叉编译核心 DLL、Lua/JavaScript 脚本引擎与全部发现的 Mod：

```sh
cargo ue4ssl-build --target x86_64-pc-windows-msvc
```

Mod 从 `Mods/` 的直接子目录发现：

- 自带 `Cargo.toml` 的 Mod 必须是根 workspace 成员，且具有 `cdylib` 目标。包名与 DLL 目标名从 Cargo 元数据读取，不需要在 `xtask` 中注册 artifact。
- 不带 `Cargo.toml` 的 Mod 从 `native/cpp/` 编译，通过生成的 workspace 构建，无须加入根 workspace。
- 可选的 `mod.json` 声明逻辑/部署名称 `name`（默认使用目录名）和 `resources`。约定的 `resources/` 内容直接覆盖到 Mod 部署目录；其他资源根保留末级目录名。PakSync 声明 `{"name":"UE4SSL.PakSync","resources":["config"]}`，对应 `mods/UE4SSL.PakSync/config/paksync.ini`。

列出两种后端、只构建用户 Mod，或按逻辑名称选择 Mod（大小写不敏感，`--mod` 可重复）：

```sh
cargo ue4ssl-mods list
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mods-only
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mod CPP_MeowChat
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mod UE4SSL.PakSync
```

默认构建包含核心、脚本引擎和两种 Mod 后端。`--mods-only` 与 `--mod` 跳过脚本引擎，但仍先构建核心，确保 Mod 链接时的 `UE4SSL.dll.lib` 已更新。`--core-only` 排除所有引擎和 Mod。不要用一次无顺序保证的 `cargo build -p ...` 同时构建核心及依赖它的插件；公共构建辅助会跟踪核心 import library 的变化，触发依赖方重新链接。

这条交叉编译路线保留 Windows/MSVC ABI，产物仍是用于 Windows 游戏环境验证的 DLL。需要配置 Windows SDK/MSVC CRT 来源，例如 `cargo-xwin`/`xwin`，或在环境中提供等价的 `clang-cl`、`lld-link`、`llvm-lib`、Windows SDK、UCRT 和 MSVC CRT 路径。

第一阶段先保留 Unreal C++ ABI 边界、Hook trampoline、依赖 C++ 类布局和调用约定的代码。后续更适合迁移到 Rust 的部分是构建 glue、产物/路径处理、简单 Win32 FFI helper，以及不参与 Unreal C++ ABI 的元数据逻辑。

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

如果传入 `--target`，打包输出目录为：

```text
target/package/<target-triple>/<debug|release>/ue4ss/
```

安装到游戏目录：

```powershell
cargo ue4ssl-install --destination "<Game>/Binaries/Win64/ue4ss"
cargo ue4ssl-install --profile release --destination "<Game>/Binaries/Win64/ue4ss"
```

package 步骤复制核心 DLL、脚本引擎、两种 Mod 后端、PDB、启用标记和配置的资源。`--mods-only` 只暂存全部用户 Mod；`--mod <name>` 只暂存选定的 Mod，不包含核心或脚本引擎。除非使用 `--no-build`，仍会先构建核心作为链接前置条件。

```powershell
cargo ue4ssl-package --mod UE4SSL.PakSync
cargo ue4ssl-install --mod UE4SSL.PakSync --destination "<Game>/Binaries/Win64/ue4ss"
```

PakSync 部署脚本还会复制核心，并保留游戏专用工作目录的处理：

```powershell
.\Mods\ue4ssl-paksync\deploy.ps1 -Profile Release -Destination "<Game>/Binaries/Win64/ue4ss" -PreserveConfig
```

脚本从仓库根目录调用 `xtask package --mod UE4SSL.PakSync`，再部署暂存文件。`-SkipBuild` 传入 `--no-build`（仍执行打包）；`-Target <triple>` 选择交叉编译产物。`-NoPrune` 保留脚本原本会清理的可选产物和脚本引擎目录；`-PreserveConfig` 保留现有 `paksync.ini`。路径解析不依赖调用者的工作目录。

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
