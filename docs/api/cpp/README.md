# C++ API

[返回 API 总览](../README.md)

本目录面向原生 Mod 作者，记录本仓库提供的 C++ 兼容层及 Unreal 互操作接口。不是 Epic 官方 SDK；引擎布局、函数地址和版本适配来自运行时解析，不保证与任意游戏版本兼容。

## 文档导航

| 文档 | 内容 |
|---|---|
| [Mod 与运行时](runtime.md) | DLL 入口、生命周期、元数据、事件队列、路径、Mod 状态、配置 |
| [Unreal 类型与反射](unreal.md) | UObject、UClass、UFunction、FProperty、容器、字符串、内存与便捷封装 |
| [查找、Hook 与扫描](search-and-hooks.md) | 对象搜索、构造与加载、引擎回调、UFunction Hook、Detour、IAT、签名扫描 |
| [输入、日志与通用类型](platform.md) | 按键注册/取消、输出设备、日志分组、字符串转换 |

## 1. 头文件与链接

| include 根目录（相对仓库） | 使用方式 |
|---|---|
| `crates/ue4ssl-dll/native/UE4SSL/include` | `#include <Mod/CppUserModBase.hpp>`、`<UE4SSProgram.hpp>` |
| `crates/ue4ssl-unreal-support/vendor/Unreal/include` | `#include <Unreal/UObjectGlobals.hpp>`、`<Unreal/Hooks.hpp>` |
| `crates/ue4ssl-unreal-support/vendor/Unreal/generated_include` | SDK 的生成布局包装；由公开头间接包含 |
| `crates/ue4ssl-object-searcher/include` | `#include <Unreal/ObjectSearch/UObjectSearch.hpp>` |
| `crates/ue4ssl-platform/native/Common/include` | `#include <String/StringType.hpp>`、`<Helpers/String.hpp>` |
| `crates/ue4ssl-platform/native/Input/include` | `#include <Input/Handler.hpp>` |
| `crates/ue4ssl-platform/native/DynamicOutput/include` | `#include <DynamicOutput/DynamicOutput.hpp>` |
| `crates/ue4ssl-hook/include` | `#include <polyhook2/Detour/x64Detour.hpp>` |
| `crates/patternsleuth-bind/native/SinglePassSigScanner/include` | `#include <SigScanner/SinglePassSigScanner.hpp>` |

优先使用仓库构建入口；它负责生成 ABI 头、公共宏、include 路径、C++ 标准、CRT 和核心导入库。仅复制几个头文件不能构成可链接 SDK。

- Windows x64/MSVC；要求见[项目构建说明](../../../README.md#requirements)。默认原生 Mod 使用 `/std:c++23preview`。
- Mod 与核心须使用兼容的 MSVC/C Runtime、Debug/Release 配置。`CppUserModBase*` 包含 C++ 虚表；`extern "C"` 只约束导出名，不能消除类 ABI 依赖。
- 新增原生 Mod 可放在 `Mods/<ModName>/native/cpp/main.cpp`；不需要为了原生源码手工添加根 Cargo workspace 成员。
- 核心导入库 `UE4SSL.dll.lib` 必须先构建。不要用无序 `cargo build -p 核心 -p Mod` 替代统一命令。

```sh
cargo ue4ssl-mods list
cargo ue4ssl-build --mod CPP_MeowChat
cargo ue4ssl-package --mod CPP_MeowChat
```

上面使用仓库已有 Mod。新 Mod 将参数替换为其逻辑名。`--mod` 大小写不敏感，可重复；安装/打包规则见[项目说明](../../../README.md#packaging-and-install)。

## 2. 最小原生 Mod

下面是独立的 `main.cpp`。它只记录生命周期和按键，不在工作线程操作引擎对象；需要在游戏内加载才能观察日志。

```cpp
#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>

class ExampleMod final : public RC::CppUserModBase
{
public:
    ExampleMod()
    {
        ModName = STR("ExampleMod");
        ModVersion = STR("1.0.0");
        ModDescription = STR("Native API example");
        ModAuthors = STR("YourName");
    }

    auto on_unreal_init() -> void override
    {
        RC::Output::send<RC::LogLevel::Normal>(STR("[ExampleMod] Unreal ready\n"));
        register_keydown_event(RC::Input::Key::F6, [] {
            RC::Output::send(STR("[ExampleMod] F6\n"));
        });
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new ExampleMod();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
```

必须由创建对象的 Mod DLL 的 `uninstall_mod` 删除实例。不要让 Rust `Box`、其他 CRT 或核心直接释放该对象。基类会注销它拥有的输入和事件；你自己安装的游戏 Hook、线程及其他外部回调仍须先停止、等待退出，再卸载。

## 3. 可选 `mod.json`

配置放在 `Mods/<ModName>/mod.json`；原生源码模式识别以下字段。所有路径相对该 Mod 目录，不能逃出该目录；未知字段会报错。

| 字段 | 类型/默认 | 功能 |
|---|---|---|
| `name` | string；默认目录名 | 逻辑/部署名称；不能为空，不能为 `.`/`..` 或含 `/`、`\\`、`:` |
| `source_dirs` | string[]；空数组时使用 `native/cpp`，即使已提供 `sources` | 递归收集原生源文件的目录 |
| `sources` | string[]；`[]` | 显式源文件，与 `source_dirs` 合并去重 |
| `include_dirs` | string[]；`[]` | 额外 include；存在的 `native/include`、`include` 自动添加 |
| `resources` | string[]；`[]` | 资源路径；存在的 `resources/` 自动作为部署目录覆盖层，其他资源保留 basename |
| `link_libraries` | string[]；`[]` | 额外库；构建助手自动链接 `kernel32`、`user32` 与核心导入库 |
| `defines` | object，值为 string 或 null；`{}` | 编译宏，null 表示无值宏 |
| `compiler_flags` | string[]；`[]` | 额外编译选项 |
| `cpp_standard` | string；`"/std:c++23preview"` | 传给 MSVC 的标准选项 |

已有 Cargo Mod 仍要求根 workspace 成员和 `cdylib` target；不使用上述原生源文件编译配置。配置实现见 [xtask/native.rs](../../../crates/xtask/src/native.rs) 与 [ue4ssl-build](../../../crates/ue4ssl-build/src/lib.rs)。

## 4. 使用边界

- `on_update` 与 `queue_event` 在 runtime 工作线程运行，**不是 Unreal 游戏线程**。引擎对象写入、`ProcessEvent`、UI 操作须按对应 API 的线程约束执行。
- `UObject*`、反射字段指针、Mod SDK view 通常是借用；非空不等于仍有效，更不等于拥有内存。不能 `delete` 游戏对象。
- 生成成员布局依赖引擎版本；不能把示例中的结构体偏移或另一款游戏的参数结构复制到当前游戏。
- 回调参数常借用当前调用栈。同步回调结束后不能保留原始 `FFrame`、参数指针或返回值地址。
- `Compat/Generated*.hpp`、`Rust*FFI.hpp`、`Host*` 是引擎/核心边界接口，不是更稳定的第二套 Mod SDK；跨语言所有权另见 [FFI 契约](../../ffi-contract.md)，其中冻结基线与迁移补充须区分。
