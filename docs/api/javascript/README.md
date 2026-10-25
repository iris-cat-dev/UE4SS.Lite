# JavaScript API

[返回 API 总览](../README.md)

运行环境是 UE4SSL.JavaScript 插件中的 QuickJS，不是 Node.js 或完整浏览器。引擎提供的全局 API 不能从任意 Node 脚本调用；浏览器风格接口只实现本文列出的子集。

## 1. 部署与入口

使用仓库的统一构建/安装命令，见[项目说明](../../../README.md)。标准打包布局：

```text
ue4ss/
  UE4SSL.dll
  mods/
    UE4SSL.JavaScript/
      main.dll
      enabled.txt
    MyScriptMod/
      js/
        main.js
        util.js
```

- 插件部署名是 `UE4SSL.JavaScript`，打包器将它的 DLL 复制为 `mods/UE4SSL.JavaScript/main.dll`；不要依据旧文档寻找 `JSScriptMod.dll`。
- `MyScriptMod` 可替换为任意直接子目录；入口固定为 `js/main.js`，自动枚举的不是任意深度 `*.js`。
- 当前 `find_scripts()` 只检查 `js/main.js` 是否存在，**不读取脚本目录的 `enabled.txt` 或 mods.txt**，不能靠删除这些标记禁用 JS 入口。原生 DLL 发现同样以目录内 `main.dll` 为依据；打包器仍生成 `enabled.txt` 作为布局资源。
- 加载顺序来自目录遍历，没有脚本间依赖排序保证；共享代码请显式 import，不依赖另一个 Mod 恰好先执行。
- 所有入口共用当前引擎实例的 JS runtime/main context；模块顶层 `const` 是模块作用域，`globalThis` 修改会影响其他脚本。
- 引擎在 runtime 的 `on_update` 线程初始化，收到 Unreal ready 后加载脚本；之后的 timer、Promise、网络和排队事件依赖持续 tick。**不是每个游戏帧固定调用一次。**

源码：[脚本发现/执行](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)、[插件生命周期](../../../scripts/ue4ssl-javascript/native/cpp/dllmain.cpp)、[原生发现](../../../crates/ue4ssl-runtime/src/core/mods.rs)。

### 最小入口

```javascript
print("[MyScriptMod] loaded", UE4SS.version, UE4SS.configuration);

const timerId = setInterval(() => {
    const engine = FindFirstInstanceOfClass("GameEngine");
    if (engine && engine.IsValid()) {
        print(engine.GetFullName());
        clearInterval(timerId);
    }
}, 1000);
```

这段代码需要游戏内插件和真实对象；定时器回调不是游戏线程。只读查找示例不意味着所有引擎操作都可放入 timer。

## 2. ES modules

`js/main.js` 以 ES module 执行，支持相对文件 import/export。模块解析器以导入文件的目录为基准，缺少 `.js` 后缀时追加 `.js`；裸名称也按相对文件处理，**不执行 npm/node_modules 包解析**。显式写 `./util.js` 最清晰。

`util.js`：

```javascript
export function describe(object) {
    return object && object.IsValid() ? object.GetFullName() : "<missing>";
}
```

`main.js`：

```javascript
import { describe } from "./util.js";
print(describe(FindFirstInstanceOfClass("GameEngine")));
```

模块加载失败抛 `ReferenceError` 或保留解析异常；源码 UTF-8 文本按二进制读入。没有 CommonJS `require()`、Node `fs/process` 的兼容承诺。相关实现：[JSPropertyUtils.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp)。

## 3. 接口导航与覆盖清单

下列清单对应 [GLOBAL_API](../../../scripts/ue4ssl-javascript/src/lib.rs) 的 59 个函数；表中不是全部签名，参数、返回/错误及示例见对应参考。

| 分类 | API | 参考 |
|---|---|---|
| 输出与环境 | `print`、`UE4SS.version`、`UE4SS.configuration` | [system](system.md) |
| 对象查找 | `FindFirstInstanceOfClass`、`FindAllInstancesOfClass`、`FindAllActorsWithInterface`、`StaticFindObject` | [objects](objects.md) |
| 旧别名 | `FindFirstOf`、`FindAllOf` | [objects](objects.md)；新代码使用上面的明确名称 |
| 加载与创建 | `LoadObject`、`NewUObject`、`ScanBlueprintWidgetsByInterface` | [objects](objects.md) |
| 调用与属性 | `CallFunction`、`CallFunctionEx`、`GetProperty`、`SetProperty`、`ExportPropertyText`、`GetPropertyPath`、`SetPropertyPath`、`ApplyObjectPatch` | [objects](objects.md) |
| UFunction Hook | `RegisterHook`、`HookUFunction`、`UnregisterHook` | [hooks](hooks.md) |
| Bind/native Hook | `RegisterBindHook`、`UnregisterBindHook`、`RegisterNativeObjectMethodHook` | [hooks](hooks.md) |
| 地图与对象通知 | `RegisterLoadMapPreHook`、`RegisterLoadMapPostHook`、`UnregisterLoadMapHook`、`NotifyOnNewObject`、`RegisterProcessEventWatch` | [hooks](hooks.md) |
| 委托 | `BindDelegate`、`UnbindDelegate`、`ClearDelegate`、`BindDelegateCallback`、`UnbindDelegateCallback` | [hooks](hooks.md) |
| 输入与预算 | `RegisterKeyBind`、`__withExecBudget` | [system](system.md) |
| 定时器 | `setTimeout`、`setInterval`、`clearTimeout`、`clearInterval` | [system](system.md) |
| HTTP | `fetch`、`fetchSync` | [system](system.md) |
| 下载与声音 | `downloadFile`、`downloadFileSync`、`playSoundFile`、`stopSound` | [system](system.md) |
| 文件与目录 | `readFile`、`writeFile`、`getModsDirectory`、`getGameDirectory` | [system](system.md) |
| 扫描与字节 | `SigScan`、`PatchByte`、`ReadByte` | [system](system.md) |
| 内部 UMG 调度 | `__umgDispatchSync`、`__umgDispatchAsync` | [objects](objects.md) |
| 内部 UMG 创建 | `__umgCreateUserWidget`、`__umgCloneUserWidget`、`__umgSetUserWidgetRoot`、`__umgConstructWidget` | [objects](objects.md) |

额外对象/方法：

| 接收对象 | 方法 | 参考 |
|---|---|---|
| `UObject` 包装 | `GetFullName/GetName/GetClass/IsA/GetAddress/IsValid/AddToRoot/RemoveFromRoot` | [objects](objects.md) |
| 同步参数引用 `ParamRef` | `get/set/isValid/toString` | [objects](objects.md) / [Hook](hooks.md) |
| HTTP 响应/流 | `text/json`、`body.getReader().read()`；没有 `arrayBuffer()` | [system](system.md) |
| `TextDecoder` | 构造与 `decode` | [system](system.md) |
| QuickJS 辅助全局 | `console.log`、`scriptArgs`；不等于 Node/浏览器完整全局对象 | [system](system.md#quickjs-辅助全局不属于-59-个项目函数) |

## 4. 类型、线程与错误约定

- JS `string` 与宿主 UTF-16/UTF-8 按绑定转换；文件、HTTP 与原始地址接口分别有自己的编码/二进制限制。
- `UObject` 包装是引擎指针视图，**不是自动保活的 owning object**；原始地址通常用 `bigint`。不要把 `number` 当 64 位指针安全中转。
- `IsValid()` 不是跨帧锁或 GC 保证；`AddToRoot/RemoveFromRoot` 必须配对且会改变 GC 行为，不能在对象已销毁后靠 root 修复。
- `null`、`undefined`、`false`、抛异常的含义随 API 不同。不能用一条“失败都返回 null”的规则概括。
- Hook 有同步可写引用与异步快照两类；只在注明支持时修改 `ParamRef`，不能把引用保留到下次 timer。
- `__umgDispatchSync/Async` 用于在游戏线程调用指定对象的 UFunction，不接受任意 JS callback；Async 立即返回 `undefined` 而非 Promise。具体参数见 [objects](objects.md)。同步等待、VM 锁和引擎 Hook 组合需避免相互等待。
- 引擎设有 JS 内存/栈和执行预算，不支持阻塞等待 Promise 推进。同步网络/文件调用会阻塞 event loop，勿在高频回调中使用。

本目录的游戏相关示例需要目标游戏/有效路径；语法检查不等同于游戏内行为验证。
