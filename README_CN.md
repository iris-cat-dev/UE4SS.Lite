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

`crates/` 下恰好有 16 个 Cargo 包。原生源码放在实际负责构建它的模块内：

| 模块 | 职责 |
|---|---|
| `ue4ssl-abi` | 跨语言契约、C/C++ ABI 头生成。 |
| `ue4ssl-runtime` | Mod 发现、状态、事件队列和生命周期执行。 |
| `ue4ssl-platform` | Rust 输入、日志和文件服务；编译对应的 `native/Input`、`native/DynamicOutput` 薄适配，基础公共头位于 `native/Common`。 |
| `ue4ssl-dll` | 核心 `UE4SSL.dll` 组装、启动和关闭；拥有并编译 `native/UE4SSL` 下的核心 C++ 边界代码。 |
| `ue4ssl-unreal-support` | `vendor/Unreal` 下的引擎对象、布局、版本及原生调用适配；这是普通 vendor 源码树，不是 Git 子模块。 |
| `ue4ssl-hook` | Detour、IAT、指令地址辅助及对应 C++ 兼容头。 |
| `patternsleuth-scanner` | 字节模式与交叉引用扫描算法。 |
| `patternsleuth` | 映像、进程分析及地址解析器。 |
| `patternsleuth-bind` | 扫描 C ABI 与 `native/SinglePassSigScanner` 适配；不负责组装 runtime 或 Hook 库。 |
| `ue4ssl-lua` | Lua Mod 生命周期、Unreal/脚本绑定，生成 Lua 插件 DLL。 |
| `ue4ssl-lua-support` | LuaRaw VM 和 LuaMadeSimple 原生依赖。 |
| `ue4ssl-javascript` | JavaScript Mod 生命周期、Unreal/脚本绑定，生成 JS 插件 DLL。 |
| `ue4ssl-javascript-support` | QuickJS 原生依赖。 |
| `ue4ssl-proxy` | 原 DLL 导出转发与核心加载。 |
| `ue4ssl-build` | 公共构建辅助、ABI 头同步及原生 Mod 编译。 |
| `xtask` | 构建顺序、生成 Mod workspace、打包、安装及 Proxy 命令。 |

`Mods/*/native` 是通过生成的 Cargo workspace 构建的原生 Mod。各构建脚本通过依赖 `ue4ssl-build` 复用工具，不再通过相对路径包含公共 Rust 源码。平台服务不编译核心或扫描适配；核心 DLL 负责组装并 whole-archive 链接各自归属的原生库。

`docs/baseline-*` 和 `docs/migration-validation.json` 是历史快照，其中旧源码路径与哈希有意保留，不改写成当前目录，以免伪造基线来源。

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

在 macOS 上交叉编译核心 DLL、Lua/JavaScript 脚本引擎 DLL 和自动发现的原生 C++ Mod：

```sh
cargo ue4ssl-build --target x86_64-pc-windows-msvc
```

原生 C++ Mod 会从 `Mods/<ModName>/native/cpp/` 自动发现。新增 Mod 不需要修改根 workspace，也不需要注册 `xtask` artifact。只构建原生 Mod 或指定某个原生 Mod：

```sh
cargo ue4ssl-mods list
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mods-only
cargo ue4ssl-build --target x86_64-pc-windows-msvc --mod CPP_MeowChat
```

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

package 步骤会复制核心 DLL、脚本引擎 DLL、自动发现的原生 Mod DLL、PDB 和配置的资源目录。使用 `--mod <name>` 可只暂存一个原生 Mod，不包含核心和脚本引擎。

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
