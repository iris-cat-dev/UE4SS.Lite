#include <Unreal/ObjectSearch/UObjectSearch.hpp>

#include <Helpers/String.hpp>
#include <Unreal/ClassListener.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/PackageName.hpp>
#include <Unreal/Searcher/ObjectSearcher.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UPackage.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <chrono>
#include <ranges>
#include <stdexcept>

namespace RC::Unreal::UObjectGlobals
{
    namespace
    {
        Function<UObject*(UClass*, UObject*, FName, bool, bool, EObjectFlags, EInternalObjectFlags)> GStaticFindObjectFast{};

        auto CallStaticFindObjectFast(UClass* ObjectClass,
                                      UObject* InObjectPackage,
                                      FName ObjectName,
                                      bool bExactClass,
                                      bool bAnyPackage,
                                      EObjectFlags ExclusiveFlags,
                                      EInternalObjectFlags ExclusiveInternalFlags) -> UObject*
        {
            if (!GStaticFindObjectFast.is_ready()) { return nullptr; }
            return GStaticFindObjectFast(
                    ObjectClass,
                    InObjectPackage,
                    ObjectName,
                    bExactClass,
                    bAnyPackage,
                    ExclusiveFlags,
                    ExclusiveInternalFlags);
        }
    }

    auto SetupStaticFindObjectFastAddress(void* FunctionAddress) -> void
    {
        if (FunctionAddress)
        {
            GStaticFindObjectFast.assign_address(FunctionAddress);
        }
    }

    auto StaticFindObject_InternalSlow([[maybe_unused]]UClass* ObjectClass, [[maybe_unused]]UObject* InObjectPackage, const CharType* OrigInName, [[maybe_unused]]bool bExactClass) -> UObject*
    {
        UObject* FoundObject{};

        UObjectGlobals::ForEachUObject([&](UObject* Object, [[maybe_unused]]int32_t ChunkIndex, [[maybe_unused]]int32_t ObjectIndex) {
            // This call to 'get_full_name' is a problem because it relies on offsets already being found.
            // This function is called before offsets have been found as a way to check if required objects have been initialized.
            auto ObjFullName = Object->GetFullName();
            auto ObjFullNameNoType = ObjFullName.substr(ObjFullName.find(STR(" ")) + 1);

            if (String::iequal(ObjFullNameNoType, OrigInName))
            {
                FoundObject = static_cast<UObject*>(Object);
                return LoopAction::Break;
            }
            else
            {
                return LoopAction::Continue;
            }
        });

        return FoundObject;
    }

    auto StaticFindObject_InternalNoToStringFromNames(const std::vector<FName>& NameParts) -> UObject*
    {
        UObject* FoundObject{};

        for (const auto& NamePart : NameParts)
        {
            if (NamePart == NAME_None)
            {
                // NAME_None means we're not far enough along engine init for this object to exist yet.
                return nullptr;
            }
        }

        UObjectGlobals::ForEachUObject([&](UObject* Object, [[maybe_unused]]int32_t ChunkIndex, [[maybe_unused]]int32_t ObjectIndex) {
            // In order to remain safe to use early in init before we've hooked FName::ToString up to KismetStringLibrary:Conv_NameToString, we have to
            // compare FNames directly instead of using GetFullName.
            int32_t NumPathParts{};
            auto PathObject = Object;
            while (PathObject)
            {
                const auto PathName = PathObject->GetNamePrivate();
                const auto It = std::ranges::find_if(NameParts, [&](const FName NamePart) {
                    return NamePart.Equals(PathName);
                });
                if (It == NameParts.end())
                {
                    return LoopAction::Continue;
                }
                else
                {
                    PathObject = PathObject->GetOuterPrivate();
                    ++NumPathParts;
                }
            }
            if (NumPathParts == NameParts.size())
            {
                FoundObject = Object;
                return LoopAction::Break;
            }
            else
            {
                return LoopAction::Continue;
            }
        });

        return FoundObject;
    }

    auto StaticFindObject_InternalNoToStringFromStrings(const std::vector<StringViewType>& NameParts) -> UObject*
    {
        std::vector<FName> Names{};
        for (const auto& NamePart : NameParts)
        {
            Names.emplace_back(NamePart, FNAME_Find);
        }
        return StaticFindObject_InternalNoToStringFromNames(Names);
    }

    static auto IsValidObjectForFindXOf(UObject* Object) -> bool
    {
        return !Object->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)) && !Object->IsA<UClass>();
    }

    UObject* FindObject(UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass, ObjectSearcher* InSearcher)
    {
        return FindObject(Class, InOuter, FromCharTypePtr<TCHAR>(InName.data()), bExactClass, InSearcher);
    }

    UObject* FindObject(UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass, ObjectSearcher* InSearcher)
    {
        bool bObjectIsCached{};
        if (!Class && !InOuter && InName && !bExactClass)
        {
            if (auto CachedObject = GetGlobalObject(ToCharTypePtr(InName)); CachedObject)
            {
                return CachedObject;
            }
        }

        if (!InName)
        {
            throw std::runtime_error{"Call to FindObject with no InName is not allowed"};
        }

        auto GetPackageNameFromLongName = [](const RC::StringType& LongName) -> RC::StringType
        {
            auto DelimiterOffset = LongName.find(STR("."));
            if (DelimiterOffset == LongName.npos)
            {
                throw std::runtime_error{"GetPackageNameFromLongName: Name wasn't long."};
            }
            return LongName.substr(0, DelimiterOffset);
        };

        UObject* FoundObject{nullptr};
        const bool bAnyPackage = InOuter == ANY_PACKAGE;
        UObject* ObjectPackage = bAnyPackage ? nullptr : InOuter;
        const bool bIsLongName = !FPackageName::IsShortPackageName(ToCharTypePtr(InName));
        FName ShortName = bIsLongName ? NAME_None : FName(ToCharTypePtr(InName), FNAME_Add);
        FName PackageName = bIsLongName ? FName(GetPackageNameFromLongName(ToCharTypePtr(InName)), FNAME_Add) : NAME_None;

        if (bIsLongName)
        {
            auto NameView = StringViewType{InName};
            auto LastColonDelimiter = NameView.find_last_of(STR(':'));
            auto LastDotDelimiter = NameView.find_last_of(STR('.'));
            if (LastDotDelimiter == NameView.npos && LastColonDelimiter == NameView.npos)
            {
                // Name only contains path.
                ShortName = FName(NameView, FNAME_Add);
            }
            else if (LastColonDelimiter == NameView.npos)
            {
                // Only dots, so the short name should be after the last dot.
                ShortName = FName(NameView.substr(LastDotDelimiter + 1), FNAME_Add);
            }
            else if (LastDotDelimiter == NameView.npos)
            {
                // Only colons, so the short name should be after the last colon.
                ShortName = FName(NameView.substr(LastColonDelimiter + 1), FNAME_Add);
            }
            else
            {
                // Mix of dots and colons.
                if (LastColonDelimiter > LastDotDelimiter)
                {
                    // Last colon is after the last dot, so the short name should be after the last colon.
                    ShortName = FName(NameView.substr(LastColonDelimiter + 1), FNAME_Add);
                }
                else
                {
                    // Last dot is after the last colon, so the short name should be after the last dot.
                    ShortName = FName(NameView.substr(LastDotDelimiter + 1), FNAME_Add);
                }
            }
        }

        const auto& ObjectSearchConfig = UnrealInitializer::StaticStorage::GlobalConfig;
        const bool bCanUseNativeStaticFindObjectFast =
                ObjectSearchConfig.bUseNativeStaticFindObjectFast &&
                GStaticFindObjectFast.is_ready() &&
                InOuter &&
                InOuter != ANY_PACKAGE &&
                !bAnyPackage &&
                !bIsLongName;

        UObject* NativeFoundObject{nullptr};
        bool bNativeStaticFindObjectFastAttempted{false};
        int64_t NativeStaticFindObjectFastNs{};
        if (bCanUseNativeStaticFindObjectFast)
        {
            bNativeStaticFindObjectFastAttempted = true;
            const auto NativeStart = std::chrono::steady_clock::now();
            NativeFoundObject = CallStaticFindObjectFast(Class, InOuter, ShortName, bExactClass, false, RF_NoFlags, EInternalObjectFlags::None);
            NativeStaticFindObjectFastNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - NativeStart).count();

            if (!ObjectSearchConfig.bCompareNativeSearchResults && NativeFoundObject)
            {
                if (!bObjectIsCached)
                {
                    CacheGlobalObject(NativeFoundObject);
                }
                return NativeFoundObject;
            }
        }

        auto Searcher = [&InSearcher, &Class]() -> ObjectSearcher {
            return InSearcher ? *InSearcher : FindObjectSearcher(Class, nullptr);
        }();

        bool bQuickSearch = Searcher.IsFast();

        const auto SlowStart = ObjectSearchConfig.bCompareNativeSearchResults ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        Searcher.ForEach([&](UObject* Object) {
            if (bExactClass && Class != Object->GetClassPrivate()) { return LoopAction::Continue; }

            // If this is a quick search, then the object is guaranteed to be of the specified class.
            // Otherwise, we're searching the entirety of GUObjectArray, and we need to check that the class matches.
            if (Class && !bQuickSearch && !Object->IsA(Class)) { return LoopAction::Continue; }

            bool bIsInOuter{};
            if (!bAnyPackage && !ObjectPackage)
            {
                if (Object->GetOutermost()->GetNamePrivate().Equals(PackageName))
                {
                    bIsInOuter = true;
                }
            }
            else if (!bAnyPackage)
            {
                UObject* Outer = Object->GetOuterPrivate();
                do
                {
                    if (Outer == ObjectPackage)
                    {
                        bIsInOuter = true;
                        break;
                    }
                    Outer = Outer->GetOuterPrivate();
                } while (Outer);
            }

            if (!bAnyPackage && !bIsInOuter) { return LoopAction::Continue; }

            if (bIsLongName)
            {
                if (!Object->GetNamePrivate().Equals(ShortName))
                {
                    return LoopAction::Continue;
                }
                auto FullName = Object->GetFullName();
                auto ClassLessFullName = FullName.substr(FullName.find(STR(" ")) + 1);
                if (ToCharTypePtr(InName) == ClassLessFullName)
                {
                    FoundObject = Object;
                    return LoopAction::Break;
                }
            }
            else if (ObjectPackage || bAnyPackage)
            {
                if (ShortName.Equals(Object->GetNamePrivate()))
                {
                    FoundObject = Object;
                    return LoopAction::Break;
                }
            }

            return LoopAction::Continue;
        });
        const auto SlowSearchNs = ObjectSearchConfig.bCompareNativeSearchResults
                ? std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - SlowStart).count()
                : 0;

        if (FoundObject && !bObjectIsCached)
        {
            CacheGlobalObject(FoundObject);
        }

        if (bNativeStaticFindObjectFastAttempted && ObjectSearchConfig.bCompareNativeSearchResults)
        {
            if (NativeFoundObject != FoundObject)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SS.ObjectSearch] StaticFindObjectFast mismatch native={} old={} native_ns={} old_ns={}\n"),
                        static_cast<void*>(NativeFoundObject),
                        static_cast<void*>(FoundObject),
                        static_cast<int64_t>(NativeStaticFindObjectFastNs),
                        static_cast<int64_t>(SlowSearchNs));
            }
            else
            {
                Output::send<LogLevel::Verbose>(
                        STR("[UE4SS.ObjectSearch] StaticFindObjectFast match object={} native_ns={} old_ns={}\n"),
                        static_cast<void*>(FoundObject),
                        static_cast<int64_t>(NativeStaticFindObjectFastNs),
                        static_cast<int64_t>(SlowSearchNs));
            }
        }

        return FoundObject;
    }

    UObject* FindObject(struct ObjectSearcher& Searcher, UClass* Class, UObject* InOuter, RC::StringViewType InName, bool bExactClass)
    {
        return FindObject(Searcher, Class, InOuter, FromCharTypePtr<TCHAR>(InName.data()), bExactClass);
    }

    UObject* FindObject(struct ObjectSearcher& Searcher, UClass* Class, UObject* InOuter, const TCHAR* InName, bool bExactClass)
    {
        return FindObject(Class, InOuter, InName, bExactClass, &Searcher);
    }

    auto FindFirstInstanceOfClass(FName ClassName) -> UObject*
    {
        UObject* ObjectFound{nullptr};

        UObjectGlobals::ForEachUObject([&](UObject* Object, [[maybe_unused]]int32_t ChunkIndex, [[maybe_unused]]int32_t ObjectIndex) {
            UClass* Class = Object->GetClassPrivate();

            if (Class->GetNamePrivate().Equals(ClassName) && IsValidObjectForFindXOf(Object))
            {
                ObjectFound = Object;
                return LoopAction::Break;

            }

            if (!IsValidObjectForFindXOf(Object)) { return LoopAction::Continue; }

            for (UStruct* SuperStruct : TSuperStructRange(Class))
            {
                if (SuperStruct->GetNamePrivate().Equals(ClassName))
                {
                    ObjectFound = Object;
                    break;
                }
            }

            return LoopAction::Continue;
        });

        return ObjectFound;
    }

    auto FindFirstInstanceOfClass(const CharType* ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(FName(ClassName));
    }

    auto FindFirstInstanceOfClass(StringViewType ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(FName(ClassName));
    }

    auto FindFirstInstanceOfClass(const StringType& ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(FName(ClassName));
    }

    auto FindFirstInstanceOfClass(std::string_view ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(FName(ensure_str(ClassName)));
    }

    auto FindFirstInstanceOfClass(const std::string& ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(FName(ensure_str(ClassName)));
    }

    auto FindAllInstancesOfClass(FName ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        UObjectGlobals::ForEachUObject([&](UObject* Object, [[maybe_unused]]int32_t ChunkIndex, [[maybe_unused]]int32_t ObjectIndex) {
            if (!Object) { return LoopAction::Continue; }

            UClass* Class = Object->GetClassPrivate();
            if (!Class) { return LoopAction::Continue; }

            if (Class->GetNamePrivate().Equals(ClassName) && IsValidObjectForFindXOf(Object))
            {
                OutStorage.emplace_back(Object);
                return LoopAction::Continue;
            }

            if (!IsValidObjectForFindXOf(Object)) { return LoopAction::Continue; }

            for (UStruct* SuperStruct : TSuperStructRange(Class))
            {
                if (SuperStruct->GetNamePrivate().Equals(ClassName))
                {
                    OutStorage.emplace_back(Object);
                    break;
                }
            }

            return LoopAction::Continue;
        });
    }

    auto FindAllInstancesOfClass(const CharType* ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(FName(ClassName), OutStorage);
    }

    auto FindAllInstancesOfClass(StringViewType ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(FName(ClassName), OutStorage);
    }

    auto FindAllInstancesOfClass(const StringType& ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(FName(ClassName), OutStorage);
    }

    auto FindAllInstancesOfClass(std::string_view ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(FName(ensure_str(ClassName)), OutStorage);
    }

    auto FindAllInstancesOfClass(const std::string& ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(FName(ensure_str(ClassName)), OutStorage);
    }

    auto FindFirstOf(FName ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindFirstOf(const CharType* ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindFirstOf(StringViewType ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindFirstOf(const StringType& ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindFirstOf(std::string_view ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindFirstOf(const std::string& ClassName) -> UObject*
    {
        return FindFirstInstanceOfClass(ClassName);
    }

    auto FindAllOf(FName ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindAllOf(const CharType* ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindAllOf(StringViewType ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindAllOf(const StringType& ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindAllOf(std::string_view ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindAllOf(const std::string& ClassName, std::vector<UObject*>& OutStorage) -> void
    {
        FindAllInstancesOfClass(ClassName, OutStorage);
    }

    auto FindObjectsByClassAndName(size_t NumObjectsToFind, const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        bool bCareAboutClass = ClassName != FName(0u, 0u);
        bool bCareAboutName = ObjectShortName != FName(0u, 0u);

        if (!bCareAboutClass && !bCareAboutName)
        {
            throw std::runtime_error{"[UObjectGlobals::find_objects] Must supply class_name, object_short_name, or both"};
        }

        size_t NumObjectsFound{};

        ForEachUObject([&](UObject* Object, int32, int32) {
            bool bNameMatches{};
            // Intentionally not using the 'Equals' function here because names can have an instance number that we care about.
            if (bCareAboutName && Object->GetNamePrivate() == ObjectShortName)
            {
                bNameMatches = true;
            }

            bool bClassMatches{};
            if (bCareAboutClass)
            {
                auto* ObjClass = Object->GetClassPrivate();
                if (bExactClass)
                {
                    if (ObjClass->GetNamePrivate().Equals(ClassName))
                    {
                        bClassMatches = true;
                    }
                }
                else
                {
                    while (ObjClass)
                    {
                        if (ObjClass->GetNamePrivate().Equals(ClassName))
                        {
                            bClassMatches = true;
                            break;
                        }

                        ObjClass = ObjClass->GetSuperClass();
                    }
                }
            }

            if ((bCareAboutClass && bClassMatches && bCareAboutName && bNameMatches) ||
                (!bCareAboutName && bCareAboutClass && bClassMatches) ||
                (!bCareAboutClass && bCareAboutName && bNameMatches))
            {
                bool bRequiredFlagsPasses = RequiredFlags == EObjectFlags::RF_NoFlags || Object->HasAllFlags(static_cast<EObjectFlags>(RequiredFlags));
                bool bBannedFlagsPasses = BannedFlags == EObjectFlags::RF_NoFlags || !Object->HasAnyFlags(static_cast<EObjectFlags>(BannedFlags));

                if (bRequiredFlagsPasses && bBannedFlagsPasses)
                {
                    OutStorage.emplace_back(Object);
                    ++NumObjectsFound;
                }
            }

            if (NumObjectsToFind != 0 && NumObjectsFound >= NumObjectsToFind)
            {
                return LoopAction::Break;
            }
            else
            {
                return LoopAction::Continue;
            }
        });
    }

    auto FindObjectsByClassAndName(size_t NumObjectsToFind, const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(NumObjectsToFind, FName(ClassName), FName(ObjectShortName), OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObjectByClassAndName(const FName ClassName, const FName ObjectShortName, int32 RequiredFlags, int32 BannedFlags) -> UObject*
    {
        std::vector<UObject*> FoundObject{};
        FindObjectsByClassAndName(1, ClassName, ObjectShortName, FoundObject, RequiredFlags, BannedFlags);

        if (FoundObject.empty())
        {
            return nullptr;
        }
        else
        {
            return FoundObject[0];
        }
    };

    auto FindObjectByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, int32 RequiredFlags, int32 BannedFlags) -> UObject*
    {
        return FindObjectByClassAndName(FName(ClassName), FName(ObjectShortName), RequiredFlags, BannedFlags);
    }

    auto FindObjectsByClassAndName(const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(0, ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObjectsByClassAndName(const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(0, ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObjects(size_t NumObjectsToFind, const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(NumObjectsToFind, ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObjects(size_t NumObjectsToFind, const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(NumObjectsToFind, ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObject(const FName ClassName, const FName ObjectShortName, int32 RequiredFlags, int32 BannedFlags) -> UObject*
    {
        return FindObjectByClassAndName(ClassName, ObjectShortName, RequiredFlags, BannedFlags);
    }

    auto FindObject(const CharType* ClassName, const CharType* ObjectShortName, int32 RequiredFlags, int32 BannedFlags) -> UObject*
    {
        return FindObjectByClassAndName(ClassName, ObjectShortName, RequiredFlags, BannedFlags);
    }

    auto FindObjects(const FName ClassName, const FName ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }

    auto FindObjects(const CharType* ClassName, const CharType* ObjectShortName, std::vector<UObject*>& OutStorage, int32 RequiredFlags, int32 BannedFlags, bool bExactClass) -> void
    {
        FindObjectsByClassAndName(ClassName, ObjectShortName, OutStorage, RequiredFlags, BannedFlags, bExactClass);
    }
}
