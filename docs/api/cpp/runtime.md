# C++ Mod 与运行时 API

[返回 C++ API](README.md) · [输入/日志](platform.md) · [Unreal](unreal.md)

主要头文件：[CppUserModBase.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/Mod/CppUserModBase.hpp)、[UE4SSProgram.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/UE4SSProgram.hpp)。命名空间 `RC`。下文“入/出/入出”指调用方向；无显式返回的回调均返回 `void`。

## 1. DLL 入口与元数据

```cpp
extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod();
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod);
```

| 入口 | 输入 | 输出/所有权 | 功能与失败 |
|---|---|---|---|
| `start_mod()` | 无 | 新创建的 Mod 实例；空指针表示未启动 | 宿主加载 DLL 后调用；对象由该 DLL 管理 |
| `uninstall_mod(mod)` | 入：先前返回的实例 | 无 | 先释放自有回调/线程/资源，再删除实例；宿主随后可释放 DLL |

加载器默认读取 Mod 目录下的 `main.dll`。这两个导出必须成对存在；若同时提供 Rust v1 生命周期入口，加载器优先选择 Rust v1，并拒绝不完整的 Rust 入口组合，不能指望退回 C++。实现：[cpp_mod.rs](../../../crates/ue4ssl-runtime/src/core/cpp_mod.rs)。

继承 `CppUserModBase`，在构造函数中设置下面的公开 `StringType` 字段：

| 字段 | 功能/默认 |
|---|---|
| `ModName` | 显示名，初始空；不是更改部署目录的接口 |
| `ModVersion` | Mod 自身版本，初始空 |
| `ModDescription` | 描述，初始空 |
| `ModAuthors` | 作者，初始空 |
| `ModIntendedSDKVersion` | 基类构造函数设为编译时 `UE4SS_LIB_VERSION_MAJOR.MINOR.HOTFIX`，派生类可覆盖 |

`ModMetadata` 是含同名五个 `const StringType` 字段的值类型，不是 `start_mod` 的返回类型。字符串使用 `STR("...")`，见[平台类型](platform.md)。

## 2. 生命周期回调

| 可覆盖方法 | 输入/输出 | 实际调用时机/限制 |
|---|---|---|
| 构造函数/析构函数 | 无；无返回 | 构造时不应使用未就绪的 Unreal；析构前必须解除自有外部回调 |
| `on_ui_init()` | 无；void | 当前 runtime 首次启动在引擎准备前广播；不代表可用 ImGui 渲染上下文 |
| `on_unreal_init()` | 无；void | 引擎适配准备成功后；此前不要使用 Unreal API；不代表所有地图资产已加载 |
| `on_program_start()` | 无；void | 当前实现是在 `on_unreal_init` 之后、进入常规更新之前；不要套用旧注释中的顺序 |
| `on_update()` | 无；void | runtime 循环调用，不是 UE 帧率回调，不是游戏线程；过慢更新可能被 guard 禁用 |
| `on_dll_load(StringViewType dll_name)` | 入：借用的 DLL 名称；void | runtime 分发 DLL 加载通知时；需保存则复制，勿留 view |
| `render_tab()` | 无；void | 保留虚方法；当前 runtime 未提供对应 UI 渲染分发契约 |

首次启动顺序：加载 DLL → `start_mod` → `on_ui_init` → 准备引擎 → `on_unreal_init` → program ready → `on_program_start` → `on_update`。重新安装时重建 Mod 并补发 Unreal/program 回调；当前 reload 路径不重新广播 UI 回调。源码依据：[state.rs 的 Runtime::run](../../../crates/ue4ssl-runtime/src/core/state.rs)。

`on_ui_init` 注释仍提及 `UE4SS_ENABLE_IMGUI`，但该宏在当前 `UE4SSProgram.hpp` 中整段注释，`register_tab` 也未启用；不要将它们当作可用 UI API。

### 基类保护成员：按键

```cpp
void register_keydown_event(Input::Key key,
    const Input::EventCallbackCallable& callback, uint8_t custom_data = 0);
void register_keydown_event(Input::Key key,
    const Input::Handler::ModifierKeyArray& modifiers,
    const Input::EventCallbackCallable& callback, uint8_t custom_data = 0);
```

- 入：`key` 为键码，`modifiers` 为修饰键数组，`callback` 为 `std::function<void()>`；回调无入参、无返回值。
- 出：无。失败可能抛 `std::runtime_error`。注册被绑定到 `reinterpret_cast<uintptr_t>(this)`，析构自动注销。
- `custom_data` 在当前基类实现中被忽略，固定使用 C++ kind；不能通过它传用户数据或区分注销类别。
- 需要单独 handle 的注册/注销请使用[底层 Handler](platform.md)；基类接口不返回 handle。

## 3. `UE4SSProgram` 单例与路径

```cpp
static UE4SSProgram& UE4SSProgram::get_program();
```

无入参；出：借用的运行中单例引用。直接解引用 `s_program`，没有空值保护；不能在宿主未创建单例时调用，也不能销毁它。普通 Mod 不应自行构造 `UE4SSProgram` 或调用 `init()`。

下面方法均无入参，返回独立 `RC::StringType` 值，不需调用方另行释放：

| 方法 | 返回内容 |
|---|---|
| `get_module_directory()` | **当前返回模块文件完整路径**（`m_module_file_path`），不是仅目录；命名有误导性 |
| `get_game_executable_directory()` | 游戏可执行文件所在目录 |
| `get_working_directory()` | UE4SSL 计算的工作目录，不承诺等于进程 CWD |
| `get_mods_directory()` | 解析后的 Mod 根目录 |
| `get_legacy_root_directory()` | 兼容布局根目录 |
| `get_object_dumper_output_directory()` | 对象转储输出目录；签名返回 `const StringType` 值，不是借用引用 |

实现：[UE4SSProgram.cpp](../../../crates/ue4ssl-dll/native/UE4SSL/src/UE4SSProgram.cpp)。

```cpp
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

void LogModRoot()
{
    const auto root = RC::UE4SSProgram::get_program().get_mods_directory();
    RC::Output::send(STR("Mods: {}\n"), root);
}
```

## 4. 事件队列：不是游戏线程调度器

```cpp
void queue_event(void (*callable)(void*), void* data);
bool queue_event_owned(uintptr_t owner, void (*callable)(void*), void* data,
                       void (*release)(void*));
bool is_queue_empty();
bool can_process_events();
bool is_program_started();
void reinstall_mods();
```

| 参数 | 方向 | 约束 |
|---|---|---|
| `owner` | 入 | 不解引用的生命周期身份；不能在旧 owner 仍有在途任务时复用 |
| `callable` | 入 | `void(void*)`，在 runtime actor 线程执行；空值导致拒绝 |
| `data` | 入→转移/借用 | 原样传入 callback/release；生命周期由是否提供 `release` 决定 |
| `release` | 入 | 完成、取消或拒绝时释放 `data`；可为 null；不应抛出异常 |

- `queue_event_owned`：成功入队返回 `true`，停机、reload 关闭入口、owner 退役、空 callback、分配失败返回 `false`。**即使返回 false，非空 `release` 也已经负责释放 `data`，调用方不能再释放。**
- 接受后，正常执行完 callback 或被取消时调用一次 release；release 不保证固定线程。不要将带 VM 线程亲和性的资源直接交给无约束的 release。
- `queue_event`：自动获取当前 runtime/input owner，`release=nullptr`，丢弃入队结果。调用方仍须保证 data 在执行前存活；无法靠此接口回收被取消的堆对象。
- `is_queue_empty`：返回当前请求队列是否空；不表示当前没有执行中的 callback。
- `can_process_events`：读取 runtime `processing_events` 标志；不代表某个 callback 已完成，也不单独体现 paused 状态。
- `is_program_started`：读取启动标志，不是“游戏地图已准备好”。
- `reinstall_mods`：提交全体 Mod 重装请求，无返回状态；当前回调中的重装会延后到回调返回，旧的 Mod view/对象引用随后失效。

下面在 Mod 方法内调用，演示唯一释放责任；任务只记日志，不访问游戏对象：

```cpp
#include <cstdint>
#include <new>
#include <UE4SSProgram.hpp>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

bool QueueLog(RC::CppUserModBase& owner)
{
    auto* value = new (std::nothrow) int(42);
    if (!value) return false;
    return RC::UE4SSProgram::get_program().queue_event_owned(
        reinterpret_cast<uintptr_t>(&owner),
        [](void* p) { RC::Output::send(STR("value={}\n"), *static_cast<int*>(p)); },
        value,
        [](void* p) { delete static_cast<int*>(p); });
}
```

## 5. Program 按键代理

```cpp
void register_keydown_event(Input::Key key, const Input::EventCallbackCallable& callback,
                           uint8_t custom_data = 0, void* custom_data2 = nullptr);
void register_keydown_event(Input::Key key, const Input::Handler::ModifierKeyArray& modifiers,
                           const Input::EventCallbackCallable& callback,
                           uint8_t custom_data = 0, void* custom_data2 = nullptr);
uint64_t register_keydown_event_owned(Input::Key key,
    const Input::EventCallbackCallable& callback, uint8_t kind, uintptr_t owner);
uint64_t register_keydown_event_owned(Input::Key key,
    const Input::Handler::ModifierKeyArray& modifiers,
    const Input::EventCallbackCallable& callback, uint8_t kind, uintptr_t owner);
void unregister_input_owner(uintptr_t owner);
bool is_keydown_event_registered(Input::Key key);
bool is_keydown_event_registered(Input::Key key, const Input::Handler::ModifierKeyArray& modifiers);
```

- 入：键码、修饰键、无参 callback；`custom_data` 是 kind，`custom_data2` 转为 owner，而非 callback 的附加入参。
- 出：owned 注册返回非零 handle；非 owned 版本丢弃 handle；底层失败抛异常。
- `unregister_input_owner` 同时注销该 owner 的输入，**并取消其 runtime 队列事件，使事件 owner 退役**；不能把它当成临时取消一个按键后马上复用同一事件 owner 的接口。
- `is_keydown_event_registered` 返回布尔，只匹配指定组合，不保证 callback 此后仍注册。底层取消单个事件见[平台文档](platform.md)。

## 6. 查询其他 Mod 与 SDK view

头文件：[Mod.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/Mod/Mod.hpp)、[CppMod.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/Mod/CppMod.hpp)。

```cpp
UE4SSProgram::find_mod_by_name<CppMod>(StringViewType name,
    UE4SSProgram::IsInstalled installed = UE4SSProgram::IsInstalled::No,
    UE4SSProgram::IsStarted started = UE4SSProgram::IsStarted::No) -> CppMod*;
// 另有 std::string_view name 重载。
```

入：`name` 是**大小写敏感**逻辑名；`Yes` 要求对应状态为真，`No` 表示不筛选，并非“只找未安装/未启动”。出：借用的 `CppMod*` 或 `nullptr`。reload/卸载后失效；不得 delete。通用模板对未支持的 `T` 调用 `std::abort()`，不要随意换成自己的派生类。

| `Mod`/`CppMod` 方法 | 输入 | 输出/语义 |
|---|---|---|
| `get_name() const` | 无 | 借用的 `StringViewType`，view 销毁后失效 |
| `is_installable/is_installed/is_started() const` | 无 | bool；CppMod 从 Rust runtime 状态查询 |
| `are_updates_disabled() const` | 无 | bool；是否禁用后续 update |
| `set_installable/set_installed/set_updates_disabled(bool)` | 入：新状态 | void；CppMod 提交 runtime action，不是同步直接改 C++ 字段 |
| `start_mod()/uninstall()` | 无 | void；生命周期控制，普通消费者不应手动重复调用 |
| `fire_unreal_init/fire_ui_init/fire_program_start/fire_update()` | 无 | void；宿主分发入口，不是用户手动补发回调的推荐方式 |
| `CppMod::fire_dll_load(StringViewType)` | 入：DLL 名 | void；提交该 Mod 的通知 |
| `Mod::update_async()` | 无 | void；基类当前为空实现，不会自动产生线程 |

## 7. 设置与其他公开头

`UE4SSProgram::settings_manager` 是公开静态 `SettingsManager`。`deserialize(std::filesystem::path& file_name) -> void` 入参为配置文件路径（虽为非 const 引用，当前实现只读），从 Rust 配置快照拷贝到 C++ 字段；**不是写回磁盘或动态重装 Hook 的 API**。不建议 Mod 重载宿主全局配置。完整字段和初始值见 [SettingsManager.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/SettingsManager.hpp)，实际加载字段见 [SettingsManager.cpp](../../../crates/ue4ssl-dll/native/UE4SSL/src/SettingsManager.cpp)。

| 字段组 | 含义与重要默认值 |
|---|---|
| `General` | cache、热重载、调试键、扫描超时；slow update guard 默认启用，阈值 `4000ms` |
| `ObjectSearch` | native fast find、native class enumeration、对比结果，默认均 false |
| `EngineVersionOverride` | Major/Minor 默认 `-1`，表示无显式版本覆盖 |
| `Debug` | simple/debug console 默认启用，GUI 缩放 `1.0`；字段存在不代表 GUI 功能可用 |
| `CrashDump` | dumping 默认 true，full memory dump 默认 false |
| `Threads` | scanner 默认 8 线程，启用多线程的模块阈值 `16777216` 字节 |
| `Memory` | asset loading 内存使用百分比上限默认 `85` |
| `Hooks` | 各类底层 Hook 开关默认 true；LocalPlayer FExec vtable offset 默认 `0x28` |
| `Overrides/ObjectDumper/CXXHeaderGenerator/UHTHeaderGenerator/Experimental` | 保留字段组；当前 `deserialize` 没有从快照填充这些组，不应据字段推断功能启用 |

其余核心头文件的定位：

| 头文件 | 用途 |
|---|---|
| `UnrealCustom/CustomProperty.hpp` | 自定义反射属性，见 [Unreal 参考](unreal.md) |
| `Compat/*` | Rust/core/host/SEH 桥、生成 ABI 与内部路径计算；普通 Mod 使用上层接口，不直接接管跨语言句柄 |
| `ErrorObject.hpp`、`ProgramFeatures/CanError.hpp`、`ExceptionHandling.hpp` | 核心内部错误状态与异常包装；不是对任意坏 UObject 的安全保证 |
| `SehFramework.hpp`、`CrashDumper.hpp` | 核心诊断和 crash dump 基础设施，不替代 Mod 的所有权/线程约束 |
| `MProgram.hpp`、`UnrealDef.hpp`、`Common.hpp` | 核心启动选项、基础定义与导入/导出宏，不是第二套生命周期 |

基类、队列和输入实现分别见 [CppUserModBase.cpp](../../../crates/ue4ssl-dll/native/UE4SSL/src/Mod/CppUserModBase.cpp)、[UE4SSProgram.cpp](../../../crates/ue4ssl-dll/native/UE4SSL/src/UE4SSProgram.cpp)、[runtime/state.rs](../../../crates/ue4ssl-runtime/src/core/state.rs)。

## 8. 从原生 Mod 执行 JavaScript

JavaScript 插件额外导出以下 C 入口，定义在插件 [dllmain.cpp](../../../scripts/ue4ssl-javascript/native/cpp/dllmain.cpp)，并非 `UE4SSL.dll` 核心的常规导入符号：

```cpp
extern "C" bool eval_js_code(const char* code, size_t code_len, const char* filename);
```

| 参数 | 方向 | 约束 |
|---|---|---|
| `code` | 入，调用期间借用 | 非空 UTF-8 源码字节指针，不应早于调用结束失效 |
| `code_len` | 入 | 源码字节数，不是 UTF-16 长度，不要求源码 NUL 结尾 |
| `filename` | 入，调用期间借用 | 诊断使用的 UTF-8 文件名，推荐非空 NUL 结尾字符串 |

出：成功返回 `true`；引擎尚未就绪、线程不符、JS 异常或 native 错误返回 `false`。必须从共享 JS event-loop 线程调用，例如其他原生 Mod 的 `on_update`；不能从任意游戏 Hook/自建线程直接调用。执行方式是 **global script**，不同于 `js/main.js` 的 ES module，不支持在这段源码顶层写静态 `import`。全局修改会影响其他脚本，异步任务仍需后续 JS tick 推进。

按实际加载模块句柄使用 `GetProcAddress` 解析，不要假定发布文件名为旧 README 中的 `JSScriptMod.dll`，也不要把内部 `ue4ssl_js_cpp_*` 桥函数当成稳定 SDK。脚本端接口见 [JS 文档](../javascript/README.md)。
