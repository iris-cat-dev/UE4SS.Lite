# UE4.27 对象查找机制与 UE4SS 缓存取舍

本文记录 UE4.27 自身如何保证 UObject 查找高效，以及它和 UE4SS.Lite 里 `ObjectSearcherPool` / `StaticConstructObject` hook 的关系。

源码参考：

- `D:\Project\UnrealEngine-4.27\Engine\Source\Runtime\CoreUObject\Private\UObject\UObjectHash.cpp`
- `D:\Project\UnrealEngine-4.27\Engine\Source\Runtime\CoreUObject\Private\UObject\UObjectBase.cpp`
- `D:\Project\UnrealEngine-4.27\Engine\Source\Runtime\CoreUObject\Public\UObject\UObjectArray.h`
- `D:\Project\UE4SS.Lite\crates\ue4ssl-unreal-support\vendor\Unreal\src\UnrealInitializer.cpp`
- `D:\Project\UE4SS.Lite\crates\ue4ssl-unreal-support\vendor\Unreal\src\Searcher\ObjectSearcher.cpp`

## 结论

UE4.27 自身不是每次查对象都扫 `GUObjectArray`。核心对象查找靠 `FUObjectHashTables` 维护多张 hash/index：

- 按对象名查：`Hash`
- 按对象名加 Outer 查：`HashOuter`
- 按 Outer 枚举子对象：`ObjectOuterMap`
- 按 Class 枚举实例：`ClassToObjectListMap`
- 按 Class 枚举派生类：`ClassToChildListMap`
- 按 Package 枚举对象：`PackageToObjectListMap`

`GUObjectArray` 是全局对象数组和 GC/weak pointer/遍历基础设施，不是 `FindObjectFast` 的主要加速结构。

UE4SS.Lite 的 `ObjectSearcherPool` 是额外加的一层缓存。它能让某些 `FindAllInstancesOfClass`（旧名 `FindAllOf` 已废弃）/ object search 更快，但代价是需要通过 `StaticConstructObject` hook 跟踪新对象，Actor 大量生成时会增加热路径开销。

## UE 自身维护的索引

`FUObjectHashTables` 位于 `UObjectHash.cpp`，核心字段如下：

```cpp
TMap<int32, FHashBucket> Hash;
TMultiMap<int32, UObjectBase*> HashOuter;

TMap<UObjectBase*, FHashBucket> ObjectOuterMap;
TMap<UClass*, FHashBucket> ClassToObjectListMap;
TMap<UClass*, TSet<UClass*>> ClassToChildListMap;

TMap<UPackage*, FHashBucket> PackageToObjectListMap;
TMap<UObjectBase*, UPackage*> ObjectToPackageMap;
```

含义：

| 表 | Key | Value | 用途 |
| --- | --- | --- | --- |
| `Hash` | `GetTypeHash(FName)` | 同名对象 bucket | 没有 Outer 或任意包查找 |
| `HashOuter` | `GetTypeHash(FName) + (Outer >> 6)` | 对象指针 | 已知 Outer 时快速查 |
| `ObjectOuterMap` | Outer 指针 | 直接子对象 bucket | `GetObjectsWithOuter` |
| `ClassToObjectListMap` | UClass 指针 | 实例 bucket | `GetObjectsOfClass` |
| `ClassToChildListMap` | UClass 指针 | 子类集合 | 包含派生类时枚举 |
| `PackageToObjectListMap` | UPackage 指针 | 包内对象 bucket | package object 枚举 |
| `ObjectToPackageMap` | UObject 指针 | external package | external package 支持 |

`FHashBucket` 做了小对象优化：bucket 里 0、1、2 个对象时直接用两个指针存；超过两个才分配 `TSet`。这能减少常见低碰撞 bucket 的内存和分配成本。

## 对象创建时如何进入索引

对象创建路径中，`UObjectBase::AddObject()` 会先分配全局对象索引，再加入 hash：

```cpp
GUObjectArray.AllocateUObjectIndex(this);
HashObject(this);
```

`HashObject()` 会做这些事情：

```cpp
Hash = GetObjectHash(Name);
ThreadHash.AddToHash(Hash, Object);

if (Outer)
{
    Hash = GetObjectOuterHash(Name, Outer);
    ThreadHash.HashOuter.Add(Hash, Object);
    AddToOuterMap(ThreadHash, Object);
}

AddToClassMap(ThreadHash, Object);
```

所以 UE 对象一旦创建，就同时进入：

- `GUObjectArray`
- 按 `FName` 的 hash
- 按 `FName + Outer` 的 hash
- Outer 子对象表
- Class 实例表
- 如果对象本身是 `UClass`，还会更新父类到子类表

## 改名、换 Outer、换 Class 如何保持一致

UE 不依赖外部 hook 来维护这些索引，而是在 UObject 自身变更入口里更新。

`UObjectBase::LowLevelRename()`：

```cpp
UnhashObject(this);
NamePrivate = NewName;
OuterPrivate = NewOuter;
HashObject(this);
```

`UObjectBase::SetClass()`：

```cpp
UnhashObject(this);
ClassPrivate = NewClass;
HashObject(this);
```

对象销毁时走 `UnhashObject()`，从各表中移除。

这点和 UE4SS 的外部缓存不同：UE 自己是在对象生命周期内部维护索引，因此不需要 detour `StaticConstructObject` 才能保持索引完整。

## StaticFindObjectFast 为什么快

`StaticFindObjectFastInternalThreadSafe()` 分两种情况：

### 已知 Outer

如果传入了 `ObjectPackage / Outer`：

```cpp
int32 Hash = GetObjectOuterHash(ObjectName, (PTRINT)ObjectPackage);
for (HashOuter iterator)
{
    check Name、Outer、Class、flags
}
```

这条路径接近：

```text
FName + Outer -> hash bucket -> 少量候选对象 -> 精确过滤
```

### 未知 Outer

如果没有传 Outer：

```cpp
Hash = GetObjectHash(SearchPath.Inner);
Bucket = ThreadHash.Hash.Find(Hash);
for (Bucket iterator)
{
    check Name、AnyPackage、Class、flags、outer path
}
```

这比已知 Outer 慢一些，因为同名对象更多，但仍然不是全局扫描。

## StaticFindObject 和 FindObjectFast 的区别

`StaticFindObject()` 会先解析字符串路径：

- `ResolveName`
- strip object class
- 构造 `FName`
- 再调用 `StaticFindObjectFast`

`FindObjectFast<T>(Outer, Name)` 更直接，要求传入未限定的 `FName`，最终直接走：

```cpp
StaticFindObjectFast(T::StaticClass(), Outer, Name, ...)
```

性能建议：

- 已知 Outer 时优先用 `FindObjectFast` / `StaticFindObjectFast`。
- 不要频繁传完整字符串路径走 `StaticFindObject`。
- 如果要高频查对象，缓存 `FName` 和 Outer。

## 按 Class 查找为什么也快

`GetObjectsOfClass()` / `ForEachObjectOfClass()` 不扫 `GUObjectArray`，而是：

1. 从 `ClassToChildListMap` 找派生类。
2. 对每个 class，从 `ClassToObjectListMap` 取实例 bucket。
3. 对 bucket 内对象做 flags 过滤。

核心路径：

```cpp
FHashBucket* List = ThreadHash.ClassToObjectListMap.Find(SearchClass);
for (FHashBucketIterator ObjectIt(*List); ObjectIt; ++ObjectIt)
{
    UObject* Object = static_cast<UObject*>(*ObjectIt);
    if (!Object->HasAnyFlags(...) && !Object->HasAnyInternalFlags(...))
    {
        Operation(Object);
    }
}
```

这意味着 UE 原生按类枚举已经有索引支持。

## GUObjectArray 的角色

`GUObjectArray` 负责：

- 分配 UObject 全局 index
- `Object -> InternalIndex`
- `Index -> FUObjectItem`
- weak pointer serial number
- GC 遍历
- create/delete listener
- 全局对象迭代

`FChunkedFixedUObjectArray` 采用 chunked fixed array，扩容时不移动已有元素，`IndexToObject` 可以通过 index 直接定位到 `FUObjectItem`。

但对象按名字、Outer、Class 查找时，优先使用 `FUObjectHashTables`，不是优先线性扫描 `GUObjectArray`。

## UE4SS ObjectSearcherPool 的作用

UE4SS.Lite 里有额外的 `ObjectSearcherPool`：

```cpp
ObjectSearcherPool<UClass, AnySuperStruct>
ObjectSearcherPool<UClass, AActor>
ObjectSearcherPool<AActor, AnySuperStruct>
```

初始化时会遍历现有 UObject，填充这些池。随后在 `UnrealInitializer::PostInitialize()` 注册 `StaticConstructObject` post callback：

```cpp
Hook::RegisterStaticConstructObjectPostCallback(... ObjectSearcherPoolHook ...);
```

每次构造对象后，如果是 `AActor`：

```cpp
lock ObjectSearcherPool<AActor, AnySuperStruct>::PoolMutex
ObjectSearcherPool<AActor, AnySuperStruct>::Add(object->GetObjectItem())
```

这样 UE4SS 自己的 `FindAllInstancesOfClass("ActorClass")`（旧名 `FindAllOf` 已废弃）一类搜索可以直接扫较小的 Actor 池，而不是扫全局 UObject。

## 这层缓存的代价

优点：

- 对 UE4SS 脚本层的泛型查找可能更快。
- `FindAllInstancesOfClass(AActor)` 这类搜索可以少扫很多非 Actor 对象。

代价：

- Actor 生成路径多一次 `StaticConstructObject` detour 分发。
- 每个新 Actor 多一次 `IsA<AActor>()` 判断。
- 每个新 Actor 多一次 mutex lock。
- 每个新 Actor 多一次 pool push。
- 如果 Lua 也注册了 `StaticConstructObject` 回调，成本会叠加。

对大量 Actor spawn 的游戏，`StaticConstructObject` hook 可能比查找缓存收益更明显。

## 关闭 StaticConstructObject 缓存回调的影响

如果只关闭 UE4SS 的 `ObjectSearcherPoolHook`，而保留 UE 原始 `StaticConstructObject` 函数地址：

- 游戏自身对象创建不受影响。
- UE 自己的对象 hash/index 不受影响。
- `FindObjectFast` / `StaticFindObjectFast` 不受影响。
- UE 原生 `GetObjectsOfClass` 不受影响。
- UE4SS 的 `ObjectSearcherPool` 不再追踪运行中新生成 Actor。
- UE4SS 快速 Actor 池可能不完整。

如果同时 `bUseUObjectArrayCache=false`，UE4SS 搜索可以退回慢路径，结果更完整，但查询更慢。

## 不建议完全关闭的内容

不建议直接让 `StaticConstructObjectInternal` 地址不可用，因为 JS/Lua 创建 UObject、Widget、WidgetTree 等功能可能依赖这个原始函数地址：

- JS `NewUObject`
- JS UMG widget 创建/clone
- Lua `StaticConstructObject`

更安全的做法是：

- 保留 `UObjectGlobals::StaticConstructObject` 原始函数调用能力。
- 只让 UE4SS 的对象池追踪回调可配置。
- Lua `NotifyOnNewObject` 改成按需安装回调。

## 建议改造方向

### 1. 给 ObjectSearcherPoolHook 增加独立开关

建议配置：

```ini
[General]
bUseUObjectArrayCache = false

[Hooks]
HookStaticConstructObjectObjectCache = 0

[ObjectSearch]
UseNativeStaticFindObjectFast = 0
UseNativeClassEnumeration = 0
CompareNativeSearchResults = 0
```

实现目标：

- `bUseUObjectArrayCache=false` 时不构建 UE4SS 对象池。
- `HookStaticConstructObjectObjectCache=0` 时不注册 `ObjectSearcherPoolHook`。
- 不影响 `UObjectGlobals::StaticConstructObject` 原始函数地址。

当前实现已将 `ObjectSearcher` 编译边界抽到 `crates/ue4ssl-object-searcher`，并保留旧头文件 include 路径，降低迁移影响。

### 2. 优先复用 UE 原生查找

在 UE4SS 脚本/native API 内，能映射到 UE 原生查找时优先用：

- `FindObjectFast<T>(Outer, FName)`
- `StaticFindObjectFast`
- `GetObjectsOfClass`
- `ForEachObjectOfClass`
- `GetObjectsWithOuter`

避免在高频路径里自己维护重复且不完整的缓存。

当前实现里 `UseNativeStaticFindObjectFast=1` 只覆盖已知 `Outer` 且传入短名的精确查找，并可用 `CompareNativeSearchResults=1` 同时跑新旧路径记录耗时/一致性。`UseNativeClassEnumeration=1` 目前只会打一次 fallback 日志，实际 `GetObjectsOfClass` ABI 接入需要后续单独处理 `TArray` / `TFunctionRef` 边界。

### 3. UE4SS 泛型搜索策略

建议按场景分层：

| 场景 | 推荐路径 |
| --- | --- |
| 已知 Outer + Name | UE `FindObjectFast` |
| 已知完整路径但低频 | UE `StaticFindObject` |
| 按 class 枚举 | UE `ForEachObjectOfClass` |
| 查所有 Actor 且非常频繁 | 可选 UE4SS Actor pool |
| Actor 大量生成场景 | 关闭 UE4SS Actor pool 追踪 |

## 对当前性能优化的结论

UE4SS.Lite 当前无条件用 `StaticConstructObject` 追踪新 Actor，并不是 UE 自身查找高效所必需的。UE4.27 已经有自己的 UObject hash/class/outer/package 索引。

因此对 Actor 生成卡顿，最合理的优化不是破坏 UE 的对象系统，而是：

1. 保留 UE 原生对象 hash/index。
2. 保留 `StaticConstructObject` 原始函数地址。
3. 让 UE4SS 的 `ObjectSearcherPoolHook` 可关闭。
4. 让 Lua `NotifyOnNewObject` 按需安装。
5. 高频查找优先走 UE 原生 fast path，只有脚本确实需要时才启用 UE4SS 自建 Actor pool。
