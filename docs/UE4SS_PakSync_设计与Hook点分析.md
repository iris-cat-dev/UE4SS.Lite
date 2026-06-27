# UE4SS PakSync 设计与 Hook 点分析

> 目标游戏：Deep Rock Galactic / FSD  
> 引擎版本：UE4.27  
> 实现位置：`d:\Project\UE4SS.Lite\crates\ue4ssl-paksync`  
> 目标：主机通过 UE 网络通道把 pak 同步给客户端，客户端落盘校验后动态 mount，并保证在进入依赖资产的地图/任务前完成同步。

## 目标

要解决的问题分成两部分：

1. **网络同步**：客户端加入主机后，主机把缺失的 pak 传给客户端。
2. **资产加载**：客户端收到 pak 后 mount 到 UE 文件系统，使 pak 内资产能被 UE 正常加载。

关键约束：

- 不依赖 DRG 的聊天 RPC 或某个 gameplay RPC。
- 传输层尽量走 UE 自带可靠 bunch/channel 机制，而不是裸 UDP/TCP packet。
- pak 必须在依赖资产首次加载前 mount，否则已经加载过的资产不会自动被替换。
- 当前实现应先以 dry-run 和日志验证为主，避免 hook 错点导致连接崩溃。

## 总体方案

推荐同步窗口是：

```text
客户端连接建立
  -> PakSync 协议握手
  -> 比对 pak manifest / hash
  -> 传输缺失 chunk
  -> 客户端落盘 .pak.tmp
  -> SHA-256 校验
  -> rename 为 .pak
  -> FPakPlatformFile::Mount
  -> 标记 MountedReady
  -> 允许进入依赖资产的地图/任务
```

不要在最早的网络握手阶段传 pak。更稳的做法是等 `UNetConnection` / `UControlChannel` 已经建立之后再走自定义控制帧。

当前实现已经加入了 LoadMap 前置门禁：

- 默认只打印日志，不阻止。
- 当 `dry_run=false` 且 `block_travel_until_ready=true` 时，如果 PakSync 没 ready，会阻止 `LoadMap` 并返回 false。

默认配置位于：

```text
d:\Project\UE4SS.Lite\crates\ue4ssl-paksync\config\paksync.ini
```

当前默认值：

```ini
dry_run=true
enable_detours=false
block_travel_until_ready=false
auto_mount_verified_paks=false
dump_vtables=true
chunk_size=32768
send_window=8
vtable_entries=96
```

## 同步状态机

`PakSync.cpp` 里目前使用的同步阶段：

- `WaitingForConnection`：还没有检测到 `NetConnection`。
- `NoPakWork`：已有连接，但没有本地 pak manifest，也没有待接收 session。
- `Preparing`：预留阶段，后续可用于 manifest/handshake 准备。
- `TransferPending`：已经准备好 manifest/chunk frame，但尚未 flush 到 UE bunch。
- `MountedReady`：本地 pak 工作已完成，可以进入依赖资产的地图。

LoadMap 前置 hook 会输出：

```text
[UE4SSL.PakSync] LoadMapPre engine=... pending_net_game=... phase=... ready=... dry_run=... block=...
```

这样可以在 `UE4SS.log` 中确认 travel 是否发生在 pak sync ready 之前。

## 网络协议设计

PakSync 私有帧头：

```cpp
struct FrameHeader
{
    uint32_t magic;        // "UPSK" little-endian
    uint16_t version;      // 当前为 1
    uint16_t kind;         // Manifest / Chunk / Ack / Nak / Resume / Done
    uint64_t session_id;
    uint8_t  pak_hash[32]; // SHA-256
    uint32_t seq;
    uint32_t total;
    uint32_t payload_size;
    uint32_t payload_crc;
};
```

帧类型：

- `Manifest`：pak 名称、大小、chunk size、chunk count。
- `Chunk`：pak 原始二进制分片。
- `Ack`：确认收到某个 chunk 或区间。
- `Nak`：请求重传。
- `Resume`：断点续传。
- `Done`：传输完成。

当前代码已经实现：

- manifest payload 编码。
- chunk payload 读取。
- frame encode/decode。
- CRC32 校验。
- `.pak.tmp` 写入。
- SHA-256 校验。
- 完成后 rename 到 `incoming/<pakName>.pak`。

尚未最终接入的是：把 `QueuedFrame` 写入真正的 `FOutBunch` 并通过 `UControlChannel::SendBunch` / `UNetConnection::SendRawBunch` 发出。这一步要等 FOutBunch 写入偏移完全确认后再启用。

## Hook 点确认方法

分析使用了两个 IDA 样本：

- **PDB UE4.27 本地工程**：用于确认函数名、签名、vtable slot 和语义。
- **真实 DRG Steam 无 PDB 目标**：用于确认最终运行地址和可迁移特征。

原则：

- PDB 样本只作为语义参考，不直接使用它的 RVA。
- 真正写入 resolver 的必须是 DRG 无 PDB 目标上的地址和字节特征。
- 如果 PDB 函数签名在真实目标不命中，要改用语义特征、vtable 对齐、字符串 xref、调用关系确认。

## 网络 Hook 点

### `UControlChannel::ReceivedBunch`

用途：接收控制通道 bunch，是 PakSync 私有帧的优先接收入口。

PDB 样本：

```text
UControlChannel::ReceivedBunch
地址：0x142cfb780
符号：?ReceivedBunch@UControlChannel@@UEAAXAEAVFInBunch@@@Z
```

PDB 语义特征：

- `this + 0x28` 取 `UNetConnection*`。
- 检查 `this + 0x68` 的控制通道 endian/握手状态。
- 读取 `FInBunch.Buffer` 和 bit reader 状态。
- 循环解析 control message。

真实 DRG：

```text
sub_14367CA90
RVA: 0x367CA90
```

真实 DRG 特征：

```text
40 55 53 56 57 41 54 41 57 48 8D AC 24 78 FF FF FF
48 81 EC 88 01 00 00 33 F6 48 8B DA
```

进一步确认：

- vtable 附近 `0x145914ad8` 指向 `0x14367CA90`。
- 它比 `UControlChannel::SendBunch` 的 vtable entry `0x145914b10` 早 7 个 slot。
- 反编译中可见 `CheckEndianess` 失败后关闭连接的日志路径，和 PDB 样本控制通道接收逻辑一致。

当前代码状态：

- resolver 名称：`UControlChannel::ReceivedBunch`
- 当前 detour 安装入口已改为这个函数。
- detour 签名：`void(__fastcall*)(void* channel, void* bunch)`

### `UControlChannel::SendBunch`

用途：控制通道发送入口。后续可用于发送 PakSync 的 manifest/chunk/ack 帧。

PDB 样本：

```text
UControlChannel::SendBunch
地址：0x142d00ef0
符号：?SendBunch@UControlChannel@@UEAA?AUFPacketIdRange@@PEAVFOutBunch@@_N@Z
```

PDB 语义特征：

- 检查控制通道队列数量。
- 如果队列未溢出且 bunch 没错误，则调用 `UChannel::SendBunch`。
- 如果溢出，则关闭连接。

真实 DRG：

```text
sub_143683690
RVA: 0x3683690
```

真实 DRG 特征：

```text
48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30
8B 41 78 49 8B F0 48 8B DA 48 8B F9
```

重要修正：

- 早期曾误判 `sub_143683690` 为接收入口。
- 通过 PDB 样本对比后确认它是 `UControlChannel::SendBunch`。

### `UChannel::SendBunch`

用途：通用 channel 发送入口。`UControlChannel::SendBunch` 会调用它。

PDB 样本：

```text
UChannel::SendBunch
地址：0x142d00550
符号：?SendBunch@UChannel@@UEAA?AUFPacketIdRange@@PEAVFOutBunch@@_N@Z
```

真实 DRG：

```text
sub_143682A60
RVA: 0x3682A60
```

真实 DRG 特征：

```text
4C 89 44 24 18 48 89 54 24 10 55 53 56 57 41 55
48 8D AC 24 20 FF FF FF
```

语义特征：

- 大量访问 `FOutBunch/FInBunch` 风格字段：`+0xF4`、`+0xF5`、`+0xF6`。
- 被控制通道发送逻辑调用。

### `UNetConnection::SendRawBunch`

用途：更底层的 raw bunch 发送入口。可以作为发送路径分析/备用调用点。

PDB 样本：

```text
UNetConnection::SendRawBunch
地址：0x142ebf8f0
符号：?SendRawBunch@UNetConnection@@QEAAHAEAVFOutBunch@@_NPEBVFNetTraceCollector@@@Z
```

真实 DRG：

```text
sub_14385B5B0
RVA: 0x385B5B0
```

真实 DRG 特征：

```text
48 89 5C 24 10 55 56 57 41 56 41 57 48 83 EC 50
48 8B 01 45 0F B6 F8
```

语义特征：

- 编码 bunch flags。
- 写 sequence / packet id。
- 日志中可出现 `Sending: %s`。
- 被 ack timeout / NAK 重发 / channel send 核心调用。

## Pak 加载 Hook 点

### `FPakPlatformFile::Initialize`

用途：确认 Pak 平台文件实现存在，并定位 Pak 函数簇。

真实 DRG：

```text
sub_1433C09B0
RVA: 0x33C09B0
字符串：FPakPlatformFile::Initialize
```

### `FPakPlatformFile::Mount`

用途：真正 mount pak 文件。

PDB 样本：

```text
FPakPlatformFile::Mount
地址：0x142a765f0
符号：?Mount@FPakPlatformFile@@QEAA_NPEB_WI0_N@Z
签名：bool(this, const wchar_t* pakPath, uint32 pakOrder, const wchar_t* mountPoint, bool notify)
```

真实 DRG：

```text
sub_1433C4A10
RVA: 0x33C4A10
```

真实 DRG 特征：

```text
4C 8B DC 55 53 57 49 8D AB A8 FD FF FF
48 81 EC 40 03 00 00
```

调用方式：

```cpp
mount(pak_file, pak_path.c_str(), 0, nullptr, true);
```

当前代码已经把 `PakMountFn` 改为 5 参数。

### `HandleMountPakDelegate`

用途：UE 委托包装入口，会调用真正的 `Mount`。

真实 DRG：

```text
sub_1433C00E0
RVA: 0x33C00E0
```

确认特征：

- 打印字符串：`Mounting pak file: %s`
- 如果 pak order 为 `-1`，调用 `GetPakOrderFromPakFilePath`。
- 调用 `sub_1433C4A10(this, path, order, nullptr, true)`。
- 遍历 mounted pak list。

当前实现选择直接调用 `FPakPlatformFile::Mount`，而不是调用委托包装入口。

## 加入连接后的同步时机

同步不应该发生在最早的连接握手阶段，而应发生在：

```text
NetConnection / ControlChannel 已建立
  -> PakSync manifest 比对
  -> 传输缺失 chunk
  -> 客户端校验并 mount
  -> LoadMap 前检查状态
```

当前 `PakSync.cpp` 已实现：

- 每帧查找 `NetConnection`。
- 维护同步阶段。
- 注册 `LoadMapPre` hook。
- 在进入地图前输出当前同步状态。
- 默认只 dry-run。
- 仅当 `dry_run=false` 且 `block_travel_until_ready=true` 时阻止 LoadMap。

相关日志：

```text
[UE4SSL.PakSync] discovered N NetConnection object(s)
[UE4SSL.PakSync] sync phase A -> B
[UE4SSL.PakSync] LoadMapPre ... phase=... ready=... dry_run=... block=...
```

## 当前实现状态

已完成：

- `UE4SSL.PakSync` native mod crate。
- xtask package artifact 接入。
- resolver 表。
- 控制通道接收 detour，并已验证客户端 `UControlChannel::ReceivedBunch` 能命中。
- frame encode/decode。
- manifest/chunk 预生成。
- 通过 `FControlChannelOutBunch` + `UControlChannel::SendBunch` 发送 PakSync 私有帧。
- 从 `FInBunch` 读取 payload，识别 PakSync 私有帧并分发。
- chunk 落盘、flush/close 后 SHA-256 校验。
- 完成后 rename 为 `incoming/<pak>.pak`。
- `FPakPlatformFile::Mount` 调用入口，并修正 `FPlatformFileManager::Get` resolver 为 DRG 真实 RVA `0x1C77C80`。
- mount 后尝试通过 `AssetRegistry` 的 `ScanPathsSynchronous(["/Game"], true)` 刷新资产注册表。
- LoadMap 前置同步门禁。
- 默认 dry-run 配置。
- 默认禁用 UE4SS `HookEngineTick` 和 `HookInitGameState`，规避 DRG 启动/进图时 trampoline 崩溃。

已验证：

- 服务端可以准备并 flush manifest + chunk 帧。
- 客户端可以接收 `PakSync frame`，解析 manifest/chunk。
- 客户端可以写入 `.pak.tmp`，校验成功后 rename 为 `.pak`。
- `FPakPlatformFile::Mount` 之前的 manager 查找已修正；后续需要继续验证 `mount ... ok=true` 与 AssetRegistry scan 的实际效果。

尚未完成：

- ack/nak/resume 的完整发送与重传。
- 只发送 warm window 内 chunk，尚未实现完整滑动窗口和重传。
- `FPackageName::RegisterMountPoint` resolver 当前仍低置信度，暂未直接调用，避免误调。
- mount 后 AssetRegistry scan 是否足以让 DRG 立即发现自定义任务仍需验证。
- 如果 pak 覆盖已加载资产，UE 不会自动热替换内存中的 `UObject`，需要游戏侧缓存刷新或更精确的重新加载策略。
- DRG 原版 `UUGCRegistry::MountUGCPackage(UUGCPackage*, bool)` 依赖完整 `UUGCPackage` 对象模型，直接复用游戏 API 成本高，暂不作为第一实现路径。

## 当前验证结论

### 传输链路

当前 PakSync 已经不再停留在 dry-run：

```text
服务端 paks/
  -> build manifest
  -> prepare manifest/chunk frames
  -> 连接建立后 flush queued frames
  -> UControlChannel::SendBunch
  -> 客户端 UControlChannel::ReceivedBunch detour
  -> detect PakSync frame
  -> manifest/chunk 落盘
  -> SHA-256 校验
  -> rename 为 incoming/<pak>.pak
```

关键日志：

```text
[UE4SSL.PakSync] flushed frame <pak>:manifest bytes=...
[UE4SSL.PakSync] flushed frame <pak>:chunk bytes=...
[UE4SSL.PakSync] PakSync frame detected in incoming bunch ...
[UE4SSL.PakSync] manifest received session=... chunks=... size=...
[UE4SSL.PakSync] received pak verified: ...\incoming\<pak>.pak
```

### Mount 与立即生效

`FPakPlatformFile::Mount` 只让 pak 进入 UE 文件系统。它不会自动：

- 替换已经加载过的 `UObject`。
- 重建已经实例化的蓝图类/CDO。
- 刷新 DRG 已缓存的任务列表、mission template 列表或 UGC registry。

因此“从空降仓进入任务后生效”是合理现象：任务进入流程重新读取相关数据时，pak 已经存在。

当前通用方向是：

```text
pak verified
  -> FPakPlatformFile::Mount
  -> AssetRegistry ScanPathsSynchronous(["/Game"], true)
  -> 让后续软引用/AssetRegistry 查询能看到新资产
```

这能解决“后续首次加载能发现资源”，但不能保证“覆盖已加载资产即时热替换”。

### DRG 原版 UGC 流程观察

SimpleUGC 头文件显示原版核心入口是：

```cpp
bool UUGCRegistry::MountUGCPackage(UUGCPackage* Package, bool FromJoining);
void UUGCRegistry::RegisterAssetFromPackage(UUGCPackage* Package);
void UUGCRegistry::ResetUGCPackagesManipulatedDuringJoin();
void UUGCSubsystem::ApplyPendingMods(bool FromJoining);
bool UUGCSubsystem::FetchModsForSession(TArray<FString> HostMods, FUGRequiredModsFetched OnModsFetched);
```

`UUGCPackage` 不是简单 pak 路径，关键字段包括：

```cpp
FString Name;
FString ModPath;
FString PakFileLocation;
TArray<FString> PakFileAssets;
EUGCApprovalStatus Status;
EUGCDownloadVersion DownloadVersion;
bool IsMounted;
bool MountingToBeApplied;
```

因此原版 DRG 更像是：

```text
加入/进任务前
  -> FetchModsForSession(host mods)
  -> ApplyPendingMods(FromJoining=true)
  -> 对完整 UUGCPackage 调 MountUGCPackage
  -> RegisterAssetFromPackage
  -> 维护 UGCPackagesInstalledDuringJoin / UnmountedDuringJoin
  -> broadcast OnPackageMounted / OnLocalUserModsInstalled
  -> 任务流程随后读取 mission/template 数据
```

直接复用 `MountUGCPackage` 需要构造完整 `UUGCPackage` 对象并维护 registry/subsystem 状态，风险和复杂度较高。

## 下一步目标

1. **确认 mount + AssetRegistry scan 是否稳定**
   - 看客户端是否输出 `mount ... ok=true`。
   - 看是否输出 `AssetRegistry ScanPathsSynchronous completed`。
   - 如果没有，优先修正 mount point / AssetRegistry 反射调用。

2. **修正 `FPackageName::RegisterMountPoint`**
   - 当前 resolver 低置信度多命中，暂不调用。
   - 需要用 IDA/UE 源码确认真实 DRG RVA 与签名。
   - 目标是把 pak 内容路径明确注册到 `/Game/` 或正确插件/UGC root。

3. **缩小 AssetRegistry scan 范围**
   - 当前粗暴扫描 `/Game`。
   - 后续应解析 pak index 或 mount point，只扫描 pak 内实际路径，避免卡顿。

4. **定位 DRG mission/custom mission 缓存刷新点**
   - 重点追 `CustomMissionTemplate`、`GetMissionTemplate`、`MissionTemplateItem`。
   - 找到任务列表/任务终端使用的 registry/cache。
   - PakSync ready 后触发对应刷新，而不是伪造完整 UGC 包。

5. **完整可靠传输**
   - ack/nak/resume。
   - 滑动窗口。
   - 断线重连后的 resume。
   - 多 pak manifest 比对和缺失 pak 选择。

## 部署与排查

### 构建与安装

推荐用 `xtask install` 直接部署，避免手动复制漏文件或复制到错误目录：

```powershell
cd d:\Project\UE4SS.Lite
cargo build -p ue4ssl-dll -p ue4ssl-paksync
cargo run -p xtask -- install --no-build --destination "d:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss"
```

注意：

- `xtask install --no-build` 会把 `crates/ue4ssl-paksync/config` 下的默认 `paksync.ini` 覆盖到游戏目录。
- 每次 install 后，如果要实测，需要重新确认游戏目录里的配置。
- 只改核心 UE4SSL.dll 时可用：

```powershell
cargo build -p ue4ssl-dll
cargo run -p xtask -- install --core-only --no-build --destination "d:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss"
```

### 目录与资源

游戏部署目录：

```text
d:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss
```

PakSync mod 目录：

```text
...\ue4ss\Mods\UE4SSL.PakSync
```

服务端待发送 pak 放置目录：

```text
...\ue4ss\Mods\UE4SSL.PakSync\paks
```

客户端接收 pak 输出目录：

```text
...\ue4ss\Mods\UE4SSL.PakSync\incoming
```

配置文件：

```text
...\ue4ss\Mods\UE4SSL.PakSync\config\paksync.ini
```

实测配置建议：

```ini
dry_run=false
enable_detours=true
block_travel_until_ready=false
auto_mount_verified_paks=true
dump_vtables=true
chunk_size=32768
send_window=8
vtable_entries=96
```

说明：

- `block_travel_until_ready=true` 会在 PakSync 未 ready 时阻止 `LoadMap`，容易弹出 `waiting for pak sync before map load`，实测传输时建议先保持 `false`。
- `dry_run=false` 才会执行自动 mount 等有副作用路径。
- `enable_detours=true` 才会安装 `UControlChannel::ReceivedBunch` detour。
- 服务端必须有 `paks/*.pak`，否则日志会显示 `pak directory not present` 或 `manifests=0 queued_frames=0`。

### UE4SS 核心设置

当前为了规避 DRG 中不稳定的 trampoline，Rust 默认设置里关闭：

```ini
HookEngineTick = 0
HookInitGameState = 0
```

如果游戏目录存在：

```text
...\ue4ss\UE4SS-settings.ini
```

它会覆盖内置默认值。排查崩溃时需要确认该文件是否显式开启了上述 hook。

### 服务端日志检查点

服务端成功准备和发送 pak 时应看到：

```text
[UE4SSL.PakSync] unreal init ... dry_run=false enable_detours=true auto_mount=true
[UE4SSL.PakSync] pak manifest <pak> size=... chunks=... sha256=...
[UE4SSL.PakSync] prepared N frame(s) for <pak> with send_window=...
[UE4SSL.PakSync] ControlChannel::ReceivedBunch detour installed=true
[UE4SSL.PakSync] discovered 1 NetConnection object(s)
[UE4SSL.PakSync] sync phase WaitingForConnection -> TransferPending
[UE4SSL.PakSync] flushing N queued frame(s) via connection=... control_channel=...
[UE4SSL.PakSync] flushed frame <pak>:manifest bytes=...
[UE4SSL.PakSync] flushed frame <pak>:chunk bytes=...
[UE4SSL.PakSync] sync phase TransferPending -> MountedReady
```

常见异常：

- 没有 `pak manifest`：服务端 `paks` 目录不存在或没有 `.pak`。
- 没有 `flushing`：没有检测到 live `NetConnection` / `ControlChannel`，或配置仍是 dry-run/detour off。
- 连续 `[CppMod] SEH exception in on_update`：通常是某个 native mod 的 `on_update` 崩，需要逐步隔离；历史上 `HookEngineTick` / `HookInitGameState` 也会导致 DRG 崩溃。

### 客户端日志检查点

客户端成功接收、校验、mount 时应看到：

```text
[UE4SSL.PakSync] unreal init ... dry_run=false enable_detours=true auto_mount=true
[UE4SSL.PakSync] pak directory not present: ...\paks
[UE4SSL.PakSync] ControlChannel::ReceivedBunch detour installed=true
[UE4SSL.PakSync] discovered 1 NetConnection object(s)
[UE4SSL.PakSync] PakSync frame detected in incoming bunch ...
[UE4SSL.PakSync] manifest received session=... chunks=... size=...
[UE4SSL.PakSync] received pak verified: ...\incoming\<pak>.pak
[UE4SSL.PakSync] auto-mounting verified incoming pak(s)
[UE4SSL.PakSync] pak mount lookup manager=... pak_file=...
[UE4SSL.PakSync] mount ... ok=true
[UE4SSL.PakSync] AssetRegistry candidates=...
[UE4SSL.PakSync] AssetRegistry ScanPathsSynchronous completed
```

常见异常：

- 没有 `PakSync frame detected`：服务端未 flush，或客户端 detour 未安装。
- 有 `PakSync frame detected` 但没有 `manifest received`：帧头/CRC/偏移解析失败。
- `hash mismatch`：chunk 写入或顺序/重复处理有问题；曾修复过写入后未 flush/close 就校验的问题。
- `received pak verified` 后崩溃：通常在 mount 阶段，重点检查 `FPlatformFileManager::Get` / `FindPlatformFile` / `FPakPlatformFile::Mount` resolver。
- `mount ok=true` 但游戏里看不到资源：通常是 AssetRegistry/mount point/游戏侧缓存刷新问题，不是传输问题。

### Mount 与资源可见性排查

`mount ... ok=true` 只表示 UE 文件系统能看到 pak。还要继续确认：

- `AssetRegistry ScanPathsSynchronous completed` 是否出现。
- pak 内资源路径是否位于 `/Game/...` 或已注册的 mount point。
- 如果 pak 覆盖的是已加载资产，内存中的 `UObject` 不会自动热替换。
- DRG 的任务列表/CustomMission 缓存可能需要额外刷新，重点分析：
  - `CustomMissionTemplate`
  - `GetMissionTemplate`
  - `MissionTemplateItem`
  - UGC/mission registry 相关缓存。

### 外部参考资源

UE4.27 源码：

```text
d:\Project\UnrealEngine-4.27
```

DRG SimpleUGC 头文件：

```text
d:\UnrealProjects\FSD-Template\Plugins\SimpleUGC\Source\SimpleUGC
```

IDA MCP：

```text
http://127.0.0.1:13337/mcp
```

已确认有用的 IDA 信息：

- `FControlChannelOutBunch::ctor`：RVA `0x365DDD0`
- `UControlChannel::ReceivedBunch`：RVA `0x367CA90`
- `UControlChannel::SendBunch`：RVA `0x3683690`
- `UChannel::SendBunch`：RVA `0x3682A60`
- `UNetConnection::SendRawBunch`：RVA `0x385B5B0`
- `FPakPlatformFile::Mount`：RVA `0x33C4A10`
- `FPlatformFileManager::FindPlatformFile`：RVA `0x1C76BB0`
- `FPlatformFileManager::Get`：RVA `0x1C77C80`

### 最小验证流程

1. 服务端放入测试 pak：

```text
...\Mods\UE4SSL.PakSync\paks\<test>.pak
```

2. 服务端和客户端都使用实测配置。

3. 启动服务端进入大厅。

4. 客户端加入。

5. 服务端确认 `flushed frame`。

6. 客户端确认：

```text
PakSync frame detected
manifest received
received pak verified
mount ... ok=true
AssetRegistry ScanPathsSynchronous completed
```

7. 再测试进入任务/空降仓后资源是否可见。

### 安全验证配置

```ini
dry_run=true
enable_detours=false
block_travel_until_ready=false
```

首次验证 resolver 时保持以上配置，只看日志，不执行 detour/mount。检查：

```text
[UE4SSL.PakSync] resolver UControlChannel::ReceivedBunch
[UE4SSL.PakSync] resolver UControlChannel::SendBunch
[UE4SSL.PakSync] resolver UNetConnection::SendRawBunch
[UE4SSL.PakSync] resolver FPakPlatformFile::Mount
[UE4SSL.PakSync] LoadMap gate registered
```

resolver 全部命中后，再开启：

```ini
enable_detours=true
```

接收 hook 稳定命中后，再测试阻止 travel：

```ini
dry_run=false
block_travel_until_ready=true
```

## 风险与注意事项

- PDB 样本和真实 DRG 的函数前导签名并不完全一致，不能直接迁移 PDB RVA。
- 网络 hook 点必须以真实 DRG 的 resolver 为准。
- `UControlChannel::ReceivedBunch` 是接收 PakSync 控制帧的优先 hook 点。
- 不建议第一版 hook `LowLevelSend`，它太底层，会绕过 UE bunch 语义。
- 如果 pak 覆盖已有资产，必须在资产首次加载前 mount。
- 如果玩家 mid-join 到任务中，可能已经错过资产加载时机。第一版建议只支持大厅/任务开始前同步。
- `FPakPlatformFile::Mount` 只负责让 pak 进入文件系统；新增资产是否可见，还取决于路径、mount point、AssetRegistry 和加载时机。

