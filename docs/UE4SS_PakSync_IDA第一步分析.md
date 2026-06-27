# UE4SS PakSync IDA 第一步分析

## 分析环境

- IDA MCP: `ida-pro-mcp`
- IDB: `D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\FSD-Win64-Shipping.exe.i64`
- Binary: `FSD-Win64-Shipping.exe`
- Image base: `0x140000000`
- Auto analysis: ready
- Hex-Rays: ready

本轮目标是确认 DRG 原版 UGC、pak mount、AssetRegistry、mission/custom mission 的关键调用链，输出后续 PakSync 可安全复用的最小白名单。

## UGC 关键反射函数

### `UUGCSubsystem`

IDA 反射表显示：

| 函数 | 字符串地址 | 绑定表 | Wrapper | Native 调用 |
| --- | ---: | ---: | ---: | --- |
| `ApplyPendingMods` | `0x144D91FD8` | `0x144D97C50` | `0x141341BD0` | vtable `+0x2C8` |
| `FetchModsForSession` | `0x144D92030` | `0x144D97C80` | `0x141341F00` | vtable `+0x2E8` |

`ApplyPendingMods` 的 wrapper 读取 **两个 bool 参数**，再调用：

```cpp
(*vtable + 0x2C8)(this, bool_arg0, bool_arg1)
```

所以不能按旧文档假设只传一个 `FromJoining`。后续实现必须运行时遍历 `UFunction` 参数名和 `GetParmsSize()`，只在参数布局匹配时调用。

`FetchModsForSession` 的 wrapper 读取：

- `TArray<FString>`，参数名在反射字符串里是 `HostMods`
- delegate/回调参数，字符串是 `OnModsFetched`

它不适合作为 PakSync 第一版自动调用入口，因为需要构造正确 delegate，对象生命周期复杂。

### `UUGCRegistry`

IDA 反射表显示：

| 函数 | 字符串地址 | 绑定表 | Wrapper | Native 调用 |
| --- | ---: | ---: | ---: | --- |
| `MountUGCPackage` | `0x144D92CD8` | `0x144D97260` | `0x141343FC0` | vtable `+0x2F0` |
| `RegisterAssetFromPackage` | `0x144D92D18` | `0x144D97290` | `0x1413442C0` | vtable `+0x340` |
| `ResetUGCPackagesManipulatedDuringJoin` | `0x144D92D38` | `0x144D972A0` | `0x141344430` | vtable `+0x318` |
| `TryGetPackageFromId` | `0x144D92D60` | `0x144D972B0` | `0x141344980` | vtable `+0x2C8` |

`MountUGCPackage` wrapper 确认参数为：

```cpp
bool MountUGCPackage(UUGCPackage* Package, bool FromJoining)
```

`RegisterAssetFromPackage` wrapper 确认参数为：

```cpp
void RegisterAssetFromPackage(UUGCPackage* Package)
```

`ResetUGCPackagesManipulatedDuringJoin` wrapper 反编译失败，但反汇编确认它是无参 thunk：

```asm
mov rax, [rcx]
jmp qword ptr [rax+318h]
```

## `UUGCPackage` 字段

IDA 反射元数据确认这些字段存在，后续应通过反射字段访问，不要先写裸偏移：

| 字段 | 字符串地址 | 说明 |
| --- | ---: | --- |
| `IsMounted` | `0x144D91EB0` | bool |
| `MountingToBeApplied` | `0x144D91EC0` | bool |
| `ModPath` | `0x144D91EF0` | string/path |
| `PakFileLocation` | `0x144D91EF8` | pak 路径 |
| `PakFileAssets` | `0x144D91F08` | asset path 列表 |
| `DownloadVersion` | `0x144D91F70` | `EUGCDownloadVersion` |
| `ApprovalStatus` | `0x144D93068` | `EUGCApprovalStatus` |

`UGCRegistry` 还暴露这些 join 状态字段：

- `UGCPackagesInstalledDuringJoin`
- `UGCPackagesUnmountedDuringJoin`
- `OnPackageMounted`

这些说明原版 join 流程不仅 mount pak，还维护 during-join 变更集合和广播事件。

## Pak Mount 与 AssetRegistry

### `FPakPlatformFile::Mount`

IDA 确认当前代码使用的目标地址语义正确：

```cpp
bool FPakPlatformFile::Mount(
    FPakPlatformFile* this,
    const wchar_t* pak_path,
    uint32_t pak_order,
    const wchar_t* mount_point,
    bool notify
)
```

DRG 内部封装 `0x1433C00E0` 会打印 `Mounting pak file: %s`，随后调用 `0x1433C4A10(..., mount_point = 0, notify = true)`。

### `FPackageName::RegisterMountPoint`

当前 resolver 使用的 `0x140B52CD0` 仍不适合作为稳定调用点：

- IDA 将其归入大函数 `sub_140B524E0` 内部；
- 没有明确独立函数 xref；
- 不适合在 PakSync 第一版直接调用。

结论：第一版继续跳过直接 `RegisterMountPoint`，改用 `FPakPlatformFile::Mount` + AssetRegistry scan + UGC/mission 刷新。

### `AssetRegistry::ScanPathsSynchronous`

IDA 反射表确认：

| 函数 | 字符串地址 | 绑定表 | Wrapper |
| --- | ---: | ---: | ---: |
| `ScanPathsSynchronous` | `0x14570F4A8` | `0x145710100` | `0x143377360` |

wrapper 参数为：

```cpp
ScanPathsSynchronous(TArray<FString> InPaths, bool bForceRescan)
```

当前 PakSync 通过反射传 `["/Game"]` 和 `true` 的方向正确。后续优化点是缩小 `InPaths`，不要长期全扫 `/Game`。

## Mission / Custom Mission 结论

IDA 找到这些反射字段/函数：

- `GetMissionTemplate`
- `MissionTemplate`
- `CustomMissionTemplate`
- `MissionTemplateItem`
- `CustomMissionName`
- `missionTemplate`

直接代码引用到 `0x1417C62F0` 的字符串是：

```text
Seed mission type did not match MissionTemplate, defaulting to MissionTemplate
```

这属于任务生成/校验路径，不适合作为 PakSync 第一版刷新入口。

反射元数据说明 mission/custom mission 多处是对象字段，而非单一“刷新函数”。第一版不应尝试 hook 任务生成函数来强刷，而应：

1. mount 后扫描 AssetRegistry；
2. 调用 UGCRegistry 注册/刷新；
3. 记录已加载 mission/custom mission 相关对象，输出 partial 状态；
4. 后续再针对具体 UI/终端缓存找白名单刷新函数。

## 最小白名单

### 可以第一版尝试的反射调用

这些调用必须在游戏线程执行，且调用前检查对象存在、`UFunction` 存在、参数名/参数大小匹配。

1. `AssetRegistry.ScanPathsSynchronous(InPaths, bForceRescan)`
   - 已有实现方向正确。
   - 推荐先传 pak 内实际路径；找不到路径时保底 `/Game`。

2. `UUGCRegistry.ResetUGCPackagesManipulatedDuringJoin()`
   - 无参数 wrapper 已确认。
   - 风险较低，但只应在 join/mount 完成后的 refresh 阶段调用。

3. `UUGCRegistry.RegisterAssetFromPackage(Package)`
   - 参数为 `UUGCPackage*`。
   - 只有当能拿到真实 `UUGCPackage` 对象时调用。

4. `UUGCRegistry.MountUGCPackage(Package, FromJoining)`
   - 参数为 `UUGCPackage*` + `bool`。
   - 不建议第一版构造伪 `UUGCPackage`；只在找到真实 package 对象时调用。

### 谨慎调用

1. `UUGCSubsystem.ApplyPendingMods(...)`
   - IDA 确认为两个 bool 参数，不是单 bool。
   - 可作为候选，但必须运行时打印参数名并人工确认后再启用自动调用。

2. `UUGCSubsystem.FetchModsForSession(HostMods, OnModsFetched)`
   - 需要 delegate，第一版不应自动调用。

### 不建议第一版直接调用

1. `FPackageName::RegisterMountPoint`
   - 目标地址仍不稳定。

2. mission 生成/校验函数 `0x1417C62F0`
   - 不是通用刷新入口。

3. 通用 UObject 卸载/重载
   - UE 不会安全替换已加载蓝图 CDO/实例。

## 对 PakSync 下一步实现的约束

- 不要伪造 `UUGCPackage` 裸内存布局。
- 新增 pak 对象应先通过反射找已有 `UUGCPackage` 或游戏工厂路径。
- mid-mission “立即生效”第一版应定义为：新增资产/任务列表能被重新发现，已加载资产只标记 partial。
- `ApplyPendingMods` 必须按 IDA 新结论处理两个 bool 参数。
- `RegisterMountPoint` 继续保持禁用，直到找到稳定独立函数和 xref。

## 原版入房等待机制补充

后续通过 IDA MCP 继续分析 `FSD-Win64-Shipping.exe.i64` 后，确认原版 UGC 体验不是通过 `LoadMapPre` 这类“返回失败”的 hook 实现等待。

### UGC 层机制

原版 UGC 更像是游戏层状态机：

```text
Session host mods
  -> UUGCSubsystem::FetchModsForSession(HostMods, OnModsFetched)
  -> UUGCSubsystem::ApplyPendingMods(FromJoining, FromStartScreen)
  -> UUGCRegistry::MountUGCPackage(Package, FromJoining)
  -> UUGCRegistry::RegisterAssetFromPackage(Package)
  -> 维护 UGCPackagesInstalledDuringJoin / UGCPackagesUnmountedDuringJoin
  -> 触发 UGC package mounted / local user mods installed 相关事件
```

其中 `ApplyPendingMods` 已确认有两个 bool 参数：

```text
FromJoining
FromStartScreen
```

因此 PakSync 不应继续把它当作单参数函数调用。

### UE 原生 travel 等待点

`UEngine::LoadMap` 地址：

```text
0x1436F3EA0
```

`UEngine::Browse` 地址：

```text
0x143B30D10
```

`UEngine::TickWorldTravel` 地址：

```text
0x143B50CC0
```

UE4.27 `UEngine` vtable 中三者相邻：

```text
Browse          +0x460
TickWorldTravel +0x468
LoadMap         +0x470
```

IDA 反编译 `TickWorldTravel` 后确认关键逻辑如下：

```cpp
PendingNetGame = WorldContext->PendingNetGame; // FWorldContext + 416
if (!PendingNetGame)
{
    return;
}

PendingNetGame->Tick(); // vtable + 624

PendingNetGame = WorldContext->PendingNetGame;
if (!PendingNetGame)
{
    return;
}

if (*(bool*)(PendingNetGame + 0xA8))
{
    Engine->Browse(...);
    PendingNetGame->...;
    WorldContext->PendingNetGame = nullptr;
}
```

所以真正可安全延迟 join 的点不是 `LoadMapPre`。`LoadMapPre` 里 `PreventOriginalFunctionCall()` / 返回 false 会被游戏视为 travel 失败，并弹窗退出。

更合适的短期切入点是：

```text
hook UPendingNetGame::Tick 后置
  -> 保留原始 Tick，让网络握手和控制通道继续推进
  -> 如果 PakSync 未 ready，则把 *(bool*)(PendingNetGame + 0xA8) 压回 false
  -> PakSync 完成下载、校验、mount、AssetRegistry/UGC refresh 后，不再压 ready 标志
  -> TickWorldTravel 自然进入 Browse/LoadMap
```

这比 `LoadMapPre` 安全，因为它不是“返回失败”，而是让原版 `PendingNetGame` 保持 pending。

### 对实现的直接影响

- `LoadMapPre` 只应作为日志和最后诊断，不应继续作为硬阻断点。
- PakSync 应新增 `UPendingNetGame::Tick` detour，或等价地在 `TickWorldTravel` 进入 `Browse` 前压住 `PendingNetGame + 0xA8`。
- 网络传输仍应在 `ControlChannel` 建立后进行，不能阻断 `PendingNetGame::Tick` 本身。
- 只有当 PakSync 状态达到 `ReadyToTravel` 或明确 `NoPakWork` 时，才允许 `PendingNetGame + 0xA8` 保持 true。
