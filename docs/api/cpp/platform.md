# C++ 输入、日志与通用类型

[返回 C++ API](README.md) · [Mod 与运行时](runtime.md)

## 1. 字符串与基本类型

头文件：[StringType.hpp](../../../crates/ue4ssl-platform/native/Common/include/String/StringType.hpp)。

| 类型/宏 | 含义 |
|---|---|
| `RC::CharType` | 默认 `wchar_t`；Windows 下为 UTF-16 code unit。测试开关 `FORCE_U16` 改为 `char16_t`，不是 Mod 的运行时编码选项 |
| `RC::StringType` | `std::basic_string<CharType>`，拥有字符串存储 |
| `RC::StringViewType` | `std::basic_string_view<CharType>`，不拥有存储；不能超过原字符串生命周期 |
| `RC::StreamIType/StreamOType` | 对应字符类型的输入/输出文件流别名 |
| `STR("text")` / `STR('x')` | 默认展开为宽字符串/宽字符字面量；避免给需要 UTF-16 的接口传窄字符串 |

```cpp
template<class T> auto RC::ToCharTypePtr(T* ptr);
template<class T, class CharT> auto RC::FromCharTypePtr(CharT* ptr);
```

入：现有字符指针；出：保持 constness 的重解释指针，编译时要求元素大小相同，`FromCharTypePtr` 还要求来源是 `CharType`。**不转码、不复制、不延长生命周期**；不能用它把 UTF-8 `char*` 变成 UTF-16。

### 字符转换

头文件：[Helpers/String.hpp](../../../crates/ue4ssl-platform/native/Common/include/Helpers/String.hpp)。除特殊标注外均只有输入，返回新字符串/值，不修改输入；转换错误可由底层转换器抛出。

| 接口 | 输入 | 输出/约束 |
|---|---|---|
| `to_wstring(...)` | `std::string&`、`std::string_view` | UTF-8 → `std::wstring`；Windows 分支按 UTF-16 转换 |
| `to_wstring(...)` | `std::wstring&`、`std::wstring_view` | 宽字符串副本 |
| `to_wstring(...)` | `std::u16string&`、`std::u16string_view` | Windows 下逐 code unit 转宽字符串；非 Windows 抛异常 |
| `to_string(...)` | `std::wstring&`、`std::wstring_view`、`std::u16string_view` | 返回 UTF-8 `std::string` |
| `to_u16string(...)` | `std::wstring&`、`std::wstring_view` | 逐元素构造 `std::u16string`；Windows 宽字符为 16 位 |
| `to_u16string(...)` | `std::string&`、`std::string_view` | **当前仅逐字节扩宽，不做 UTF-8 解码**，不能用于非 ASCII UTF-8 文本转码 |
| `to_charT_string<CharT>(arg)` | 已支持的字符字符串 | 按目标 `wchar_t/char16_t/char` 分发上述转换 |
| `to_charT_string_path<CharT>(path)` | 路径对象 | 调用 `wstring/u16string/string`，遵循 filesystem 的编码规则 |
| `to_charT<CharT>(arg)` | 字符指针、string/view、filesystem::path 或其他值 | 指针先转 view，异类型字符转换，同字符/未知类型按值传出；不能统一假定返回 owning string |
| `ensure_str(arg)` | 同上 | `to_charT<CharType>` 的便捷入口 |
| `ensure_str_as<CharT>(const StringType&)` | 宿主字符串 | 指定字符类型的字符串副本 |
| `to_utf8_string(...)` | wstring/u16string/CharType 指针/StringViewType/path | UTF-8 副本；路径先取 `generic_wstring()` |
| `to_generic_string(const auto&)` | 支持的字符/字符串类型 | `StringType` 值 |
| `ensure_str_const(std::string_view)` | UTF-8 view | 池中 `const StringType&`；键本身是 string_view，**输入底层字节必须长期存活**，勿传临时字符串；普通调用优先 `ensure_str` |
| `stringviewify(ptr)` | NUL 结尾字符指针 | 借用的 basic_string_view，不负责查坏指针 |
| `String::iequal(a,b)` | 同字符类型 view/view，或 string&/指针、指针/string& | bool，长度相同且逐字符 `towlower` 相等；不是 Unicode 标准化比较 |
| `String::str_cmp_insensitive(ptr, string&)` | 同上 | `iequal` 的同义包装 |

```cpp
#include <Helpers/String.hpp>

void ConvertText()
{
    const auto wide = RC::to_wstring(std::string_view{"Hello"});
    const auto utf8 = RC::to_utf8_string(std::wstring_view{wide});
    const auto equal = RC::String::iequal<wchar_t>(wide, std::wstring_view{L"hello"});
    (void)utf8;
    (void)equal;
}
```

### 其他通用助手

| 接口 | 参数/方向 | 返回/注意事项 |
|---|---|---|
| `explode_by_occurrence(str, delimiter, ExplodeType::FromStart/FromEnd)` | 入：字符串、分隔字符、方向 | 返回首个分隔符前/末个分隔符后的部分；无分隔符返回原串副本 |
| `explode_by_occurrence(str, delimiter, int32_t occurrence)` | 入：字符串、分隔符、次数 | 返回指定查找次数前的前缀；找不到返回空串；实现从偏移 1 开始搜索，不应假定标准 split/nth 语义 |
| `explode_by_occurrence(str, char delimiter)` | 入：字符串和分隔符 | 返回字符串 vector；旧实现对连续/末尾分隔符不等价标准 split |
| `explode_by_occurrence_with_quotes(str, delimiter=STR(' '), escape_character=STR('\\'))` | 入：字符串、分隔符、引号转义符 | 返回非空 token vector；仅满足 token 边界的双引号用于分组，不是 shell/JSON parser；分隔符不能与引号/转义符相同（assert） |
| `Helper::Integer::to<IntTo>(IntFrom)` | 入：integral 值；见 [Integer.hpp](../../../crates/ue4ssl-platform/native/Common/include/Helpers/Integer.hpp) | 转换值，超出检查范围抛 runtime_error；当前实现用 signed/unsigned 比较，不应据此宣称任意符号组合均可靠 |
| `SysError(unsigned long err)` | 入：如 `GetLastError()` 的码；见 [SysError.hpp](../../../crates/ue4ssl-platform/native/Common/include/Helpers/SysError.hpp) | 系统错误消息 `StringType`，未知码返回 `Unknown error <n>` |
| `fmt(const char*/wchar_t* format, Args...)` | 入：printf 风格格式与实参；见 [Format.hpp](../../../crates/ue4ssl-platform/native/Common/include/Helpers/Format.hpp) | 返回字符串，旧式固定 1000 字符缓冲区；不用于不受控长文本，优先 `std::format`/`Output::send` |

`Constructs/*` 是编译期字符串、生成器和视图等实现辅助模板，不提供宿主资源或生命周期；不要把它们与 Unreal `TArray` 或事件队列当作可互换容器。

## 2. 按键输入 `RC::Input::Handler`

头文件：[Handler.hpp](../../../crates/ue4ssl-platform/native/Input/include/Input/Handler.hpp)、[KeyDef.hpp](../../../crates/ue4ssl-platform/native/Input/include/Input/KeyDef.hpp)。Windows 专用。普通 Mod 优先使用[基类按键方法](runtime.md)，由宿主管理轮询和销毁。

```cpp
using EventCallbackCallable = std::function<void()>;
using EventHandle = uint64_t;
using ModifierKeyArray = std::array<Input::ModifierKey, max_modifier_keys>; // Handler 内别名

Handler(const wchar_t* window_class, /* 也可传入多个窗口类名 */ ...);
void process_event();
EventHandle register_keydown_event(Key key, EventCallbackCallable callback,
                                  uintptr_t owner = 0, uint8_t kind = 0);
EventHandle register_keydown_event(Key key, const ModifierKeyArray& modifiers,
                                  const EventCallbackCallable& callback,
                                  uintptr_t owner = 0, uint8_t kind = 0);
bool unregister_event(EventHandle handle);
size_t unregister_owner(uintptr_t owner);
size_t unregister_kind(uint8_t kind);
bool is_keydown_event_registered(Key key);
bool is_keydown_event_registered(Key key, const ModifierKeyArray& modifiers);
bool get_allow_input();
void set_allow_input(bool new_value);
```

构造器真实形式是变参模板 `explicit Handler(WindowClasses... window_classes)`，要求每个实参类型为 `const wchar_t*`；上面构造签名只描述调用形态。无默认构造、不可复制；析构注销全部事件。

| 参数 | 方向 | 含义 |
|---|---|---|
| `window_classes` | 入 | 允许接收输入的前台窗口类名，如 `L"UnrealWindow"`、`L"ConsoleWindowClass"`，不是窗口标题 |
| `key` | 入 | Windows virtual-key 对应枚举；如 `Key::A`、`Key::F6` |
| `modifiers` | 入 | SHIFT=`0x10`、CONTROL=`0x11`、ALT=`0x12`；数组剩余项零初始化。它是固定数组，不要硬编码长度 3 |
| `callback` | 入→保存副本 | 无参数/无返回值。不要捕获短命局部引用 |
| `owner` | 入 | 注销身份；0 会继承当前输入回调 owner（若有），不是永远独立的全局 owner |
| `kind` | 入 | 调用方分组标签，不传给 callback |
| `handle` | 入 | 同一 Handler 的注册结果，不是内存地址 |
| `new_value` | 入 | 是否允许分发；false 暂停，不删除注册 |

返回/行为：

- 注册成功返回非零 handle；创建 Handler 或注册失败抛 `std::runtime_error`，而不是返回“成功的空句柄”。
- `process_event()` 轮询键状态并在**调用线程**同步执行 callback；不是自动线程、不是游戏线程调度器。需要主动轮询，宿主自有 Handler 由 runtime 轮询。
- 只在键从未按下到按下的边沿触发，不随系统键盘重复连发。需要当前前台窗口类匹配，且 `allow_input=true`。
- 修饰键集合**完全匹配**：注册 Ctrl+A 不会在 Ctrl+Shift+A 时触发；未指定修饰键的 A 也不会在按 Ctrl 时触发。
- `unregister_event` 返回是否找到注册；owner/kind 注销返回匹配移除数。注销先禁止新调用，再等待其他线程在途回调结束；当前回调自注销不等待自己，释放延迟到返回。不能在回调自己的栈仍执行时卸载其 DLL。
- 在 callback 内新增的注册不加入当前分发快照；注销尚未开始的 callback 可阻止本轮执行。
- `process_event()` 会在 native 边界收集 C++ 错误并在该线程重新抛出；不能当作任意无效内存访问后的可恢复保障。

```cpp
#include <Input/Handler.hpp>

void PollOnce()
{
    RC::Input::Handler input(L"UnrealWindow");
    RC::Input::Handler::ModifierKeyArray modifiers{};
    modifiers[0] = RC::Input::ModifierKey::CONTROL;
    const auto handle = input.register_keydown_event(
        RC::Input::Key::F6, modifiers, [] {}, 123, 1);
    input.process_event();
    const bool removed = input.unregister_event(handle);
    (void)removed;
}
```

此例仅展示创建/轮询/注销语法；实际工具须保持 Handler 存活并周期轮询。当前头文件的 `max_callbacks_per_event=30` 是常量，Rust 注册实现没有用它实施每键 30 个限制，不能写成运行时上限。

实现：[Handler.cpp](../../../crates/ue4ssl-platform/native/Input/src/Handler.cpp)、[platform/lib.rs](../../../crates/ue4ssl-platform/src/lib.rs)。

## 3. 日志 `RC::Output`

统一 include：[DynamicOutput.hpp](../../../crates/ue4ssl-platform/native/DynamicOutput/include/DynamicOutput/DynamicOutput.hpp)；核心签名：[Output.hpp](../../../crates/ue4ssl-platform/native/DynamicOutput/include/DynamicOutput/Output.hpp)。

### 3.1 默认输出

```cpp
void send(StringViewType content);
template<typename... Args> void send(StringViewType format, Args... args);
template<int32_t level, typename... Args> void send(StringViewType format, Args... args);
template<EnumType E, typename... Args> void send(StringViewType format, E level, Args... args);
template<int32_t level> void set_default_log_level();
bool has_internal_error();
```

- 入：`format` 使用 `std::format` 的 `{}` 语法；`args` 为匹配实参。运行时枚举重载的 `level` 是格式之后的第一个参数，编译时重载为 `send<LogLevel::Warning>`。
- 出：send/set 返回 void；`has_internal_error` 返回是否记录过内部错误，不是“上一条日志是否成功”。
- `LogLevel::Default/Normal/Verbose/Warning/Error` 对应默认/无色/青/黄/红；并非最低严重度过滤器。`Color` 另含 Cyan/Yellow/Red/Green/Blue/Purple。
- 不自动添加换行；默认 formatter 为 `[时间] 内容`。格式错误可抛 `std::format_error`，无设备/设备失败可抛 `std::runtime_error` 或设备自己的异常。
- 默认设备为宿主全局资源。Mod 不应随意清空或替换它们。

```cpp
#include <DynamicOutput/DynamicOutput.hpp>

void LogCount(int count)
{
    RC::Output::send<RC::LogLevel::Normal>(STR("count={}\n"), count);
    RC::Output::send(STR("check {}\n"), RC::LogLevel::Warning, count);
}
```

### 3.2 输出组和设备

| 接口 | 输入 | 输出/所有权 |
|---|---|---|
| `Targets<Device, Devices...>()` | 设备类型，均须可默认构造 | 创建独立输出组；不可复制，析构销毁组与设备 |
| `targets.send(...)` | 与全局 send 相同的格式/level/实参 | void，仅发往该组 |
| `targets.get_device<Device>()` | 类型参数 | 借用引用，未找到抛 runtime_error；不得 delete，不能超过组生命周期 |
| `set_default_devices<Device>()` | 类型参数 | **添加**默认设备，单类型重载返回引用；不是先删除旧设备；多类型重载返回 void |
| `get_device<Device>()` | 类型参数 | 默认组中首个匹配设备引用，找不到抛异常 |
| `clear_all_default_devices()/close_all_default_devices()` | 无 | void，关闭并清空默认组；宿主全局副作用 |
| `DefaultTargets::get_group()` | 无 | uint64_t opaque 日志组身份，不是指针 |
| `DefaultTargets::set_default_log_level(int32_t)/get_default_log_level()` | 入：level / 无 | void / int32_t；配置默认颜色/级别 |
| `Output::Lock()` | 无 | RAII 调用默认设备 lock/unlock |
| `Output::Lock(const OutputDevice* device)` | 入：借用设备，可为 null | 对该设备加锁；null 走默认组；不能假定所有设备都实现了互斥（基类为空） |

设备接口定义在 [OutputDevice.hpp](../../../crates/ue4ssl-platform/native/DynamicOutput/include/DynamicOutput/OutputDevice.hpp)：

| 方法 | 功能/参数/返回 |
|---|---|
| `receive(StringViewType content) const -> void` | 纯虚；入：借用消息，仅本次调用有效；实现需要异步保存时必须复制 |
| `has_optional_arg() const -> bool` | 默认 false；true 时日志分发调用下方方法 |
| `receive_with_optional_arg(StringViewType, int32_t level=0) const -> void` | 入：文本与级别；默认实现抛异常，声明支持的设备必须覆盖 |
| `set_formatter(StringType (*)(StringViewType)) -> void` | 入：有效函数指针；不捕获的 formatter，返回拥有文本；不要设为 null |
| `lock()/unlock() const -> void` | 可覆盖，默认无操作 |

| 内置设备 | 配置/行为 |
|---|---|
| `FileDevice` / `DefaultFileDevice` | `set_file_name_and_path(const StringType&) -> void` 入：完整路径；首次写入才打开，持续持有文件，默认追加；写失败抛异常 |
| `NewFileDevice` | 同上，但首次打开时清空/重建；不是每条日志都重建 |
| `DebugConsoleDevice` | stdout 控制台输出，支持颜色/level；不可复制 |
| `TestDevice` | 调试设备及示例 OptionalArgTest，不适合作为生产日志接口 |

```cpp
#include <DynamicOutput/DynamicOutput.hpp>

void LogToOwnFile(const RC::StringType& file_path)
{
    RC::Output::Targets<RC::Output::FileDevice> targets;
    targets.get_device<RC::Output::FileDevice>().set_file_name_and_path(file_path);
    targets.send(STR("own log: {}\n"), 42);
}
```

设备销毁的 flush/close 失败不向外抛异常，设置 `has_internal_error()`；不要据此将此前每次写入视为成功。自定义设备的虚表/formatter 位于 Mod DLL，组须在 DLL 卸载前销毁，不能让默认组保留已卸载代码指针。实现见 [Output.cpp](../../../crates/ue4ssl-platform/native/DynamicOutput/src/Output.cpp)。
