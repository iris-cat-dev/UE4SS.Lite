# JavaScript Hook、地图事件与委托

[返回 JavaScript API](README.md) · [对象与属性](objects.md) · [系统 API](system.md) · [C++ 运行时](../cpp/runtime.md)

全局函数列表见 [Rust `GLOBAL_API`](../../../scripts/ue4ssl-javascript/src/lib.rs)，按索引绑定到 [C++ `GlobalFunctionImplementations`](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)。本页以 C++ 实际运行时校验为准，非 `Function.length` 保证；所有回调返回值均被忽略，回调异常会记录并可能触发对应子系统的 circuit breaker。注册前请确保引擎已初始化；安全模式或 Hook 子系统被禁用时相关 Hook/LoadMap 注册抛 `InternalError`。大多数全局 Hook 直接作用于引擎原生调用栈，需要主动解除注册。签名中的 `callback?` 可省略或传 `null`/`undefined`，但至少需要第二个位置实参。

## UFunction Hook

```text
RegisterHook(functionPath: string, pre: HookCallback | null, post?: HookCallback | null, options?: {sync?: boolean}): [number, number]
HookUFunction(functionObject: UObject, pre: HookCallback | null, post?: HookCallback | null, options?: {sync?: boolean}): [number, number]
RegisterBindHook(functionPath: string, pre: HookCallback | null, post?: HookCallback | null): [number, number]
UnregisterHook(preId: number, postId: number): boolean
UnregisterBindHook(firstBindId: number, secondBindId: number): boolean
```

`functionPath` 为 UFunction 路径（入），`functionObject` 为包装了 UFunction 的 UObject（入；实现只提取包装器指针，**未严格校验真的是 UFunction**）。`pre`/`post` 为入的函数或空；至少给出 `pre` 位置参数，即使只用 post 也须 `null` 占位。`options` 唯一读取的字段为 `sync`：**只有 JS 布尔 `true` 才开启**，默认 `false`；必须放在**第四**参数，不能把 `{sync:true}` 放在第三参数当 post。`RegisterBindHook` 不读取第四参数，即使传入 `{sync:true}` 也无效。注册返回 `[preId, postId]`，底层注册失败可能返回非正/负 ID，而不是统一抛错；UFunction 不存在抛 `ReferenceError`，错误参数抛 `TypeError`，内部异常抛 `InternalError`。[入口实现](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)、[Hook 状态/注册](../../../scripts/ue4ssl-javascript/native/cpp/JSHook.cpp)。

`RegisterHook` 立即解析已加载 UFunction。`RegisterBindHook` 保存目标函数路径：若已找到则立即激活，未找到时等 `UFunction::Bind` watcher 后续激活；返回 `[bindId, bindId]`（两个元素是**同一个 ID**），并不是原生 pre/post Hook ID。即使 watcher 未注册成功，函数也可能返回 ID，但只支持立即激活；等待不等于已 Hook。`UnregisterBindHook` 用返回的两个 ID 注销记录和已激活实例；找不到返回 `false`。`UnregisterHook` 使用普通/NativeObjectMethod Hook 的 ID 对注销；实现以任一个 ID 匹配已注册记录，注销时仍会移除该记录的两个底层 ID；找不到或底层注销失败返回 `false`。`UnregisterHook` **不能**注销 BindHook。模块销毁时清理已拥有的 Hook；显式注销可防止在不再需要时继续回调。

### Hook 回调与线程

`HookCallback(context: UObject | null, params: Array<unknown | ParamRef>, resultAddress: bigint | undefined): void`。`context` 为本次调用的 Unreal 对象包装器；`params` 依 UFunction `CPF_Parm` 原顺序，**不含返回字段**，最多前 32 个；直接执行时普通参数按 JS 值转换，非 const 的 Out 参数为 `ParamRef`（`get/set/toString/isValid` 见[对象 API](objects.md#hook-参数-paramref借用)）。直接回调第三参数是 `context.RESULT_DECL` 的 BigInt 地址（仅作地址标识，**不是返回值写入器**），无效时可能是零；不得保留/解引用裸地址。pre 在原调用前，post 在调用后，回调返回值不改变 Unreal 返回值。源码：[提取/调度/借用](../../../scripts/ue4ssl-javascript/native/cpp/JSHook.cpp)、[属性转换](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp)。

默认 `sync:false`：若 Hook 发生在 JS event-loop 线程，直接运行回调；若发生在其他线程，则将参数**复制/快照**入队，在 JS tick 执行回调，不阻塞原生调用。异步快照回调的第三参数为 `undefined`；`params` 是值而**不是 `ParamRef`**，更改数组/对象不写回 Unreal。快照支持 FString/FName/FText 字符串、字符串数组、enum/数值/布尔、UObject 包装指针、struct 的 JSON 解析对象；其他/不可读字段为 `null`。UObject 指针虽存在快照中但不被 GC 保活，使用前复查存活。Out 参数在非 event-loop 线程也会降级为快照并打印警告，修改其 JS 值**不能**改变原调用。

`sync:true`：无论 Hook 来源线程都在**原生 Hook 调用栈**直接执行 JS 回调并持有 JS 互斥锁；这才能用 `ParamRef.set()` 在回调返回前写回非 const Out 参数。它可能阻塞游戏线程；在回调内等待需要游戏线程处理的同步 dispatcher 会死锁/超时，避免递归重入与耗时操作。`ParamRef` 只在该次直接回调内有效，不能跨事件、Promise、计时器保留。直接路径用 `is_executing` 防递归，回调的异常被记录而非透传给触发该 UFunction 的调用者。

```javascript
const ids = RegisterHook("/Script/Engine.Actor:ReceiveBeginPlay", (actor, params, resultAddress) => {
  print(actor?.GetName(), params.length, resultAddress);
}, null, {sync: false});
// 在卸载脚本的清理路径执行：
UnregisterHook(ids[0], ids[1]);
```

函数路径格式必须与本游戏里实际加载的 UFunction 匹配。不能通过上例假设 Blueprint 事件一定已加载；`RegisterBindHook` 适用于需要等待 Bind 的函数路径。

## 原生对象方法 Hook（不执行 JS 回调）

```text
RegisterNativeObjectMethodHook(functionPath: string, options: NativeMethodOptions): [number, number]
```

它在所 Hook 的 UFunction pre/post 调用栈里，对选中 UObject 执行目标的**另一个 UFunction**，不是 JS 回调；用 `UnregisterHook(ids[0], ids[1])` 注销。`functionPath` 为入的已加载 UFunction 路径；不存在抛 `ReferenceError`。`options` 为入对象，缺参数/错误类型、缺 `method` 且 `diagnose !== true` 抛 `TypeError`；安全模式/断路器拒绝时抛 `InternalError`。底层注册失败可能返回非正/负 ID。实现：[参数解析](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)、[选目标/执行](../../../scripts/ue4ssl-javascript/native/cpp/JSHook.cpp)、[默认状态](../../../scripts/ue4ssl-javascript/native/include/JSMod.hpp)。

| `options` 字段 | 类型、默认值及效果 |
|---|---|
| `method` | `string`，正常执行时必需且非空；从选中 target 的类链查找同名方法并 `ProcessEvent`；仅 `diagnose: true` 可省略，此时只诊断不调用方法。 |
| `diagnose` | `boolean`，仅字面 `true` 开启，默认 `false`；记录原函数参数/返回值诊断。 |
| `label` | 可转 `string`，默认空；诊断日志标签。 |
| `phase` | `"pre" \| "post" \| "both"`，默认 `"post"`；不认识的值退回 post。 |
| `target` | `"param" \| "context"`，默认 `"context"`；只有字面 `"param"` 才读取原调用的某个对象参数，其余值为原调用 context。 |
| `paramIndex` | 可转 `int32`，默认 `0`；`target:"param"` 时的参数索引，优先于 `targetParamIndex`。越界/非 `FObjectProperty` 时跳过执行。 |
| `targetParamIndex` | 可转 `int32`，默认 `0`；仅在 `paramIndex` 缺失或转换失败时使用。 |
| `fullNameContains` | 可转 `string`，默认空；非空时只在目标 `GetFullName()` 包含该子串时调用。 |
| `args` | 类数组对象，默认空；读取 `length` 和前**最多 16 个**元素作为目标方法的位置实参，`null`/`undefined` 为 Null，布尔/数字/字符串分别转换；其他对象也按 Null 处理。不会将原 Hook 参数自动转发。 |

`args` 的数字内部保存浮点数及其整数截断值；实际 `fill_native_method_arg` 支持目标属性的 bool、整数/浮点数值、字符串 `FName`；FString、UObject、struct 等目标方法参数**不支持**该填充器，保持零初始化。返回值/Out 参数不送回 JS。目标方法调用失败仅记录日志，不改变注册返回值；同步执行可能递归触发其他 Hook，应选择不会重入的目标。

```javascript
const ids = RegisterNativeObjectMethodHook("/Script/Engine.Actor:ReceiveBeginPlay", {
  method: "SomeExistingNoArgFunction",
  phase: "post",
  target: "context",
  fullNameContains: "MyActor"
});
UnregisterHook(ids[0], ids[1]);
```

## 地图加载事件与 ProcessEvent 监听

```text
RegisterLoadMapPreHook(callback: (event: LoadMapEvent) => void): number
RegisterLoadMapPostHook(callback: (event: LoadMapEvent) => void): number
UnregisterLoadMapHook(callbackId: number): boolean
RegisterProcessEventWatch(functionPath: string, callback: HookCallback): number
NotifyOnNewObject(className: string, callback: (object: UObject) => void): undefined
```

`RegisterLoadMapPreHook`/`RegisterLoadMapPostHook` 各接收入的函数；注册返回正 callback ID，失败抛 `InternalError`，类型/缺参抛 `TypeError`；使用 `UnregisterLoadMapHook(id)` 对任一阶段注销（找到返回 `true`，否则 `false`）。事件在游戏线程捕获，排队至 JS event-loop tick 调用；回调仅有**一个**入参：`{phase: "pre" | "post", map: string, host: string, portal: string, redirectUrl: string, error: string, hasPendingNetGame: boolean}`。这些字段为对应捕获时的快照，不是可修改的原生 LoadMap 参数，回调返回值无作用。回调注销后不会再作为注册项调度。源码：[注册/事件结构/排队](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)、[入口](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)。

`RegisterProcessEventWatch` 的 `functionPath` 是入的已加载 UFunction 路径，`callback` 是入的函数；返回 `m_pe_watches` 的数字索引，找不到 UFunction 返回 `-1`，参数错误抛 `TypeError`。当前**没有对应 UnregisterProcessEventWatch 全局函数**；监听持有回调直至 mod 清理。原生 ProcessEvent pre 回调只匹配缓存的函数指针，取最多 8 个非返回参数：仅 FString/FText/FName 生成字符串，其余位置为 `null`；每字符串固定缓冲区可能截断。JS event-loop tick 中调用 `callback(context, params, undefined)`；纯快照，不提供写回，也不保证 Watch 子系统在断路器禁用后继续递送。源码：[JSHook.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSHook.cpp)、[JSMod.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)。

`NotifyOnNewObject` **当前仅校验**类名和回调，写一条注册日志，然后返回 `undefined`；没有保存/触发回调，也没有注销 ID，**不能用作新对象通知**。不要依赖它驱动初始化；需选择真实 Hook/事件。源码：[JSGlobalFunctions.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)。

```javascript
const mapId = RegisterLoadMapPostHook(event => {
  print(event.phase, event.map, event.hasPendingNetGame);
});
UnregisterLoadMapHook(mapId);
```

## 多播委托

以下 `owner`、`target` 均为入 `UObject`；`delegateName` 是 owner 上的多播委托属性名，`functionName` 是 target 上供 Unreal 调用的 UFunction 名，均为入字符串。实现：[JSDelegate.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSDelegate.cpp)。

| 签名 | 功能、返回和失败 |
|---|---|
| `BindDelegate(owner: UObject, delegateName: string, target: UObject, functionName: string): boolean` | 创建并加入绑定 `(target, functionName)`；成功 `true`，非多播属性/未找到/捕获异常 `false`；错误实参抛 `TypeError`。绑定影响后续原生广播。 |
| `UnbindDelegate(owner: UObject, delegateName: string, target: UObject, functionName: string): boolean` | 移除对应绑定；属性存在且未抛异常返回 `true`，不保证原本存在这条绑定；找不到/异常 `false`。 |
| `ClearDelegate(owner: UObject, delegateName: string): boolean` | 清空该多播委托上的**全部**绑定，不仅是当前脚本添加的；属性不存在/异常 `false`，成功 `true`。 |
| `BindDelegateCallback(owner: UObject, delegateName: string, callback: HookCallback): number` | 直接绑定 JS 回调而无需 Blueprint 中介：注入委托签名 UFunction 到 owner 类 FuncMap、令签名 native、注册共享 post Hook；返回 callback ID，失败 `-1`（safe mode/Delegate circuit breaker 同样 `-1`），参数错误抛 `TypeError`。 |
| `UnbindDelegateCallback(callbackId: number): boolean` | 按先前 ID 移除 JS 回调与委托项并释放共享 Hook 引用；已不存在/数字转换失败返回 `false`，成功 `true`；owner 陈旧时跳过原生 RemoveDelegate 但仍移除 JS 记录。 |

`BindDelegateCallback` 的 JS 函数接收 Hook 形状的 `(context, params, resultAddress)`，不是直接展开的委托实参；共享 post Hook 默认 `force_sync:false`：非 event-loop 线程广播时参数按上文的[异步快照规则](#hook-回调与线程)后在 tick 送达；不会写回 Out 参数。分发只向该签名函数和 owner 匹配的已注册回调发送，回调返回值无效。多个绑定复用同一签名 Hook；解除最后一个绑定时解除 detour，但源码刻意**保留 native stub/注入状态**，不能承诺完全还原 Unreal 函数状态。`UnbindDelegate` 或 `ClearDelegate` 操作原生委托，不代替 `UnbindDelegateCallback` 释放 JS 回调 ID；应按持有的 ID 显式清理。调用原生委托 API 前确保 owner/target 存活，不要对别的 Mod 持有的多播委托盲目 `ClearDelegate`。

```javascript
const owner = FindFirstInstanceOfClass("MyWidget");
if (owner) {
  const callbackId = BindDelegateCallback(owner, "OnClicked", (context, params) => {
    print(context?.GetName(), params.length);
  });
  if (callbackId >= 0) UnbindDelegateCallback(callbackId);
}
```

委托属性名、函数名及目标类均需从本游戏实际反射信息确认；示例中的名称只是调用形式，不表示每个游戏都有对应类/委托。
