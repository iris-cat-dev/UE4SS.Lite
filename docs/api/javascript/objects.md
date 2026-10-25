# JavaScript 对象、属性与函数调用

[返回 JavaScript API](README.md) · [Hook 与委托](hooks.md) · [运行环境](system.md) · [C++ 运行时](../cpp/runtime.md)

本文描述当前 QuickJS 全局对象接口，而非旧 Lua API。全局名称及声明的 `length` 由 [Rust `GLOBAL_API`](../../../scripts/ue4ssl-javascript/src/lib.rs) 给出，顺序映射到 [C++ 实现表](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)。表中的参数均为**入**，返回均为**出**；显式标为“入出”的参数会影响 Unreal 原始内存。缺参数/错误类型通常抛 JS `TypeError`；具体未找到与底层失败按各条目处理。`UObject` 包装器仅保存 Unreal 指针，不会自动延长对象生命期；地图切换或 GC 后重新查找。跨线程、根集与调用限制见下文。

## 对象查找、资产加载与创建

| 签名（`text` 记法） | 参数、功能与返回 | 失败和副作用 |
|---|---|---|
| `FindFirstInstanceOfClass(className: string): UObject \| null` | `className`：类名/查找名；返回匹配的第一个已存在实例。 | 无匹配、SEH 或 C++ 异常返回 `null`；SEH 时更新全局 `__UE4SSL_LastObjectSearchFailure__`。不加载资产。 |
| `FindAllInstancesOfClass(className: string): UObject[]` | 同上，返回当前已存在实例列表。 | 无结果、SEH 或 C++ 异常返回 `[]`；SEH 时更新失败记录。 |
| `FindFirstOf(className: string): UObject \| null`、`FindAllOf(className: string): UObject[]` | 分别调用上述相同实现。 | 已弃用：每种别名首次调用会打印警告；失败记录的 `operation` 为别名。 |
| `FindAllActorsWithInterface(interfacePath: string): UObject[]` | 先按路径加载接口 `UClass`，遍历名为 `Actor` 的实例，按地址去重并过滤实现接口者。 | 接口不存在抛 `ReferenceError`；迭代/匹配异常返回 `[]` 或跳过出错 actor；可能触发资产加载。 |
| `StaticFindObject(objectPath: string): UObject \| null` | 只查已加载对象路径；不加载。 | 未找到/查找异常返回 `null`。 |
| `LoadObject(objectPath: string): UObject \| null` | 先查已加载对象，随后经 AssetRegistry 获取资产；`_C` 路径还尝试去掉后缀后的资产路径。 | 未找到、路径无效或加载异常返回 `null`；可能加载资产。 |
| `ScanBlueprintWidgetsByInterface(interfacePath: string, rootPath?: string): WidgetAsset[]` | `rootPath` 默认 `/Game`，空字符串也回退 `/Game`；枚举 AssetRegistry 的 WidgetBlueprint/WidgetBlueprintGeneratedClass，按包名前缀、非抽象 `UserWidget` 子类及接口过滤、类路径去重。每条含 `classPath`, `packageName`, `assetName`, `objectPath`, `assetClass`（均 string）。 | 接口不存在抛 `ReferenceError`；Registry 不可用或 SEH 返回 `[]`；其他 C++ 异常抛 `InternalError`。可能加载生成类，结果是**资产元信息**而非 widget 实例。 |
| `NewUObject(classPathOrClass: string \| UObject, outer: UObject \| null): UObject` | `classPathOrClass` 须是可查到的 UClass 路径或 UClass 包装器；`outer` 可传 `null`；创建对象并返回包装器。 | 路径未找到抛 `ReferenceError`，构造失败抛 `InternalError`。优先在注册成功的 UMG dispatcher 上同步排队至游戏线程；未注册成功则**直接在调用线程构造**。 |

上表源码：[查找/加载/资产扫描](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)、[创建和调度](../../../scripts/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp)。`__UE4SSL_LastObjectSearchFailure__` 是最近一次 **SEH 查找失败** `{operation: string, query: string, sehCode: number}`，成功调用并不清空旧值；不能以它是否存在判断当前查找失败。上述查找/属性接口不统一切换到游戏线程；资产/对象访问需选择引擎安全的调用时机。

`LoadObject("/Game/UI/WBP_HUD.WBP_HUD_C")` 在回退分支可能返回去掉 `_C` 的 Blueprint **资产对象**，不保证是生成的 `UClass`。`NewUObject` 和 `__umgCreateUserWidget` 的 class 包装器分支仅强制转换指针，并未验证运行时类型；不要把任意 `LoadObject` 结果当 UClass，应确认路径与实际类型。`NewUObject` 的 `outer` 位置参数必须给出，即使希望为空也显式传 `null`。

```javascript
const player = FindFirstInstanceOfClass("PlayerController");
if (player && player.IsValid()) {
  print(player.GetFullName());
}
const widgetAssets = ScanBlueprintWidgetsByInterface("/Game/UI/BPI_Widget.BPI_Widget_C", "/Game/UI");
for (const asset of widgetAssets) print(asset.classPath);
```

## `UObject` 包装器的八个方法

以下均为 `object.method(...)`，无参数者不应传其他参数；`IsA` 并非 Unreal 继承链检查，而只对**当前类名做子串匹配**。实现：[JSUObject.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp)、[JSUObject.hpp](../../../scripts/ue4ssl-javascript/native/include/JSType/JSUObject.hpp)。

| 方法/签名 | 输出、限制和副作用 |
|---|---|
| `GetFullName(): string` | 类和对象的完整名称。无效包装器抛 `TypeError`，陈旧指针/访问异常抛 `InternalError`。 |
| `GetClass(): UObject \| null` | 对象当前 `UClass` 的包装器，缺失则 `null`；指针异常同上。 |
| `IsA(className: string): boolean` | 将参数转字符串，在 `GetClassPrivate()->GetName()` 内查找子串；不检查父类或接口。缺参数/无效指针抛错。 |
| `GetAddress(): number` | 指针转有符号 64 位 JS 数值；**不做陈旧指针探测**，不用于解引用，超过 JS 精确整数范围可能失精度。 |
| `IsValid(): boolean` | 包装器非空且指针首字可读时 `true`；仅浅层探测，不能保证对象未被 GC 或类型正确。 |
| `GetName(): string` | Unreal 对象短名称；无效/陈旧指针抛错。 |
| `AddToRoot(): undefined` | `SetRootSet` 并标记**此包装器**负责解除根集；防止 GC。调用失败抛 `InternalError`。 |
| `RemoveFromRoot(): undefined` | 仅当此包装器曾 `AddToRoot` 时执行 `ClearRootSet`，并清掉自身标记。 |

包装器 finalizer 在标记仍为 true 时调用 `ClearRootSet`；同一 UObject 可有多个独立包装器，根集不是引用计数，混用多个包装器/其他代码管理根集会提前解除根。不要把裸地址当对象所有权；显式、成对管理自己添加的根。

```javascript
const instance = StaticFindObject("/Script/Engine.Default__Actor");
if (instance?.IsValid()) {
  print(instance.GetName(), instance.GetClass()?.GetName());
}
```

## 调用 UFunction

签名：`CallFunction(object: UObject, functionName: string, ...args: unknown[]): unknown`；`CallFunctionEx(object: UObject, functionName: string, ...args: unknown[]): Record<string, unknown>`。`object` 和 `functionName` 为入，后续位置实参依 Unreal `CPF_Parm` 字段顺序（跳过返回字段）填充；未提供者保留零初始化。JS 接口虽然在 manifest 标为 `arity: 3`，实际可变参数个数不受该值限制。函数从对象类链按名查找；找不到抛 `ReferenceError`，缺/错参数抛 `TypeError`，分配失败、参数写入 SEH 或 `ProcessEvent` 失败抛 `InternalError`。[实现与转换](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)。

- `CallFunction`：非网络函数同步 `ProcessEvent`，有返回字段时返回该字段转换后的 JS 值，否则 `true`；不返回其他 Out 字段。带 `FUNC_Net` 的函数若游戏线程 dispatcher 注册成功则**排队**并立即返回 `true`（不是执行成功凭证，也拿不到返回/Out）；注册失败时退回直接调用。
- `CallFunctionEx`：拒绝 `FUNC_Net`（抛 `InternalError`）；同步执行后返回按字段名索引的 Out/Return 参数快照，以及 `__success: true`, `__hasOutputs: boolean`。这里并不把 Out 值写回调用者原有 JS 对象。
- 参数填充支持 FString/AnsiString/FText/FName 字符串、布尔、数值、`UObject`、普通 JS 对象填结构体、枚举/字节；未支持的类型不保证正确传入。底层有临时参数内存，函数返回后不得保留其中的引用。直接调用不是游戏线程安全保证；需要游戏线程执行 UI 操作时用本页 UMG dispatcher。

```javascript
const actor = FindFirstInstanceOfClass("Actor");
if (actor) {
  const outputs = CallFunctionEx(actor, "GetActorBounds", false);
  if (outputs.__success && outputs.__hasOutputs) print(outputs.Origin, outputs.BoxExtent);
}
```

## 属性读写、路径与文本

实现：[JSPropertyAccess.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp)、[JSPropertyUtils.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp)。全部 `object` 为入 `UObject`，`propertyName`/`path` 为入 `string`（内部 JS 字符串转换），`value` 为入；不自动派发游戏线程。路径以 `.` 分段，首段在对象及类链查找，后续段**仅能穿过结构体字段**，不能穿过对象属性、数组索引或 map；空分段失败。单段名称可用于所有同组函数。

| 签名 | 返回和失败/作用 |
|---|---|
| `GetProperty(object: UObject, propertyName: string): unknown` | 返回 JS 快照；属性/路径不存在、无法读取或不支持的类型返回 `null`。 |
| `GetPropertyPath(object: UObject, path: string): unknown` | 与 `GetProperty` 完全相同（同一实现），并非更强的路径解析器。 |
| `SetProperty(object: UObject, propertyName: string, value: unknown): boolean` | 找到字段并调用转换器后返回 `true`，路径不存在/解析异常返回 `false`；**`true` 不保证转换器支持该类型、写入成功或写入了预期值**。 |
| `SetPropertyPath(object: UObject, path: string, value: unknown): boolean` | 与 `SetProperty` 完全相同。 |
| `ExportPropertyText(object: UObject, propertyName: string): string \| null` | 对解析出的叶字段调用 Unreal `ExportTextItem`，可能返回空字符串；字段不存在/访问失败返回 `null`。不是 `GetProperty` 返回值的 JSON。 |
| `ApplyObjectPatch(object: UObject, patchObject: object): {applied: number, failed: number, __success: boolean}` | 枚举自身字符串键（包括形如 `Transform.Location.X` 的路径），对每个非 `undefined` 值调用 `SetProperty`；`applied`/`failed` 为各布尔结果的计数，`__success` 等于 `failed === 0`。逐项原地写入，**不原子回滚**；跳过 `undefined` 键不计数。枚举失败抛 `InternalError`。 |

读取映射：FString/FAnsiString/FName/FText→`string`，bool→`boolean`，数值/enum/byte→`number`，对象/接口→`UObject | null`，struct→递归 JS 普通对象，array→JS 数组；不支持类型→`null`。写入映射：FString/FName/FText→字符串，bool/数值/enum/byte→JS 转换，struct→仅覆盖 JS 对象中提供的字段，array→按 JS 数组长度替换（最多 4096 元素，超限仅日志拒绝），对象→`UObject`/`null`，接口→设置 `ObjectPointer` 但 `InterfacePointer` 置空；未列出的类型没有写入实现。FAnsiString 可读但通用属性写入器**不支持**。输入对象/数组结构和输出对象/数组均非 Unreal 原内存可写视图；修改输出后须调用 SetProperty 写回。

```javascript
const actor = FindFirstInstanceOfClass("Actor");
const root = actor && GetProperty(actor, "RootComponent");
if (root) {
  const before = GetPropertyPath(root, "RelativeLocation.X");
  const result = ApplyObjectPatch(root, {"RelativeLocation.X": 100});
  print(before, result.applied, result.failed, ExportPropertyText(root, "RelativeLocation"));
}
```

路径的中间段只能是结构体；不能直接使用 `actor` 的 `RootComponent.RelativeLocation.X`（`RootComponent` 是对象属性）。上例会将组件相对 X 坐标设为 100，需在游戏允许的位置/时机操作。`ApplyObjectPatch` 的 `__success` 也不能用来证明所有值已真正写入。

## Hook 参数 `ParamRef`（借用）

直接同步 Hook 中 `CPF_OutParm` 且非 `CPF_ConstParm` 的实参是 `ParamRef`，由 [JSPropertyUtils.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp) 创建；普通输入及 const Out 为转换后的 JS 值。它不是可自行构造的全局类型。以下方法的接收者都是这个**入出的借用引用**：

| 签名 | 返回和行为 |
|---|---|
| `ref.get(): unknown` | 读取当前底层属性并按上面映射转换；无效/SEH 返回 `null`。 |
| `ref.set(value: unknown): boolean` | 尝试将 JS 值写入原始属性；缺 `value` 抛 `TypeError`，无效/SEH 返回 `false` 并在 SEH 后标无效；`true` 同样不保证转换器实际支持。 |
| `ref.toString(): string` | 将 `get()` 的值用 JS 字符串转换，失效则 `"[ParamRef invalid]"`；对象值可能成为 `"[object Object]"`。 |
| `ref.isValid(): boolean` | 仅检验保存的属性和数据指针非空；**不探测实际内存是否仍有效**。 |

`ParamRef` 指向当前 UFunction 栈/Out 参数内存，**仅在该直接同步回调期间使用**；绝不能放到异步回调、Promise、计时器或模块全局。原始类型转换失败/不支持时不保证抛错。Hook 的 `sync`、异步快照和第三回调参数见 [Hook 文档](hooks.md)。

## UMG 游戏线程辅助函数

这六个 `__umg*` 全局函数由 [JSGameThreadDispatcher.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp) 实现，面向游戏线程 UI 操作，不是任意 JS 回调调度。同步 dispatcher 最多等 **15 秒**；超时返回失败，但已排队操作未必取消，之后仍可能执行。JS 包装器只保留 Unreal 指针，异步操作期间必须确保所指对象存活。

| 签名 | 参数、输出与失败 |
|---|---|
| `__umgDispatchSync(object: UObject, functionName: string, ...args: unknown[]): unknown` | 按 UFunction 非返回参数顺序用通用属性转换器填充，游戏线程同步 `ProcessEvent`；返回 Return 字段或 `true`。无函数抛 `ReferenceError`，对象/参数/dispatcher/调用失败抛 Type/InternalError；不输出 Out 字段。 |
| `__umgDispatchAsync(object: UObject, functionName: string, ...args: unknown[]): undefined` | 调用时填充参数内存，排队游戏线程执行并立即返回 `undefined`；并非 Promise，也不返回 Return/Out；延后访问时对象/函数失效会跳过并记录日志。查找和分配失败抛错。 |
| `__umgCreateUserWidget(widgetClass: string \| UObject, worldContextObject?: UObject \| null, owningPlayer?: UObject \| null): UObject` | 先查类或接收 UClass 包装器；缺世界上下文时依次尝试 player、GameInstance、PlayerController、World；缺 owning player 时查 PlayerController；经 WidgetBlueprintLibrary `Create` 在游戏线程创建。无法解析、派发或创建抛错。 |
| `__umgCloneUserWidget(sourceWidget: UObject, outerObject?: UObject \| null): UObject` | 以源 widget 为模板构造同类对象；缺 outer 时依次尝试源 Outer、GameInstance、PlayerController、World。目标缺失/构造失败抛错。并不在这里执行 `Create`。 |
| `__umgSetUserWidgetRoot(userWidget: UObject, rootWidget: UObject): undefined` | 在游戏线程获取/必要时创建 WidgetTree，再直接设置其 `RootWidget` 属性；失败抛 `InternalError`（带内部错误码）。该函数当前**未显式 ensure dispatcher 已注册**。 |
| `__umgConstructWidget(userWidget: UObject, widgetClassName: string): UObject` | 在游戏线程获取/必要时创建 WidgetTree，按 `/Script/UMG.` + `widgetClassName` 查类，以 WidgetTree 为 outer 创建 transient widget；类/树/构造失败抛错。当前**未显式 ensure dispatcher 已注册**。 |

这些参数均为入，返回的新 widget/UObject 包装器仍为借用引用，不意味着自动 `AddToRoot`。`__umgDispatchSync/Async` 的 manifest `arity` 是 1，实际实现**至少要求 2 参数**；以此表的运行时校验为准。设置/构造函数若尚未注册 dispatcher，可能等满 15 秒；`__umgConstructWidget` 随后抛错，**`__umgSetUserWidgetRoot` 的超时空结果却会被当作成功返回 `undefined`**，已排队的写入之后仍可能执行。先用会注册 dispatcher 的 API。`__umgDispatchAsync` 的异常仅覆盖入队前，实际游戏线程执行失败只写日志。

```javascript
const controller = FindFirstInstanceOfClass("PlayerController");
const widgetClass = StaticFindObject("/Game/UI/WBP_HUD.WBP_HUD_C");
if (controller && widgetClass) {
  const widget = __umgCreateUserWidget(widgetClass, controller, controller);
  __umgDispatchAsync(widget, "AddToViewport");
}
```
