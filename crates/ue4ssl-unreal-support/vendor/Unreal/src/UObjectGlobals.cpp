#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UPackage.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/PackageName.hpp>
#include <Unreal/UnrealVersion.hpp>
#include <Unreal/VersionedContainer/Container.hpp>
#include <Unreal/Searcher/ObjectSearcher.hpp>
#include <Unreal/Searcher/ObjectSearcherProfiler.hpp>
#include <Unreal/ClassListener.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#include <format>
#include <excpt.h>
#include <shared_mutex>

namespace RC::Unreal::UObjectGlobals
{
    static void log_global_script_hook_seh(const StringType& function_name,
                                           const char* phase,
                                           int32_t pre_id,
                                           int32_t post_id)
    {
        Output::send<LogLevel::Error>(
            STR("[UE4SS.GlobalScriptHook] SEH in {} callable for {} (pre_id={}, post_id={}), isolating callable\n"),
            ensure_str(phase ? phase : ""),
            function_name,
            pre_id,
            post_id);
    }

    static void invoke_global_script_callable(const UnrealScriptFunctionCallable* callable,
                                              UnrealScriptFunctionCallableContext* callable_context,
                                              void* custom_data,
                                              const StringType* function_name,
                                              const char* phase,
                                              int32_t pre_id,
                                              int32_t post_id)
    {
        __try
        {
            // UnrealScriptFunctionCallable is std::function; empty means "no hook" (e.g. pre=nullptr from C).
            if (callable && callable_context && *callable)
            {
                (*callable)(*callable_context, custom_data);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (function_name)
            {
                log_global_script_hook_seh(*function_name, phase, pre_id, post_id);
            }
        }
    }

    RC_UE_API Function<UObject*(StaticConstructObject_Internal_Params_Deprecated)> GlobalState::StaticConstructObjectInternalDeprecated{};
    RC_UE_API Function<UObject*(const FStaticConstructObjectParameters&)> GlobalState::StaticConstructObjectInternal{};

    auto SetupStaticConstructObjectInternalAddress(void* FunctionAddress) -> void
    {
        GlobalState::StaticConstructObjectInternal.assign_address(FunctionAddress);
        GlobalState::StaticConstructObjectInternalDeprecated.assign_address(FunctionAddress);
    }

    namespace Below426
    {
        static auto StaticConstructObject(const FStaticConstructObjectParameters& Params) -> UObject*
        {
            return GlobalState::StaticConstructObjectInternalDeprecated(
                    Params.Class,
                    Params.Outer,
                    Params.Name,
                    Params.SetFlags,
                    Params.InternalSetFlags,
                    Params.Template,
                    Params.bCopyTransientsFromClassDefaults,
                    Params.InstanceGraph,
                    Params.bAssumeTemplateIsArchetype,
                    Params.ExternalPackage
            );
        }
    }
    namespace Below56
    {
        static auto StaticConstructObject(const FStaticConstructObjectParameters& Params) -> UObject*
        {
            static Function<UObject*(const FStaticConstructObjectParameters&)> StaticConstructObjectInternal = [&]() {
                return GlobalState::StaticConstructObjectInternal.get_function_address();
            }();

            if (!StaticConstructObjectInternal.is_ready()) { return nullptr; }
            if (Params.Class == nullptr) { return nullptr; }

            return StaticConstructObjectInternal(Params);
        }
    }
    namespace Above55
    {
        static auto StaticConstructObject(const FStaticConstructObjectParameters& Params) -> UObject*
        {
            static Function<UObject*(const FStaticConstructObjectParameters&)> StaticConstructObjectInternal = [&]() {
                return GlobalState::StaticConstructObjectInternal.get_function_address();
            }();

            if (!StaticConstructObjectInternal.is_ready()) { return nullptr; }
            if (Params.Class == nullptr) { return nullptr; }

            return StaticConstructObjectInternal(Params);
        }
    }

    auto StaticConstructObject(const FStaticConstructObjectParameters& GenericParams) -> UObject*
    {
        if (Version::IsBelow(4, 26))
        {
            return Below426::StaticConstructObject(GenericParams);
        }
        else if (Version::IsBelow(5, 6))
        {
            Below56::FStaticConstructObjectParameters Params{};
            Params.Class = GenericParams.Class;
            Params.Outer = GenericParams.Outer;
            Params.Name = GenericParams.Name;
            Params.SetFlags = GenericParams.SetFlags;
            Params.InternalSetFlags = GenericParams.InternalSetFlags;
            Params.Template = GenericParams.Template;
            Params.bCopyTransientsFromClassDefaults = GenericParams.bCopyTransientsFromClassDefaults;
            Params.InstanceGraph = GenericParams.InstanceGraph;
            Params.bAssumeTemplateIsArchetype = GenericParams.bAssumeTemplateIsArchetype;
            Params.ExternalPackage = GenericParams.ExternalPackage;
            return Below56::StaticConstructObject(Params);
        }
        else
        {
            Above55::FStaticConstructObjectParameters Params{};
            Params.Class = GenericParams.Class;
            Params.Outer = GenericParams.Outer;
            Params.Name = GenericParams.Name;
            Params.SetFlags = GenericParams.SetFlags;
            Params.InternalSetFlags = GenericParams.InternalSetFlags;
            Params.Template = GenericParams.Template;
            Params.bCopyTransientsFromClassDefaults = GenericParams.bCopyTransientsFromClassDefaults;
            Params.InstanceGraph = GenericParams.InstanceGraph;
            Params.bAssumeTemplateIsArchetype = GenericParams.bAssumeTemplateIsArchetype;
            Params.ExternalPackage = GenericParams.ExternalPackage;
            return Above55::StaticConstructObject(Params);
        }
    }

    using ForEachUObjectCallback = std::function<LoopAction(UObject*, int32, int32)>;

    static auto ForEachUObject_NonChunked(const ForEachUObjectCallback& Callable) -> void
    {
        GUOBJECTARRAY_PROFILE_ITER_BEGIN()
        if (!GUObjectArray)
        {
            return;
        }

        LoopAction Action{};

        const auto& ObjObjects = GUObjectArray->GetObjObjects();
        static const auto ItemSize = FUObjectItem::UEP_TotalSize();

        for (int32_t ItemIndex = 0; ItemIndex < ObjObjects.GetNumElements(); ++ItemIndex)
        {
            const auto& ChunkPtr = ObjObjects.GetObjects();
            const auto ObjectItem = std::bit_cast<FUObjectItem*>(&std::bit_cast<uint8_t*>(ChunkPtr)[ItemIndex * ItemSize]);
            const auto Object = ObjectItem->GetUObject();
            if (ObjectItem->IsUnreachable() || !Object) { continue; }
            GUOBJECTARRAY_PROFILE_ITER_COUNT()
            Action = Callable(Object, 0, ItemIndex);
            if (Action == LoopAction::Break) { break; }
        }
        GUOBJECTARRAY_PROFILE_ITER_END()
    }

    static auto ForEachUObject_Chunked(const ForEachUObjectCallback& Callable) -> void
    {
        GUOBJECTARRAY_PROFILE_ITER_BEGIN()
        if (!GUObjectArray)
        {
            return;
        }

        LoopAction Action{};

        const auto& ObjObjects = GUObjectArray->GetObjObjects();
        const auto NumChunks = ObjObjects.GetNumChunks();
        static const auto ItemSize = FUObjectItem::UEP_TotalSize();

        for (int32_t ChunkIndex = 0; ChunkIndex < NumChunks; ++ChunkIndex)
        {
            for (int32_t ItemIndex = 0; ItemIndex < TUObjectArray::NumElementsPerChunk; ++ItemIndex)
            {
                const auto& ChunksPtr = ObjObjects.GetObjects();
                const auto ObjectItem = std::bit_cast<FUObjectItem*>(&std::bit_cast<uint8_t*>(ChunksPtr[ChunkIndex])[ItemIndex * ItemSize]);
                const auto Object = ObjectItem->GetUObject();
                if (ObjectItem->IsUnreachable() || !Object) { continue; }
                GUOBJECTARRAY_PROFILE_ITER_COUNT()
                Action = Callable(Object, ChunkIndex, ItemIndex);
                if (Action == LoopAction::Break) { break; }
            }
            if (Action == LoopAction::Break) { break; }
        }
        GUOBJECTARRAY_PROFILE_ITER_END()
    }

    static auto ForEachUObject_NonChunkedInRange(int32_t Start, int32_t End, const ForEachUObjectCallback& Callable) -> void
    {
        if (!GUObjectArray)
        {
            return;
        }

        LoopAction Action{};

        const auto& ObjObjects = GUObjectArray->GetObjObjects();
        const auto NumElements = ObjObjects.GetNumElements();
        static const auto ItemSize = FUObjectItem::UEP_TotalSize();

        const int32_t StartItemIndex = Start;
        const int32_t EndItemIndex = End < NumElements ? End : NumElements;

        for (int32_t ItemIndex = StartItemIndex; ItemIndex < EndItemIndex; ++ItemIndex)
        {
            const auto& ChunkPtr = ObjObjects.GetObjects();
            const auto ObjectItem = std::bit_cast<FUObjectItem*>(&std::bit_cast<uint8_t*>(ChunkPtr)[ItemIndex * ItemSize]);
            const auto Object = ObjectItem->GetUObject();
            if (ObjectItem->IsUnreachable() || !Object) { continue; }
            Action = Callable(Object, 0, ItemIndex);
            if (Action == LoopAction::Break) { break; }
        }
    }

    static auto ForEachUObject_ChunkedInRange(int32_t Start, int32_t End, const ForEachUObjectCallback& Callable) -> void
    {
        if (!GUObjectArray)
        {
            return;
        }

        LoopAction Action{};

        const auto& ObjObjects = GUObjectArray->GetObjObjects();
        const auto NumElements = ObjObjects.GetNumElements();
        const auto NumChunks = ObjObjects.GetNumChunks();
        static const auto ItemSize = FUObjectItem::UEP_TotalSize();

        const int32_t EndClamped = End < NumElements ? End : NumElements;
        const int32_t StartChunk = Start / TUObjectArray::NumElementsPerChunk;
        const int32_t StartItemIndex = Start % TUObjectArray::NumElementsPerChunk;

        int32_t CurrentTotalItem = Start;
        for (int32_t ChunkIndex = StartChunk; ChunkIndex < NumChunks; ++ChunkIndex)
        {
            bool ShouldBreak{};
            for (int32_t ItemIndex = StartItemIndex; ItemIndex < TUObjectArray::NumElementsPerChunk; ++ItemIndex)
            {
                const auto& ChunksPtr = ObjObjects.GetObjects();
                const auto ObjectItem = std::bit_cast<FUObjectItem*>(&std::bit_cast<uint8_t*>(ChunksPtr[ChunkIndex])[ItemIndex * ItemSize]);
                const auto Object = ObjectItem->GetUObject();
                if (ObjectItem->IsUnreachable() || !Object) { continue; }
                Action = Callable(Object, ChunkIndex, ItemIndex);
                if (Action == LoopAction::Break || CurrentTotalItem >= EndClamped)
                {
                    ShouldBreak = true;
                    break;
                }
            }
            ++CurrentTotalItem;
            if (ShouldBreak) { break; }
        }
    }

    auto ForEachUObject(const ForEachUObjectCallback& Callable) -> void
    {
        // TODO: Expose whether FUObjectArray is chunked in ini.
        //       This is just in case there's a game with custom changes where they pulled the chunked array into a non-chunked UE version.
        if (Version::IsAtMost(4, 19))
        {
            ForEachUObject_NonChunked(Callable);
        }
        else
        {
            ForEachUObject_Chunked(Callable);
        }
    }

    auto ForEachUObjectInChunk(int32_t ChunkIndex, const std::function<LoopAction(UObject*, int32)>& Callable) -> void
    {
        if (Version::IsAtMost(4, 19))
        {
            ForEachUObject_NonChunked([&](UObject* Object, int32_t, int32_t ObjectIndex) {
                return Callable(Object, ObjectIndex);
            });
        }
        else
        {
            if (!GUObjectArray || ChunkIndex >= GUObjectArray->GetObjObjects().GetNumChunks())
            {
                return;
            }

            LoopAction Action{};

            const auto& ObjObjects = GUObjectArray->GetObjObjects();
            static const auto ItemSize = FUObjectItem::UEP_TotalSize();

            for (int32_t ItemIndex = 0; ItemIndex < TUObjectArray::NumElementsPerChunk; ++ItemIndex)
            {
                const auto& ChunksPtr = ObjObjects.GetObjects();
                const auto ObjectItem = std::bit_cast<FUObjectItem*>(&std::bit_cast<uint8_t*>(ChunksPtr[ChunkIndex])[ItemIndex * ItemSize]);
                const auto Object = ObjectItem->GetUObject();
                if (ObjectItem->IsUnreachable() || !Object) { continue; }
                Action = Callable(Object, ItemIndex);
                if (Action == LoopAction::Break) { break; }
            }
        }
    }

    auto ForEachUObjectInRange(int32_t Start, int32_t End, const std::function<LoopAction(UObject*, int32, int32)>& Callable) -> void
    {
        if (Version::IsAtMost(4, 19))
        {
            ForEachUObject_NonChunkedInRange(Start, End, Callable);
        }
        else
        {
            ForEachUObject_ChunkedInRange(Start, End, Callable);
        }
    }

    struct GlobalHooksInternal
    {
        struct CallableData
        {
            struct InternalData
            {
                UnrealScriptFunctionCallable CallablePre{};
                UnrealScriptFunctionCallable CallablePost{};
                void* CustomData{};
                int32_t PreId{};
                int32_t PostId{};

                InternalData() = default;
                InternalData(UnrealScriptFunctionCallable PreCallable, UnrealScriptFunctionCallable PostCallable, void* CustomData, int32_t PreId, int32_t PostId) :
                      CallablePre(PreCallable),
                      CallablePost(PostCallable),
                      CustomData(CustomData),
                      PreId(PreId),
                      PostId(PostId) {}
            };
            std::vector<InternalData> Callables{};
        };
        static inline std::unordered_map<StringType, CallableData> GlobalScriptHooks{};
        static inline std::shared_mutex GlobalScriptHooksMutex{};
        static inline bool bIsHookEnabled{};
        static inline int32_t LastGenericHookId{};
        static inline std::unordered_map<int32_t, int32_t> GenericHookIdToNativeHookId{};
    };

    static auto GlobalScriptHookPre([[maybe_unused]]Hook::TCallbackIterationData<void>& CallbackIterationData, [[maybe_unused]]Unreal::UObject* Context, Unreal::FFrame& Stack, [[maybe_unused]]void* RESULT_DECL) -> void
    {
        std::shared_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
        if (GlobalHooksInternal::GlobalScriptHooks.empty()) { return; }
        auto* Node = Stack.Node();
        if (!Node) { return; }
        if (auto it = GlobalHooksInternal::GlobalScriptHooks.find(Node->GetFullName()); it != GlobalHooksInternal::GlobalScriptHooks.end())
        {
            UnrealScriptFunctionCallableContext CallableContext{Context, Stack, RESULT_DECL};
            auto function_name = Node->GetFullName();
            for (const auto& Callable : it->second.Callables)
            {
                invoke_global_script_callable(
                    &Callable.CallablePre,
                    &CallableContext,
                    Callable.CustomData,
                    &function_name,
                    "pre",
                    Callable.PreId,
                    Callable.PostId);
            }
        }
    }

    static auto GlobalScriptHookPost([[maybe_unused]]Hook::TCallbackIterationData<void>& CallbackIterationData, [[maybe_unused]]Unreal::UObject* Context, Unreal::FFrame& Stack, [[maybe_unused]]void* RESULT_DECL) -> void
    {
        std::shared_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
        if (GlobalHooksInternal::GlobalScriptHooks.empty()) { return; }
        auto* Node = Stack.Node();
        if (!Node) { return; }
        if (auto it = GlobalHooksInternal::GlobalScriptHooks.find(Node->GetFullName()); it != GlobalHooksInternal::GlobalScriptHooks.end())
        {
            UnrealScriptFunctionCallableContext CallableContext{Context, Stack, RESULT_DECL};
            auto function_name = Node->GetFullName();
            for (const auto& Callable : it->second.Callables)
            {
                invoke_global_script_callable(
                    &Callable.CallablePost,
                    &CallableContext,
                    Callable.CustomData,
                    &function_name,
                    "post",
                    Callable.PreId,
                    Callable.PostId);
            }
        }
    }

    auto RegisterHook(UFunction* Function, UnrealScriptFunctionCallable PreCallback, UnrealScriptFunctionCallable PostCallback, void* CustomData) -> std::pair<int, int>
    {
        if (!Function)
        {
            Output::send<LogLevel::Error>(STR("RegisterHook failed: UFunction is nullptr\n"));
            return {-1, -1};
        }
        auto NativeFunction = Function->GetFunc();
        if (NativeFunction &&
            NativeFunction != UObject::ProcessInternalInternal.get_function_address() &&
            Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
        {
            auto PreId = Function->RegisterPreHook(PreCallback, CustomData);
            auto PostId = Function->RegisterPostHook(PostCallback, CustomData);
            GlobalHooksInternal::GenericHookIdToNativeHookId.emplace(++GlobalHooksInternal::LastGenericHookId, PreId);
            auto GenericPreId = GlobalHooksInternal::LastGenericHookId;
            GlobalHooksInternal::GenericHookIdToNativeHookId.emplace(++GlobalHooksInternal::LastGenericHookId, PostId);
            auto GenericPostId = GlobalHooksInternal::LastGenericHookId;
            return {GenericPreId, GenericPostId};
        }
        else if (NativeFunction &&
                 NativeFunction == UObject::ProcessInternalInternal.get_function_address() &&
                 !Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
        {
            if (!GlobalHooksInternal::bIsHookEnabled)
            {
                const Hook::FCallbackOptions GlobalScriptHookOptions {false, false, STR("UE4SS"), STR("GlobalScriptHook")};
                if (UObject::ProcessLocalScriptFunctionInternal.is_ready() && Version::IsAtLeast(4, 22))
                {
                    Hook::RegisterProcessLocalScriptFunctionPreCallback(GlobalScriptHookPre, GlobalScriptHookOptions);
                    Hook::RegisterProcessLocalScriptFunctionPostCallback(GlobalScriptHookPost, GlobalScriptHookOptions);
                }
                else if (UObject::ProcessInternalInternal.is_ready() && Version::IsBelow(4, 22))
                {
                    Hook::RegisterProcessInternalPreCallback(GlobalScriptHookPre, GlobalScriptHookOptions);
                    Hook::RegisterProcessInternalPostCallback(GlobalScriptHookPost, GlobalScriptHookOptions);
                }
                GlobalHooksInternal::bIsHookEnabled = true;
            }
            ++GlobalHooksInternal::LastGenericHookId;
            auto GenericPreId = GlobalHooksInternal::LastGenericHookId;
            auto GenericPostId = GlobalHooksInternal::LastGenericHookId;
            {
                std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
                auto [Data, _] = GlobalHooksInternal::GlobalScriptHooks.emplace(Function->GetFullName(), GlobalHooksInternal::CallableData{});
                Data->second.Callables.emplace_back(PreCallback, PostCallback, CustomData, GenericPreId, GenericPostId);
            }
            return {GenericPreId, GenericPostId};
        }
        else
        {
            std::string error_message{"Was unable to register a UFunction hook, information:\n"};
            error_message.append(std::format("UFunction::Func: {}\n", std::bit_cast<void*>(NativeFunction)));
            error_message.append(std::format("ProcessInternal: {}\n", UObject::ProcessInternalInternal.get_function_address()));
            error_message.append(std::format("FUNC_Native: {}\n", static_cast<uint32_t>(Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))));
            throw std::runtime_error{error_message};
        }
    }

    auto RegisterHook(const StringType& FunctionFullNameNoType, UnrealScriptFunctionCallable PreCallback, UnrealScriptFunctionCallable PostCallback, void* CustomData) -> std::pair<int, int>
    {
        auto Function = StaticFindObject<UFunction*>(nullptr, nullptr, FunctionFullNameNoType);
        if (!Function)
        {
            Output::send<LogLevel::Error>(STR("RegisterHook failed: UFunction '{}' not found\n"), FunctionFullNameNoType);
            return {-1, -1};
        }
        return RegisterHook(Function, PreCallback, PostCallback, CustomData);
    }

    auto UnregisterHook(class UFunction* Function, std::pair<int, int> Ids) -> void
    {
        if (!Function)
        {
            Output::send<LogLevel::Error>(STR("UnregisterHook failed: UFunction is nullptr\n"));
            return;
        }
        Output::send(STR("Unregistering hook\n"));
        auto NativeFunction = Function->GetFunc();
        if (NativeFunction &&
            NativeFunction != UObject::ProcessInternalInternal.get_function_address() &&
            Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
        {
            Output::send(STR("Unregistering native hook ({}, {})\n"), Ids.first, Ids.second);
            if (auto PreNativeId = GlobalHooksInternal::GenericHookIdToNativeHookId.find(Ids.first); PreNativeId != GlobalHooksInternal::GenericHookIdToNativeHookId.end())
            {
                Function->UnregisterHook(PreNativeId->second);
                Output::send(STR("Native hook unregistered\n"));
            }
            if (auto PostNativeId = GlobalHooksInternal::GenericHookIdToNativeHookId.find(Ids.second); PostNativeId != GlobalHooksInternal::GenericHookIdToNativeHookId.end())
            {
                Function->UnregisterHook(PostNativeId->second);
            }
        }
        else if (NativeFunction &&
                 NativeFunction == UObject::ProcessInternalInternal.get_function_address() &&
                 !Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
        {
            {
                std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
                if (auto CallbackDataIt = GlobalHooksInternal::GlobalScriptHooks.find(Function->GetFullName()); CallbackDataIt != GlobalHooksInternal::GlobalScriptHooks.end())
                {
                    auto& Callbacks = CallbackDataIt->second.Callables;
                    Callbacks.erase(std::remove_if(Callbacks.begin(), Callbacks.end(), [&](GlobalHooksInternal::CallableData::InternalData& CallbackData) -> bool {
                        return Ids.first == CallbackData.PreId && Ids.second == CallbackData.PostId;
                    }), Callbacks.end());
                }
            }
        }
        else
        {
            std::string error_message{"Was unable to unregister a UFunction hook, information:\n"};
            error_message.append(std::format("UFunction::Func: {}\n", std::bit_cast<void*>(NativeFunction)));
            error_message.append(std::format("ProcessInternal: {}\n", UObject::ProcessInternalInternal.get_function_address()));
            error_message.append(std::format("FUNC_Native: {}\n", static_cast<uint32_t>(Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))));
            throw std::runtime_error{error_message};
        }
    }

    auto UnregisterHook(const StringType& FunctionFullNameNoType, std::pair<int, int> Ids) -> void
    {
        auto Function = StaticFindObject<UFunction*>(nullptr, nullptr, FunctionFullNameNoType);
        if (!Function) { throw std::runtime_error{std::format("Unable to find function: {}", to_string(FunctionFullNameNoType))}; }
        UnregisterHook(Function, Ids);
    }
}
