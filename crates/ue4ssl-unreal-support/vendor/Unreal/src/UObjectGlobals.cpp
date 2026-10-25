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
#include <atomic>
#include <condition_variable>
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
        struct InternalData
        {
            UnrealScriptFunctionCallable CallablePre{};
            UnrealScriptFunctionCallable CallablePost{};
            void* CustomData{};
            int32_t PreId{};
            int32_t PostId{};

        private:
            std::mutex StateMutex{};
            std::condition_variable IdleCondition{};
            size_t ActiveInvocations{};
            bool Registered{true};

        public:
            InternalData(UnrealScriptFunctionCallable PreCallable,
                         UnrealScriptFunctionCallable PostCallable,
                         void* InCustomData,
                         int32_t InPreId,
                         int32_t InPostId) :
                CallablePre(std::move(PreCallable)),
                CallablePost(std::move(PostCallable)),
                CustomData(InCustomData),
                PreId(InPreId),
                PostId(InPostId)
            {
            }

            auto BeginInvocation() -> bool
            {
                std::lock_guard lock(StateMutex);
                if (!Registered)
                {
                    return false;
                }
                ++ActiveInvocations;
                return true;
            }

            auto EndInvocation() -> void
            {
                std::lock_guard lock(StateMutex);
                if (--ActiveInvocations == 0)
                {
                    IdleCondition.notify_all();
                }
            }

            auto Disable() -> void
            {
                std::lock_guard lock(StateMutex);
                Registered = false;
            }

            auto WaitUntilIdle() -> void
            {
                std::unique_lock lock(StateMutex);
                IdleCondition.wait(lock, [this] {
                    return ActiveInvocations == 0;
                });
            }
        };

        using Registration = std::shared_ptr<InternalData>;
        using CallbackList = std::vector<Registration>;
        using CallbackSnapshot = std::shared_ptr<const CallbackList>;

        struct CallableData
        {
            CallbackSnapshot Callables{};
            uint64 ShortName{};
        };

        struct NativeCallbackData
        {
            UFunction* Function{};
            int32_t NativeId{};
        };

        static inline std::unordered_map<StringType, CallableData> GlobalScriptHooks{};
        // Count full-name entries, not callbacks; full names remain the authoritative keys.
        static inline std::unordered_map<uint64, size_t> HookedShortNames{};
        static inline std::unordered_map<int32_t, StringType> GenericScriptHookIdToFunctionName{};
        static inline std::unordered_map<int32_t, NativeCallbackData> GenericHookIdToNativeHook{};
        static inline std::atomic<bool> bAnyScriptHook{};
        static inline std::shared_mutex GlobalScriptHooksMutex{};
        static inline bool bIsHookEnabled{};
        static inline int32_t LastGenericHookId{};
        static inline thread_local size_t ScriptHookInvocationDepth{};
    };

    class GlobalScriptHookInvocationScope
    {
    private:
        GlobalHooksInternal::Registration m_Registration{};
        bool m_Active{};

    public:
        explicit GlobalScriptHookInvocationScope(GlobalHooksInternal::Registration Registration) :
            m_Registration(std::move(Registration))
        {
            if (m_Registration && m_Registration->BeginInvocation())
            {
                ++GlobalHooksInternal::ScriptHookInvocationDepth;
                m_Active = true;
            }
        }

        ~GlobalScriptHookInvocationScope()
        {
            if (m_Active)
            {
                --GlobalHooksInternal::ScriptHookInvocationDepth;
                m_Registration->EndInvocation();
            }
        }

        GlobalScriptHookInvocationScope(const GlobalScriptHookInvocationScope&) = delete;
        auto operator=(const GlobalScriptHookInvocationScope&) -> GlobalScriptHookInvocationScope& = delete;

        explicit operator bool() const
        {
            return m_Active;
        }
    };

    static auto FindGlobalScriptHookSnapshot(FFrame& Stack, StringType& FunctionName) -> GlobalHooksInternal::CallbackSnapshot
    {
        if (!GlobalHooksInternal::bAnyScriptHook.load(std::memory_order_acquire))
        {
            return {};
        }
        auto* Node = Stack.Node();
        if (!Node)
        {
            return {};
        }

        std::shared_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
        if (!GlobalHooksInternal::HookedShortNames.contains(Node->GetFName().ToUnstableInt()))
        {
            return {};
        }

        FunctionName = Node->GetFullName();
        if (auto It = GlobalHooksInternal::GlobalScriptHooks.find(FunctionName);
            It != GlobalHooksInternal::GlobalScriptHooks.end())
        {
            return It->second.Callables;
        }
        return {};
    }

    static auto RunGlobalScriptHooks(UObject* Context, FFrame& Stack, void* RESULT_DECL, bool IsPre) -> void
    {
        StringType FunctionName{};
        auto Callbacks = FindGlobalScriptHookSnapshot(Stack, FunctionName);
        if (!Callbacks)
        {
            return;
        }

        UnrealScriptFunctionCallableContext CallableContext{Context, Stack, RESULT_DECL};
        for (const auto& Callable : *Callbacks)
        {
            const auto* Function = IsPre ? &Callable->CallablePre : &Callable->CallablePost;
            if (!*Function)
            {
                continue;
            }

            GlobalScriptHookInvocationScope Invocation{Callable};
            if (!Invocation)
            {
                continue;
            }

            invoke_global_script_callable(
                Function,
                &CallableContext,
                Callable->CustomData,
                &FunctionName,
                IsPre ? "pre" : "post",
                Callable->PreId,
                Callable->PostId);
        }
    }

    static auto GlobalScriptHookPre([[maybe_unused]] Hook::TCallbackIterationData<void>& CallbackIterationData,
                                    Unreal::UObject* Context,
                                    Unreal::FFrame& Stack,
                                    void* RESULT_DECL) -> void
    {
        RunGlobalScriptHooks(Context, Stack, RESULT_DECL, true);
    }

    static auto GlobalScriptHookPost([[maybe_unused]] Hook::TCallbackIterationData<void>& CallbackIterationData,
                                     Unreal::UObject* Context,
                                     Unreal::FFrame& Stack,
                                     void* RESULT_DECL) -> void
    {
        RunGlobalScriptHooks(Context, Stack, RESULT_DECL, false);
    }

    static auto RemoveGlobalScriptHook(const StringType& FunctionName, std::pair<int, int> Ids) -> bool
    {
        GlobalHooksInternal::Registration Removed{};
        {
            std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
            auto CallbackDataIt = GlobalHooksInternal::GlobalScriptHooks.find(FunctionName);
            if (CallbackDataIt == GlobalHooksInternal::GlobalScriptHooks.end() || !CallbackDataIt->second.Callables)
            {
                return false;
            }

            const auto& CurrentCallbacks = *CallbackDataIt->second.Callables;
            auto Match = std::find_if(CurrentCallbacks.begin(), CurrentCallbacks.end(), [&](const auto& CallbackData) {
                return Ids.first == CallbackData->PreId && Ids.second == CallbackData->PostId;
            });
            if (Match == CurrentCallbacks.end())
            {
                return false;
            }

            Removed = *Match;
            Removed->Disable();
            GlobalHooksInternal::GenericScriptHookIdToFunctionName.erase(Removed->PreId);
            GlobalHooksInternal::GenericScriptHookIdToFunctionName.erase(Removed->PostId);

            if (CurrentCallbacks.size() == 1)
            {
                if (auto ShortNameIt = GlobalHooksInternal::HookedShortNames.find(CallbackDataIt->second.ShortName);
                    ShortNameIt != GlobalHooksInternal::HookedShortNames.end() && --ShortNameIt->second == 0)
                {
                    GlobalHooksInternal::HookedShortNames.erase(ShortNameIt);
                }
                GlobalHooksInternal::GlobalScriptHooks.erase(CallbackDataIt);
            }
            else
            {
                auto UpdatedCallbacks = std::make_shared<GlobalHooksInternal::CallbackList>();
                UpdatedCallbacks->reserve(CurrentCallbacks.size() - 1);
                for (const auto& CallbackData : CurrentCallbacks)
                {
                    if (CallbackData != Removed)
                    {
                        UpdatedCallbacks->push_back(CallbackData);
                    }
                }
                CallbackDataIt->second.Callables = std::move(UpdatedCallbacks);
            }

            GlobalHooksInternal::bAnyScriptHook.store(
                !GlobalHooksInternal::GlobalScriptHooks.empty(),
                std::memory_order_release);
        }

        // Waiting here preserves the existing external-unregister guarantee. Waiting
        // from a callback can deadlock with another callback doing the inverse removal;
        // owned registrations keep CustomData alive until their final invocation exits.
        if (GlobalHooksInternal::ScriptHookInvocationDepth == 0)
        {
            Removed->WaitUntilIdle();
        }
        return true;
    }

    auto RegisterHook(UFunction* Function,
                      UnrealScriptFunctionCallable PreCallback,
                      UnrealScriptFunctionCallable PostCallback,
                      void* CustomData) -> std::pair<int, int>
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

            std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
            auto GenericPreId = ++GlobalHooksInternal::LastGenericHookId;
            auto GenericPostId = ++GlobalHooksInternal::LastGenericHookId;
            GlobalHooksInternal::GenericHookIdToNativeHook.emplace(
                GenericPreId,
                GlobalHooksInternal::NativeCallbackData{Function, PreId});
            GlobalHooksInternal::GenericHookIdToNativeHook.emplace(
                GenericPostId,
                GlobalHooksInternal::NativeCallbackData{Function, PostId});
            return {GenericPreId, GenericPostId};
        }
        else if (NativeFunction &&
                 NativeFunction == UObject::ProcessInternalInternal.get_function_address() &&
                 !Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))
        {
            auto FunctionName = Function->GetFullName();
            auto ShortName = Function->GetFName().ToUnstableInt();

            std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
            if (!GlobalHooksInternal::bIsHookEnabled)
            {
                const Hook::FCallbackOptions GlobalScriptHookOptions{false, false, STR("UE4SS"), STR("GlobalScriptHook")};
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

            auto GenericPreId = ++GlobalHooksInternal::LastGenericHookId;
            auto GenericPostId = GenericPreId;
            auto Registration = std::make_shared<GlobalHooksInternal::InternalData>(
                std::move(PreCallback),
                std::move(PostCallback),
                CustomData,
                GenericPreId,
                GenericPostId);

            auto [Data, Inserted] = GlobalHooksInternal::GlobalScriptHooks.emplace(
                FunctionName,
                GlobalHooksInternal::CallableData{});
            if (Inserted)
            {
                Data->second.ShortName = ShortName;
                ++GlobalHooksInternal::HookedShortNames[ShortName];
            }

            auto UpdatedCallbacks = std::make_shared<GlobalHooksInternal::CallbackList>();
            if (Data->second.Callables)
            {
                *UpdatedCallbacks = *Data->second.Callables;
            }
            UpdatedCallbacks->push_back(std::move(Registration));
            Data->second.Callables = std::move(UpdatedCallbacks);
            GlobalHooksInternal::GenericScriptHookIdToFunctionName.emplace(GenericPreId, FunctionName);
            GlobalHooksInternal::bAnyScriptHook.store(true, std::memory_order_release);
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

    auto RegisterHook(const StringType& FunctionFullNameNoType,
                      UnrealScriptFunctionCallable PreCallback,
                      UnrealScriptFunctionCallable PostCallback,
                      void* CustomData) -> std::pair<int, int>
    {
        auto Function = StaticFindObject<UFunction*>(nullptr, nullptr, FunctionFullNameNoType);
        if (!Function)
        {
            Output::send<LogLevel::Error>(STR("RegisterHook failed: UFunction '{}' not found\n"), FunctionFullNameNoType);
            return {-1, -1};
        }
        return RegisterHook(Function, std::move(PreCallback), std::move(PostCallback), CustomData);
    }

    static auto MakeOwnedHookCallback(UnrealScriptFunctionCallable Callback,
                                      std::shared_ptr<void> CustomData) -> UnrealScriptFunctionCallable
    {
        if (!Callback)
        {
            return {};
        }
        return [Callback = std::move(Callback), CustomData = std::move(CustomData)](
                   UnrealScriptFunctionCallableContext& Context,
                   void*) {
            Callback(Context, CustomData.get());
        };
    }

    auto RegisterHookOwned(UFunction* Function,
                           UnrealScriptFunctionCallable PreCallback,
                           UnrealScriptFunctionCallable PostCallback,
                           std::shared_ptr<void> CustomData) -> std::pair<int, int>
    {
        auto OwnedPreCallback = MakeOwnedHookCallback(std::move(PreCallback), CustomData);
        auto OwnedPostCallback = MakeOwnedHookCallback(std::move(PostCallback), std::move(CustomData));
        return RegisterHook(Function, std::move(OwnedPreCallback), std::move(OwnedPostCallback), nullptr);
    }

    auto RegisterHookOwned(const StringType& FunctionFullNameNoType,
                           UnrealScriptFunctionCallable PreCallback,
                           UnrealScriptFunctionCallable PostCallback,
                           std::shared_ptr<void> CustomData) -> std::pair<int, int>
    {
        auto Function = StaticFindObject<UFunction*>(nullptr, nullptr, FunctionFullNameNoType);
        if (!Function)
        {
            Output::send<LogLevel::Error>(STR("RegisterHookOwned failed: UFunction '{}' not found\n"), FunctionFullNameNoType);
            return {-1, -1};
        }
        return RegisterHookOwned(
            Function,
            std::move(PreCallback),
            std::move(PostCallback),
            std::move(CustomData));
    }

    auto UnregisterHook(std::pair<int, int> Ids) -> bool
    {
        StringType ScriptFunctionName{};
        bool IsScriptHook{};
        {
            std::shared_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
            auto ScriptHook = GlobalHooksInternal::GenericScriptHookIdToFunctionName.find(Ids.first);
            if (ScriptHook == GlobalHooksInternal::GenericScriptHookIdToFunctionName.end())
            {
                ScriptHook = GlobalHooksInternal::GenericScriptHookIdToFunctionName.find(Ids.second);
            }
            if (ScriptHook != GlobalHooksInternal::GenericScriptHookIdToFunctionName.end())
            {
                ScriptFunctionName = ScriptHook->second;
                IsScriptHook = true;
            }
        }

        if (IsScriptHook)
        {
            return RemoveGlobalScriptHook(ScriptFunctionName, Ids);
        }

        GlobalHooksInternal::NativeCallbackData PreCallback{};
        GlobalHooksInternal::NativeCallbackData PostCallback{};
        bool HasPreCallback{};
        bool HasPostCallback{};
        {
            std::unique_lock lock(GlobalHooksInternal::GlobalScriptHooksMutex);
            if (auto PreIt = GlobalHooksInternal::GenericHookIdToNativeHook.find(Ids.first);
                PreIt != GlobalHooksInternal::GenericHookIdToNativeHook.end())
            {
                PreCallback = PreIt->second;
                HasPreCallback = true;
                GlobalHooksInternal::GenericHookIdToNativeHook.erase(PreIt);
            }
            if (auto PostIt = GlobalHooksInternal::GenericHookIdToNativeHook.find(Ids.second);
                PostIt != GlobalHooksInternal::GenericHookIdToNativeHook.end())
            {
                PostCallback = PostIt->second;
                HasPostCallback = true;
                GlobalHooksInternal::GenericHookIdToNativeHook.erase(PostIt);
            }
        }

        if (HasPreCallback)
        {
            PreCallback.Function->UnregisterHook(PreCallback.NativeId);
        }
        if (HasPostCallback)
        {
            PostCallback.Function->UnregisterHook(PostCallback.NativeId);
        }
        return HasPreCallback || HasPostCallback;
    }

    auto UnregisterHook([[maybe_unused]] UFunction* Function, std::pair<int, int> Ids) -> void
    {
        if (!UnregisterHook(Ids))
        {
            Output::send<LogLevel::Warning>(
                STR("UnregisterHook did not find callback IDs ({}, {})\n"),
                Ids.first,
                Ids.second);
        }
    }

    auto UnregisterHook([[maybe_unused]] const StringType& FunctionFullNameNoType, std::pair<int, int> Ids) -> void
    {
        UnregisterHook(static_cast<UFunction*>(nullptr), Ids);
    }
}
