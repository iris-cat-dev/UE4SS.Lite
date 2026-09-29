# 迁移前行为基线（步骤 1–5）

## 证据边界

- 冻结源码提交：`fff1f21dce7f671c8736a7907ff41ccb9c2faf32`。本文引用的行号均指该提交，不是本轮并发修改后的工作区；用 `git show <提交>:<路径>` 取回原文。
- 采集日期：2026-09-22。先前只读调查 `ScriptBoundary`、`UnrealBoundary` 提供风险定位；本文进一步读取上述提交的脚本注册、加载器、核心路径、生命周期及 ABI 源码。
- **提交基线不是用户会话开始时的工作区快照。** 未建立会话开始时快照，不能追溯所有未提交变更的作者/发生时间。`baseline-sources.json` 同时记录冻结提交 SHA256 与采集时工作区 SHA256；差异不自动归因于迁移，不把并发新实现倒填成旧行为。
- 本轮只作源码取证、现有产物复制、SHA256 和 PE 结构/导出解析。没有 build/test/formatter/linter；没有 Windows 执行、游戏启动、hook 或卸载运行证据。本文的游戏场景均为 **unverified**。
- 导航：[逐项脚本 API](script-api-baseline.md)、[FFI 契约](ffi-contract.md)、[Windows 验证矩阵](windows-validation-matrix.md)、[缺陷及迁移差异](known-defects.md)、[产物完整清单](baseline-artifacts.json)。

## DLL / Mod 入口及产物

| 边界 | 冻结契约 | 错误/有效期与取证位置 |
|---|---|---|
| 核心 | `UE4SSL.dll`；C++ 程序壳初始化 Unreal，然后启动/等待事件循环。构造期路径、配置、Mod 设置与 `init()` 是不同阶段。 | `crates/ue4ssl-cpp-support/vendor/UE4SSL/src/UE4SSProgram.cpp:337–544`；构造/初始化异常进入错误对象，不能把模块载入成功视作 Unreal 就绪。 |
| 旧 C++ Mod | `extern "C" start_mod() -> CppUserModBase*`、`uninstall_mod(CppUserModBase*)`。名字为 C 链接，返回对象仍是 MSVC C++ 虚表/字符串 ABI。 | `crates/ue4ssl-cpp-support/vendor/UE4SSL/include/Mod/CppUserModBase.hpp:13–68`：相同 C Runtime、Debug/Release 兼容限制仍存在。原分配 DLL 执行 delete。 |
| Rust Mod v1 | `ue4ssl_mod_start_v1(const RustModStartContext*) -> opaque instance`；`ue4ssl_mod_uninstall_v1(instance)`；可选 `ue4ssl_mod_on_unreal_init_v1`、`on_ui_init_v1`、`on_program_start_v1`、`on_update_v1`、`on_dll_load_v1`（均带此前缀）。 | `crates/ue4ssl-runtime/src/core/cpp_mod.rs:14–24,85–230`。只要出现任一 Rust start/uninstall 符号就选择 Rust ABI；缺另一半拒绝安装，不回退旧 C++。不得对同一实例同时调用两套入口。 |
| Mod DLL 搜索 | 默认 `<mod_path>/main.dll`，内建 Mod 可提供 DLL 文件名；`AddDllDirectory(mod_path)`；`LoadLibraryExW` 使用 DLL_LOAD_DIR 与 DEFAULT_DIRS。 | 缺目录、LoadLibrary 失败（保存 GetLastError）、生命周期符号缺失为不同失败；start 返回 null 导致 started=false。`cpp_mod.rs:85–219`。 |
| Lua 引擎 | 旧 C++ start/uninstall；`get_lua_state_by_mod_name(const char*) -> lua_State*`；`execute_lua_in_mod(const char*, const char*, char*) -> const char*`。 | `crates/ue4ssl-lua/native/include/LuaLibrary.hpp:39–97` 与 `native/cpp/LuaLibrary.cpp`；外借 VM 不允许调用方关闭，Mod 重启/卸载即失效；输出缓冲 ABI 不带容量，不能擅自推断安全长度。 |
| JS 引擎 | 旧 C++ start/uninstall；`eval_js_code(const char* code, size_t code_len, const char* filename) -> bool`。 | `crates/ue4ssl-javascript/native/cpp/dllmain.cpp:201–244`；仅事件循环线程调用，false 是失败；空闲无本地 JS Mod 时 VM 仍允许外部 eval。 |
| Proxy | 原 DLL 的导出跳板与核心加载是不同能力；名称/ordinal/forwarder 必须逐项比对，不能只看 DLL 文件名。 | 本轮 Proxy 改变 attach/worker 时序，见差异 M-PROXY；并非冻结基线原本已具备的保证。 |

### 已保存的现有产物

`target/migration-baseline/<提交>/<debug或release>/<SHA256>/<原文件名>` 保存 8 个 DLL/PDB 的只读、内容寻址副本；复制后重新读取验证 SHA256 一致。`baseline-artifacts.json` 给出全部原路径、副本路径、长度、mtime、hash、PE machine、时间戳及逐项导出（含 ordinal/RVA/forwarder）。**只读权限不是防管理员篡改机制；SHA256 是内容身份，迁移后比较时必须重算。**

debug/release 各存在 `UE4SSL.dll`、`UE4SSL.pdb`、`ue4ssl_mod_meowchat.dll`、对应 PDB。4 个 DLL 均为 PE x64；核心各 3856 个非空导出槽，MeowChat 各 2 个。未见 `UE4SSL.Lua.dll` / `UE4SSL.JavaScript.dll` 同级产物；未把静态库/源码当作这两个 DLL。现有文件没有已确认的构建提交/工具链 provenance：**build-candidate-unverified，不是 game-verified，也不能证明由冻结 HEAD 构建。**

## 路径与配置优先级

来源：`crates/ue4ssl-runtime/src/core/paths.rs:18–103`；C++ 应用快照/设置见 `UE4SSProgram.cpp:337–435,546–616`。

1. `root_directory = UE4SSL 模块文件所在目录`；`game_executable_directory = 游戏 EXE 所在目录`。默认 working/root 相同，Mods 为 root/Mods，legacy root 为游戏 EXE 目录。
2. 用 `game_executable_directory.ancestors().nth(3).file_name()` 取得候选游戏特定目录名；在 root 的直接子目录中找到同名项，则 working、Mods、settings、log、object-dump 切换到该目录。不能把它泛化成任意寻找父目录或固定项目名。
3. 首选 `working/UE4SS-settings.ini`。仅当首选不存在且 legacy settings 存在时回退 legacy；Mods 同理，仅首选不存在才用 legacy/Mods。不存在与空目录不同。
4. 读取 settings 后应用 `Overrides.ModsFolderPath`：相对路径相对于 **working**，绝对路径保持。不是相对于当前进程 CWD 或 Mods 本身。
5. `MemberVariableLayout.ini` 使用已有 Rust INI parser 再由生成 MacroSetter 应用；`VTableLayout.ini` 按继承段长度计算字节 offset。UE4.25 前后 FProperty 的继承基类不同。版本默认表仅插入缺项，保留先应用 override；已缓存 offset 后运行中改文件不承诺即时生效。
6. 扫描先取得版本/FName ToString/StaticConstructObject/Tick 等第一阶段信息，选择版本容器，然后应用 GMalloc/GUObjectArray 等第二阶段。Rust PatternSleuth 主模块扫描与 legacy override 多模块路由不等价；不能删后者而宣称兼容。

## Mod 发现和启动顺序

- `core/mods.rs` 先按 `core/contract.rs` 的内建清单检查 working 下 JavaScript、Lua、DRG DLL；随后枚举 Mods 的直接子目录，忽略大小写为 `shared` 的目录，只发现有 `main.dll` 的普通 native Mod。普通目录枚举顺序没有排序保证。
- `host.rs:140–154` 明确启动 **JS → Lua → 普通发现 Mod**；DRG 在普通发现阶段，不能把内建发现清单误作所有实例的完整生命周期顺序。
- Lua 引擎发现脚本 Mod、建立各自 Lua VM；等待 program-start 与 Unreal-ready 后启动。Lua 的 main/hook/async coroutine 共享同一 Mod VM；不同 Mod 不是同一 VM。
- Lua loader 搜索 `lua/main.lua`；require 包含 Mod lua 目录、shared/module.lua、shared/module/module.lua，并保留 DLL cpath、UTF-8 路径处理。目录过滤、大小写与精确顺序见 `LuaMod.cpp:1144–1304`、Lua `dllmain.cpp:13–125`。
- JS 寻找 `js/main.js`，多个本地 JS Mod 使用现有单 runtime/context。module loader 按 importer 目录拼接、补 `.js`、lexically_normal，交 QuickJS module compile-only；不是 Node 模块解析。`JSPropertyUtils.cpp:799–932`。
- `JSMod::load_scripts` 返回“找到脚本”，不等价“每个脚本执行成功”（`JSMod.cpp:240–294`）。迁移不能把不同的成功概念合并。

## 初始化、更新、重载、停止、退出

| 阶段 | 基线可观察行为 | 必须保留的区别 |
|---|---|---|
| init | setup_unreal → native Mod on_unreal_init → Unreal property setup/内存限制 → 启事件循环并 join | Unreal 游戏线程 ID 来自第一次 EngineTick，不来自该初始化线程或事件线程。 |
| event loop | 先 on_program_start 并设置 started；循环处理事件、输入、Mod update；暂停/引擎关闭时不进行正常分发 | 旧循环中暂停分支 `continue`；不能误报旧代码已有阻塞等待机制。`UE4SSProgram.cpp:887–1018`。 |
| JS tick | keybind → bind activation → pending hook → LoadMap → timers → fetch → download → QuickJS jobs | jobs 有 256 个/6ms budget；guard/SEH/circuit-breaker 会影响后续处理。`JSMod.cpp:722–874`。 |
| Lua async | 每 Mod worker，约 5ms 检查；LoopAsync 回调返回 true 才终止 | 与 EngineTick/ProcessEvent 上的游戏线程时间/帧 action 分开。`LuaMod.cpp:6588–6660`。 |
| 全部重载 | 重置 DLL dispatch cache → 设置 pause → uninstall Mods → 清 Lua/JS keybind → 调整 pause → setup Mods → JS/Lua/普通 Mod start → 根据原状态重发 Unreal/program-start | 精确状态分支由 `host.rs:58–138` 决定；不重复 UI 初始通知；重载不是新进程启动。 |
| Lua 单 Mod 重载 | RestartCurrentMod/RestartMod 请求经事件队列延后卸载/重建 | callback 返回后才能拆当前 VM；模块状态重建，显式 shared variable 是另一个寿命域。`LuaCompat.cpp:36–160`。 |
| JS stop | 取消/释放 hook/keybind/timer 等，通知 worker stop，join，再清理结果/Promise 和 VM | 在途 WinHTTP 阻塞可能延长 stop；未建立固定关闭上限。默认 deferred hooks 不能修改原调用参数；force_sync 才在原调用栈执行。 |
| program destructor | 将 processing、started 设 false，关闭默认输出设备 | 冻结析构函数本身不是完整的有序 Mod drain 证明；不得宣称已保证所有 listener/回调静默。`UE4SSProgram.cpp:470–481`。 |

## 脚本返回/错误分类（逐 API 见附录）

- Lua C function 的整数返回是 **Lua 栈返回值个数**，不是自动转换成 Lua integer/bool；真实值看 `set_*`/construct/pusher。这使部分 FName/ThreadId 等比较实现存在待验证风险。Lua 参数错误通过 `throw_error`→`luaL_error`，与 C++ exception、Windows SEH 不同。
- JS arity 是 `function.length` 注册元数据，不是参数验证规则。真正检查来自函数的 argc/JS_To* 和分支；新增参数或可选选项以该实现为准。
- JS 区分 TypeError（参数/转换）、InternalError（host/runtime/引擎服务）、RangeError/普通 Error（各实现）、`JS_EXCEPTION`（已挂起 VM 异常）、普通 sentinel（null/false/undefined）以及 Promise resolve/reject。不得把它们统一为 throw。
- `readFile` 缺文件/打开失败/捕获异常→null；`writeFile` 创建父目录并截断、按字符串长度写入，打开/捕获失败→false。当前写后没有验证所有 stream 状态，不能声称 true 证明所有字节持久化。路径 getter 用 `/`；getGameDirectory 是游戏 EXE 目录上两层。
- fetchSync 返回 `{ok,status,body}`，网络失败抛错；fetch 还提供 Response.text/json、SSE body.getReader/read、TextDecoder。download 普通失败通常 resolve `{ok,status,bytesWritten,path,error}`，不是普遍 reject。
- JS timeout 负值夹为 0，interval 最少 10ms；Lua frame delay 不能替换为 ProcessEvent 次数。Lite dump/generator API 的兼容告警不等于实际支持生成器。

## 兼容判定

对同一游戏 EXE/hash、配置/hash、扫描 override/hash、Mod/hash、操作序列和输入时序，对照冻结候选与迁移候选的返回值、错误类别、回调顺序/线程、外部副作用、释放时点。源码风险不要求作为正确行为保留；但修复必须进入 `known-defects.md` 的迁移差异区，不能用“语言迁移”掩盖 API/时序变化。未覆盖的游戏/布局/packing 组合一律 unverified。
