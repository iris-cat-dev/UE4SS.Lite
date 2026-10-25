# C++ Unreal 类型与反射 API

[返回 C++ API](README.md) · [Mod 生命周期](runtime.md) · [输入与日志](platform.md) · [查找与 Hook](search-and-hooks.md)

本页记录仓库附带的 `RC::Unreal` 兼容层，而非完整的 Epic SDK。示例假设 Unreal 初始化完成、相关类/对象已加载，并在**游戏线程**或该游戏明确允许的引擎调用上下文执行；`on_update` 和 `queue_event` 并不是游戏线程（见[运行时](runtime.md)）。除非明确说明，反射返回的 `UObject*`、`UClass*`、`UFunction*`、`FProperty*`、容器内元素地址都是借用：不拥有对象、不阻止 GC、不可跨关卡/卸载无条件缓存，也不可 `delete`。类型布局、函数地址、脚本参数和可用性依赖所运行的 UE 版本及游戏；签名不能替代运行时检查。

## 1. 头文件家族索引

以下路径相对 SDK include 根目录 `crates/ue4ssl-unreal-support/vendor/Unreal/include`，使用 `#include <Unreal/...>`。核心头的源码链接：

| 家族 | 入口与用途 |
|---|---|
| 对象 | [UObject.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObject.hpp)、[UObjectArray.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectArray.hpp)、[TObjectPtr.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/TObjectPtr.hpp)、[FWeakObjectPtr.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FWeakObjectPtr.hpp)：身份、标志、对象表、弱句柄 |
| 反射 | [Class.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/CoreUObject/UObject/Class.hpp)、[UnrealType.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/CoreUObject/UObject/UnrealType.hpp)、[FField.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FField.hpp)、[ReflectedFunction.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/ReflectedFunction.hpp)、[UFunctionStructs.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UFunctionStructs.hpp)：类、字段、属性、参数与调用；`UClass.hpp`、`UStruct.hpp`、`UFunction.hpp` 等是兼容转发入口，推荐直接包含 `Class.hpp` |
| 名字和文本 | [NameTypes.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/NameTypes.hpp)、[FString.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core/Containers/FString.hpp)、[FText.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FText.hpp)；`Unreal/FString.hpp` 转发到 Core 容器头 |
| 容器与内存 | [Array.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core/Containers/Array.hpp)、[Map.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core/Containers/Map.hpp)、[Set.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core/Containers/Set.hpp)、[UnrealMemory.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Core/HAL/UnrealMemory.hpp)、[FMemory.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FMemory.hpp) |
| 游戏便捷类 | [AActor.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/AActor.hpp)、[World.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/World.hpp)、[GameplayStatics.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/GameplayStatics.hpp)、[UDataTable.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/Engine/UDataTable.hpp)、[UAssetRegistry.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UAssetRegistry.hpp)、[UAssetRegistryHelpers.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UAssetRegistryHelpers.hpp) |
| 工具/宏 | [PackageName.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/PackageName.hpp)、[UnrealVersion.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UnrealVersion.hpp)、[ClassListener.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/ClassListener.hpp)、[PropertyMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/PropertyMacros.hpp)、[UObjectMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectMacros.hpp)、[BPMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/BPMacros.hpp) |
| 核心扩展 | [CustomProperty.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/UnrealCustom/CustomProperty.hpp)：核心内伪造属性元数据，**非通用新建 UE 属性接口** |

搜索/创建/加载对象、`UObjectGlobals`、对象扫描及 Hook 的注册/注销见[查找与 Hook](search-and-hooks.md)，不在此重复。

### 生成布局 getter 的阅读规则

诸如 `UObjectBase` 的 `GetClassPrivate()` / `GetOuterPrivate()`、`UStruct::GetPropertiesSize()`、`UFunction::GetParmsSize()`、`FProperty::GetOffset_Internal()`，声明来自 `#include <MemberVariableLayout_HeaderWrapper_*.hpp>`，不一定直接出现于类手写源码。示例：[UObjectBase 包装](../../../crates/ue4ssl-unreal-support/vendor/Unreal/generated_include/MemberVariableLayout_HeaderWrapper_UObjectBase.hpp)、[UStruct 包装](../../../crates/ue4ssl-unreal-support/vendor/Unreal/generated_include/MemberVariableLayout_HeaderWrapper_UStruct.hpp)、[UFunction 包装](../../../crates/ue4ssl-unreal-support/vendor/Unreal/generated_include/MemberVariableLayout_HeaderWrapper_UFunction.hpp)、[FProperty 包装](../../../crates/ue4ssl-unreal-support/vendor/Unreal/generated_include/MemberVariableLayout_HeaderWrapper_FProperty.hpp)。多数 `GetX()` 同时有可写引用与 `const` 引用版本；读出的引用指向游戏对象字段，**不是快照**，只能在对象及版本布局仍有效时使用。`GetSuperStruct()` 和 `GetMinAlignment()` 是显式版本适配封装，不应自行推测成员偏移。不可把任一生成 getter 的返回类型、固定偏移或 `sizeof` 推广到其他引擎版本；生成包装中的私有 getter 也不是 Mod 扩展点。

## 2. UObject：身份、继承、标志与属性

定义：[UObject.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObject.hpp)。`UObjectBase → UObjectBaseUtility → UObject`；`UObjectBase` 提供 `GetInternalIndex()`、`GetObjectItem()`、`IsA(UClass*) const` / `IsA<T>() const`，`UObject` 再提供以下操作：

| API | 入参/默认 | 出参、用途与失败 |
|---|---|---|
| `GetName() const -> StringType`；`GetFName() const -> FName` | 无 | 值返回对象短名；`FName` 用于名字查找 |
| `GetPathName(UObject* StopOuter = nullptr) const -> StringType`；`GetFullName(UObject* StopOuter = nullptr) const -> StringType` | 入：可选路径截断外层对象 | 值返回路径或带类名全名；另有 `GetPathName(StopOuter, StringType& ResultString) -> void`，后者写入出参。路径形式通常为 `Package.Outer:ObjectName` |
| `GetOutermost() -> UObject*`；`GetTypedOuter(UClass* OuterType) -> UObject*` / `GetTypedOuter<T>() -> T*` | 入：所需外层类 | 借用外层指针；找不到符合类型的外层返回空，使用前判空 |
| `IsA(UClass*) const -> bool` / `IsA<T>() const -> bool`；`Cast<T>(UObject*) -> T*` | 入：类型或对象 | 判断继承/安全向下转换；`Cast` 的对象为空或类型不匹配时返回空；`T::StaticClass()` 需要目标类已解析 |
| `SetFlagsTo(EObjectFlags NewFlags)`、`SetFlags(EObjectFlags NewFlags)`、`ClearFlags(EObjectFlags ClearFlags)` | 入：旗标 | `SetFlagsTo` **覆盖全部**，其余仅增/减；均返回 `void`。`HasAnyFlags(EObjectFlags)` / `HasAllFlags(EObjectFlags)` 返回布尔 |
| `HasAnyInternalFlags(EInternalObjectFlags)`、`IsUnreachable()`、`SetRootSet()`、`ClearRootSet()`、`IsRootSet()` | 入：内部标志（如有） | 对象表条目上的检查或 root 状态；`SetRootSet` / `ClearRootSet` 无返回。root 会影响 GC，可造成常驻，不是跨线程有效性保证；配对移除且不能释放游戏对象 |
| `GetPropertyByName(FName 或 const TCHAR*)`、`GetPropertyByNameInChain(...)` | 入：属性名 | 借用 `FProperty*`，找不到为 `nullptr`；`InChain` 查继承链。`GetFunctionByName(...)` / `GetFunctionByNameInChain(...)` 同理返回 `UFunction*` |
| `GetValuePtrByPropertyName(...)` / `GetValuePtrByPropertyNameInChain(...)` | 入：`FName` 或 `const TCHAR*`，可指定模板返回类型 | 指向对象内部值的借用 `void*` / `ReturnType*`；先确认属性确实存在且类型匹配，不能将 bitfield bool 当普通 `bool*` |
| `ProcessEvent(UFunction* Function, void* Parms)` | 入：目标函数与其匹配的可写参数内存 | `void`；调用函数并写出/返回参数到同一块内存。**不做类型、长度或线程自动适配**；找不到函数、参数布局不符或对象失效不能靠返回值判断 |

`Rename(const TCHAR* NewName = nullptr, UObject* NewOuter = nullptr, ERenameFlags Flags = REN_None) -> bool` 更改名称/外层；`nullptr` 表示对应引擎默认处理，`false` 为失败，旗标如 `REN_Test` 定义在 [UObjectMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectMacros.hpp)。这是引擎状态变更，不是字符串改写。`UObject::AllocateUObject(int32 Size, int32 Alignment, bool bAllowPermanent) -> UObjectBase*` 只分配**未构造的**内存；`bAllowPermanent` 当前会记录不支持并退回普通 malloc，不是普通 Mod 的对象创建方式。部分虚函数参数（`FRestoreForUObjectOverwrite`、`FObjectDuplicationParameters`、`FResourceSizeEx`、`FAssetRegistryTag`）目前仅 `void*` 占位，不应照搬 Epic 签名直接调用。

```cpp
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

void InspectObject(RC::Unreal::UObject* object) // 在合适的引擎调用上下文中
{
    using namespace RC::Unreal;
    if (!object || !object->IsA<UObject>()) return;
    const auto path = object->GetPathName(); // 值；可保留字符串，不能靠它保活对象
    if (auto* property = object->GetPropertyByNameInChain(STR("Health"))) {
        const auto offset = property->GetOffset_ForInternal(); // 当前版本的属性偏移
        (void)offset;
    }
    (void)path;
}
```

## 3. 类、字段、属性与调用

源码：[Class.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/CoreUObject/UObject/Class.hpp)、[UnrealType.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/CoreUObject/UObject/UnrealType.hpp)、[FField.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FField.hpp)。`UField` 是 `UObject`，`UStruct : UField`，`UClass` / `UFunction` / `UScriptStruct` 派生自 `UStruct`；UE 4.25 起属性多为独立 `FField` 层，**不能将 `FProperty*` 当作 `UField*` 或 UObject 指针**。`FField::GetOwnerVariant() -> FFieldVariant` 可表示字段或对象 owner；`GetTypedOwner<T>() -> T*` / `GetOutermostOwner() -> UObject*` 返回借用指针，缺失时须判空。`UField::AsFFieldUnsafe()` 在高于 4.25 的版本会抛异常；优先使用版本无关的字段迭代。

| API | 输入方向/默认 | 输出与边界 |
|---|---|---|
| `UStruct::GetSuperStruct() -> UStruct*&`（const 重载） / `UClass::GetSuperClass()` | 无 | 借用父类；根节点可空。`IsChildOf(UStruct*) -> bool` / `IsChildOf<T>() const -> bool` 检查继承 |
| `UStruct::FindProperty(FName PropertyName) -> FProperty*`；`GetFirstProperty() -> FProperty*` | 入：属性名字 | 借用字段，找不到为空；`HasChildren() -> bool` 仅检查子字段指针 |
| `TFieldRange<FProperty>(UStruct*, EFieldIterationFlags = Default)`；`TFieldRange<UFunction>(...)` | 入：结构、迭代旗标 | 可用于 range-for；`Default = IncludeSuper \| IncludeDeprecated`，`None` 不包括父类，`IncludeAll` 另含接口。`TReverseFieldRange<FProperty>` 为父到子，`TSuperStructRange` 从**父节点**开始，不含自身。旧 `ForEachProperty/Function/...` 已标弃用 |
| `UStruct::GetPropertiesSize() -> int32&`、`GetStructureSize() const -> int32` | 无 | 属性内存大小及按最小对齐计算的大小；`UScriptStruct::InitializeStruct(void* Dest, int32 ArrayDim = 1) const` / `DestroyStruct(...)` 对调用方提供的匹配大小内存做生命周期管理，不是内存分配器 |
| `UScriptStruct::CopyScriptStruct(void* Dest, const void* Src, int32 ArrayDim = 1)` | 入出：已准备的目标，入：源 | `void`；要求匹配 struct/数组维数；`GetSuperScriptStruct() -> UScriptStruct*` 在没有父 struct 时为空 |
| `UClass::CreateDefaultObject() -> UObject*`；`GetClassDefaultObject() -> TObjectPtr<UObject>&` | 无 | 引擎的默认对象（借用），不是给 Mod 所有的实例；`GetClassFlags() -> uint32&`、`HasAnyClassFlags(EClassFlags)`、`HasAllClassFlags(EClassFlags)`、`HasAnyCastFlag(EClassCastFlags) const` / `HasAllCastFlags(...) const` 用于查询/修改元数据，写引用须非常谨慎 |
| `UFunction::GetFunctionFlags() -> uint32&`（const 重载）；`HasAnyFunctionFlags(EFunctionFlags)` / `HasAllFunctionFlags(...)` | 入：函数旗标 | 元数据/布尔查询；生成 getter `GetNumParms() -> uint8&`、`GetParmsSize() -> uint16&` 分别含返回/输出参数计数及参数内存字节数；`GetReturnProperty() -> FProperty*` 无返回值时为空 |
| `FProperty::GetOffset_ForInternal() const -> int32`、`GetSize() const -> int32`、`GetPropertyFlags()`、`HasAnyPropertyFlags(uint64)` / `HasAllPropertyFlags(EPropertyFlags)` | 入：可选旗标 | 偏移、`ArrayDim * ElementSize` 总大小、旗标；这是当前反射布局，不是 C++ 任意结构的 `offsetof` |
| `FProperty::ContainerPtrToValuePtr<T>(UObject* 或 void* ContainerPtr, int32 ArrayIndex = 0) const -> T*` | 入：对象/非对象容器、固定数组索引 | 借用元素地址；UObject 顶层属性传 `UObject*`，非对象容器传 `void*`；实现对索引、对象有效性及所属类做 `check`，不是失败返回空 |
| `FProperty::Identical(const void* A, const void* B, uint32 PortFlags = 0) -> bool`、`Identical_InContainer(const void* A, const void* B, int32 ArrayIndex = 0, uint32 PortFlags = 0) -> bool` | 入：**已偏移**的值地址或**尚未偏移**的容器地址、端口旗标 | 比较值是否相同，不混淆两种地址语义 |
| `FProperty::InitializeValue(void* Dest) const` / `DestroyValue(void* Dest) const` / `CopyCompleteValue(void* Dest, const void* Src) const` | 入出：完整属性值内存；入：源 | 初始化**未构造**值、销毁已构造值、复制完整固定数组；`ClearValue(void*)` 只清理已有效的单个值。容器版本使用 `_InContainer`，并不负责分配容器 |
| `FProperty::AllocateAndInitializeValue() const -> void*` / `DestroyAndFreeValue(void*)` | 无 / 入：此前分配的值 | 一组配对分配和释放，不能以 `delete` / `std::free` 替换；不属于 UObject 管理 |
| `FProperty::ImportText_Direct(const TCHAR* Buffer, void* PropertyPtr, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const -> const TCHAR*`；`ImportText_InContainer(..., void* Container, ...)` | 入：文本、值地址或容器、owner、旗标、可选诊断设备 | 成功返回剩余输入指针，不能读取则空；`PPF_RestrictImportTypes` 时拒绝 config 属性；两者自动包含弃用属性处理。`ExportTextItem_Direct(FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope = nullptr) const` 与 `_InContainer` 写出文本 |

`FBoolProperty::GetPropertyValueInContainer(const void* A, int32 ArrayIndex = 0) -> bool` / `SetPropertyValueInContainer(void* A, bool Value, int32 ArrayIndex = 0) -> void` 按 bit mask 读写，别把 bool bitfield 强转成 `bool&`。[数值属性](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/CoreUObject/UObject/UnrealType.hpp) `FNumericProperty::GetSignedIntPropertyValue(void*) -> int64`、`GetUnsignedIntPropertyValue(const void*) -> uint64`、`GetFloatingPointPropertyValue(const void*) -> double` 用**已偏移**地址；`SetIntPropertyValue(void*, int64/uint64)`、`SetFloatingPointPropertyValue(void*, double)` 写值。`FArrayProperty::GetInner()`、`FMapProperty` 的 key/value、`FSetProperty` 的 element、`FStructProperty::GetStruct()` 来自生成 getter；不要把容器元素大小猜作主机侧 `sizeof(T)`。

```cpp
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

bool ReadFlag(RC::Unreal::UObject* object)
{
    using namespace RC::Unreal;
    if (!object) return false;
    auto* prop = CastField<FBoolProperty>(object->GetPropertyByNameInChain(STR("bEnabled")));
    return prop ? prop->GetPropertyValueInContainer(object) : false;
}

void ListFields(RC::Unreal::UStruct* type)
{
    using namespace RC::Unreal;
    if (!type) return;
    for (FProperty* prop : TFieldRange<FProperty>(type, EFieldIterationFlags::None)) {
        const auto name = prop->GetName(); // 仅当前类型声明的字段
        (void)name;
    }
}
```

### UFunction 参数与反射辅助

`UObject::ProcessEvent(UFunction* Function, void* Parms)` 的 `Parms` 是当前函数签名对应的可写参数区；用 `GetParmsSize()`/每个参数的属性偏移和旗标核对，`GetReturnProperty()` 确认返回字段。**无通用 C++ 类型自动封送**；带构造/析构的属性不可只 `Memzero` 一块内存就视为安全。脚本调用栈见 [UFunctionStructs.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UFunctionStructs.hpp)：`UnrealScriptFunction = void(*)(UObject*, FFrame&, void* RESULT_DECL)`，`UnrealScriptFunctionCallableContext` 含借用的 `Context`、`TheStack`、`RESULT_DECL`；`GetParams<ParamStruct>() -> ParamStruct&` 将 `TheStack.Locals()` 强转，`SetReturnValue<ReturnType>(ReturnType)` 写向 `RESULT_DECL`，调用者必须自行确保布局、指针非空和时机，回调结束后不可保留这些地址。具体 Hook 生命周期见[查找与 Hook](search-and-hooks.md)。

[ReflectedFunction.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/ReflectedFunction.hpp) 提供 `ReflectedFunctionBase(const CharType* FunctionName)`、`AssignFunction(UFunction*) -> void`、`IsValid() -> bool`、`GetFullName() const -> const CharType*`、`GetReturnValueOffset() -> int32`。`InstancedReflectedFunction::operator()(UObject* Instance, ParamsType& Params)` 与 `StaticReflectedFunctionBase<Owner>::operator()(ParamsType& params)` 都返回 `void`、直接调用 `ProcessEvent`；前者要求 `Instance` 存活，后者要求 `Owner::Self` 非空；先解析函数并检查 `IsValid()`。`Params` 是入出同一对象，返回值需从其 `ReturnValue` 字段读取；按具体版本构造正确的参数结构，不能将 `IsValid()` 当成对象/参数校验。

## 4. FName、FString、容器、弱引用与内存

| API | 参数方向/默认 | 结果、所有权及限制 |
|---|---|---|
| `FName(const CharType* StrName, EFindName FindType = FNAME_Add, void* FunctionAddressOverride = nullptr)`；`FName(StringViewType, EFindName = FNAME_Add, void* = nullptr)` | 入：名字、查找策略、内部地址覆盖 | 值类型；默认会添加名字，查现有名字用 `FNAME_Find`；底层构造函数未就绪且未覆盖地址时实现提前返回，可能仍为 `None`。`FNAME_Replace_Not_Safe_For_Threading` 不适合普通调用；`StringViewType` 构造实际取 `.data()`，传入必须是 NUL 结尾文本 |
| `FName::IsNone() const -> bool`、`ToString() const -> const StringType`、`IsEqual(const FName&, ENameCase = IgnoreCase, bool bCompareNumber = true) const -> bool` | 入：比较对象、大小写及序号策略 | 字符串按值返回；`GetComparisonIndex() -> FNameEntryId`，`ToUnstableInt()` / `FNameEntryId::ToUnstableInt()` 仅进程内有效，不持久化 |
| `FString(const TCHAR*)` / `FString(const StringType&)`；`Len()` / `GetCharArray()`；`Left(int32)`、`Mid(int32 Start, int32 Count = MAX_int32)`、`Replace(const TCHAR* SearchText, const TCHAR* ReplacementText, ESearchCase::Type = IgnoreCase)` | 入：字符串及截取/替换参数 | UE TCHAR 字符串值；`Replace` 返回新 `FString`，`ReplaceInline` 原地修改并返回替换次数。`GetCharArray()` 返回内部 `TArray<TCHAR>&`（const 对象返回 const 引用），不是副本；修改它必须保持末尾 NUL，修改/销毁字符串后不能沿用原缓冲地址；`FStringOut` 用于 SDK 的输出缓冲 |
| `FText::ToString() const -> StringType`；`ToFString() const -> FString` | 无 | 按值返回；`Data` 可能为空，头文件特别标明此时不能用 `ToFString()`，改用 `ToString()`。从 `CharType*` / `FString&&` 构造经 Kismet 转换，依赖引擎已就绪 |
| `TArray<T>::Num() const`、`IsValidIndex(SizeType) const`、`GetData()`、`Add(const T&) -> SizeType`、`Emplace(...) -> SizeType`、`RemoveAt(SizeType Index, EAllowShrinking = Default)`、`Reserve(SizeType)` | 入：元素、索引、容量 | `Add/Emplace` 返回插入索引；`GetData()` 借用数组存储。扩容、删除会使元素指针/迭代器失效；`Empty(SizeType Slack = 0)` 清元素及调整空间 |
| `TMap<K,V>::Find(Key) -> V*`、`Add(Key, Value) -> V&`、`Remove(Key) -> int32`、`Num() const -> int32` | 入：键/值 | 找不到空指针；删除返回数量；返回的引用/指针只在映射保持相应节点时有效。`TSet` / `TMap` 需要匹配键类型的相等及 `GetTypeHash` |
| `FWeakObjectPtr(UObject*)`、`Get() const -> UObject*`、`Get(bool bEvenIfGarbage) const -> UObject*`、`GetEvenIfUnreachable() const -> UObject*` | 入：目标对象、垃圾可见性策略 | 序号+索引弱句柄，消失时可返回空；允许垃圾/不可达的重载**不是安全使用许可**。`TWeakObjectPtr<T>(T*)` 的非 const `Get() -> T*` 同理；原始 `TObjectPtr<T>` 当前封装仅保存裸指针，`Get()` / `ToRawPtr()` 不做存活或 GC 保证 |
| `FMemory::Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) -> void*`、`Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) -> void*`、`Free(void* Original) -> void` | 入：字节数、对齐、此前同分配器的块 | 游戏内存分配/释放成对使用；失败/分配器未就绪不能靠本头保证。`Memzero(void*, SIZE_T)`、`Memcpy(void*, const void*, SIZE_T)` 等是原始字节操作，不会构造非平凡类型。`FMalloc** GMalloc` 与实例 `TryMalloc/TryRealloc` 是底层分配器接口，不要混用 C Runtime `free` |

```cpp
#include <Unreal/UObject.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/FWeakObjectPtr.hpp>

void UseNamesAndWeakRefs(RC::Unreal::UObject* object)
{
    using namespace RC::Unreal;
    FName key(STR("Health"), FNAME_Find); // 只查现有名字；可仍是 None
    TArray<FName> names;
    if (!key.IsNone()) names.Add(key);
    FWeakObjectPtr weak(object);
    if (UObject* current = weak.Get()) {
        // 仅在仍允许访问游戏对象的上下文中立即使用 current
        (void)current->GetFName();
    }
}
```

## 5. 常用引擎封装

这些头是**局部便捷封装**，不是完整 `AActor` / `UWorld` API。`AActor` 的 `FVector`、`FRotator`、`FTransform` 跨 UE 主版本有精度/布局差异；不要复制 [GameplayStatics.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/GameplayStatics.hpp) 中的版本专用 `_Params_As...` 到别的版本。

| 类与 API | 输入方向/默认 | 输出/错误及注意 |
|---|---|---|
| `AActor::GetWorld() -> UWorld*`、`GetLevel() -> UObject*`、`K2_GetActorLocation() -> FVector`、`GetTransform() -> FTransform` | 无 | 借用 world/level 或值返回变换；world/level 可因 actor 状态不适用，应检查指针 |
| `AActor::K2_SetActorLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport) -> bool`；`K2_SetActorRotation(FRotator NewRotation, bool bTeleportPhysics) -> bool`；`K2_TeleportTo(FVector DestLocation, FRotator DestRotation) -> bool` | 入：位置、旋转、移动策略；入出：碰撞结果 | 布尔反映引擎调用结果；`K2_DestroyActor() -> void` 请求销毁，调用后勿再持有裸指针。`SetActorScale3D(FVector)`、`SetActorHiddenInGame(bool)`、`SetActorTickEnabled(bool)`、`SetActorTickInterval(float)` 均返回 `void` |
| `UWorld::SpawnActor(UClass* InClass, const FVector* Location = nullptr, const FRotator* Rotation = nullptr) -> AActor*`；`SpawnActor(UClass* Class, const FTransform* Transform) -> AActor*` | 入：actor 类、可选变换指针 | 借用新 actor；失败可空；world、class、变换须与当前游戏兼容。对象生命周期由引擎管理，不 `delete` |
| `UGameplayStatics::BeginDeferredActorSpawnFromClass(const UObject* WorldContextObject, UClass* ActorClass, const FTransform& SpawnTransform, ESpawnActorCollisionHandlingMethod CollisionHandlingOverride = Undefined, AActor* Owner = nullptr) -> AActor*`；`FinishSpawningActor(AActor* Actor, const FTransform& SpawnTransform) -> AActor*` | 入：上下文、类、变换、碰撞策略、可选 owner | 分步创建与完成，返回借用 actor 或失败空；开始后应按引擎语义及时 finish；`GetAllActorsOfClass(const UObject*, UClass*, TArray<AActor*>& 或 std::vector<AActor*>& OutActors) -> void` 写出匹配 actor 借用指针，不保活；`FindNearestActor(FVector Origin, const std::vector<AActor*>& ActorsToCheck, float& Distance) -> AActor*` 写回距离，没候选者应处理空值 |
| `UDataTable::FindRow<T>(FName RowName, const TCHAR* ContextString, bool bWarnIfRowMissing = true) const -> T*` | 入：行名、诊断上下文、是否警告 | 表无 RowStruct、类型不匹配、None 或缺行返回空；返回的是表中行存储的借用指针；`GetAllRows<T>(..., TArray<T*>& OutRowArray) const -> void` **追加**行指针，遇类型错误记录日志并提前返回；`ForeachRow<T>(..., TFunctionRef<void(const FName&, const T&)>) const -> void` 同步遍历 |
| `UDataTable::GetRowNames() const -> TArray<FName>`、`FindRowUnchecked(FName) const -> uint8*`、`AddRow(FName, const FTableRowBase&)` / `AddRow(FName, const uint8*, const UScriptStruct*)`、`RemoveRow(FName)`、`EmptyTable()` | 入：行名、行数据和反射类型 | 行名数组值返回；unchecked 不检查行类型，缺行空；`AddRow` **拷贝**传入行，`RemoveRow` 找不到则无操作，`EmptyTable` 不清 RowStruct。变更行会使此前借用的行地址失效 |
| `UAssetRegistry::GetAllAssets(TArray<FAssetData>& OutAssetData, bool bIncludeOnlyOnDiskAssets) -> bool`、`GetAssetsByClass(FName ClassName, TArray<FAssetData>& OutAssetData, bool bSearchSubClasses = false) -> bool`、`GetAssetByObjectPath(FName ObjectPath, bool bIncludeOnlyOnDiskAssets = false) -> FAssetData` | 入：筛选项；出：数组 | bool 表示引擎查询结果；返回 `FAssetData` **值**不是已加载 UObject，使用字段前核验有效性/版本。`UAssetRegistryHelpers::GetAssetRegistry() -> FScriptInterface` 返回接口值，`GetAsset(FAssetData& InAssetData) -> UObject*` 返回借用对象或空，可能触发加载 |
| `FPackageName::IsShortPackageName(StringViewType)`、`IsValidLongPackageName(StringViewType, bool bIncludeReadOnlyRoots = false, EErrorCode* OutReason = nullptr)`、`IsValidTextForLongPackageName(StringViewType, EErrorCode* OutReason = nullptr)` | 入：路径文本、只读根策略；出：可选失败原因 | 布尔结果；还有 `FText* OutReason` 重载，需显式传 bool 或 `FText*` 以选所需重载。仅检验文本/路径，不加载包 |
| `Version::IsAtLeast(int32 major, int32 minor)`、`IsBelow(...)`、`IsEqual(...)`、`IsAtMost(...)`、`IsAbove(...)`、`IsDebug()` | 入：主/次版本 | 静态 `Major/Minor/DebugBuild` 的布尔比较，不是任意游戏特性探测；`Initialize(void* Address) -> VersionStatus` 是初始化通道，`Status` 为 `SUCCESS/FAILURE`，失败消息在 `ErrorMessage`，Mod 不应重复初始化 |
| `CacheGlobalObject(UObject*)`、`InvalidateGlobalObject(UObject*)`、`GetGlobalObject(const StringType& FullNameWithoutType) -> UObject*` | 入：对象或**不带类型**的完整名字，例如 `/Script/Engine.Actor:K2_SetActorLocation` | 全局缓存读为借用指针；`FClassCreateListener`/`FClassDeleteListener` 负责创建/删除通知，不能仅因查到缓存就认定对象仍可用。三个自由函数在头中没有导出标记，外部 Mod 不应假定可直接链接；监听器挂接细节见 [UObjectArray.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectArray.hpp) |

`UAssetRegistry::LoadAllAssets() -> void` 是特殊的全量加载助手：设置请求、注册一次 engine-tick 回调，在调用线程循环等待回调完成；**不要从游戏线程调用**，否则等不到后续 tick。回调在游戏线程加载并将成功加载的对象标记 root/GC keep；`AssetsAreLoading() -> bool`、`ShouldLoadAllAssets() -> bool` 查询状态，`SetAssetsAreLoading(bool)` / `SetShouldLoadAllAssets(bool)` 写状态（非普通 Mod 的调度入口），`SetMaxMemoryUsageDuringAssetLoading(int64 NewMaxMemoryUsageDuringAssetLoading)` 设置内存负载阈值，当前实现按 Windows `dwMemoryLoad` 百分比比较，`0` 禁用该条件。内存不足会提前结束加载；`FreeAllForcefullyLoadedAssets() -> void` 撤销所保存对象的 root/GC keep 标记，不自动保证它们此前没有别的 root 使用者，调用前须协调生命周期。实现见 [UAssetRegistry.cpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/src/UAssetRegistry.cpp)，不能把阻塞调用误当成可在任何线程安全调用的异步 API。

```cpp
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/World.hpp>

void VisitActors(RC::Unreal::UObject* world_context, RC::Unreal::UClass* actor_class)
{
    using namespace RC::Unreal;
    if (!world_context || !actor_class) return;
    TArray<AActor*> actors;
    UGameplayStatics::GetAllActorsOfClass(world_context, actor_class, actors);
    for (AActor* actor : actors) {
        if (!actor) continue;
        const auto location = actor->K2_GetActorLocation();
        (void)location; // 本次调用使用；数组/裸指针不负责保活 actor
    }
}
```

其他具体便捷头：[UEngine.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UEngine.hpp) 的 `LoadMapInternal` / `TickInternal`、[UPlayer.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UPlayer.hpp) / [ULocalPlayer.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/ULocalPlayer.hpp) 的版本字段和 `ExecInternal`、[AGameModeBase.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/AGameModeBase.hpp) 的 `InitGameStateInternal`、[UKismetSystemLibrary.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UKismetSystemLibrary.hpp) 的 `GetGameName/GetEngineVersion/GetBuildConfiguration() -> FString`、[UKismetNodeHelperLibrary.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UKismetNodeHelperLibrary.hpp) 的 `GetEnumeratorUserFriendlyName(const UEnum*, uint8) -> StringType`；这些只是有条件解析的接口，不应把内部 `Function<Signature>` 成员当成稳定可调用的游戏事件。`UGameViewportClient` 头明确指出其 C++ 继承关系不准确；`UPackage` 目前只是 UObject 占位派生（[声明](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UPackage.hpp)），但提供 `GetTransientPackage() -> UPackage*` 借用的临时包入口。`FAssetData` 的版本布局仍有 TODO，尤其 4.27、5.1 后字段变化（[声明](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/FAssetData.hpp)）；不要以硬编码 `sizeof` 序列化。

## 6. 宏与自定义属性：约束而非自动生成

[UObjectMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/UObjectMacros.hpp) 的 `DECLARE_EXTERNAL_OBJECT_CLASS(ClassName, ModuleName)` / `_WITH_CAST(ClassName, ModuleName, CastFlags)` 声明 `StaticClass()` 与注册信息；配套 `IMPLEMENT_EXTERNAL_OBJECT_CLASS(ClassName)` 实现按路径查找，未找到会抛 `std::runtime_error`。`DECLARE_FIELD_CLASS...` / `IMPLEMENT_FIELD_CLASS...` 用于 `FField` 类；后者无有效 class 时抛异常，标记为当前版本不存在时返回空 variant。`UPROPERTY(...)`、`UFUNCTION(...)`、`USTRUCT(...)` 等在这里是空标记，**不会对 Mod 自定义 C++ 类运行 Unreal Header Tool 或生成反射属性**；`GENERATED_BODY()` 依赖已有生成代码，不能仅加宏就注册游戏类型。

[PropertyMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/PropertyMacros.hpp) 开头明确标注**实验性、非稳定公开 API**。`RC_INTERNAL_DEFINE_BP_PROPERTY_PRIVATE(Type, Name[, Alias])` 展开 `RC_INTERNAL_BP_GetName()`（值）、`RC_INTERNAL_BP_GetRefName()`（引用，不支持 bool）、`RC_INTERNAL_BP_SetName(Type NewValue)`（`void`）；若有别名则方法以 alias 命名，但反射查找仍按 `Name`。继承链查找失败会记录日志并 `std::abort()`；非 bool 首次按成员 getter 缓存偏移，bool 每次找 `FBoolProperty` 并按掩码处理。只在**已核实类型/对象/版本**时使用，不能当作缺字段时安全返回空的查询器。源码注释中的 `GetJumpMaxCount`/`SetJumpMaxCount` 是旧命名例子；实际展开名字有 `RC_INTERNAL_BP_` 前缀。

[BPMacros.hpp](../../../crates/ue4ssl-unreal-support/vendor/Unreal/include/Unreal/BPMacros.hpp) 的 `UE_BEGIN_SCRIPT_FUNCTION_BODY(FunctionPath)` 每次找 `UFunction`，`UE_BEGIN_NATIVE_FUNCTION_BODY(FunctionPath)` 缓存函数指针；找不到抛异常。两者从 `GetParmsSize()` 用 `_malloca` 分配并 `Memzero` 参数，`UE_COPY_PROPERTY(Name, Type)`/`UE_COPY_OUT_PROPERTY(Name, Type)` 用反射偏移入/出，`UE_CALL_FUNCTION()` 或 `UE_CALL_STATIC_FUNCTION()` 执行，`UE_RETURN_PROPERTY(Type)` / `UE_END_VOID_FUNCTION()` 负责 `_freea`。`UE_SET_STATIC_SELF(ObjectPath)` 只查对象，**未检查空指针**；宏没有完整对象/参数生命周期保障，异常中途也可能绕过释放。`UE_COPY_STL_VECTOR_AS_TARRAY` 自带 `MACRO NEEDS TESTING!` 警告；不要假定这些宏适合任意含 `FString`/容器的反射参数。

[CustomProperty.hpp](../../../crates/ue4ssl-dll/native/UE4SSL/include/UnrealCustom/CustomProperty.hpp) 暴露的是核心内部的 `FProperty` 派生对象和嵌入的未类型化 `m_data[0x128]`，不是引擎注册接口：

| 工厂/构造 | 入参方向 | 输出与风险 |
|---|---|---|
| `CustomProperty(int32_t offset_internal, int32_t element_size)`；`CustomProperty::construct(int32_t offset_internal, UClass* belongs_to_class, UClass* inner_class, int32_t element_size) -> std::unique_ptr<CustomProperty>` | 入：容器中的偏移、单元素字节数、所属类与属性类 | 构造仅设置偏移和元素大小；工厂另外设置字段 class/owner（UE 4.25 前使用 UObject 布局）。返回 `unique_ptr` 拥有**伪属性包装**，绝不拥有 `UClass`，不将属性注册进 `UClass` |
| `CustomArrayProperty::construct(int32_t offset_internal, FProperty* array_inner, int32_t element_size) -> std::unique_ptr<CustomProperty>`；另有 `(int32_t, UClass* belongs_to_class, UClass* inner_class, FProperty* array_inner, int32_t)` | 入：偏移、借用的数组 inner property、单元素大小；长重载额外 class/owner | 返回 owning `unique_ptr<CustomProperty>`；短重载给数组字段设置 inner，长重载另造一个 raw `new CustomProperty` 用于 inner 并将其接到返回对象（[实现](../../../crates/ue4ssl-dll/native/UE4SSL/src/UnrealCustom/CustomProperty.cpp)），实现中没有展示该 raw 分配的配对释放；勿作为可反复大量构造的通用 API |
| `CustomStructProperty::construct(int32_t offset_internal, UScriptStruct* script_struct, int32_t element_size) -> std::unique_ptr<CustomProperty>` | 入：偏移、借用的反射 struct、大小 | 返回 owning 包装，但不拥有 `script_struct`；不是游戏对象的 struct 实例 |

工厂实现用 `std::bit_cast<FArrayProperty*>` / `std::bit_cast<FStructProperty*>` 写字段，依赖所选引擎布局；不等同完整 `FProperty` 虚表及生命周期。持有时间必须覆盖所有使用伪属性的代码，避免将 `get()` 指针放进长期引擎状态；不要调用不确定的虚方法或假设这些对象可供 UE GC 自动管理。项目普通 Mod 应首先使用真实反射字段与类型检查。
