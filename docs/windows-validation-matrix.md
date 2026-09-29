# Windows 可重复验证矩阵

## 当前结论与真实缺失条件

**所有游戏行：unverified。** 当前执行环境为 macOS arm64；没有可用 Windows 游戏进程、Windows/游戏版本与 EXE hash 记录，也没有配置的 SSH Windows 执行端。已取得 4 个 x64 PE DLL 和 4 个 PDB 的既有文件，但缺脚本引擎 DLL；没有证据把现有 DLL 关联到某次源码/工具链构建。不能称“可运行基线”或“Windows 游戏验证通过”。

现有文件的可重复静态证据：`baseline-artifacts.json` 列出每个文件 SHA256、大小和 PE 导出；只读副本在 `target/migration-baseline/`。目标目录不提交 binary；传给 Windows 验证端后重算 hash。基线和迁移候选必须独立目录，不覆盖冻结副本。

## 记录格式（每一次实际运行写一条 JSON）

下列是**本次真实未运行记录**，不是伪造游戏名称的示例。执行端新增运行记录时必须把适用的 null 换成真实观测值；未获得的数据保持 null 并说明原因，禁止写 PASS。

```json
{
  "run_id": "2026-09-22-baseline-capture-only",
  "status": "unverified",
  "source_contract_revision": "fff1f21dce7f671c8736a7907ff41ccb9c2faf32",
  "candidate_manifest": "baseline-artifacts.json",
  "artifact_build_revision": null,
  "windows_build": null,
  "cpu_arch": null,
  "game_title": null,
  "game_store_build_id": null,
  "game_exe_path": null,
  "game_exe_sha256": null,
  "engine_version_detected": null,
  "engine_version_override": null,
  "module_hashes": [],
  "settings_sha256": null,
  "member_layout_sha256": null,
  "vtable_layout_sha256": null,
  "scan_override_files": [],
  "mods": [],
  "scenarios": [],
  "logs": [],
  "crash_dump": null,
  "blockers": [
    "No Windows execution endpoint or game process",
    "No identified game EXE/build/version for comparison",
    "UE4SSL.Lua.dll and UE4SSL.JavaScript.dll absent from captured profile roots",
    "Existing DLL build provenance unestablished"
  ]
}
```

每个 `mods` 元素记录 `{name, abi: cpp|rust-v1|lua|js, relative_path, sha256, enabled, config_sha256}`；脚本 Mod hash 覆盖全部入口及依赖文件，不能只 hash main。每个 scan override 记录 `{path,sha256,kind,targets,expected_address_source}`；kind 明确 builtin/custom/member/vtable，不能笼统写“有 override”。每个 scenario 记录 `{id,start_utc,end_utc,steps,expected,observed,status,thread_ids,attachments}`。status 只能是 pass/fail/unverified/blocked；pass 必须有 observed 与附件。日志附文件 hash，时间至少毫秒，并记录 pid/tid 和 callback owner/handle。重载/停止记录最后一次 callback 与最后一次 release 的时间。

## 执行前取证（Windows PowerShell）

在游戏目录外建立两个独立包目录，把已冻结文件按清单复制进去；不要在联网多人游戏或未获授权目标使用输入/内存/hook 场景。用固定离线地图/存档和已获准的 Mod。以下命令只取证，不启动程序：

```powershell
$ErrorActionPreference = 'Stop'
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber, OsArchitecture
Get-FileHash -Algorithm SHA256 -LiteralPath .\UE4SSL.dll
Get-FileHash -Algorithm SHA256 -LiteralPath .\UE4SSL.pdb
Get-ChildItem -LiteralPath .\Mods -Recurse -File |
  Get-FileHash -Algorithm SHA256 | Sort-Object Path
# 在 VS Developer PowerShell 中对每个候选 DLL 分别执行：
dumpbin /headers .\UE4SSL.dll
dumpbin /exports .\UE4SSL.dll
dumpbin /dependents .\UE4SSL.dll
```

记录准确 Windows SDK/MSVC/Rust target/profile/panic 策略以及候选构建日志；如果不是本次构建，保留 provenance unknown。不要将 DLL COFF timestamp 当构建提交证据。PDB 文件 hash 只识别文件；匹配调试符号还需比较 PE CodeView GUID/age，不仅文件名。

## 版本 / 布局覆盖矩阵

每一行至少选一个实际已获准游戏 build；取得之前保持 unverified。不是声称仓库中的生成表覆盖过所有游戏。

| 版本组 | 必须区分的真实分支 | 扫描组合 | 状态 |
|---|---|---|---|
| UE4.10–4.19 | 连续 GUObjectArray、旧 UProperty/UField | 默认；仅 GUObjectArray override；仅 FMemory override | unverified：未提供该组游戏 |
| UE4.20–4.24 | 分块对象数组、4.25 前 property 继承 | 默认；VTableLayout；modular EXE+DLL override | unverified |
| UE4.25 | FField/FProperty 分流，旧 StaticConstructObject 参数签名尾端 | 默认；MemberVariableLayout；ProcessInternal 单 override | unverified |
| UE4.26–4.27 | 参数结构版 StaticConstructObject、UE4 最大已列 minor | 默认；扫描失败/超时；版本强制覆盖 | unverified |
| UE5.0 | 5.1 前 FFrame/ImportText 边界 | 默认；Tick scan/vtable；FName 策略 | unverified |
| UE5.1–5.3 | 5.1 FFrame/ImportText 参数变化 | 默认；case-preserving FName；自定义成员表 | unverified |
| UE5.4–5.5 | Optional property 类型初始化 | 默认；缺 required reflected object；延迟加载 | unverified |
| UE5.6 | UTF8 property、StaticConstructObject 新签名 | 默认；builtin GUObjectArray fallback 开/关；单 override | unverified |
| UE5.7 | ANSI property、FUObjectItem FlagsAndRefCount/packing | 默认；实际 packing 变体分别记录，不把未实现分支算支持 | unverified |
| 不支持的版本 | resolver 找到版本与容器支持范围不同 | 强制不支持 major/minor，应明确初始化拒绝，不到对象解引用 | unverified |

UE4.10–4.27、UE5.0–5.7 是 `VersionedContainer/Container.cpp:40–188` 的选择范围，不是承诺每个游戏兼容。Rust resolver 可识别更广 UE4/UE5 范围。必须记录 WITH_CASE_PRESERVING_NAME/FNAME_ALIGN8/outline-number、modular/non-modular 与游戏定制布局信息；无法取得则标 unknown，不能以“UE5.7”一项代替这些组合。

## 可重复场景与判定

每行对**同一游戏/配置/输入**跑冻结候选一次、迁移候选一次；运行中记录实际路径、扫描地址来源、Mod顺序和线程。除故意错误用例外，初始配置关闭其他 Mod 干扰。每次重启进程恢复相同初始地图。

| ID | 精确操作 | 比较点 / 通过条件 | 当前 |
|---|---|---|---|
| W01 路径优先级 | 在 root 与 legacy 各放内容不同的 settings；首选存在时运行，再仅移走首选；分别创建游戏特定同名目录；相对/绝对 ModsFolderPath 各一次 | 日志记录实际 settings/Mods/log 路径；只在首选不存在时回退；相对 override 相对于 working | unverified |
| W02 入口选择 | 单独装旧 C++ Mod、Rust v1 Mod；再用同时导出新旧入口的受控 fixture、缺一个 Rust 必需符号的 fixture | JS→Lua→普通 Mod；双入口只 start/uninstall 一次；不完整 Rust ABI 拒绝且不回退 C++；错误码与 GetLastError 分开 | unverified |
| W03 脚本加载 | 两 Lua Mod 与两 JS Mod；同名 shared/module.lua 与 Mod lua/module.lua；JS 相对 import 不写扩展名；各加入一个语法错误入口 | Lua 搜索顺序/VM 隔离；JS 单 context/module cache；单脚本失败不误变整个发现结果；错误包含来源文件 | unverified |
| W04 输入 | 游戏前台/后台各按下、保持、释放同一键；空修饰符、Ctrl、Ctrl+Shift；禁用状态下重复；绑定 callback 内注销自己及另一个尚未执行绑定 | 记录按下/保持重复策略与组合匹配；不死锁、不悬挂、不重复 release；关 owner 后无回调。旧缺陷单列，不要求保留 UAF | unverified |
| W05 回调卸载 | 按键同时触发 Mod reload；用计数 fixture 重复100次 start/register/dispatch/unregister/stop；callback 内注册新 callback | 每实例注册/释放成对；最后 release 早于 DLL unload；新实例只有一份回调；不能仅看进程没有崩溃 | unverified |
| W06 日志 | callback 中递归日志、换 sink、关闭 sink；两个线程写含中文/非BMP/内嵌NUL消息；关闭后再写 | 与既有 sink 的换行/长度规则对照；无死锁/截断差异；旧 sink release 一次且不再调用 | unverified |
| W07 生命周期 | 首次启动记录 start/UI/Unreal/program/update；分别在 program_started=false/true 与 Unreal ready=false/true 路径发重载；重复停止 | 状态相关通知顺序与 host sequence 一致；事件线程不误标 game thread；停止后无 update/queued callback | unverified |
| W08 Lua shutdown | Lua A 注册 EndPlay、async、游戏线程 action；Lua B持续 LoopAsync；重启A后触发Actor EndPlay，再卸载A，B继续执行 | A旧state不再访问；B不因全局Lua锁关闭而失败；one-shot引用可回收；错误必须带dump与回调来源 | unverified |
| W09 JS timers | setTimeout(-1)、setInterval(0)；timer callback 中创建大量timer、取消自己与另一个due timer；抛错后再安排timer | timeout非负、interval最小10ms；无vector指针失效；取消语义与错误后续处理一致 | unverified |
| W10 文件/网络 | readFile缺文件；writeFile中文+NUL并读回；拒绝写入路径；受控HTTP服务依次返回200/404/无效JSON/0字节/延迟响应/SSE多chunk | 参数TypeError与null/false不同；fetchSync与fetch不同；普通download失败resolve ok=false；临时文件/字节数；卸载在途请求的实际延迟 | unverified |
| W11 游戏线程/借用 | 原同步hook改OutParam；默认deferred hook尝试观察；UMG dispatch超过15秒后恢复游戏tick；存ParamRef至callback之后 | 同步写入可见；deferred不承诺改原参数；记录超时后迟到副作用；不得读失效参数栈 | unverified |
| W12 对象/扫描 | Actor创建销毁/对象索引重用；热载class；packed bool读写；default/custom/module override逐项切换 | stale UObject不冒充新对象；override来源可辨；只改目标bool位；未知布局拒绝而不是系统allocator兜底 | unverified |
| W13 Proxy导出 | 对照生成def逐项比较名字/ordinal（含ordinalN别名），再对目标系统DLL检查跳板目的；LoadLibrary后立即调用无副作用导出；受控原DLL缺一个符号 | 无额外DllMain/mProcs导出；目标在attach返回前齐全；缺符号拒绝attach，无null jump；不是只比较count | unverified |
| W14 Proxy路径/退出 | 默认、override后旁置与裸名fallback；空文件/空首行/CRLF/^Z/BOM/空白/引号；CRT UTF-8及ACP/OEM；相对、C:relative、异盘D:relative、根相对、UNC/verbatim；无效编码；worker创建失败；立即卸载/进程退出 | override用UE4SS.dll；fallback用UE4SSL.dll；不normalize/trim；无效编码新行为debug-log并跳过；核心异步就绪；缺失分支拒绝或弹错+ExitProcess(0)；worker引用保活；reserved退出不重复FreeLibrary | unverified |
| W15 停机竞态 | callback等待可控事件，另一线程发stop；释放事件；loader-lock外ue4ssl_shutdown后FreeLibrary调用者引用；重复shutdown/进程退出；受控IAT线程取目标后暂停、shutdown/unhook后再进入 | Mod/VM/队列资源drain，不等于core卸载；**PIN core在shutdown/FreeLibrary后仍映射，至进程退出才释放**；迟到IAT调用original非null；inflight计数不单独证明可unmap；detach不join；actor/worker不是游戏线程 | unverified |

## 每个公开 API 的参数/返回/错误回归方法

`script-api-baseline.md` 列出每个公开注册和 operator 的源位置、参数读取/校验、返回栈/JS对象构造与错误分支；它是 W03/W08–W12 的逐 API 子矩阵，不以几个代表 API 代替完整清单。

每个条目至少执行：最小合法参数一次；每个文档 overload 一次；少一个必需参数一次；错类型一次（仅源码确实校验的分支）；找不到对象/文件等 sentinel 分支一次。异步 API 追加正常完成、回调错误、完成前卸载；可变参数/属性 API 使用实际游戏反射签名并保存签名及参数hash。不要对读任意地址、PatchByte、PlaySoundSync 等接口生成无约束随机调用；仅用自己分配的受控内存/受控文件。错误类别和返回类型是判定项；对没有运行证据的实现疑点登记 defect，不自动把源码推断提升为 pass。

## 结果升级规则

1. SHA/PE检查通过只能记录 artifact-inspected。
2. Win64 host fixture 跑通只能记录 native-smoke-passed，不能填写游戏通过。
3. 一组真实游戏 W01–W15 通过，只覆盖该组 EXE/hash/版本/配置/Mods；不外推其他 minor、packing、modular 游戏。
4. 失败保留原始日志、dump、复现脚本和包hash，关联缺陷ID。预期因迁移而修正的差异必须在缺陷表的 migration 列有解释；不能无说明修改基线 expected。

## 本轮已执行证据（不升级游戏矩阵）

机器为 macOS arm64；以下是交叉构建和宿主行为验证，不是 Windows 游戏执行结果。候选 DLL/PDB 的 SHA-256、实际 PE 导出和内容寻址快照见 `migration-validation.json`。

- `xtask build --target x86_64-pc-windows-msvc --mod CPP_MeowChat` 完整流水线通过；核心先生成当前 import library，再编译原生 Mod。
- `cargo xwin build -p ue4ssl-lua -p ue4ssl-javascript --target x86_64-pc-windows-msvc` 通过，两个脚本 DLL 已链接迁移后的核心 SDK。
- Support 原实现的四个宿主回归通过：输入组合/重复/焦点状态、自注销与延迟释放、日志 callback 内关闭、持久文件写入及关闭错误。
- `cargo test -p ue4ssl-runtime --lib --target aarch64-apple-darwin`：11 通过。运行的是原 actor；native gates 为测试适配，非 Windows DLL loader 明确拒绝调用，不能据此声称 Windows 加载通过。
- 额外并发日志 smoke 通过：callback 被可控屏障阻塞时另一线程关闭日志组；关闭等待 callback 返回，context 恰好释放一次。
- `cargo xwin test -p ue4ssl-runtime -p ue4ssl-hook --target x86_64-pc-windows-msvc --no-run` 通过。包括实际 Win32 IAT/源模块引用回归的 exe **只编译、未执行**。
- 核心 PE 有 3899 个导出；冻结核心的非 C++ 修饰 C 导出无缺失；不再额外导出 `DllMain`。这不能证明 C++ 对象布局兼容。
- Proxy 使用受控 PE 输入完成构建，实际导出精确为 `Probe@7`、`ordinal9@9`、`Forwarded@11`，无 `DllMain`/`mProcs` 额外导出；覆盖名字、ordinal 空洞及输入 forwarder。没有运行跳板，也没有替代真实系统 DLL 的验证。

原冻结产物缺少 Lua/JavaScript DLL；本轮新构建候选不能冒充旧版可运行基线。当前没有可用 Windows runner/游戏；本机 Wine 未安装，Homebrew Wine cask 因 Gatekeeper 检查被禁用，未绕过安全策略安装。因此 W01–W15 保持 unverified，真实 DLL 加载/停机、SEH 故障注入和游戏矩阵仍需要 Windows 环境。
