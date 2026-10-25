# UE4SS-Lite API 文档

面向 Mod 开发者的当前实现参考，包含 **C++ API** 与 **JavaScript API**。正文使用中文；接口名称保留源码拼写，按功能给出签名、输入/输出参数、返回值与失败行为、使用示例，以及线程/生命周期限制。

## 从这里开始

| 开发方式 | 入门 | 详细参考 |
|---|---|---|
| 原生 C++ Mod | [C++ API 与最小 Mod](cpp/README.md) | [Mod/运行时](cpp/runtime.md) · [Unreal 类型/反射](cpp/unreal.md) · [对象查找/Hook/扫描](cpp/search-and-hooks.md) · [输入/日志/字符串](cpp/platform.md) |
| JavaScript Mod | [JS API、部署和模块](javascript/README.md) | [对象/函数/属性/UMG](javascript/objects.md) · [Hook/委托/通知](javascript/hooks.md) · [定时器/文件/网络/音频/内存](javascript/system.md) |

### 按任务查阅

| 任务 | C++ | JS |
|---|---|---|
| 创建 Mod、处理启动和卸载 | [runtime](cpp/runtime.md) | [部署与入口](javascript/README.md) |
| 按类/完整路径查对象、加载资产 | [search-and-hooks](cpp/search-and-hooks.md) | [objects](javascript/objects.md) |
| 读取/修改属性、调用反射函数 | [unreal](cpp/unreal.md) | [objects](javascript/objects.md) |
| Hook UFunction、地图切换和引擎事件 | [search-and-hooks](cpp/search-and-hooks.md) | [hooks](javascript/hooks.md) |
| 注册按键、记录日志 | [platform](cpp/platform.md) | [system](javascript/system.md) |
| 处理委托、UMG、游戏线程调度 | [unreal](cpp/unreal.md) / [Hook](cpp/search-and-hooks.md) | [objects](javascript/objects.md) / [hooks](javascript/hooks.md) |
| 签名扫描、读取/修改内存 | [search-and-hooks](cpp/search-and-hooks.md) | [system](javascript/system.md) |
| 下载、HTTP、文件、声音 | 使用对应原生库；本项目未提供同名高级 C++ 包装 | [system](javascript/system.md) |

## 阅读约定

- **入**：调用方提供；**出**：函数填充/返回；**入出**：既读取又可能修改。C++ 的 `T&`/`T*` 本身不能证明方向，按实现说明。
- `void` / JS `undefined` 不表示所有操作都成功；请分别看异常、布尔结果、空值和错误字段。
- C++ 指针、JS `UObject` 包装、Hook 参数通常都是借用；非空不等于仍有效。JS 包装不会自动阻止 Unreal GC。
- 原生 runtime/JS event loop **不是 Unreal 游戏线程**。入参可转换不代表可以在任意线程调用引擎；需要游戏线程时按相应调度接口处理。
- JS 地址用 `bigint`（如 `0x1234n`），不要先转 `number` 丢失精度；不是所有返回数字都是地址。
- 示例中的游戏类名、函数路径、属性名和内存签名必须与目标游戏匹配。文档里的声明块不是可直接当完整程序运行的代码。
- `__` 前缀入口虽已暴露，但用于内部协作，不享有额外稳定性承诺；兼容别名单独标注，不建议新代码继续使用。

## 范围与来源

- C++：Mod SDK、宿主可用接口、项目提供的 Unreal 反射/搜索/Hook/扫描和平台适配。附头文件家族索引；不重复第三方 STL、QuickJS C API 或 Epic 通用 Core 模板库的整套文档。
- JS：以 [GLOBAL_API 注册表](../../scripts/ue4ssl-javascript/src/lib.rs) 和 [绑定安装代码](../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp) 为入口，覆盖 59 个注册函数、`UE4SS` 对象、额外安装的 `TextDecoder`、UObject/参数引用与服务返回对象上的方法。
- 各页附有当前仓库源码链接。声明但没有启用、空实现或有限功能的入口按实际状态说明，不把注释/旧 README 当作功能保证。
- 构建、打包、安装见[项目 README](../../README.md) / [中文 README](../../README_CN.md)。跨语言内部所有权见 [FFI 契约](../ffi-contract.md)。

## 与已有文档的关系

[对象查找专题](../UE4SS_对象查找API参考.md) 保留原文件并链接到本参考。`script-api-baseline.md`、`migration-baseline.md`、`baseline-*`、`migration-validation.json` 是历史迁移证据，**不是当前 API 的入口**；不为整理现行文档而重写历史路径或哈希。
