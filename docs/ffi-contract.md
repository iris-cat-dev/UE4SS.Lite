# FFI / 回调所有权契约

## 单一来源与版本

冻结 v1 来源是提交 `fff1f21dce7f671c8736a7907ff41ccb9c2faf32` 的 `crates/ue4ssl-abi/src/lib.rs`。所有新增 ABI 类型/函数指针进入该 crate，再生成 C++ 头；不能在 HostShim、Input、Runtime 各定义一份不同结构。本文不定义 Unreal 布局、VM 内部值布局或新的平行 Mod ABI。

- `#[repr(C)]`、字段顺序、宽度、对齐、符号名、调用约定构成 v1；现有结构不能追加字段、换 bool 宽度、将 opaque pointer 换 Rust reference，或把 `usize` 当固定 32 位。Windows x64 上 usize/uintptr_t/指针为 64 位；仍须目标端生成/编译 layout static_assert，不能用 macOS ARM ABI 检查代替。
- `SliceU16 { const u16* data; usize len; }`、`OwnedString { u16* data; usize len; }` 均为 pointer + UTF-16 code-unit 数，非字节数，非默认 NUL 结尾。两者在 Win64 大小 16、对齐 8。`RustModStartContext` 只有 `mod_path: SliceU16`，不能塞 host vtable 改变旧布局。
- `HostVector` 是三个 f64；`HostHookHandle` 是 opaque function pointer + i32 pre_id + i32 post_id；`HostHookContext` 只有 opaque context pointer。不能将其布局当作 UObject/FVector/FFrame。
- 新能力用新增符号或独立的新版本结构；旧 v1 调用方仍能加载。缺能力明确返回失败，不创建“成功的空句柄”。动态加载 v1 优先规则见基线；不以旧 C++ 路径静默替代不完整 Rust v1。
- `render_rustcore_header`、`render_scan_header`、host header 中已有 size/align/offsetof assertions 是生成契约；Rust/C++ 都从同一来源消费。修改者必须同步每个调用点，不保留长期双实现/别名。

### 本轮核心 DLL 卸载契约变更

Main最终报告：核心 DllMain 启动专用 Rust worker，不再探测主线程/APC。安装DLL通知前，**core模块PIN至进程退出**：外部线程可能已从IAT取出目标、尚未进入callback，inflight计数无法覆盖该窗口，因此不支持运行期物理卸载core。worker持有的普通引用在 `FreeLibraryAndExitThread` 释放不会解除PIN。`ue4ssl_shutdown` 在loader lock外完成Mod/VM/队列资源shutdown；其后 `FreeLibrary` 仅释放调用者引用，**不使core unmap**。detach仅atomic request_shutdown，不join/清理。不得将shutdown或detach返回描述为core卸载完成；这是显式安全取舍，不是冻结v1原本的保证。Windows执行仍unverified。

RuntimeOwner报告：保留的 `ue4ssl_core_cppmod_*` 导出现在使用 opaque actor ID token，不能将句柄当内存地址解引用、`Box::from_raw` 或自行free；下文 Box所有权行描述的是冻结基线，不是迁移后实现。外部非callback线程请求enqueue+wait；actor/input callback中的请求延后至当前callback返回，以避免自卸载。runtime尚未接受请求时raw create返回null；`find_mod`返回借用SDK view，在reload/shutdown后失效。native game hooks及Mod自建线程仍是Mod卸载义务，owned输入/事件静默不等于所有外部线程都安全。

IAT backend迁移修正：original函数地址在安装通知前通过atomic提前发布，unhook后不再清空original，避免回调读取null original的竞态。此举与core进程期PIN共同处理迟到调用；单靠inflight==0不构成可解除映射的证明。

## 借用 / 转移 / 释放表

| 数据或入口 | 输入/输出与 owner | 释放及有效期 | 线程与错误边界 |
|---|---|---|---|
| SliceU16 | 调用方借出连续可读 UTF-16，callee 只在调用期间读取；需要保存须复制 | callee 不 free；空可用 `{null,0}`；非空必须真实有效，长度不能越界 | ABI 无法探测任意野指针安全性；catch_unwind 不验证地址。不得把它标成任意 Rust `&str`。 |
| OwnedString | Rust 返回已分配 UTF-16 数组，调用方接收唯一释放责任 | `ue4ssl_core_free_string` 回 Rust；不能 C++ delete/free、不能构造 Vec 使用虚构 capacity；free 后原 data/len 失效 | 冻结实现由 Box slice 分配/回收，见 `core/wide.rs:31–63`。C ABI 按值释放不会自动清零调用方副本，禁止再释放副本。 |
| PathSnapshot | Rust 分配九个 OwnedString；调用方借读或整体接管 | `ue4ssl_core_free_path_snapshot` 一次释放所有字段；不得单独 free 字段后再整体 free | `core/mod.rs:248–261`；跨线程传快照须转移唯一 owner 或复制。 |
| ModDiscovery | Rust 分配 DiscoveredMod 数组及每项三个字符串 | `ue4ssl_core_free_mod_discovery` 释放嵌套字段和数组；遍历 view 在释放后无效 | `core/mod.rs:263–280`；不能只 free 外层或复制结构后重复释放。 |
| IniHandle / CppModHandle | Rust Box 经 opaque pointer 转移给调用方 | 匹配 `ue4ssl_core_ini_destroy` / `ue4ssl_core_cppmod_destroy`；句柄不可重建/重复销毁 | handle 方法无自动通用线程安全承诺；owner 生命周期线程串行调用。 |
| RustModStartContext.mod_path | loader 借出启动路径；start 返回实例 owned by Mod | start 返回前要持久使用路径则复制；实例只交该 Mod 的 uninstall；不能 host free | `cpp_mod.rs:206–230` 及 start helper；uninstall 完成且所有回调静默后才 FreeLibrary。 |
| CppUserModBase* | Mod DLL new，host 仅通过旧兼容壳调用 | Mod uninstall/delete；host 不能 Rust Box::from_raw 或跨 CRT delete | MSVC C++ ABI、exception/SEH 薄门仍必要。extern C 不会抹去虚表 ABI。 |
| PsCtx / PsLogFn | config 按值，函数指针由 caller 提供；log 参数为 NUL 结尾 UTF-16（此处不同于 SliceU16） | callbacks 必须在扫描调用/其工作完成期间有效；日志如异步保留应复制文本 | scanner 的调用线程不是游戏线程保证；log adapter 必须可重入且不可反向抛异常。 |
| PsScanResults | 数字地址与版本按值返回，未转移引擎对象 | 不 free 地址；只在对应模块、引擎阶段和布局有效时使用 | 发现地址不等于验证布局/签名；未知版本不能靠地址非零就放行。 |
| ps_scan_aob | 输入 pattern/region 借用；callback(address,pattern_len,userdata) 非零终止 | 调用结束不保留 userdata；无输入→0；坏 pattern/无 callback/panic→usize::MAX | callback 不得 unwind；wide-string 搜索返回地址或 0。`patternsleuth-bind/src/lib.rs:235–389`。 |
| HostHookHandle / Context | function/context 是引擎/adapter opaque 借用；ID 是注销凭据，不是 allocator 指针 | 只通过配套 unregister 释放注册；参数借用最多到该次同步 callback 返回 | 同步 OutParam 才允许修改；deferred callback 必须使用独立快照，不能留下 ParamRef/raw frame 地址。 |
| JSValue / Lua ref | VM retain/registry ref 保活，不能作为普通 Rust owned value 任意跨线程 | 对应 JS_DupValue/JS_FreeValue 或 Lua ref/unref；必须先停止来源再关闭 VM | JS event-loop owner；force_sync hook 例外仍遵从现有 VM 锁。Lua coroutine 同属一个 VM，不是可并发随意调用的独立 state。 |
| Unreal FString/TArray/FText | 引擎布局/allocator/非 POD 析构规则仍由 native seam 处理 | 引擎 FMemory 分配必须回引擎释放；Rust String/Vec 不是可替换布局 | GMalloc 未就绪不准系统 malloc 兜底；C++ exception、SEH 由引擎调用门隔离。 |

## 步骤 2–3 的新回调所有权要求（不是旧实现已保证的行为）

InputOwner/RuntimeOwner/Main 的实施契约是：handle 用 `uint64_t`，owner 用不解引用的 `uintptr_t` 身份，callback 为 `callback(ctx)`，释放函数为 `release(ctx)`；最终声明以 `ue4ssl-abi` 为准。owner token 不应在旧 owner 未完全销毁时重用。

1. InputOwner确认：新增 owned `register_keydown_event_v2` 仅成功取得非零 handle 后 Rust 接管 ctx；失败 caller 仍持有并释放。旧 native v1 注册入口保留签名、只借用 ctx，并转发同一 Rust registry。不得一边 adapter 失败时 free，一边 Rust 也调用 release。
2. 注册成功后 Rust 从注册到注销/销毁拥有一份 ctx 的释放责任；任何 caller 不得复制注册结构后另行释放。release 只能调用一次，包括从未触发、回调抛错、owner 清理、重复注销和 handler 销毁路径。
3. 遍历先 pin 待执行注册，再解锁调用 callback；不能持 registry 锁执行用户代码或 release。回调中注册新回调不使当前遍历裸指针失效。
4. 注销立即阻止新的 dispatch；批量注销先取消所有目标，再等待其他线程在途 callback 和 finalizer。自注销不等待自己，延迟 release 到 callback 返回；注销另一个尚未开始的排队回调应阻止其执行。
5. owner 注销是外部 Mod teardown 的屏障；DLL/VM 释放不能早于最后 callback/release 完成。当前 callback 内触发所属 owner 销毁仍必须延后完整 teardown，不能把“已标记取消”当作可在自己调用栈尚在时 FreeLibrary。
6. InputOwner确认：无在途时 release 在注销线程执行；有在途时在执行 callback 的线程退出后释放。release 回到拥有 allocator/VM ref 的适配层；涉及 VM 时，adapter 必须处理 owner 线程交接或停机静默条件，不能假定 Rust registry 自带 JS/Lua GC 线程安全。
7. 输入事件线程仍不是 Unreal game thread；用户 callback 需要游戏线程时使用已有 dispatcher，不要把事件循环当游戏线程调用引擎。
8. 日志 sink 接收的文本默认只借用到调用返回；异步日志队列必须拥有独立文本。sink callback/release 同样不能在全局 registry 锁下执行；更换/关闭 sink 不能留下已卸载 DLL 的函数指针。

InputOwner最终实现报告补充（未运行验证）：日志组、设备callback owner、持久File句柄、Console VT模式共享租约及最后设备恢复由Rust负责；C++仅保留format/std::function/虚拟设备适配。空或已关闭日志组send返回0，C++抛既有internal_error；析构flush/restore失败不抛，设置统一atomic has_internal_error。C++ exception catch与SEH leaf在native边界内处理，不跨Rust帧。

输入与VM关闭顺序：Lua先停async，再cancel/drain input，再取Lua锁关闭VM；input registry ref由std::function捕获的RAII owner释放。JS先cancel/drain input，再取得既有m_js_mutex等待已出队keybind，清pending后释放JSValue。此处明确adapter的保活/静默策略，不将“任意线程release”泛化成任意线程可直接调用VM。

**C++ SDK 非等价变化：** 删除公开可变 `get_events` / `get_all_input_events` 容器路径并迁移仓库调用方，不能承诺原 C++ SDK binary/layout 兼容；旧第三方 C++ 消费者需重编/迁移。Rust Mod v1 结构及入口保持。删除双权威容器不应通过假容器/空适配器伪装旧语义。

### 必跑所有权轨迹

- register → dispatch → unregister → release，释放计数 1；再次 unregister 不增加。
- callback 内 self-unregister → callback 正常结束 → release，不死锁、不提前 free。
- A callback 注销 B，B 尚未开始则本轮不执行；A 创建 C 不访问失效 vector 地址。
- 两线程同时 dispatch/unregister：注销完成语义与在途屏障相符，DLL unload 之后 callback/release 计数不再增加。
- handler/owner 销毁无 dispatch 的注册仍 release；拒绝注册由指定失败 owner 释放一次。

这些是 Windows/受控 native smoke 的验收轨迹；本文没有声称已经运行。

## panic、C++ exception、SEH、VM 错误不能互相替代

| 错误 | 正确隔离位置 | 不成立的替代 |
|---|---|---|
| Rust unwind panic | 每个导出 C ABI 和 Rust callback trampoline 在 Rust 栈内 catch_unwind，转换为契约的失败值；panic=abort 无法恢复 | 不能让 panic 穿过 extern C；catch_unwind 不拦截访问违例/abort，不保证内存错误后的进程可继续。 |
| C++ exception | native adapter 内 try/catch 转明确状态；不得跨 Rust frame | Rust panic hook 不是 C++ exception handler；`noexcept` 终止不等于返回失败。 |
| Windows SEH | 精确的 MSVC native 引擎调用门/已有 SEH filter | POSIX 运行通过或 Rust Result 不能证明 SEH 等价；不能删 SEH 门后声称仍能处理 stale UObject。 |
| Lua error | VM 的受保护 C 调用（pcall 等）内消费 luaL_error/longjmp，再向 Rust 返回状态 | 禁止 longjmp 越过持有 Rust Drop/锁 guard 的栈；不能从 Rust 直接把 Lua 报错当普通 exception。 |
| QuickJS exception | `JS_EXCEPTION` 配合 ctx 的 pending exception，在 owner 线程取出/释放；Promise rejection 分开记录 | false/null/undefined 不是所有函数的异常；不得吞掉 pending exception 再伪报成功。 |

## 兼容证据最低标准

新旧 C/C++ 消费者都以同一生成头检查 Win64 size/align/offset；旧 v1 DLL 不重编译加载一次；带两套入口的 fixture 只启动一套；缺半套 Rust 符号必须拒绝；所有上述回调轨迹有计数/线程/退出证据。构建日志、PE 导出和 host smoke 各证明不同层面，任何一项都不能升级成游戏验证。
