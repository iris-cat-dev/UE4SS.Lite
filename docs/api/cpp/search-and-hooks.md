# C++ 对象查找、Hook 与签名扫描

[返回 C++ API](README.md) · [Unreal 类型](unreal.md) · [Mod 与运行时](runtime.md) · [输入与日志](platform.md) · [项目说明](../../../README.md)

本文描述当前仓库的 C++ 接口，不是 Epic SDK。头文件分别来自 `Unreal/UObjectGlobals.hpp`、`Unreal/ObjectSearch/UObjectSearch.hpp`、`Unreal/Searcher/ObjectSearcher.hpp`、`Unreal/Hooks.hpp`（转入 `Hooks/Hooks.hpp`）、`polyhook2/*` 和 `SigScanner/SinglePassSigScanner.hpp`；include 根目录和原生 Mod 接入方式见 [C++ API 总览](README.md)。示例须在 Unreal 初始化、目标函数/对象可用之后调用；**原生回调运行于被 Hook 函数的调用线程，不自动切换到游戏线程**。所有 `UObject*`/引擎地址均为非拥有借用，切勿 `delete` 或跨 GC/关卡卸载无条件保存。

## 1. 对象查找与遍历

源码：[搜索声明](../../../crates/ue4ssl-object-searcher/include/Unreal/ObjectSearch/UObjectSearch.hpp)、[搜索实现](../../../crates/ue4ssl-object-searcher/native/src/UObjectSearch.cpp)、[全局声明](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectGlobals.hpp)、[遍历实现](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/UObjectGlobals.cpp)。以下 `UObjectGlobals` 均指 `RC::Unreal::UObjectGlobals`；`CharType`、`StringType`、`StringViewType` 是仓库字符串类型，`TCHAR` 是 Unreal 字符类型。不要将只包含子串而不以 NUL 结尾的 `StringViewType` 交给 `FindObject`/`StaticFindObject`：当前实现取 `data()` 后按 C 字符串解析。

### 精确路径/Outer 搜索

```cpp
UObject* FindObject(UClass* Class, UObject* InOuter, RC::StringViewType InName,
                    bool bExactClass = false, ObjectSearcher* Searcher = nullptr);
UObject* FindObject(UClass* Class, UObject* InOuter, const TCHAR* InName,
                    bool bExactClass = false, ObjectSearcher* Searcher = nullptr);
UObject* FindObject(ObjectSearcher& Searcher, UClass* Class, UObject* InOuter,
                    RC::StringViewType InName, bool bExactClass = false);
UObject* FindObject(ObjectSearcher& Searcher, UClass* Class, UObject* InOuter,
                    const TCHAR* InName, bool bExactClass = false);
template<class ObjectType> ObjectType* FindObject(UObject* Outer, const TCHAR* Name,
                                                   bool ExactClass = false);
template<class ObjectType = UObject*> ObjectType StaticFindObject(
    UClass* ObjectClass, UObject* InObjectPackage, const CharType* OrigInName,
    bool bExactClass = false); // 另有 StringViewType、const StringType& 同签名重载
```

- `Class` 是期望类，`nullptr` 不限定类；`bExactClass=false` 允许派生类，`true` 只接受完全相同的类。模板 `FindObject<ObjectType>` 使用 `ObjectType::StaticClass()`；`StaticFindObject<ObjectType>` 的模板类型为**返回指针类型**（如 `UFunction*`），仅作 `static_cast`，并无额外运行时类型校验。
- `InOuter` 指定所属 Outer 链（实现接受链中的祖先）；`ANY_PACKAGE` 表示任意包；`nullptr` 与短名组合不能当“全局短名搜索”使用，长名则按其包和无类型全路径查找。`InName` 为短名或无类型完整路径（例如 `/Script/Engine.Actor`）；空指针抛 `std::runtime_error`。`ObjectSearcher*` 为可选候选集合，**不是**搜索结果缓存；必须与 `Class` 过滤意图一致。
- 返回首个匹配 `UObject*` 或 `nullptr`；返回地址不转移所有权。查找可能调用已解析的原生 `StaticFindObjectFast`，或遍历 `GUObjectArray`。满足 `!Class && !InOuter && !bExactClass` 时会先查全路径对象缓存，其他条件不会命中该快路；结果受初始化和缓存配置影响。

### 类短名与对象短名

```cpp
UObject* FindObjectByClassAndName(FName ClassName, FName ObjectShortName,
                                  int32 RequiredFlags = {}, int32 BannedFlags = {});
// 另有 (const CharType* ClassName, const CharType* ObjectShortName, ...) 重载
void FindObjectsByClassAndName(size_t NumObjectsToFind, FName ClassName,
                               FName ObjectShortName, std::vector<UObject*>& OutStorage,
                               int32 RequiredFlags = {}, int32 BannedFlags = {},
                               bool bExactClass = true);
// 另有 CharType* 名称重载；省略 NumObjectsToFind 的两个重载搜索全部匹配项
```

`ClassName`/`ObjectShortName` 为**短名**，至少提供一个非空 `FName`；两者均为空时抛 `std::runtime_error`。`RequiredFlags` 要求全部对象标志位，`BannedFlags` 排除任一对象标志位，默认 `0` 不过滤。多对象版本的 `bExactClass=true` 只接受直接类；`false` 遍历父类。单对象版等效限制数量为 1、精确类；不存在返回 `nullptr`。`NumObjectsToFind=0` 表示无限制；`OutStorage` **追加**匹配结果，不清空、不持有对象；`void` 返回，不以结果数表示成功。匹配对象名时使用 `FName` 的严格相等（包括编号），与路径 API 的语义不同。已声明的 `FindObject(FName/CharType* 类名, FName/CharType* 对象名, ...)` 及 `FindObjects(...)` 是 `[[deprecated]]` 别名，参数、默认值和返回值同相应 `*ByClassAndName` 函数，不要用于新代码。

```cpp
UObject* FindFirstInstanceOfClass(FName ClassName);
void FindAllInstancesOfClass(FName ClassName, std::vector<UObject*>& OutStorage);
// 两组均另有 const CharType*、StringViewType、const StringType&、
// std::string_view、const std::string& 的 ClassName 重载。
```

输入类**短名**，匹配该类及其父类名。排除 `RF_ClassDefaultObject`、`RF_ArchetypeObject` 与 `UClass` 对象；不等同于查类定义，也不保证返回对象长期存活。首个版本未找到返回 `nullptr`，全部版本追加到 `OutStorage` 且返回 `void`。`FindFirstOf`/`FindAllOf` 是分别具有相同六种参数类型和同样输出的 `[[deprecated]]` 别名。

### 枚举 `GUObjectArray`

```cpp
auto ForEachUObject(const std::function<LoopAction(UObject*, int32, int32)>& Callback) -> void;
auto ForEachUObjectInChunk(int32_t ChunkIndex,
    const std::function<LoopAction(UObject*, int32)>& Callback) -> void;
auto ForEachUObjectInRange(int32_t Start, int32_t End,
    const std::function<LoopAction(UObject*, int32, int32)>& Callback) -> void;
```

回调入参依次为借用的 `UObject*`、**chunk index**、chunk 内 item index（旧版非分块数组的 chunk 为 `0`）；`InChunk` 只传对象和 item index。注意 `ForEachUObject` 头文件参数名将两个 `int32` 称作 `object_index, chunk_index`，但实现实际顺序为 chunk、item，应按实现使用。回调返回 `LoopAction::Continue` 继续，`LoopAction::Break` 终止；函数本身返回 `void`。未初始化的 `GUObjectArray` 不访问对象。跳过空槽和 unreachable 对象；未保证快照、并发遍历安全、跨回调存活。`InChunk` 在 UE ≤4.19 的非分块数组上退化为**全数组**遍历，`ChunkIndex` 不生效；分块版本超出最大 chunk 直接返回。`InRange` 的非分块分支 `[Start, min(End, NumElements))`，分块分支当前实现的计数/起点处理不同，**不要依赖分块版本的严格 `[Start, End)` 边界**；负索引没有校验。调用方应提供合法非负索引并在回调内自行确认目标。

```cpp
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/ObjectSearch/UObjectSearch.hpp>
#include <Unreal/UPackage.hpp>

void SearchExamples()
{
    using namespace RC::Unreal;
    auto* obj = UObjectGlobals::FindObject(nullptr, UObjectGlobals::ANY_PACKAGE,
                                           TEXT("/Script/Engine.Actor"));
    std::vector<UObject*> instances;
    UObjectGlobals::FindAllInstancesOfClass(STR("Actor"), instances);
    (void)obj; // 使用前仍须验证存活与线程。
}
```

### `ObjectSearcher` 候选池

源码：[接口](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Searcher/ObjectSearcher.hpp)、[实现](../../../crates/ue4ssl-object-searcher/native/src/ObjectSearcher.cpp)、[池初始化](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/UnrealInitializer.cpp)、[创建/删除监听](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/ClassListener.cpp)。

```cpp
using ObjectSearcherForEachPredicate = std::function<LoopAction(UObject*)>;
ObjectSearcher FindObjectSearcher(UClass* Class, UStruct* SuperStruct);
template<class ClassType, class SuperStructType = AnySuperStruct>
ObjectSearcher FindObjectSearcher();
// AnyClass、AnySuperStruct 为 StaticClass() 返回 nullptr 的筛选哨兵。
size_t ObjectSearcher::PoolSize();
bool ObjectSearcher::IsFast();
void ObjectSearcher::ForEach(const ObjectSearcherForEachPredicate& Predicate);
```

`Class` 限制 `IsA(Class)`；`SuperStruct` 对类对象检查 `IsChildOf`，对其他对象检查其类的 `IsChildOf`；两个 `nullptr` 表示不限。`ForEach` 回调对象为借用指针，返回 `Break` 可中断；无值返回。`PoolSize()` 为池内条目数（无池返回 0），`IsFast()` 为**池非空**而非“使用了快搜索实现”（空池即使配有快搜索器也返回 `false`）。当前仅为 `UClass` 全类、`UClass` 的 Actor 子类、`AActor` 实例建立专用池；其他组合回退全数组遍历。`HashSearcherKey` 和 `AllSearcherPools` 暴露为底层机制，不要自行添加未配套生命周期通知的池。池只保存 `FUObjectItem*`；遍历时检查空对象/unreachable，删除监听从池移除对象；对象指针不能当稳定句柄。创建监听在当前实现中为空，参见下一段。

缓存边界：全路径 `GetGlobalObject` 缓存仅在 `bUseUObjectArrayCache` 打开时启用，由成功查找填充、删除监听失效；缓存读取并未加锁，且对象重命名/换 Outer 并无对应更新逻辑，不可把它当强一致对象注册表。专用池在后初始化阶段填充并注册删除监听，但当前 `NotifyUObjectCreated` 为空，**初始化之后新建对象未必加入快池**；空池的 `IsFast=false` 不会自动切换搜索器到慢路径。`ObjectSearcher::ForEach` 持池锁执行回调：不能在回调里重入同池遍历或触发需要该锁的对象删除。慢路径启用 `bUseNativeClassEnumeration` 而未装原生枚举器时仍退回 `GUObjectArray`。这些行为见上述实现，不应被误写为“缓存命中保证完整/线程安全”。

## 2. 构造与加载边界

源码：[声明与参数](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectGlobals.hpp)、[版本适配](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/UObjectGlobals.cpp)、[临时包](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UPackage.hpp)。

```cpp
FStaticConstructObjectParameters(const UClass* InClass, UObject* InOuter = nullptr);
UObject* UObjectGlobals::StaticConstructObject(const FStaticConstructObjectParameters& Params);
template<class ObjectType = UObject*>
ObjectType UObjectGlobals::StaticConstructObject(const FStaticConstructObjectParameters& Params);
template<class ObjectType>
ObjectType* UObjectGlobals::NewObject(UObject* Outer, const UClass* Class,
    FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags,
    UObject* Template = nullptr, bool bCopyTransientsFromClassDefaults = false,
    FObjectInstancingGraph* InInstanceGraph = nullptr, UPackage* ExternalPackage = nullptr);
template<class ObjectType>
ObjectType* UObjectGlobals::NewObject(UObject* Outer, FName Name,
    EObjectFlags Flags = RF_NoFlags, UObject* Template = nullptr,
    bool bCopyTransientsFromClassDefaults = false,
    FObjectInstancingGraph* InInstanceGraph = nullptr);
UPackage* GetTransientPackage(); // RC::Unreal 命名空间；返回借用的临时包
```

`Params` 必填 `Class`，`Outer` 默认空；`Name` 默认构造，若 `NAME_None` 则由引擎分配唯一名；`SetFlags=RF_NoFlags`、`InternalSetFlags=None`、`Template=nullptr`（缺省用类默认对象）、`bCopyTransientsFromClassDefaults=false`、`bAssumeTemplateIsArchetype=false`、`InstanceGraph=nullptr`、`ExternalPackage=nullptr`。`Class` 是要实例化的类；`Outer` 是新对象 Outer；`Template` 提供属性原型；`InstanceGraph` 携带实例对象映射；`ExternalPackage` 指定外部包。返回借用的新对象，未就绪或 `Class==nullptr`（UE ≥4.26 路径）返回 `nullptr`；引擎构造失败也不可假定成功。UE ≤4.25 使用旧版引擎函数 ABI；版本布局由封装层适配，不要直接使用 `Below56/Above55` 结构。`NewObject` 第一重载使用显式 `Class`，第二重载使用 `ObjectType::StaticClass()` 且**要求 `Name`**；模板静态转换不验证运行时类型。必须在可安全调用引擎构造的线程/阶段使用，`Outer`/模板/类必须有效。

`UObjectGlobals::SetupStaticConstructObjectInternalAddress(void* FunctionAddress) -> void` 及 `GlobalState::StaticConstructObjectInternal[Deprecated]` 虽出现在头中，但用于运行时初始化绑定目标引擎函数地址；调用前者会同时覆盖新旧 ABI 的地址，既不构造也不加载对象。Mod 不应自行传扫描到的未经 ABI/版本确认的地址。

此处并**没有**导出的通用 `StaticLoadObject`、`LoadObject` 或 `LoadClass` API；`FindObject*` 仅查**已加载**对象，`GetTransientPackage()` 也不加载磁盘资源。地图加载的 `RegisterLoadMap*Callback` 是观察/拦截 `UEngine::LoadMap`，不是资产加载函数。不要把未找到对象解释为资产不存在。

## 3. UFunction 专用 Hook

源码：[声明](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectGlobals.hpp)、[实现](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/UObjectGlobals.cpp)、[参数上下文](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UFunctionStructs.hpp)。

```cpp
using UnrealScriptFunctionCallable =
    std::function<void(UnrealScriptFunctionCallableContext& Context, void* CustomData)>;
std::pair<int,int> UObjectGlobals::RegisterHook(UFunction* Function,
    UnrealScriptFunctionCallable Pre, UnrealScriptFunctionCallable Post, void* CustomData);
std::pair<int,int> UObjectGlobals::RegisterHook(const StringType& FunctionFullNameNoType,
    UnrealScriptFunctionCallable Pre, UnrealScriptFunctionCallable Post, void* CustomData);
std::pair<int,int> UObjectGlobals::RegisterHookOwned(UFunction* Function,
    UnrealScriptFunctionCallable Pre, UnrealScriptFunctionCallable Post,
    std::shared_ptr<void> CustomData);
// RegisterHookOwned 同样有 const StringType& FunctionFullNameNoType 重载
bool UObjectGlobals::UnregisterHook(std::pair<int,int> Ids);
void UObjectGlobals::UnregisterHook(UFunction* Function, std::pair<int,int> Ids);
void UObjectGlobals::UnregisterHook(const StringType& FunctionFullNameNoType,
                                    std::pair<int,int> Ids);
```

`Function` 是借用的 `UFunction*`；名称重载用不含类型的**完整路径**查找，例如 `/Script/Engine.Actor:K2_DestroyActor`；未找到或空函数返回 `{-1,-1}` 并记录错误；无法识别原生/脚本执行路径则抛 `std::runtime_error`。`Pre`/`Post` 可为空的 `std::function`（仅需一阶段时传 `{}`）；默认值**没有**，四个实参都要提供。返回 `{preId,postId}`：原生函数是两个不同的通用 ID，脚本函数两者相同；保持整对传回注销函数，不要单独拼装。原生 `FUNC_Native` 分派到 `UFunction::RegisterPreHook/PostHook`；脚本函数经全局 `ProcessLocalScriptFunction`（UE ≥4.22）或 `ProcessInternal`（更早）执行。名称版注册时必须已加载函数，不能注册未来才加载的路径。

`Context.Context` 为调用对象，`Context.TheStack` 为本次 `FFrame&`，`Context.RESULT_DECL` 为结果内存 `void*`；`GetParams<ParamStruct>() -> ParamStruct&` 将 `TheStack.Locals()` 解作**当前函数的正确参数布局**，`SetReturnValue<ReturnType>(NewValue) -> void` 向 `RESULT_DECL` 写入值；类型/布局不匹配、空结果地址均非自动验证。回调返回 `void`，通过修改参数/结果缓冲影响执行；该 API **没有** `PreventOriginalFunctionCall()` 参数。`CustomData` 原始指针由调用者持有，不能在回调仍执行时释放；`RegisterHookOwned` 将 `shared_ptr<void>` 保存在有回调的闭包中，回调第二参数收到 `.get()`，用于避免异步回调访问已释放状态。脚本回调中发生 SEH 会被记录并隔离，勿据此假设 C++ 异常同样安全。

`UnregisterHook(Ids)` 找到时返回 `true`，不存在返回 `false`；带 `Function`/名称的 `void` 版本实际上**忽略该参数**，按 ID 注销，没找到会写警告。脚本路径在**外部**注销会等待活跃调用退出；在任何全局脚本回调内注销则不等待其他回调，因此原始 `CustomData` 有并发释放危险，应使用 `RegisterHookOwned`。Mod 卸载前仍要注销，等待已执行的回调退出，不要让回调代码随 DLL 卸载。

```cpp
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>

void ObserveFunction(RC::Unreal::UFunction* function)
{
    using namespace RC::Unreal;
    if (!function) return; // 调用方还须确认函数当前有效。
    auto ids = UObjectGlobals::RegisterHookOwned(
        function,
        [](UnrealScriptFunctionCallableContext& call, void* state) {
            // call.GetParams<该 UFunction 的真实参数结构>()；勿保存 call 引用。
            (void)call; (void)state;
        },
        {}, std::shared_ptr<void>{});
    if (ids.first != -1) {
        // 此例立即移除；实际 Mod 保存 ID，到回调外且卸载前注销。
        UObjectGlobals::UnregisterHook(ids);
    }
}
```

## 4. 引擎生命周期与通用 Hook

源码：[Hook API/类型](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Hooks/Hooks.hpp)、[回调数据](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Hooks/CallbackIterationData.hpp)、[ID](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Hooks/GlobalCallbackId.hpp)、[注册实现](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/Hooks/Hooks.cpp)。命名空间 `RC::Unreal::Hook`。所有下表现代注册函数具有**精确模式**：

```cpp
GlobalCallbackId Register<表中的名称>Callback(<别名>CallbackWithData Callback,
                                                FCallbackOptions Options);
using <别名>CallbackWithData =
    std::function<void(TCallbackIterationData<R>& Data, 表中其余参数...)>;
```

其中 `Callback`、`Options` **没有默认值**；`R` 是原函数返回类型，而回调自己总返回 `void`。`Options` 字段：`bOnce=false` 首次执行后自动失效；`bReadonly=false`，设为 true 时在同阶段其他回调后执行且不应调用非 const 的 `Data` 方法；`OwnerModName`、`HookName` 默认为空（内部生成 `UnknownMod`、`PreHook`/`PostHook` 等名），建议显式填写。成功返回非零 `GlobalCallbackId`，安装失败返回 `ERROR_ID=0`。每个 Pre/Post 注册是**独立 ID**，需分别注销。表内 `XPre/Post` 意为**两个确切名称** `RegisterXPreCallback`、`RegisterXPostCallback`，两者相同回调别名、参数/返回类型；唯一例外 ProcessConsoleExec 单阶段见下。

| 函数组（`Register...Callback`） | `CallbackWithData` 别名；`R` | `Data` 后的参数（顺序/类型、含义） |
|---|---|---|
| `StaticConstructObjectPre/Post` | `StaticConstructObjectCallbackWithData`; `UObject*` | `const FStaticConstructObjectParameters& Params`：构造设置；Post 可用 Data 读原对象 |
| `ProcessEventPre/Post` | `ProcessEventCallbackWithData`; `void` | `UObject* Context` 调用者，`UFunction* Function`，`void* Parms` 参数缓冲 |
| `ProcessConsoleExecGlobalPre/Post` | `ProcessConsoleExecCallbackWithData`; `bool` | `UObject* Context`, `const TCHAR* Cmd`, `FOutputDevice& Ar`, `UObject* Executor` |
| `ProcessConsoleExec`（仅 Post，且只在 `UGameViewportClient` Context 时触发） | `ProcessConsoleExecCallbackWithData`; `bool` | 同上 |
| `ProcessInternalPre/Post` | `ProcessInternalCallbackWithData`; `void` | `UObject* Context`, `FFrame& Stack`, `void* RESULT_DECL` |
| `ProcessLocalScriptFunctionPre/Post` | `ProcessLocalScriptFunctionCallbackWithData`; `void` | `UObject* Context`, `FFrame& Stack`, `void* RESULT_DECL` |
| `InitGameStatePre/Post` | `InitGameStateCallbackWithData`; `void` | `AGameModeBase* Context` |
| `LoadMapPre/Post` | `LoadMapCallbackWithData`; `bool` | `UEngine* Context`, `FWorldContext& WorldContext`, `FURL URL`（值传递）, `UPendingNetGame* PendingGame`, `FString& Error`（可写错误文本） |
| `EngineTickPre/Post` | `EngineTickCallbackWithData`; `void` | `UEngine* Context`, `float DeltaSeconds`, `bool bIdleMode` |
| `BeginPlayPre/Post` | `BeginPlayCallbackWithData`; `void` | `AActor* Context` |
| `EndPlayPre/Post` | `EndPlayCallbackWithData`; `void` | `AActor* Context`, `EEndPlayReason EndPlayReason` |
| `AActorTickPre/Post` | `AActorTickCallbackWithData`; `void` | `AActor* Context`, `float DeltaSeconds` |
| `GameViewportClientTickPre/Post` | `GameViewportClientTickCallbackWithData`; `void` | `UGameViewportClient* Context`, `float DeltaSeconds` |
| `CallFunctionByNameWithArgumentsPre/Post` | `CallFunctionByNameWithArgumentsCallbackWithData`; `bool` | `UObject* Context`, `const TCHAR* Str` 命令, `FOutputDevice& Ar`, `UObject* Executor`, `bool bForceCallWithNonExec` |
| `ULocalPlayerExecPre/Post` | `ULocalPlayerExecCallbackWithData`; `bool` | `ULocalPlayer* Context`, `UWorld* InWorld`, `const TCHAR* Cmd`, `FOutputDevice& Ar` |
| `UFunctionBindPre/Post` | `UFunctionBindCallbackWithData`; `void` | `UFunction* Function`；Post 可读取/替换绑定后的原生函数指针 |

`ProcessConsoleExec` 单阶段函数的完整名称为 `RegisterProcessConsoleExecCallback(ProcessConsoleExecCallbackWithData, FCallbackOptions) -> GlobalCallbackId`，另外两者为 `RegisterProcessConsoleExecGlobalPreCallback`/`RegisterProcessConsoleExecGlobalPostCallback`。其余各行逐一按表的 `RegisterXPreCallback`/`RegisterXPostCallback` 展开，不存在隐式默认 `Options`。`StaticConstructObjectDeprecatedSignature` 和 `ProcessConsoleExecGlobalSignature` 也在头中作为签名别名公开：前者使用 `StaticConstructObject_Internal_Params_Deprecated`（旧版类/Outer/Name/flags/内部 flags/template/copy/graph/archetype/external package 十参数）；后者的参数与 `ProcessConsoleExecSignature` 相同。现代可注册入口按表使用通用版本。

`TCallbackIterationData<void>` 与 `TCallbackIterationData<R>` 均提供 `PreventOriginalFunctionCall() -> void`（只在 Pre 阶段真正阻止；Post 只更新标记）、`OriginalFunctionCallPrevented() -> bool`、`GetCallbackName()/GetOwnerModName()/GetDetourName() -> StringViewType`、`RemoveSelf() -> void`、`Log(StringViewType Msg) -> void`、`IsReadOnly() -> bool`。非 `void` 版本另有 `TrySetReturnValue(R Value) -> bool`（只允许第一个成功设置的回调覆盖结果，readonly 返回 false）、`GetCurrentResolvedReturnValue()/GetReturnValueOverride()/GetOriginalFunctionCallResult() -> R`、`IsReturnValueOverrode() -> bool`。Pre 中尚未执行原函数时结果为默认构造值；阻止 `bool`/指针原函数却不覆盖返回值会将默认 `false`/`nullptr` 返回游戏。参数为按原引擎函数签名传给各回调的值/引用；修改值传递的局部参数不等于修改原引擎调用实参，指针与引用仅在本次执行期间有效。`bReadonly` 回调不应取消原调用、改返回值或调用 `RemoveSelf()`。同阶段普通回调按注册顺序执行，readonly 排在后面；`bOnce` 至多执行一次。

`GlobalCallbackId` 实际为 `uint64_t`。调试 ID 可构造 `explicit FGlobalCallbackIdDecoder(GlobalCallbackId InId)`，其 `IsOnceCallback()/IsPreHook()/IsReadOnly() -> bool`、`GetDetourOwnerValue() -> uint64_t`、`GetId() -> GlobalCallbackId`、`GetDetourOwnerName() -> StringViewType` 分解注册标记及所属 detour；不用于确认回调仍然存活，不能替代注销结果。返回的名称视图不拥有底层存储。


```cpp
auto UnregisterCallback(GlobalCallbackId Id) -> bool;
auto StartCallbackGarbageCollector() -> bool;
auto StopCallbackGarbageCollector() -> bool;
void AddRequiredObject(const std::vector<StringViewType>& ObjectFullTypelessName);
bool AllRequiredObjectsConstructed();
```

`UnregisterCallback` 返回是否找到并移除 ID（`0`/不存在为 false）；**不得在任一该体系 Hook 回调内直接或间接调用**，会死锁；在自己的回调内使用 `Data.RemoveSelf()`。在外部保存每个 ID，卸载前注销并确保回调不再运行。`Start/StopCallbackGarbageCollector` 返回对应后台线程开始/停止是否成功，仅负责失效回调清理，**不替代**卸载前的安全注销。`AddRequiredObject` 接受按 Outer 路径顺序拆开的无类型名字视图，已存在则不加入待构造列表，否则登记等待并返回 `void`；`AllRequiredObjectsConstructed()` 搜索并移除已找到项，列表清空时为 `true`；它们面向初始化流程，并非构造 API。`HookStaticConstructObject()`、`HookProcessEvent()`、`HookProcessConsoleExec()`、`HookUStructLink()`、`HookProcessInternal()`、`HookProcessLocalScriptFunction()`、`HookLoadMap()`、`HookEngineTick()`、`HookInitGameState()`、`HookBeginPlay()`、`HookEndPlay()`、`HookAActorTick()`、`HookGameViewportClientTick()`、`HookCallFunctionByNameWithArguments()`、`HookULocalPlayerExec()`、`HookUFunctionBind()` 是 `RC::Unreal` 下兼容的 `void()` 底层安装入口，不是回调注册/注销 API；通常使用 `Register*Callback` 自动安装对应目标，不自行重复安装。

旧版**无 `Data`**、标有 `[[deprecated]]` 的同名入口均只接收一个旧回调并返回 `void`，因此没有 ID 供调用方注销：

- `StaticConstructObjectPre`: `std::function<UObject*(const FStaticConstructObjectParameters&)>`；`Post`: `std::function<UObject*(const FStaticConstructObjectParameters&, UObject* ConstructedObject)>`；非空返回值尝试覆盖结果。
- `ProcessEventPre/Post`: `std::function<void(UObject*, UFunction*, void*)>`；`ProcessInternalPre/Post`、`ProcessLocalScriptFunctionPre/Post`: `std::function<void(UObject*, FFrame&, void*)>`；`InitGameStatePre/Post`: `std::function<void(AGameModeBase*)>`；`BeginPlayPre/Post`: `std::function<void(AActor*)>`；`EndPlayPre/Post`: `std::function<void(AActor*, EEndPlayReason)>`；`AActorTickPre/Post`: `std::function<void(AActor*, float)>`；`GameViewportClientTickPre/Post`: `std::function<void(UGameViewportClient*, float)>`。
- `LoadMapPre/Post`: `std::function<std::pair<bool,bool>(UEngine*, FWorldContext&, FURL, UPendingNetGame*, FString&)>`；`CallFunctionByNameWithArgumentsPre/Post`: `std::function<std::pair<bool,bool>(UObject*, const TCHAR*, FOutputDevice&, UObject*, bool)>`；`ProcessConsoleExecGlobalPre/Post`: `std::function<std::pair<bool,bool>(UObject*, const TCHAR*, FOutputDevice&, UObject*)>`；其 pair 第一项决定是否尝试用第二项覆盖 bool 结果，**不是**取消原函数。
- `EngineTickPre/Post`: `std::function<void(UEngine*, float)>`（不暴露 `bIdleMode`）；`ProcessConsoleExec`（仅 Post/Viewport）：`std::function<bool(UObject*, const TCHAR*, FOutputDevice&, UObject*)>`，返回 `true` 才尝试覆盖为 `true`；`ULocalPlayerExecPre/Post`: `std::function<ULocalPlayerExecCallbackReturnValue(ULocalPlayer*, UWorld*, const TCHAR*, FOutputDevice&)>`，返回结构默认 `{UseOriginalReturnValue=true, NewReturnValue=false, ExecuteOriginalFunction=true}`；设 `ExecuteOriginalFunction=false` 在 Pre 才有效、设 `UseOriginalReturnValue=false` 才尝试覆盖结果。`UFunctionBind` 无旧版注册入口。

```cpp
#include <Unreal/Hooks.hpp>

void ObserveBeginPlay()
{
    using namespace RC::Unreal::Hook;
    FCallbackOptions opts{};
    opts.OwnerModName = STR("ExampleMod");
    opts.HookName = STR("ObserveBeginPlay");
    auto id = RegisterBeginPlayPostCallback(
        [](TCallbackIterationData<void>& data, RC::Unreal::AActor* actor) {
            // actor 仅在回调内借用；回调内撤销使用 data.RemoveSelf()。
            (void)data; (void)actor;
        }, opts);
    if (id != ERROR_ID) {
        // 此例立即移除；实际 Mod 保存 ID，在回调外、卸载前注销。
        UnregisterCallback(id);
    }
}
```

## 5. 原生 Detour、IAT 与指令辅助

源码：[x64Detour](../../../crates/ue4ssl-hook/include/polyhook2/Detour/x64Detour.hpp)、[IatHook](../../../crates/ue4ssl-hook/include/polyhook2/PE/IatHook.hpp)、[FnCast](../../../crates/ue4ssl-hook/include/polyhook2/Misc.hpp)、[C ABI](../../../crates/ue4ssl-hook/include/UE4SSHook/ffi.hpp)、[后端行为](../../../crates/ue4ssl-hook/src/lib.rs)。这是一组 Windows x64 原生地址 Hook，不是 `UFunction`/生命周期 Hook；回调和原函数的调用约定、参数、返回类型必须完全一致。内存修补与模块卸载时不可与执行中的目标代码争用。

```cpp
PLH::x64Detour(uint64_t fn_address, uint64_t fn_callback, uint64_t* user_tramp_var);
bool hook(); bool unHook(); bool isHooked() const;
PLH::x64Detour::detour_scheme_t getDetourScheme() const;
void setDetourScheme(detour_scheme_t scheme);
// scheme: VALLOC2=1, INPLACE=2, CODE_CAVE=4, INPLACE_SHORT=8,
// RECOMMENDED=VALLOC2|INPLACE|CODE_CAVE, ALL=RECOMMENDED|INPLACE_SHORT。
PLH::IatHook(const std::string& dll_name, const std::string& api_name,
             const char* fn_callback, uint64_t* user_orig_var,
             const std::wstring& module_name);
PLH::IatHook(const std::string& dll_name, const std::string& api_name,
             uint64_t fn_callback, uint64_t* user_orig_var,
             const std::wstring& module_name);
bool hook(); bool unHook(); bool isHooked() const;
template<class FnCastTo> FnCastTo PLH::FnCast(uint64_t fn_to_cast, FnCastTo);
// 另有 FnCast(void* fn_to_cast, FnCastTo)；均仅 reinterpret_cast，无验证。
```

`fn_address` 为目标代码地址，`fn_callback` 为替换函数地址；`user_tramp_var` 指向**调用方持有且持续有效的 `uint64_t` 槽**，安装成功写 trampoline 供回调调用原函数；创建时置零，成功 `unHook` 后置零。`IatHook` 的 `dll_name`/`api_name` 为导入表 DLL/API 名，`module_name` 是目标**导入方模块**名（空宽字符串则枚举已加载模块），`user_orig_var` 同样由调用方持续持有，写入导入表原函数地址；成功卸载 Hook **不会清零**该槽，不能把它误当活跃状态。创建失败的 C++ 包装对象仍可存在，但 `hook()` 为 false。`hook()` 已安装返回 true；`unHook()` 未安装或恢复失败返回 false；`isHooked()` 反映包装器后端状态。析构调用 destroy，尝试恢复；恢复失败不意味着目标已安全撤销，**不要在可能仍跳到回调时卸载 DLL**。类不可拷贝/移动，`setDetourScheme` 记录并下传枚举位掩码；当前后端未按该掩码选择多种算法，切勿以它保证特定跳板布局。

底层 `extern "C"` 提供与上面包装等价的接口（明确所有权，方便非 C++ 边界）：

```cpp
Ue4ssHookDetourHandle* ue4ss_detour_create(uint64_t target, uint64_t callback,
                                            uint64_t* user_trampoline);
void ue4ss_detour_destroy(Ue4ssHookDetourHandle* handle);
bool ue4ss_detour_hook(Ue4ssHookDetourHandle* handle);
bool ue4ss_detour_unhook(Ue4ssHookDetourHandle* handle);
bool ue4ss_detour_is_hooked(const Ue4ssHookDetourHandle* handle);
void ue4ss_detour_set_scheme(Ue4ssHookDetourHandle* handle, uint32_t scheme);
Ue4ssHookIatHookHandle* ue4ss_iat_hook_create(const char* dll_name,
    const char* api_name, uint64_t callback, uint64_t* user_original,
    const wchar_t* module_name);
void ue4ss_iat_hook_destroy(Ue4ssHookIatHookHandle* handle);
bool ue4ss_iat_hook_hook(Ue4ssHookIatHookHandle* handle);
bool ue4ss_iat_hook_unhook(Ue4ssHookIatHookHandle* handle);
bool ue4ss_iat_hook_is_hooked(const Ue4ssHookIatHookHandle* handle);
```

`create` 返回需由**对应 destroy** 销毁的不透明 handle，失败 `nullptr`；非零目标/回调、非空槽为 detour 必要条件；IAT 还需有效 DLL/API 名。`destroy(nullptr)` 安全；其余 null handle 的 bool 操作返回 false，setter 忽略。`dll_name`/`api_name` 为 NUL 结尾字符串，`module_name` 可空；不能把 C++/Rust 内存直接混用释放。回调和写回槽直到成功撤销且无在途调用前必须有效。

```cpp
void* ue4ss_asm_resolve_jmp(void* instruction_ptr);
void* ue4ss_asm_resolve_call(void* instruction_ptr);
void* ue4ss_asm_resolve_function_address_from_potential_jmp(void* function_ptr);
void* ue4ss_asm_find_nth_call_target(void* function_ptr, uint64_t max_bytes,
                                     uint32_t nth_call);
void* ue4ss_asm_resolve_jmp_target_or_self(void* function_ptr, uint64_t max_bytes);
bool ue4ss_helper_check_readable(void* handle, void* src_ptr);
```

前两者解析指令位置的 `jmp`/`call` 目标，不匹配或不可解析时返回 `nullptr`；`resolve_function_address_from_potential_jmp` 沿跳转链解析，非跳转（或首指令解码失败）则可能返回原地址；`find_nth_call_target` 在 `max_bytes` 范围找第 `nth_call` 个 call 目标，**从 1 开始计数**（0/未找到返回 `nullptr`）；`resolve_jmp_target_or_self` 解析范围内跳转，未发现时返回原地址，解码失败则返回 `nullptr`。这些结果均不是拥有的内存；传入地址/长度须确实指向可读的本进程指令，不能用任意指针试探。`ue4ss_helper_check_readable` 的 `handle` **不是** detour handle，而是指向进程 `HANDLE` 值的内存，`src_ptr` 是待读地址；读取一个指针大小字节成功返回 true，参数为空/不可读返回 false，不保证后续访问仍安全。

```cpp
#include <polyhook2/Detour/x64Detour.hpp>

void InstallAndRemoveDetour(uint64_t target_address, uint64_t replacement_address)
{
    // 实际 Mod 将 detour 和 trampoline 同时保存为长生命周期成员；
    // 替换函数 ABI 必须与目标一致，且能安全读取 trampoline。
    uint64_t trampoline = 0;
    PLH::x64Detour detour{target_address, replacement_address, &trampoline};
    if (detour.hook()) {
        // 停止新调用、等待在途调用后再撤销；失败时不得卸载回调 DLL。
        (void)detour.unHook();
    }
}
```

## 6. 单遍签名扫描

源码：[接口](../../../crates/patternsleuth-bind/native/SinglePassSigScanner/include/SigScanner/SinglePassSigScanner.hpp)、[实现](../../../crates/patternsleuth-bind/native/SinglePassSigScanner/src/SinglePassSigScanner.cpp)。命名空间 `RC`，Windows 已加载模块内存扫描，不加载资源/模块，也不安装 Hook。

```cpp
enum class ScanTarget { MainExe, /* Unreal 各模块，详见头文件 */, Max };
std::string ScanTargetToString(ScanTarget scan_target);
std::string ScanTargetToString(size_t scan_target);
struct SignatureData {
    std::string signature{}; int32_t custom_data{}; std::string mask{};
};
struct SignatureContainerLight {
    size_t index_into_signatures{}; uint8_t* match_address{};
};
// 以下为 SignatureContainer 的公开成员；构造模板按值接收回调。
template<typename OnMatchFound, typename OnScanFinished>
SignatureContainer(std::vector<SignatureData> sig_param,
                   OnMatchFound on_match_found_param,
                   OnScanFinished on_scan_finished_param);
template<typename OnMatchFound, typename OnScanFinished>
SignatureContainer(std::vector<SignatureData> sig_param,
                   OnMatchFound on_match_found_param,
                   OnScanFinished on_scan_finished_param, bool store_results_param);
// 回调须可转换为 std::function<bool(SignatureContainer&)> /
// std::function<void(SignatureContainer&)>；不带 bool 构造时 store_results=false。
uint8_t* get_match_address() const;
size_t get_index_into_signatures() const;
bool& get_did_succeed(); bool get_did_succeed() const;
const std::vector<SignatureData>& get_signatures() const;
const std::vector<SignatureContainerLight>& get_result_store() const;
size_t get_match_signature_size() const;
// SinglePassScanner 的公开成员；SignatureContainerMap 为类内别名。
using SignatureContainerMap =
    std::unordered_map<ScanTarget, std::vector<SignatureContainer>>;
static void start_scan(SignatureContainerMap& signature_containers);
static void* string_scan(std::wstring_view string_to_scan_for,
                         ScanTarget target = ScanTarget::MainExe);
static void scanner_work_thread(uint8_t* start_address, uint8_t* end_address,
    SYSTEM_INFO& info, std::vector<SignatureContainer>& signature_containers);
static void scanner_work_thread_scalar(uint8_t* start_address, uint8_t* end_address,
    SYSTEM_INFO& info, std::vector<SignatureContainer>& signature_containers);
static void scanner_work_thread_stdfind(uint8_t* start_address, uint8_t* end_address,
    SYSTEM_INFO& info, std::vector<SignatureContainer>& signature_containers);
```

`ScanTarget` 的全部模块枚举见头文件；`ScanTarget::Max` 是计数哨兵，不能作为模块下标。`ScanTargetToString` 返回名称的 `std::string`，未知值抛 `std::runtime_error`。`SigScannerStaticData::m_modules_info` 是 `ScanTargetArray`，其 `operator[](ScanTarget) -> MODULEINFO&`；`m_is_modular` 决定逐模块扫描还是合并扫描（非 modular 路径会复制容器并使用合并模块信息）。调用前由宿主初始化模块信息；不存在/大小为零的模块不是有效扫描目标。`SignatureData.signature` 是空格分隔的十六进制 AoB，`?` 为通配 nibble/字节；`custom_data` 为用户自定义 `int32_t` 标签（默认 0），`mask` 字段存在但当前 scalar 解析器实际从签名文本生成匹配位，不应依赖另填 `mask` 纠正格式。`SinglePassScanner::ScanMethod {Scalar, StdFind}`、静态 `m_scan_method`（默认 Scalar）、`m_num_threads`（默认 8）、`m_multithreading_module_size_threshold`（默认 `0x1000000` 字节）可配置；多线程匹配回调可能在工作线程调用，匹配顺序不保证，回调要短且线程安全，不能在此假定游戏线程。`StdFind` 不允许签名以通配符开始（抛异常）。

非 modular 模式中多个 `ScanTarget` 的容器会合并，但模块范围取遍历中最后一次赋值的目标；不要以此实现跨模块扫描，必须将 `m_is_modular` 设为 true 后分别扫描已初始化的模块。该模式的容器是副本，写入的 `did_succeed`/`result_store` 不会同步回传到传入的 `jobs`，请在 `on_scan_finished` 中消费结果。多线程分片由 `m_num_threads` 控制，不要将其设为 0。

`scanner_work_thread` 及 `_scalar`、`_stdfind` 虽为公开静态函数，但为低层范围扫描入口：`start_address`/`end_address` 是本进程扫描范围边界，`info` 是 Windows `SYSTEM_INFO&`，最后一个参数是可修改的容器数组；返回 `void`，错误由底层异常或调用方检查结果处理。直接使用时必须提供有效地址、已初始化的系统信息及容器，且**不会替你调用**各容器的 `on_scan_finished`；常规调用选 `start_scan`。

每次命中先填充 `get_match_address()`（扫描内存中的借用地址，回调期间保证非空）、`get_index_into_signatures()`（当前容器中签名下标）、`get_match_signature_size()`（Scalar 为匹配字节数；StdFind 当前实现返回含对齐填充的模式长度，不能当真实签名字节数），再调用 `on_match_found`；返回 **true 表示停止该容器后续匹配**，false 继续扫描，不是“成功/失败”标志。设置 `store_results=true` 才把 `{index_into_signatures, match_address}` 追加到 `get_result_store()`；扫描结束调用 `on_scan_finished`，可读取结果。`get_did_succeed()` 是调用方可写标志，默认 false；扫描器**不会因为匹配自动设为 true**，需要调用方在有效命中时设定。`start_scan` 返回 `void`，未找到依然正常完成；`string_scan` 在目标模块搜索 `wstring_view` 文本，返回首个地址或 `nullptr`，地址指向模块内存而非新分配字符串。

```cpp
#include <SigScanner/SinglePassSigScanner.hpp>

void ScanMainModule()
{
    RC::SinglePassScanner::SignatureContainerMap jobs;
    jobs[RC::ScanTarget::MainExe].emplace_back(
        std::vector<RC::SignatureData>{{"48 8B ?? 90", 7, ""}},
        [](RC::SignatureContainer& hit) -> bool {
            if (hit.get_index_into_signatures() == 0) {
                hit.get_did_succeed() = true;
                // 地址仅在模块仍加载且内存有效期间使用。
                return true;
            }
            return false;
        },
        [](RC::SignatureContainer& done) {
            if (done.get_did_succeed()) {
                // 此处读取匹配结果，不要长期保存容器引用。
                (void)done.get_match_address();
            }
        });
    RC::SinglePassScanner::start_scan(jobs);
}
```

扫描地址可能随模块卸载、重载失效；再用扫描结果安装 detour 前必须验证指令、目标 ABI、模块生命周期。扫描并不提供可在任何线程安全读写引擎内存的许可。
