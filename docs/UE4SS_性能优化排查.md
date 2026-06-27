# UE4SS.Lite 性能优化排查

本文面向 UE4SS.Lite 注入后的运行时卡顿排查，重点覆盖蓝图卡顿、JS JSON 序列化卡顿、Actor 生成卡顿，以及哪些 Hook 或模块可以关闭来做性能基线测试。

当前分析基于仓库代码：

- 主程序入口：`crates/ue4ssl-cpp-support/vendor/UE4SSL`
- Unreal Hook 框架：`crates/ue4ssl-unreal-support/vendor/Unreal`
- 运行时配置：`crates/ue4ssl-runtime/src/core/settings.rs`
- JS 模块：`crates/ue4ssl-javascript`
- Lua 模块：`crates/ue4ssl-lua`

## 先给结论

最可能造成运行时卡顿的路径：

1. `HookProcessEvent`
   - 命中 `UObject::ProcessEvent`，这是 UE 蓝图、事件、RPC 等非常高频的路径。
   - JS `RegisterProcessEventWatch`、JS 游戏线程 dispatcher、UMG dispatcher、Lua `ExecuteInGameThread` 都可能依赖它。
2. `HookProcessInternal` / `HookProcessLocalScriptFunction`
   - 命中蓝图脚本执行内部路径。
   - 如果不需要 Lua/UE4SS 的蓝图脚本 hook，优先关闭。
3. JS hook 参数快照里的 `FStructProperty -> JSON`
   - 高频函数带大 struct 参数时，会在 hook 路径做 JSON 序列化，容易出现明显帧卡。
4. `StaticConstructObject`
   - 对象构造路径。Actor 生成会经过这里。
   - 当前没有配置项能完全关闭它，`UnrealInitializer::PostInitialize()` 会注册对象搜索缓存回调。
5. Lua `on_program_start()` 注册的全局回调
   - 包括 `StaticConstructObject`、`BeginPlay`、`EndPlay`、console exec、local player exec 等。
   - 如果不用 Lua，最干净的性能方案是不要加载 `UE4SSL.Lua.dll`。

优先级建议：

1. 先关 `HookProcessInternal` 和 `HookProcessLocalScriptFunction`。
2. 如果不用 JS/Lua 的 ProcessEvent 功能，再关 `HookProcessEvent`。
3. 如果不用 BeginPlay hook，关 `HookBeginPlay`。
4. 保持 `HookEngineTick=0`、`HookAActorTick=0`。
5. 对 Actor 生成卡顿，先移除 Lua/JS 模块做基线，再考虑代码层面给 `StaticConstructObject` 增加开关。

## 注入后主流程

DLL 注入后的入口：

```text
DllMain
  -> RC::Compat::Bootstrap::dll_process_attached
  -> process_initialized_impl
  -> new UE4SSProgram(...)
  -> thread_dll_start
  -> program->init()
```

关键文件：

- `crates/ue4ssl-cpp-support/vendor/UE4SSL/src/main_ue4ss_rewritten.cpp`
- `crates/ue4ssl-cpp-support/vendor/UE4SSL/src/Compat/BootstrapShim.cpp`
- `crates/ue4ssl-cpp-support/vendor/UE4SSL/src/UE4SSProgram.cpp`

构造 `UE4SSProgram` 时会先做：

- 读取 `UE4SS-settings.ini`
- 初始化 CrashDumper
- 初始化日志/console
- 安装 `LoadLibrary*` IAT hook
- 扫描并启动内置和外部 mod

`program->init()` 后会：

- 初始化 Unreal 地址和对象系统
- 根据配置决定哪些 Unreal Hook 可用
- 通知 C++/JS/Lua mod 的 Unreal 生命周期
- 启动 UE4SS 事件循环线程

## 注入后直接安装的系统 Hook

`UE4SSProgram` 构造阶段默认安装这些 `kernel32.dll` IAT hook：

| Hook | 文件 | 目的 | 热路径风险 |
| --- | --- | --- | --- |
| `LoadLibraryA` | `UE4SSProgram.cpp` | DLL load 通知 mod | 低 |
| `LoadLibraryExA` | `UE4SSProgram.cpp` | DLL load 通知 mod | 低 |
| `LoadLibraryW` | `UE4SSProgram.cpp` | DLL load 通知 mod | 低 |
| `LoadLibraryExW` | `UE4SSProgram.cpp` | DLL load 通知 mod | 低 |
| `SetUnhandledExceptionFilter` | `CrashDumper.cpp` | 阻止游戏覆盖 UE4SS crash dump handler | 低 |

`LoadLibrary*` 不属于蓝图/Actor 热路径，通常不用作为首要优化目标。

`SetUnhandledExceptionFilter` 只在 `CrashDump.EnableDumping=1` 时启用。它也不是运行时帧卡热点。

## 默认配置和 Hook 开关

默认配置来源：`crates/ue4ssl-runtime/src/core/settings.rs`。

需要重点关注：

```ini
[General]
UseCache = 1
bUseUObjectArrayCache = true
EnableHotReloadSystem = 1

[Hooks]
HookProcessInternal = 1
HookProcessLocalScriptFunction = 1
HookLoadMap = 1
HookInitGameState = 0
HookCallFunctionByNameWithArguments = 1
HookBeginPlay = 1
HookLocalPlayerExec = 1
HookEngineTick = 0
HookAActorTick = 0
HookProcessEvent = 1
HookUFunctionBind = 1
```

实际传给 UnrealInitializer 的位置：

```cpp
config.bHookProcessInternal = settings_manager.Hooks.HookProcessInternal;
config.bHookProcessLocalScriptFunction = settings_manager.Hooks.HookProcessLocalScriptFunction;
config.bHookLoadMap = settings_manager.Hooks.HookLoadMap;
config.bHookInitGameState = settings_manager.Hooks.HookInitGameState;
config.bHookCallFunctionByNameWithArguments = settings_manager.Hooks.HookCallFunctionByNameWithArguments;
config.bHookBeginPlay = settings_manager.Hooks.HookBeginPlay;
config.bHookLocalPlayerExec = settings_manager.Hooks.HookLocalPlayerExec;
config.bHookEngineTick = settings_manager.Hooks.HookEngineTick;
config.bHookAActorTick = settings_manager.Hooks.HookAActorTick;
config.bHookUObjectProcessEvent = settings_manager.Hooks.HookProcessEvent;
config.bHookUFunctionBind = settings_manager.Hooks.HookUFunctionBind;
```

文件：`crates/ue4ssl-cpp-support/vendor/UE4SSL/src/UE4SSProgram.cpp`

## Hook 热点表

| 开关或 Hook | 默认 | 命中路径 | 常见用途 | 性能风险 | 可以关闭的条件 |
| --- | ---: | --- | --- | --- | --- |
| `HookProcessEvent` | 开 | `UObject::ProcessEvent` | JS/Lua 事件、RPC、UMG、ProcessEventWatch | 很高 | 不用 JS/Lua 的 ProcessEvent 相关功能 |
| `HookProcessInternal` | 开 | 蓝图脚本执行 | Lua/UE4SS script hook | 高 | 不用蓝图脚本 hook |
| `HookProcessLocalScriptFunction` | 开 | UE4.22+ 蓝图本地脚本函数 | Lua/UE4SS script hook | 高 | 不用蓝图脚本 hook |
| `HookBeginPlay` | 开 | `AActor::BeginPlay` | BeginPlay hook | 中到高 | 不用 BeginPlay hook |
| `HookAActorTick` | 关 | `AActor::Tick` | Actor Tick hook | 极高 | 保持关闭 |
| `HookEngineTick` | 关 | Engine Tick | 延迟任务、dispatcher | 高 | 保持关闭，除非明确需要 |
| `HookUFunctionBind` | 开 | UFunction bind | JS `RegisterBindHook` | 中 | 不用 bind hook |
| `HookCallFunctionByNameWithArguments` | 开 | console/exec 调用 | Lua console command | 中 | 不用相关 API |
| `HookLocalPlayerExec` | 开 | local player exec | Lua console/input command | 中 | 不用相关 API |
| `HookLoadMap` | 开 | LoadMap | 地图切换 hook | 低到中 | 不用 LoadMap hook |
| `bUseUObjectArrayCache` | 开 | UObject 搜索缓存 | 快速 object search | 中 | 可先关掉做 A/B，但不能完全关闭 StaticConstructObject hook |

## 蓝图卡顿排查

蓝图卡顿优先检查：

1. 是否开启了 `HookProcessEvent`
2. 是否开启了 `HookProcessInternal`
3. 是否开启了 `HookProcessLocalScriptFunction`
4. JS/Lua 是否注册了全局回调
5. 脚本回调里是否做了阻塞操作、遍历对象、字符串/JSON 序列化、同步文件 IO

`ProcessEvent` 的成本不是只来自 detour 本身，还来自每次调用都要经过 UE4SS 回调分发：

```text
ProcessEvent detour
  -> 构造 callback iteration data
  -> 遍历 pre callbacks
  -> 调原始 ProcessEvent
  -> 遍历 post callbacks
```

如果有多个 JS/Lua callback，每次 `ProcessEvent` 都会支付过滤和分发成本。

### 建议

如果只想验证蓝图卡顿是否来自 UE4SS hook，先用最小配置：

```ini
[Hooks]
HookProcessInternal = 0
HookProcessLocalScriptFunction = 0
HookProcessEvent = 0
HookBeginPlay = 0
HookUFunctionBind = 0
HookEngineTick = 0
HookAActorTick = 0
```

如果关闭后卡顿明显消失，再逐个打开需要的功能。

## JS JSON 序列化卡顿排查

重点文件：`crates/ue4ssl-javascript/native/cpp/JSHook.cpp`。

相关路径：

```text
hook callback off event-loop thread
  -> snapshot_hook_params(...)
  -> 遍历 UFunction 参数
  -> 遇到 FStructProperty
  -> seh_snapshot_property_json(...)
  -> serialize_property_json(...)
  -> utf8_to_wide(...)
  -> pending callback queue
  -> JS event loop 后续 JS_ParseJSON
```

也就是说，大 struct 的 JSON 序列化不是完全异步的。真正从 UE 参数内存读取并序列化 JSON 的部分已经发生在 hook 所在线程，很多情况下就是游戏线程。

典型卡顿场景：

- 高频蓝图事件被 JS hook
- 参数包含大 struct、数组、嵌套 struct、字符串列表
- 每帧触发多次
- 使用 `RegisterProcessEventWatch` 过滤大量 `ProcessEvent`

### 建议

脚本使用层面：

- 避免 hook 高频函数。
- 避免 hook 带大 struct 参数的函数。
- 避免 `RegisterProcessEventWatch` 做全局监控。
- 回调里只记录必要字段，不要把整个参数对象转 JSON。

代码优化层面：

- 给 JS hook 增加配置：是否允许 struct JSON 快照。
- 给 JSON 快照增加最大字节数、最大字段数、最大递归深度。
- 对 `FStructProperty` 改成懒加载对象或只传指针/引用。
- 支持参数白名单，只序列化脚本声明需要的字段。

建议新增配置示例：

```ini
[JavaScript]
SerializeStructParamsAsJson = false
MaxStructJsonBytes = 65536
MaxStructJsonDepth = 3
```

当前仓库还没有这个配置，需要代码改造。

## Actor 生成卡顿排查

Actor 生成路径最相关的是 `StaticConstructObject`。

当前代码里 `UnrealInitializer::PostInitialize()` 会注册 `StaticConstructObject` post callback：

```text
StaticConstructObject
  -> UE4SS post callback
  -> 如果构造对象是 AActor
  -> lock ObjectSearcherPool<AActor, AnySuperStruct>::PoolMutex
  -> Add(object->GetObjectItem())
```

文件：`crates/ue4ssl-unreal-support/vendor/Unreal/src/UnrealInitializer.cpp`

这意味着 Actor 生成会额外经过 UE4SS detour 和对象搜索缓存更新。

`bUseUObjectArrayCache=false` 可以减少 UObject 搜索缓存初始化和 GUObjectArray listener，但当前实现里不能完全取消 `StaticConstructObject` post callback。要彻底关，需要代码层面增加开关。

Lua 也会在 `on_program_start()` 注册 `StaticConstructObject` post callback，用于 `NotifyOnNewObject` 类似功能：

文件：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp`

即使没有注册 Lua callback，也会进入这个 lambda 并做基础检查。Actor 大量生成时建议先移除 Lua 模块做基线。

### Actor 生成优化建议

配置层面：

```ini
[General]
bUseUObjectArrayCache = false

[Hooks]
HookBeginPlay = 0
HookAActorTick = 0
HookProcessEvent = 0
HookProcessInternal = 0
HookProcessLocalScriptFunction = 0
```

模块层面：

- 不用 Lua 时，不部署或移出 `UE4SSL.Lua.dll`。
- 不用 JS 时，不部署或移出 `UE4SSL.JavaScript.dll`。
- 不要使用 Lua `NotifyOnNewObject` 类功能监控大量 Actor。
- 不要在 Actor 生成期间执行全局 object search。

代码层面：

- 给 `StaticConstructObject` 对象搜索缓存回调增加配置开关。
- 让 `ObjectSearcherPool<AActor, AnySuperStruct>` 更新只在确实使用 object cache API 时启用。
- Lua `StaticConstructObject` callback 应该只在注册了 `NotifyOnNewObject` 后再安装。
- 若没有任何 callback，允许卸载或跳过 detour。

建议新增配置示例：

```ini
[Hooks]
HookStaticConstructObjectObjectCache = 0

[ObjectSearch]
UseNativeStaticFindObjectFast = 0
UseNativeClassEnumeration = 0
CompareNativeSearchResults = 0

[Lua]
InstallNotifyOnNewObjectHookOnlyWhenUsed = true
```

当前仓库还没有这些配置，需要代码改造。

## 内置 JS/Lua 模块的影响

内置 mod 发现逻辑在 `crates/ue4ssl-runtime/src/core/mods.rs`。

只要工作目录存在这些 DLL，就会作为内置 mod 被发现：

- `UE4SSL.JavaScript.dll`
- `UE4SSL.Lua.dll`
- `UE4SSL.DRG.dll`

启动顺序在 `crates/ue4ssl-runtime/src/host.rs`：

```text
UE4SSL.JavaScript
UE4SSL.Lua
discovered mods
```

### JS 模块

JS `load_scripts()` 如果发现脚本，会执行脚本，然后注册：

- game thread dispatcher
- UMG dispatcher

这两个 dispatcher 都使用 `ProcessEvent` pre callback。

如果没有 JS 脚本，`load_scripts()` 会提前返回，默认 dispatcher 不会安装。

### Lua 模块

Lua 在 `on_program_start()` 会注册多类全局 hook，包括：

- `LoadMap`
- `BeginPlay`
- `EndPlay`
- `StaticConstructObject`
- `ULocalPlayerExec`
- `CallFunctionByNameWithArguments`
- `ProcessConsoleExec`

所以不用 Lua 时，最干净的性能优化是不要加载 Lua 内置 DLL。

## 推荐 A/B 配置

### 性能基线配置

用于验证 UE4SS hook 是否是卡顿来源。

```ini
[General]
EnableHotReloadSystem = 0
UseCache = 1
bUseUObjectArrayCache = false

[Debug]
ConsoleEnabled = 0
GuiConsoleEnabled = 0

[CrashDump]
EnableDumping = 0

[Hooks]
HookProcessInternal = 0
HookProcessLocalScriptFunction = 0
HookLoadMap = 0
HookInitGameState = 0
HookCallFunctionByNameWithArguments = 0
HookBeginPlay = 0
HookLocalPlayerExec = 0
HookEngineTick = 0
HookAActorTick = 0
HookProcessEvent = 0
HookUFunctionBind = 0
```

注意：这会禁用大量脚本和 hook 功能，只适合做性能基线。

### 保守运行配置

用于保留常见功能，但减少蓝图热路径开销。

```ini
[General]
UseCache = 1
bUseUObjectArrayCache = true
EnableHotReloadSystem = 0

[Hooks]
HookProcessInternal = 0
HookProcessLocalScriptFunction = 0
HookLoadMap = 1
HookInitGameState = 0
HookCallFunctionByNameWithArguments = 0
HookBeginPlay = 0
HookLocalPlayerExec = 0
HookEngineTick = 0
HookAActorTick = 0
HookProcessEvent = 1
HookUFunctionBind = 0
```

仅在确实需要 JS/Lua 的 `ProcessEvent` 相关功能时保留 `HookProcessEvent=1`。

### JS 排查配置

如果怀疑 JSON 序列化卡顿：

```ini
[Hooks]
HookProcessEvent = 1
HookUFunctionBind = 0
HookProcessInternal = 0
HookProcessLocalScriptFunction = 0
HookBeginPlay = 0
HookAActorTick = 0
HookEngineTick = 0
```

同时在 JS 脚本里临时移除：

- `RegisterProcessEventWatch`
- 高频 `RegisterHook`
- 带大 struct 参数的 hook
- 每帧 JSON stringify/parse

如果移除这些后卡顿消失，基本可以确认是 JS hook 参数快照或脚本回调导致。

## 排查步骤

### 1. 建立无脚本基线

目标是确认 UE4SS 核心注入本身是否会造成卡顿。

操作：

1. 不加载外部 mods。
2. 不部署 `UE4SSL.JavaScript.dll` 和 `UE4SSL.Lua.dll`，或临时移出工作目录。
3. 使用性能基线配置。
4. 记录同一场景下帧时间、Actor 生成耗时、蓝图事件耗时。

如果无脚本基线仍然卡顿，重点看：

- `StaticConstructObject`
- 对象缓存
- 扫描/初始化
- 游戏自身负载

### 2. 只打开核心 Hook

逐个开启需要的 hook，每次只改一个变量：

```text
HookProcessEvent
HookProcessInternal
HookProcessLocalScriptFunction
HookBeginPlay
HookUFunctionBind
```

每次测试同一复现场景。不要一次开多个，否则无法定位。

### 3. 加回 JS

先加载 JS 模块但不加载脚本。如果没有卡顿，再逐个加载脚本。

重点检查脚本是否使用：

- `RegisterProcessEventWatch`
- 高频 `RegisterHook`
- RPC/UMG game-thread dispatcher
- 大参数 JSON
- 同步文件 IO
- 大范围 object search

### 4. 加回 Lua

Lua 模块本身会注册较多全局 hook。加回 Lua 后如果 Actor 生成或 BeginPlay 变慢，优先看：

- `NotifyOnNewObject`
- `RegisterBeginPlayPreHook`
- `RegisterProcessConsoleExec*`
- `ExecuteInGameThread*`
- 每帧 `on_update`

### 5. 观察日志

关闭 hook 后，如果某些功能仍尝试注册 callback，日志里可能出现注册失败或功能不可用提示。这是预期现象。

重点搜索：

```text
Failed to add hook
RegisterProcessEventPreCallback
RegisterUFunctionBindPostCallback failed
ProcessLocalScriptFunction is not available
EngineTick hook is not available
```

## 现象到原因速查

| 现象 | 优先怀疑 | 快速验证 |
| --- | --- | --- |
| 蓝图事件一触发就卡 | `HookProcessEvent`、JS/Lua callback | 关 `HookProcessEvent` 或移除 JS/Lua |
| 高频蓝图函数卡 | `HookProcessInternal` / `HookProcessLocalScriptFunction` | 关这两个开关 |
| JSON 序列化明显卡 | JS `FStructProperty` 参数快照 | 移除带 struct 参数的 hook |
| Actor 大量生成卡 | `StaticConstructObject`、Lua NotifyOnNewObject、BeginPlay | 移除 Lua，关 `HookBeginPlay` |
| 每帧稳定掉帧 | `HookEngineTick`、`HookAActorTick`、mod `on_update` | 确认 tick hook 为 0，检查 mod update |
| 地图切换卡 | `HookLoadMap`、PakSync/AssetRegistry | 关 `HookLoadMap` 或只保留诊断 |
| console/exec 卡 | `HookCallFunctionByNameWithArguments`、`HookLocalPlayerExec` | 关对应 hook |

## 需要改代码的优化点

### 1. 给 StaticConstructObject 对象缓存加开关

当前 `PostInitialize()` 无条件注册对象缓存回调。建议增加配置：

```ini
[Hooks]
HookStaticConstructObjectObjectCache = 0
```

实现思路：

```cpp
if (UnrealConfig.bHookStaticConstructObjectObjectCache)
{
    Hook::RegisterStaticConstructObjectPostCallback(...);
}
```

当前实现中该开关默认 `1`，保持旧行为；设为 `0` 时不注册 `ObjectSearcherPoolHook`，但仍保留 UE 原始 `StaticConstructObject_Internal` 地址，JS/Lua 创建 UObject 的能力不受这个开关影响。

精确对象查找 A/B 配置：

```ini
[ObjectSearch]
UseNativeStaticFindObjectFast = 1
CompareNativeSearchResults = 1
```

目前 native fast path 只用于“已知 Outer + 短名 FName”的安全场景；长路径、`ANY_PACKAGE`、未解析到函数地址时仍回退旧搜索。

注意：这会影响快速查找新生成 Actor 的能力，需要确认哪些 API 依赖这个缓存。

### 2. Lua StaticConstructObject 延迟安装

当前 Lua `on_program_start()` 直接注册 `StaticConstructObject` post callback。建议改成：

- 只有第一次调用 `NotifyOnNewObject` 类 API 时才注册。
- callback 数组为空时快速返回。
- 可以在没有 callback 时注销或标记跳过。

### 3. JS struct 参数快照改成可配置

建议配置：

```ini
[JavaScript]
SerializeStructParamsAsJson = false
MaxStructJsonBytes = 65536
MaxStructJsonDepth = 3
```

建议行为：

- 默认不序列化 struct。
- 脚本显式声明需要 struct JSON 时才序列化。
- 超过大小限制时返回 `{ "__truncated": true }` 或传 `undefined`。

### 4. ProcessEventWatch 降低全局成本

当前 `RegisterProcessEventWatch` 注册的是全局 `ProcessEvent` pre callback，然后在 callback 里比较目标 `UFunction`。

优化方向：

- 优先使用具体 `UFunction` hook，而不是全局 ProcessEvent watch。
- 对 watch 列表使用 hash/set，避免每次线性遍历。
- 没有 watch 时不要注册全局 callback。

## 建议提交顺序

如果要做代码优化，建议按风险从低到高拆分：

1. 增加 JS struct JSON 快照开关，默认保持旧行为或默认关闭由性能目标决定。
2. Lua `StaticConstructObject` 改为按需安装。
3. `StaticConstructObject` ObjectSearcherPool 回调增加配置开关。
4. `ProcessEventWatch` 数据结构优化。
5. 增加 hook 注册计数和运行时诊断日志，便于确认哪些回调真的安装了。

## 排查记录模板

```text
日期：
游戏版本：
UE4SS.Lite commit：
配置文件：
是否加载 UE4SSL.JavaScript.dll：
是否加载 UE4SSL.Lua.dll：
外部 mods：

复现场景：
  1.
  2.
  3.

现象：
  平均 FPS：
  1% low：
  最大帧时间：
  卡顿触发点：

已关闭：
  HookProcessEvent =
  HookProcessInternal =
  HookProcessLocalScriptFunction =
  HookBeginPlay =
  bUseUObjectArrayCache =

结论：
  是否和 ProcessEvent 相关：
  是否和 JS JSON 相关：
  是否和 Actor 生成相关：
  下一步：
```
