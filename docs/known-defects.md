# 已知缺陷、疑似风险与迁移差异

## 证据分级

- **source-observed**：冻结提交 `fff1f21dce7f671c8736a7907ff41ccb9c2faf32` 可直接看到代码结构/缺项；不代表触发后的结果已复现。
- **suspected / [INFERENCE]**：由代码推导的故障路径，需要运行复现；不得写成已经发生的崩溃。
- **reproduced**：必须关联游戏/host命令、候选hash、操作、原始日志或dump；本 BaselineOwner 取证中**没有运行复现条目**。
- **migration-change**：本轮实施者报告的改动，与基线隔离；“已编辑”不等于已构建/已游戏验证。最终验证证据由 Main 统一补充。

## 冻结基线缺陷 / 能力限制

| ID | 源码证据与风险 | 证据等级 | 最短复现方向 / 观察 | 迁移处理 |
|---|---|---|---|---|
| D01 JS timer元素失效 | `crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:13–29,83–124` 存vector元素指针，解锁调用JS，再写旧指针；callback新增timer可重分配 | source-observed；UAF suspected | W09：due callback中新建足量timer触发扩容，开启ASan可用时或PageHeap/dump观察旧指针访问 | 本轮不迁timer，不标已修复 |
| D02 Lua EndPlay清理缺项 | `LuaMod.cpp:2757–2788,5597–5614,6016–6051` 注册EndPlay callbacks，uninstall清单未列对应容器 | source-observed；旧state访问 suspected | W08：A注册EndPlay→重启A→销毁原Actor；保留旧/新state ID和dump | 不把迁移input owner清理误作所有Unreal hooks已修复 |
| D03 Lua全局锁寿命 | `crates/ue4ssl-lua-support/vendor/LuaRaw/src/luauser.c:4–39` 全局CRITICAL_SECTION，任意state关闭DeleteCriticalSection | source-observed；多VM异常 suspected | A/B同时LoopAsync，重启A时B继续调用VM | 本轮保留VM，未修复 |
| D04 Lua引用/全局表增长 | `LuaMod.cpp:3125–3206,6588–6660` async make_ref与完成erase/clear；LuaMadeSimple.cpp:14–19,193–197,1004–1007,1069–1072全局state/function表与非RAII关闭 | [INFERENCE]：本次范围未建立完整匹配unref/erase证明 | 重复one-shot/reload，跟踪registry引用/VM包装对象，不以进程RSS单点推断泄漏 | 未修复/未复现 |
| D05 JS全局callback teardown | `JSHook.cpp:1862–1872,1970–1976,2071–2076`、`JSGameThreadDispatcher.cpp:165–188` 捕获this但局部仅保存bool、不保存所有ID | source-observed；卸载后调用 suspected，是否host兜底需核对 | 独立销毁JS引擎后触发PE/watch/UMG；不只测进程退出 | input清理仅覆盖输入，不可宣称覆盖这些hooks |
| D06 JS输入raw owner | `JSHook.cpp:2101–2170` 捕获KeyBindCallback*/this；`JSMod.cpp:368–376` 清bindings | source-observed；局部stop与来源注销顺序风险 | W04/W05输入到达并发stop；统计callback/release | M-INPUT迁移，运行结论待Main |
| D07 阻塞网络停机 | `JSMod.cpp:393–455` stop后join worker；`JSFetch.cpp:41,85–94,233–255` read可阻塞；下载同类 | source-observed；关闭长等待 suspected | 控制HTTP服务发送header后停住body，再stop，记录延迟上界及线程栈 | 本轮不重写WinHTTP，不保证立即取消 |
| D08 UMG超时迟到执行/锁反转 | `JSGameThreadDispatcher.cpp:24–43,785–805` 15秒超时不移除队列且保留params；force_sync hook取JS锁 | source-observed迟到任务保活；deadlock suspected | W11阻塞game tick至超时，再恢复；同时触发force_sync观察两个线程栈 | 未修复，不能把超时返回null当取消成功 |
| D09 JS对象/参数borrow | `JSType/JSUObject.cpp:21–65,240–272,318–345` 裸UObject/逐wrapper root；`JSPropertyUtils.cpp:160–278` ParamRef借FProperty及参数地址 | source-observed；GC身份/root误释放/越期访问 suspected | 两wrapper同对象，分别Add/RemoveRoot；GC后IsValid；callback后使用保存ParamRef | 未迁引擎布局/VM，不标修复 |
| D10 TLS验证弱化 | `JSFetch.cpp:50–54`、`JSAudio.cpp:254–259` 忽略部分证书错误 | source-observed安全行为；非运行复现 | 隔离测试服务使用不受信/名字不匹配证书，记录成功/失败分类 | 修复需显式行为/安全变更，不能夹入等价迁移 |
| D11 MeowChat FString旧buffer | `Mods/CPP_MeowChat/native/cpp/main.cpp:439–460` 分配新FMemory后覆盖Data/Num/Max，函数内未释放旧Data | source-observed；泄漏/所有权约定未证实 | 连续改消息并跟踪引擎allocator；不能盲目加free | 本轮不迁该业务，不修改引擎字符串所有权 |
| D12 MeowChat PE回调 | 同文件 `835–854` 析构注销三个UFunction hooks；on_program_start两个PE回调未持有ID | source-observed；DLL卸载后失效代码 suspected | 卸载Mod后再次PE；验证host是否统一注销来源 | 不因native Mod v1存在就判修复 |
| D13 版本/packing能力 | `Unreal/.../Container.cpp:40–188`容器范围与Rust resolver不同；`UObjectArray.cpp:60–68`packed分支未实现；`UStruct.cpp:212–219`MinAlignment统一取506；NameTypes.hpp:377–397已有限制说明 | source-observed能力限制；具体游戏错误未复现 | 版本矩阵边界4.24/4.25、5.0/5.1、5.5/5.6、5.6/5.7；记录真实packing | 本轮明确不迁Unreal布局，不扩写支持声明 |
| D14 扫描override gate | `UnrealInitializer.cpp:384–391,422–430`与`Signatures.cpp:58–60,135–146` gate不包含部分仅单项override；必需resolver失败可先超时到不了fallback | source-observed条件差异；失效行为 [INFERENCE] | 仅FMemory Free、仅ProcessInternal等逐项override；记录是否进入阶段和实际地址来源 | 未修复，默认扫描通过不能覆盖 |
| D15 Lua比较返回栈 | `LuaFName.cpp:104–113,139–150` 比较结果直接作为C函数返回count，未明显push bool；ThreadId等operator需同样检查 | source-observed；脚本结果错误 suspected | pcall包装FName相等/不等、Equals，记录返回个数和类型而非仅truthiness | 未修复；API附录保留实际语句，不宣称返回bool正常 |
| D16 JS写入成功判定 | `JSFileIO.cpp:97–108` write/close后返回true，未显式检查stream状态 | source-observed；部分写入仍true suspected | 受控磁盘写入失败/空间耗尽，不影响用户盘；比对返回与文件内容hash | 未修复，不将true描述为持久化保证 |
| D17 核心可变输入容器双权威 | 旧C++ get_events/get_all_input_events公开可变容器，Rust状态无法独占注册/销毁 | source-observed结构限制 | owner清理同时外部直接erase容器；旧路径无法统一release | M-INPUT显式移除，不保留假兼容容器 |

## 本轮迁移修复 / 非等价变更（独立记录）

以下来自对应实施者消息，不能覆盖上表的冻结证据或冒充运行结果。

| ID | 实施者报告的变更 | 兼容影响 | 证据/验证状态 |
|---|---|---|---|
| M-INPUT | Rust registry唯一输入状态；新增owned `register_keydown_event_v2` 成功非零handle转移ctx，失败caller释放。callback/release在锁外；批量注销先取消，再等待其他线程在途callback和finalizer；自注销延迟到callback返回释放 | 旧native v1 register签名保留且只借ctx；转发同一服务。新增owner句柄不是另一套容器 | InputOwner报告；最终构建/smoke由Main，本文unverified |
| M-SDK | 删除可变 `get_events` / `get_all_input_events` C++ API并迁移全部repo调用方，移除双权威容器 | **不能承诺原C++ SDK二进制布局兼容**；依赖旧SDK的第三方C++消费者需重编/迁移。Rust Mod v1结构与入口保持。不是“完全等价的语言迁移” | Main/InputOwner明确报告；旧第三方DLL不能因两入口名字仍存在就被标兼容 |
| M-LUA-INPUT | 修正Lua输入注册local lambda悬挂capture及modifier table边界访问 | 是缺陷修复，不是冻结基线原本安全；合法输入语义须保持，原越界行为不保留 | InputOwner报告；未在本Baseliner中运行复现 |
| M-LOG-OWNER | 日志组/设备callback owner、持久File句柄、Console VT共享租约和最后设备恢复移入Rust；C++保留format/stdfunction/虚拟设备薄适配；native catch/SEH leaf不跨Rust帧 | 空/关闭组send返回0，由C++抛既有internal_error；析构flush/restore失败不抛，置统一atomic has_internal_error | InputOwner最终报告；构建/运行由Main统一验证，本表unverified |
| M-VM-INPUT-STOP | Lua停async→cancel/drain input→取Lua锁→close VM，RAII捕获释放input registry refs；JS cancel/drain input→取m_js_mutex等待已出队keybind→清pending→释放JSValue | 只确认owned输入来源与VM teardown的顺序，不宣称D02–D05的所有非输入callback问题同时修复 | InputOwner报告；W05/W08/W15待Windows执行 |
| M-PROXY | 原DLL加载/全部导出解析仍在attach同步完成；缺任一符号拒绝attach，防null jump。核心/override文件读取移worker，DllMain不等待。worker持proxy额外引用，结束FreeLibraryAndExitThread | **核心不再保证attach返回前就绪**。同步原DLL LoadLibrary仍留loader-lock限制，不应宣称全部loader-lock问题解决。原DLL/符号/worker创建失败拒绝attach；核心全路径失败仍弹错+ExitProcess(0) | ProxyOwner报告；W13/W14需Windows PE/路径/加载执行，unverified |
| M-PROXY-PATH | 首行按MSVC text-mode处理CRLF/^Z；使用CRT locale UTF-8或Windows ACP/OEM native编码；空文件无override，空首行指proxy目录；不trim空白/引号/BOM；保持MSVC相对/drive-relative/根相对/UNC/verbatim拼接。override下UE4SS.dll→旁置ue4ss/UE4SSL.dll→裸UE4SSL.dll | 无效native编码新行为OutputDebugString并跳过override，旧filesystem可能抛异常；这是显式失败行为修正，不是完全等价。进程退出reserved!=null不再手动FreeLibrary；显式卸载仍有loader-lock限制 | ProxyOwner报告；路径/退出未实测 |
| M-RUNTIME | C++ Program的m_mods/event queue/flags/event thread移除；Rust actor唯一owner；保持JS→Lua→普通Mod，actor不是game thread；保留ue4ssl_core_cppmod_*符号但opaque handle改actor ID token | 外部非callback线程enqueue+wait；actor/input callback内请求延到callback返回，避免self-unload；runtime未接受请求时raw create返回null；find_mod借用view在reload/shutdown失效。不是所有旧DLL自动binary兼容 | RuntimeOwner报告；owned input/events在FreeLibrary前静默；native game hooks/Mod自建线程仍需Mod负责注销/停止；游戏验证unverified |
| M-CORE-ENTRY | 核心启动Rust dedicated worker，不再main thread/APC；安装DLL通知前将core PIN至进程退出；detach仅atomic request_shutdown | IAT目标已被外部线程取出但尚未进入callback的窗口不受inflight计数保护，因此**不支持运行期物理卸载core**。loader-lock外ue4ssl_shutdown仅完成Mod/VM/队列资源shutdown，FreeLibrary调用者引用不导致core unmap；worker释放普通引用不解除PIN | Main最终报告；显式安全取舍，W15 Windows执行unverified |
| M-IAT-ORIGINAL | IAT backend original目标提前atomic发布，unhook不再清空original | 消除callback读取null original的竞态；不能据inflight==0断言所有已取目标线程完成，仍须core PIN | Main报告的迁移修正；不宣称本Baseliner运行复现 |
| M-IAT-SOURCE | 查找/读取源模块 IAT 前取得模块引用；命中后引用保留至成功 unhook；IAT 槽以对齐原子写更新；恢复失败保留源模块引用 | 源 DLL 不会在仍持有 thunk 地址时先卸载；释放模块引用可能执行 DllMain，因此通知管理锁外执行 unhook | Windows 回归 exe 已编译，未执行；见验证矩阵末节 |
| M-BUILD | 原生 Mod build helper 对齐现有 common API：whole_archive_flag 已返回 String，version_defines 返回 name/value 元组 | 修复阻塞本轮 SDK 重编的 helper 调用不匹配，不改变 Mod 业务逻辑 | MeowChat 原生 Mod 已通过 x64 Windows 交叉构建 |

### 输入释放线程的具体约束

InputOwner确认：无在途callback时，release在注销线程执行；有在途时，最后一个执行callback的线程在退出后执行release。当前callback自注销不等待自身。因而VM引用的release adapter必须兼容该调用线程或进行受控owner线程交接；不能仅凭“Rust持有ctx”推断JS/Lua GC线程安全。owner外部销毁等待其他线程在途工作；在自身callback内申请销毁所属DLL/VM仍需要延后完整teardown，不能在回调栈尚在时FreeLibrary。

Proxy新增验证还包括：导出名字/ordinal须完全等于生成def（包括ordinalN别名），不能额外导出DllMain/mProcs；CRT locale UTF-8不应被误写成总是ANSI。路径不能用Rust normalize抹掉MSVC拼接差别。

本轮已执行结果统一记录于 `windows-validation-matrix.md` 末节和 `migration-validation.json`。其中宿主回归、交叉构建、PE 导出检查各有明确范围；上表涉及真实游戏、VM hooks、SEH 或 Windows 加载的条目未因构建成功自动升级为运行验证通过。

## 新的复现记录如何添加

必须关联 Windows 矩阵 run_id 与 manifest；写明 baseline/candidate hash、触发操作、实际返回/错误/线程、日志/dump。先标 reproduced，再标 fixed-with-evidence；只有编辑/编译证据的条目保持 implemented-unverified。针对旧风险的修复验收同时包括“原触发不再发生”和相邻正常契约，不能只把异常捕获/日志消失视为修复。
