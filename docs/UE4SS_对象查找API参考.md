# UE4SS 对象查找 API 参考

本文对应实现文件：
`crates/ue4ssl-object-searcher/include/Unreal/ObjectSearch/UObjectSearch.hpp`

命名空间：
`RC::Unreal::UObjectGlobals`

说明：
以下只列主签名。`const TCHAR*`、`FName`、`StringViewType`、`std::string_view`、`std::string` 等字符串过载在实现中会提供同等入口，默认行为一致，下面不逐个重复。

## 1. UE 风格精确查找

这组 API 的语义最接近 UE 自身的对象查找：已知 `Class + Outer + Name/Path`，按精确对象路径查找。

```cpp
UObject* FindObject(UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass = false, ObjectSearcher* = nullptr);
UObject* FindObject(UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass = false, ObjectSearcher* = nullptr);

UObject* FindObject(ObjectSearcher&, UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass = false);
UObject* FindObject(ObjectSearcher&, UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass = false);
```

常见用途：
`UObject` / `UClass` / `UFunction` 等已知完整路径时，走精确查找。

补充：
`ObjectSearcher*` 是可选搜索上下文。普通调用通常不需要显式传入。

模板便捷包装：

```cpp
template<typename ObjectType>
ObjectType* FindObject(UObject* Outer, const TCHAR* Name, bool ExactClass = false);

template<typename ObjectType = UObject*>
auto StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, const CharType* OrigInName, bool bExactClass = false) -> ObjectType;
```

## 2. 按类短名 + 对象短名查找

这组 API 是 UE4SS 的短名查找层，语义是“给一个类短名，再给一个对象短名”。

```cpp
UObject* FindObjectByClassAndName(const FName ClassName, const FName ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {});
UObject* FindObjectByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, int32 RequiredFlags = {}, int32 BannedFlags = {});

void FindObjectsByClassAndName(size_t NumObjectsToFind, const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true);
void FindObjectsByClassAndName(size_t NumObjectsToFind, const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true);

void FindObjectsByClassAndName(const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true);
void FindObjectsByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags = {}, int32 BannedFlags = {}, bool bExactClass = true);
```

参数含义：
- `ClassName` 是类短名，不是完整路径。
- `ObjectShortName` 是对象短名。
- `RequiredFlags` / `BannedFlags` 用于按 `EObjectFlags` 过滤。
- `NumObjectsToFind = 0` 表示不限制数量，返回所有匹配项。
- `bExactClass = true` 时只匹配精确类，`false` 时允许继承链。

典型用途：
`FindObjectByClassAndName(STR("Class"), STR("UGCPackage"))`

## 3. 按类枚举实例

这组 API 只处理“类实例”，不处理 CDO，也不处理类对象本身。

```cpp
UObject* FindFirstInstanceOfClass(FName ClassName);
void FindAllInstancesOfClass(FName ClassName, std::vector<UObject*>& OutStorage);
```

语义：
- `FindFirstInstanceOfClass` 返回第一个活跃实例。
- `FindAllInstancesOfClass` 返回全部活跃实例。
- 都会考虑继承关系。
- 不会把 `ClassDefaultObject` 当成实例返回。

典型用途：
- 找第一个 `GameInstance` / `Actor` / `PlayerController`
- 枚举某个类的全部运行时对象

## 4. 已废弃别名

这些名字仍然保留兼容，但新代码不要再用。

| 旧名 | 新名 |
|---|---|
| `FindFirstOf` | `FindFirstInstanceOfClass` |
| `FindAllOf` | `FindAllInstancesOfClass` |
| `FindObject(ClassName, ObjectShortName, ...)` | `FindObjectByClassAndName` |
| `FindObjects(ClassName, ObjectShortName, ...)` | `FindObjectsByClassAndName` |
| `SafeFindFirstOf` | `SafeFindFirstInstanceOfClass` |

说明：
- C++ 头文件中这些旧名已标记 `[[deprecated]]`。
- Lua / JavaScript 里旧全局函数也保留为兼容入口，但会提示弃用。

## 5. 内部支持函数

这几项属于实现支持，不建议在普通模组代码里直接依赖。

```cpp
void SetupStaticFindObjectFastAddress(void* FunctionAddress);
UObject* StaticFindObject_InternalSlow(UClass* Object, UObject* ChunkIndex, const CharType* OrigInName, bool bExactClass = false);
UObject* StaticFindObject_InternalNoToStringFromStrings(const std::vector<StringViewType>& NameParts);
UObject* StaticFindObject_InternalNoToStringFromNames(const std::vector<FName>& NameParts);
```

用途：
- `SetupStaticFindObjectFastAddress`：启动阶段写入 `StaticFindObjectFast` 的地址。
- `StaticFindObject_InternalSlow`：慢路径对象查找。
- `StaticFindObject_InternalNoToStringFromStrings` / `...FromNames`：把多段路径直接拼成查找输入，避免额外字符串转换。

## 6. 推荐选型

1. 已知完整路径或外层对象时，用 `FindObject(...)` / `StaticFindObject(...)`。
2. 已知类短名和对象短名时，用 `FindObjectByClassAndName(...)`。
3. 只想拿类实例时，用 `FindFirstInstanceOfClass(...)` 或 `FindAllInstancesOfClass(...)`。
4. 旧名只当兼容层，不要在新代码里继续扩散。
