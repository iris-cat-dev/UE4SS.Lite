#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <DynamicOutput/Output.hpp>
#include <Helpers/Format.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

namespace RC::Unreal
{
    std::atomic<CallbackId> UnrealScriptFunctionData::HookIndexCounter{1};

    UnrealScriptFunctionData::UnrealScriptFunctionData(UnrealScriptFunction OriginalFuncPtr) {
        this->OriginalFunc = OriginalFuncPtr;
    }

    CallbackId UnrealScriptFunctionData::AddPreCallback(const UnrealScriptFunctionCallable& Callable, void* CustomData, UObject* FireOnInstance)
    {
        CallbackId NewCallbackId = MakeNewId();
        this->PreCallbacks.emplace(std::piecewise_construct, std::forward_as_tuple(NewCallbackId), std::forward_as_tuple(Callable, CustomData, FireOnInstance));
        return NewCallbackId;
    }

    CallbackId UnrealScriptFunctionData::AddPostCallback(const UnrealScriptFunctionCallable& Callable, void* CustomData, UObject* FireOnInstance)
    {
        CallbackId NewCallbackId = MakeNewId();
        this->PostCallbacks.emplace(std::piecewise_construct, std::forward_as_tuple(NewCallbackId), std::forward_as_tuple(Callable, CustomData, FireOnInstance));
        return NewCallbackId;
    }

    bool UnrealScriptFunctionData::RemoveCallback(CallbackId CallbackId)
    {
        auto CallbackData = GetCallbackData(CallbackId);
        if (CallbackData)
        {
            if (bIsMidExecution)
            {
                // Hook will be unregistered after the callback is done executing.
                CallbackData->UnregistrationRequested = true;
                return true;
            }
            else
            {
                return PreCallbacks.erase(CallbackId) || PostCallbacks.erase(CallbackId);
            }
        }
        else
        {
            return false;
        }
    }

    void UnrealScriptFunctionData::RemoveAllCallbacks()
    {
        for (auto PreCallbackIterator = PreCallbacks.begin(); PreCallbackIterator != PreCallbacks.end();)
        {
            if (bIsMidExecution)
            {
                // Hook will be unregistered after the callback is done executing.
                PreCallbackIterator->second.UnregistrationRequested = true;
                ++PreCallbackIterator;
            }
            else
            {
                PreCallbackIterator = PreCallbacks.erase(PreCallbackIterator);
            }
        }

        for (auto PostCallbackIterator = PostCallbacks.begin(); PostCallbackIterator != PostCallbacks.end();)
        {
            if (bIsMidExecution)
            {
                // Hook will be unregistered after the callback is done executing.
                PostCallbackIterator->second.UnregistrationRequested = true;
                ++PostCallbackIterator;
            }
            else
            {
                PostCallbackIterator = PostCallbacks.erase(PostCallbackIterator);
            }
        }
    }

    void UnrealScriptFunctionData::FirePreCallbacks(UnrealScriptFunctionCallableContext& Context)
    {
        for (auto PreCallbackIterator = PreCallbacks.begin(); PreCallbackIterator != PreCallbacks.end();)
        {
            // Skip callbacks that don't match the instance filter
            if (PreCallbackIterator->second.FireOnInstance && PreCallbackIterator->second.FireOnInstance != Context.Context)
            {
                ++PreCallbackIterator;
                continue;
            }
            // Skip callbacks that have been marked for unregistration or have invalid Callable
            if (PreCallbackIterator->second.UnregistrationRequested || !PreCallbackIterator->second.Callable)
            {
                PreCallbackIterator = PreCallbacks.erase(PreCallbackIterator);
                continue;
            }
            PreCallbackIterator->second.Callable(Context, PreCallbackIterator->second.CustomData);
            if (PreCallbackIterator->second.UnregistrationRequested)
            {
                PreCallbackIterator = PreCallbacks.erase(PreCallbackIterator);
            }
            else
            {
                ++PreCallbackIterator;
            }
        }
    }

    void UnrealScriptFunctionData::FirePostCallbacks(UnrealScriptFunctionCallableContext& Context)
    {
        for (auto PostCallbackIterator = PostCallbacks.begin(); PostCallbackIterator != PostCallbacks.end();)
        {
            // Skip callbacks that have been marked for unregistration or have invalid Callable
            if (PostCallbackIterator->second.UnregistrationRequested || !PostCallbackIterator->second.Callable)
            {
                PostCallbackIterator = PostCallbacks.erase(PostCallbackIterator);
                continue;
            }
            PostCallbackIterator->second.Callable(Context, PostCallbackIterator->second.CustomData);
            if (PostCallbackIterator->second.UnregistrationRequested)
            {
                PostCallbackIterator = PostCallbacks.erase(PostCallbackIterator);
            }
            else
            {
                ++PostCallbackIterator;
            }
        }
    }

    UnrealScriptCallbackData* UnrealScriptFunctionData::GetCallbackData(CallbackId CallbackId)
    {
        auto PreIterator = PreCallbacks.find(CallbackId);
        if (PreIterator != PreCallbacks.end()) { return &PreIterator->second; }

        auto PostIterator = PostCallbacks.find(CallbackId);
        if (PostIterator != PostCallbacks.end()) { return &PostIterator->second; }

        return nullptr;
    }

    CallbackId UnrealScriptFunctionData::MakeNewId()
    {
        return HookIndexCounter++;
    }

    UnrealScriptFunctionCallableContext::UnrealScriptFunctionCallableContext(UObject* Context, FFrame& TheStack, void* RESULT_DECL)
            : Context(Context),
              TheStack(TheStack),
              RESULT_DECL(RESULT_DECL) {}

    static Internal::HookedUFunctionMap GHookedScriptFunctions{};

    auto Internal::GetHookedFunctionsMap() -> HookedUFunctionMap&
    {
        return GHookedScriptFunctions;
    }

    static UFunction* safe_read_frame_node(FFrame& TheStack)
    {
        __try
        {
            return TheStack.Node();
        }
        __except (1 /*EXCEPTION_EXECUTE_HANDLER*/)
        {
            return nullptr;
        }
    }

    static UFunction* safe_read_bytecode_called_function(uint8* Code)
    {
        if (!Code)
        {
            return nullptr;
        }

        __try
        {
            return *reinterpret_cast<UFunction**>(Code - sizeof(uint64));
        }
        __except (1 /*EXCEPTION_EXECUTE_HANDLER*/)
        {
            return nullptr;
        }
    }

    auto Internal::UnrealScriptFunctionHook(UObject* Context, FFrame& TheStack, void* RESULT_DECL) -> void
    {
        try
        {
            HookedUFunctionMap& FunctionMap = GetHookedFunctionsMap();

            // Primary identification: CurrentNativeFunction (set by Blueprint VM before native calls)
            UFunction* FunctionBeingExecuted = TheStack.CurrentNativeFunction();
            auto Iterator = FunctionBeingExecuted ? FunctionMap.find(FunctionBeingExecuted) : FunctionMap.end();
            UFunction* FrameNode = safe_read_frame_node(TheStack);

            // Delegate broadcasts and other native-only calls can arrive with an
            // unset CurrentNativeFunction and a non-null Code pointer that is
            // not a valid bytecode stream. Prefer FFrame::Node before probing
            // bytecode so delegate signatures do not AV while being identified.
            if (Iterator == FunctionMap.end())
            {
                if (FrameNode && FrameNode != FunctionBeingExecuted)
                {
                    auto FrameNodeIt = FunctionMap.find(FrameNode);
                    if (FrameNodeIt != FunctionMap.end())
                    {
                        FunctionBeingExecuted = FrameNode;
                        Iterator = FrameNodeIt;
                    }
                }
            }

            // Fallback: when Blueprint VM sets CurrentNativeFunction to the calling Ubergraph
            // rather than the actual callee, read the UFunction pointer from the bytecode stream.
            // The pointer sits immediately before the current Code position (after EX_*CallFunction opcode).
            if (Iterator == FunctionMap.end() && TheStack.Code())
            {
                auto* BytecodeFn = safe_read_bytecode_called_function(TheStack.Code());
                if (BytecodeFn && BytecodeFn != FunctionBeingExecuted)
                {
                    auto BytecodeIt = FunctionMap.find(BytecodeFn);
                    if (BytecodeIt != FunctionMap.end())
                    {
                        FunctionBeingExecuted = BytecodeFn;
                        Iterator = BytecodeIt;
                    }
                }
            }

            if (Iterator == FunctionMap.end())
            {
                if (FunctionBeingExecuted)
                {
                    Output::send<LogLevel::Warning>(STR("Tried to execute UFunction::FuncPtr hook but there was no function map entry for UFunction:\n\t{:016X} {}.\n\tExecuting original function instead.\n"),
                                                    std::bit_cast<uintptr_t>(FunctionBeingExecuted),
                                                    FunctionBeingExecuted->GetFullName());
                    auto FuncPtr = FunctionBeingExecuted->GetFuncPtr();
                    if (FuncPtr == &Internal::UnrealScriptFunctionHook)
                    {
                        Output::send<LogLevel::Warning>(STR("Tried to execute UFunction::FuncPtr hook for UFunction without hook data and FuncPtr points back to the trampoline:\n\t{:016X} {}.\n\tSkipping to avoid recursive crash.\n"),
                                                        std::bit_cast<uintptr_t>(FunctionBeingExecuted),
                                                        FunctionBeingExecuted->GetFullName());
                    }
                    else
                    {
                        FuncPtr(Context, TheStack, RESULT_DECL);
                    }
                }
                else
                {
                    Output::send<LogLevel::Warning>(STR("Tried to execute UFunction::FuncPtr hook but there was no function map entry and no function could be identified.\n\tExecuting ProcessInternal instead.\n"));
                    static auto Object_ExecuteUbergraph = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/CoreUObject.Object:ExecuteUbergraph"));
                    static auto ProcessInternal = Object_ExecuteUbergraph->GetFuncPtr();
                    ProcessInternal(Context, TheStack, RESULT_DECL);
                }
            }
            else
            {
                FFrame* NewStackPtr = &TheStack;
                std::map<FProperty*, FOutParmRec*> OriginalOutParmsMap; // Map to track original out params
                const bool FunctionIsDelegateSignature =
                    FunctionBeingExecuted &&
                    (FunctionBeingExecuted->HasAnyFunctionFlags(EFunctionFlags::FUNC_Delegate) ||
                     FunctionBeingExecuted->HasAnyFunctionFlags(EFunctionFlags::FUNC_MulticastDelegate));
                // Match upstream UE4SS parameter behavior for normal native UFunction calls:
                // when bytecode is present, StepCompiledIn must populate a temporary params frame.
                // Delegate signatures keep the skip because their Code pointer can be non-bytecode.
                const bool ShouldUnpackBytecodeParams =
                    TheStack.Code() &&
                    GNatives_Internal &&
                    !FunctionIsDelegateSignature;

                if (ShouldUnpackBytecodeParams)
                {
                    // Create a new stack frame
                    NewStackPtr = FFrame::MallocAndMemsetNewFrame();
                    NewStackPtr->Object() = Context;
                    NewStackPtr->Node() = FunctionBeingExecuted;
                    NewStackPtr->PreviousFrame() = &TheStack;
                    if (Version::IsBelow(4, 25))
                    {
                        NewStackPtr->PropertyChainForCompiledIn() = std::bit_cast<FField*>(FunctionBeingExecuted->GetChildren().Get());
                    }
                    else
                    {
                        NewStackPtr->PropertyChainForCompiledIn() = FunctionBeingExecuted->GetChildProperties();
                    }
                    auto NewLocals = static_cast<uint8*>(alloca(FunctionBeingExecuted->GetParmsSize()));
                    NewStackPtr->Locals() = NewLocals;

                    // Map out parameters for later use
                    FOutParmRec* CurrentOutParm = TheStack.OutParms();
                    while (CurrentOutParm)
                    {
                        OriginalOutParmsMap[CurrentOutParm->Property] = CurrentOutParm;
                        CurrentOutParm = CurrentOutParm->NextOutParm;
                    }

                    // Loop through parameters and evaluate them
                    for (FProperty* prop : TFieldRange<FProperty>(FunctionBeingExecuted, EFieldIterationFlags::IncludeDeprecated))
                    {
                        // Skip non parameters and return values
                        if (!prop->HasAnyPropertyFlags(CPF_Parm) || prop->HasAnyPropertyFlags(CPF_ReturnParm)) 
                            continue;

                        // Initialize property storage
                        uint8* PropertyData = prop->ContainerPtrToValuePtr<uint8>(NewStackPtr->Locals());
                        prop->InitializeValue(PropertyData);

                        if (prop->HasAnyPropertyFlags(CPF_OutParm))
                        {
                            // Create new out parameter slot and add it to the linked list
                            FOutParmRec* NewOutParam = static_cast<FOutParmRec*>(alloca(sizeof(Unreal::FOutParmRec)));
                            NewOutParam->Property = prop;
                            NewOutParam->NextOutParm = NewStackPtr->OutParms();
                            NewStackPtr->OutParms() = NewOutParam;

                            // Step the original stack to get the address for the out param
                            TheStack.StepCompiledIn(PropertyData, prop);
                            NewOutParam->PropAddr = TheStack.MostRecentPropertyAddress();
                        }
                        else
                        {
                            // Regular parameter - evaluate it from the original stack
                            TheStack.StepCompiledIn(PropertyData, prop);
                        }
                    }

                    // Code pointer needs to move past the parameters
                    TheStack.Code() += !!TheStack.Code();
                }

                // Create function context with our (possibly new) stack frame
                UnrealScriptFunctionCallableContext FuncContext(Context, *NewStackPtr, RESULT_DECL);
                
                Iterator->second.bIsMidExecution = true;

                try
                {
                    Iterator->second.FirePreCallbacks(FuncContext);
                }
                catch (std::exception& e)
                {
                    Output::send<LogLevel::Error>(STR("Error executing hook pre-callback {}: {}\n"), 
                                 FunctionBeingExecuted->GetPathName(), ensure_str(e.what()));
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(STR("Unknown exception in hook pre-callback {}\n"),
                                 FunctionBeingExecuted->GetPathName());
                }

                // Execute the original function with our stack
                Iterator->second.GetOriginalFuncPtr()(Context, *NewStackPtr, RESULT_DECL);
                
                try
                {
                    Iterator->second.FirePostCallbacks(FuncContext);
                }
                catch (std::exception& e)
                {
                    Output::send<LogLevel::Error>(STR("Error executing hook post-callback {}: {}\n"), 
                                 FunctionBeingExecuted->GetPathName(), ensure_str(e.what()));
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(STR("Unknown exception in hook post-callback {}\n"),
                                 FunctionBeingExecuted->GetPathName());
                }

                // Copy out parameter values back to the original frame
                if (ShouldUnpackBytecodeParams)
                {
                    // Copy out parameter values back to the original frame
                    for (FOutParmRec* OutParam = NewStackPtr->OutParms(); OutParam; OutParam = OutParam->NextOutParm)
                    {
                        auto OriginalIt = OriginalOutParmsMap.find(OutParam->Property);
                        if (OriginalIt != OriginalOutParmsMap.end())
                        {
                            // Copy the value back to the original out parameter
                            OutParam->Property->CopyCompleteValue(OriginalIt->second->PropAddr, OutParam->PropAddr);
                        }
                    }

                    // Clean up the temporary stack frame
                    for (FProperty* prop : TFieldRange<FProperty>(FunctionBeingExecuted, EFieldIterationFlags::IncludeDeprecated))
                    {
                        if (!prop->HasAnyPropertyFlags(CPF_Parm)) continue;
                        if (prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;

                        uint8* PropertyData = prop->ContainerPtrToValuePtr<uint8>(NewStackPtr->Locals());
                        prop->DestroyValue(PropertyData);
                    }
                    FMemory::Free(NewStackPtr);
                }
                    
                Iterator->second.bIsMidExecution = false;
            }
        }
        catch (std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("Error executing hooked function: {}\n"), ensure_str(e.what()));
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("Unknown exception in UnrealScriptFunctionHook\n"));
        }
    }
}
