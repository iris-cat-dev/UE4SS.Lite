#include <Unreal/Searcher/ObjectSearcher.hpp>
#include <Unreal/Searcher/ObjectSearcherProfiler.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <atomic>

namespace RC::Unreal
{
    std::unordered_map<size_t, std::unique_ptr<ObjectSearcherPoolBase>> AllSearcherPools;

    size_t HashSearcherKey(UClass* Class, UStruct* SuperStruct)
    {
        auto HashClass = std::hash<size_t>()(Class ? Class->HashObject() : 0);
        auto HashSuperStruct = std::hash<size_t>()(SuperStruct ? SuperStruct->HashObject() : 0);
        return HashClass ^ (HashSuperStruct << 1);
    }

    ObjectSearcher FindObjectSearcher(UClass* Class, UStruct* SuperStruct)
    {
        if (auto It = AllSearcherPools.find(HashSearcherKey(Class, SuperStruct)); It != AllSearcherPools.end())
        {
            return ObjectSearcher{Class, SuperStruct, It->second.get(), &ObjectSearcherFastInternal};
        }
        else
        {
            return ObjectSearcher{Class, SuperStruct, nullptr, &ObjectSearcherSlowInternal};
        }
    }

    static LoopAction InternalPredicate(UObject* Object, UClass* Class, UStruct* SuperStruct, const ObjectSearcherForEachPredicate& Predicate)
    {
        if (Class && !Object->IsA(Class)) { return LoopAction::Continue; }
        if (SuperStruct)
        {
            if (Object->IsA<UClass>())
            {
                if (!static_cast<UClass*>(Object)->IsChildOf(SuperStruct)) { return LoopAction::Continue; }
            }
            else
            {
                if (!Object->GetClassPrivate()->IsChildOf(SuperStruct)) { return LoopAction::Continue; }
            }
        }
        return Predicate(Object);
    }

    void ObjectSearcherFastInternal(UClass* Class, UStruct* SuperStruct, const ObjectSearcherForEachPredicate& Predicate, std::vector<const FUObjectItem*>* Pool)
    {
        OBJSEARCHER_PROFILE_SEARCH_FAST()
        for (const auto& Item : *Pool)
        {
            if (!Item || !Item->GetUObject() || Item->IsUnreachable()) { continue; }
            if (InternalPredicate(Item->GetUObject(), Class, SuperStruct, Predicate) == LoopAction::Break) { break; }
        }
    }

    void ObjectSearcherSlowInternal(UClass* Class, UStruct* SuperStruct, const ObjectSearcherForEachPredicate& Predicate, std::vector<const FUObjectItem*>* Pool)
    {
        OBJSEARCHER_PROFILE_SEARCH_SLOW()
        (void)Pool;

        if (UnrealInitializer::StaticStorage::GlobalConfig.bUseNativeClassEnumeration)
        {
            static std::atomic_bool bWarned{false};
            bool bExpected{false};
            if (bWarned.compare_exchange_strong(bExpected, true))
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SS.ObjectSearch] UseNativeClassEnumeration is enabled, but no native class enumerator is installed yet; falling back to GUObjectArray search.\n"));
            }
        }

        UObjectGlobals::ForEachUObject([&](UObject* Object, ...) {
            if (!Object || Object->IsUnreachable()) { return LoopAction::Continue; }
            return InternalPredicate(Object, Class, SuperStruct, Predicate);
        });
    }
}
