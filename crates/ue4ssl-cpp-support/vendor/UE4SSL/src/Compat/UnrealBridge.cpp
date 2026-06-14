#include <Compat/UnrealBridge.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UnrealInitializer.hpp>

namespace RC::Compat::UnrealBridge
{
    auto setup_unreal_modules() -> void
    {
        Unreal::UnrealInitializer::SetupUnrealModules();
    }

    auto runtime_state() -> RuntimeState
    {
        return {
                .is_unreal_initialized = static_cast<uint8_t>(Unreal::UnrealInitializer::StaticStorage::bIsInitialized),
                .is_versioned_container_initialized = static_cast<uint8_t>(Unreal::UnrealInitializer::StaticStorage::bVersionedContainerIsInitialized),
                .is_pre_init_completed = static_cast<uint8_t>(Unreal::UnrealInitializer::StaticStorage::bPreInitCompleted),
                .is_scan_fully_completed = static_cast<uint8_t>(Unreal::UnrealInitializer::StaticStorage::bScanFullyCompleted),
        };
    }

    auto is_unreal_initialized() -> bool
    {
        return runtime_state().is_unreal_initialized != 0;
    }

    auto set_max_asset_loading_memory(int64_t max_memory_usage_during_asset_loading) -> void
    {
        Unreal::UAssetRegistry::SetMaxMemoryUsageDuringAssetLoading(max_memory_usage_during_asset_loading);
    }
} // namespace RC::Compat::UnrealBridge
