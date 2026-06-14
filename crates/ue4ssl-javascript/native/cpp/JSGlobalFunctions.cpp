#include "JSInternal.hpp"

#include <unordered_set>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UAssetRegistryHelpers.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UnrealVersion.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/CoreUObject/UObject/FAnsiStrProperty.hpp>
#include <Unreal/Core/Containers/FAnsiString.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Input/Handler.hpp>

namespace RC::JSScript
{
    // ============================================
    // SEH helpers for global functions
    // ============================================

    // safe_process_event is now inline in JSInternal.hpp, delegating to Seh::SafeProcessEvent

    bool safe_has_iterable_properties(Unreal::UFunction* func)
    {
        if (!func) return false;
        __try
        {
            volatile uint8_t n = func->GetNumParms();
            if (n == 0) return false;
            if (!func->HasChildren()) return false;
            return true;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"has_iterable_properties", GetExceptionCode(), GetExceptionInformation()))
        {
            return false;
        }
    }

    static constexpr const char* last_object_search_failure_property = "__UE4SSL_LastObjectSearchFailure__";

    static bool safe_find_first_of(const std::wstring& class_name, Unreal::UObject*& out)
    {
        out = nullptr;
        __try
        {
            out = Unreal::UObjectGlobals::FindFirstOf(class_name);
            return true;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"FindFirstOf", GetExceptionCode(), GetExceptionInformation()))
        {
            out = nullptr;
            return false;
        }
    }

    static bool safe_find_all_of(const std::wstring& class_name, std::vector<Unreal::UObject*>& out)
    {
        __try
        {
            Unreal::UObjectGlobals::FindAllOf(class_name, out);
            return true;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"FindAllOf", GetExceptionCode(), GetExceptionInformation()))
        {
            return false;
        }
    }

    static void set_last_object_search_failure(JSContext* ctx, const char* operation, const std::wstring& query, uint32_t seh_code)
    {
        if (!ctx)
        {
            return;
        }

        JSValue global = JS_GetGlobalObject(ctx);
        if (JS_IsException(global))
        {
            JS_FreeValue(ctx, global);
            return;
        }

        JSValue failure = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, failure, "operation", JS_NewString(ctx, operation ? operation : ""));
        JS_SetPropertyStr(ctx, failure, "query", JS_NewString(ctx, wide_to_utf8(query).c_str()));
        JS_SetPropertyStr(ctx, failure, "sehCode", JS_NewInt64(ctx, static_cast<int64_t>(seh_code)));
        JS_SetPropertyStr(ctx, global, last_object_search_failure_property, failure);
        JS_FreeValue(ctx, global);
    }

    static Unreal::UObject* safe_static_find_object(const std::wstring& path)
    {
        return Seh::SafeStaticFindObject(path);
    }

    static bool wide_starts_with(const std::wstring& value, const std::wstring& prefix)
    {
        return prefix.empty() || (value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0);
    }

    static bool wide_ends_with(const std::wstring& value, const std::wstring& suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    static void set_js_wstring_property(JSContext* ctx, JSValue object, const char* property_name, const std::wstring& value)
    {
        JS_SetPropertyStr(ctx, object, property_name, JS_NewString(ctx, wide_to_utf8(value).c_str()));
    }

    static bool split_object_path(const std::wstring& object_path, std::wstring& out_package_name, std::wstring& out_asset_name)
    {
        size_t separator_index = object_path.rfind(L'.');
        if (separator_index == std::wstring::npos || separator_index == 0 || separator_index + 1 >= object_path.size())
        {
            return false;
        }

        out_package_name = object_path.substr(0, separator_index);
        out_asset_name = object_path.substr(separator_index + 1);
        return true;
    }

    static std::wstring ensure_generated_asset_name(const std::wstring& asset_name)
    {
        return wide_ends_with(asset_name, L"_C") ? asset_name : asset_name + L"_C";
    }

    static std::wstring strip_generated_asset_name_suffix(const std::wstring& asset_name)
    {
        if (wide_ends_with(asset_name, L"_C"))
        {
            return asset_name.substr(0, asset_name.size() - 2);
        }
        return asset_name;
    }

    static std::wstring strip_generated_object_path_suffix(const std::wstring& object_path)
    {
        if (wide_ends_with(object_path, L"_C"))
        {
            return object_path.substr(0, object_path.size() - 2);
        }
        return object_path;
    }

    static std::wstring get_asset_object_path(Unreal::FAssetData& asset_data)
    {
        try
        {
            if (Unreal::Version::IsAtMost(5, 0))
            {
                return asset_data.ObjectPath().ToString();
            }
        }
        catch (...)
        {
        }

        try
        {
            std::wstring package_name = asset_data.PackageName().ToString();
            std::wstring asset_name = asset_data.AssetName().ToString();
            if (!package_name.empty() && !asset_name.empty())
            {
                return package_name + L"." + asset_name;
            }
        }
        catch (...)
        {
        }

        return {};
    }

    static std::wstring get_asset_class_name(Unreal::FAssetData& asset_data)
    {
        try
        {
            if (Unreal::Version::IsAtLeast(5, 1))
            {
                return asset_data.AssetClassPath().GetAssetName().ToString();
            }
            return asset_data.AssetClass().ToString();
        }
        catch (...)
        {
            return {};
        }
    }

    static Unreal::UObject* load_uobject_from_object_path(const std::wstring& object_path)
    {
        if (object_path.empty())
        {
            return nullptr;
        }

        if (auto* found_object = Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, object_path))
        {
            return found_object;
        }

        try
        {
            Unreal::FAssetData manual_data{};
            if (Unreal::Version::IsAtMost(5, 0))
            {
                manual_data.SetObjectPath(Unreal::FName(object_path, Unreal::FNAME_Add));
            }
            else
            {
                std::wstring package_name{};
                std::wstring asset_name{};
                if (!split_object_path(object_path, package_name, asset_name))
                {
                    return nullptr;
                }

                manual_data.SetPackageName(Unreal::FName(package_name, Unreal::FNAME_Add));
                manual_data.SetAssetName(Unreal::FName(asset_name, Unreal::FNAME_Add));
            }

            if (Unreal::UObject* loaded_object = Unreal::UAssetRegistryHelpers::GetAsset(manual_data))
            {
                return loaded_object;
            }
        }
        catch (...)
        {
        }

        if (wide_ends_with(object_path, L"_C"))
        {
            std::wstring asset_object_path = strip_generated_object_path_suffix(object_path);
            if (asset_object_path != object_path)
            {
                try
                {
                    Unreal::FAssetData manual_asset_data{};
                    if (Unreal::Version::IsAtMost(5, 0))
                    {
                        manual_asset_data.SetObjectPath(Unreal::FName(asset_object_path, Unreal::FNAME_Add));
                    }
                    else
                    {
                        std::wstring package_name{};
                        std::wstring asset_name{};
                        if (!split_object_path(asset_object_path, package_name, asset_name))
                        {
                            return nullptr;
                        }

                        manual_asset_data.SetPackageName(Unreal::FName(package_name, Unreal::FNAME_Add));
                        manual_asset_data.SetAssetName(Unreal::FName(asset_name, Unreal::FNAME_Add));
                    }

                    if (Unreal::UObject* loaded_asset = Unreal::UAssetRegistryHelpers::GetAsset(manual_asset_data))
                    {
                        return loaded_asset;
                    }
                }
                catch (...)
                {
                }
            }
        }

        return nullptr;
    }

    static Unreal::UClass* load_UClass_from_object_path(const std::wstring& object_path)
    {
        if (Unreal::UObject* loaded_object = load_uobject_from_object_path(object_path))
        {
            if (loaded_object->IsA<Unreal::UClass>())
            {
                return static_cast<Unreal::UClass*>(loaded_object);
            }
        }

        return nullptr;
    }

    static bool class_implements_interface(Unreal::UClass* object_class, Unreal::UClass* interface_class)
    {
        if (!object_class || !interface_class)
        {
            return false;
        }

        for (Unreal::UClass* current_class = object_class; current_class; current_class = current_class->GetSuperClass())
        {
            try
            {
                auto& interfaces = current_class->GetInterfaces();
                for (int32_t index = 0; index < interfaces.Num(); ++index)
                {
                    Unreal::UClass* implemented_interface = interfaces[index].Class;
                    if (!implemented_interface)
                    {
                        continue;
                    }

                    if (implemented_interface == interface_class || implemented_interface->IsChildOf(interface_class))
                    {
                        return true;
                    }
                }
            }
            catch (...)
            {
            }
        }

        return false;
    }

    static void append_asset_data(std::vector<Unreal::FAssetData>& out_assets, Unreal::TArray<Unreal::FAssetData>& source_assets)
    {
        for (int32_t index = 0; index < source_assets.Num(); ++index)
        {
            out_assets.emplace_back(source_assets[index]);
        }
    }

    static std::vector<Unreal::FAssetData> collect_candidate_widget_assets(Unreal::UAssetRegistry* asset_registry)
    {
        std::vector<Unreal::FAssetData> assets{};
        if (!asset_registry)
        {
            return assets;
        }

        const wchar_t* widget_class_names[] = {
            L"WidgetBlueprint",
            L"WidgetBlueprintGeneratedClass",
        };

        for (const wchar_t* widget_class_name : widget_class_names)
        {
            Unreal::TArray<Unreal::FAssetData> matched_assets{nullptr, 0, 0};
            if (asset_registry->GetAssetsByClass(Unreal::FName(widget_class_name, Unreal::FNAME_Add), matched_assets, true))
            {
                Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface: class '{}' yielded {} candidate asset(s)\n"),
                    widget_class_name,
                    matched_assets.Num());
                append_asset_data(assets, matched_assets);
            }
        }

        if (!assets.empty())
        {
            return assets;
        }

        Unreal::TArray<Unreal::FAssetData> all_assets{nullptr, 0, 0};
        if (!asset_registry->GetAllAssets(all_assets, false))
        {
            return assets;
        }

        for (int32_t index = 0; index < all_assets.Num(); ++index)
        {
            Unreal::FAssetData asset_data = all_assets[index];
            std::wstring asset_class_name = get_asset_class_name(asset_data);
            if (asset_class_name.find(L"WidgetBlueprint") != std::wstring::npos)
            {
                assets.emplace_back(asset_data);
            }
        }

        return assets;
    }

    static Unreal::UClass* resolve_widget_class_from_asset_data(
        Unreal::FAssetData& asset_data,
        const std::wstring& object_path,
        const std::wstring& package_name,
        const std::wstring& asset_name,
        std::wstring& out_class_path)
    {
        if (!object_path.empty() && wide_ends_with(object_path, L"_C"))
        {
            if (auto* found_class = Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, object_path))
            {
                out_class_path = object_path;
                return found_class;
            }
        }

        if (package_name.empty() || asset_name.empty())
        {
            return nullptr;
        }

        std::wstring generated_asset_name = ensure_generated_asset_name(asset_name);
        std::wstring generated_class_path = package_name + L"." + generated_asset_name;

        if (Unreal::UClass* found_generated_class = load_UClass_from_object_path(generated_class_path))
        {
            out_class_path = generated_class_path;
            return found_generated_class;
        }

        return nullptr;
    }

    static bool append_widget_asset_if_matches_impl(
        JSContext* ctx,
        Unreal::FAssetData* asset_data,
        Unreal::UClass* interface_class,
        Unreal::UClass* user_widget_class,
        const wchar_t* root_path_ptr,
        std::unordered_set<std::wstring>* seen_class_paths,
        JSValue result,
        uint32_t* result_index)
    {
        if (!asset_data || !interface_class || !seen_class_paths || !result_index)
        {
            return false;
        }

        const std::wstring root_path = (root_path_ptr && root_path_ptr[0]) ? root_path_ptr : L"/Game";

        std::wstring package_name = asset_data->PackageName().ToString();
        std::wstring asset_name = asset_data->AssetName().ToString();
        if (!wide_starts_with(package_name, root_path))
        {
            return false;
        }

        std::wstring object_path = get_asset_object_path(*asset_data);
        std::wstring asset_class_name = get_asset_class_name(*asset_data);
        std::wstring class_path{};
        Unreal::UClass* widget_class = resolve_widget_class_from_asset_data(
            *asset_data,
            object_path,
            package_name,
            asset_name,
            class_path);

        if (!widget_class)
        {
            return false;
        }

        if (user_widget_class && !widget_class->IsChildOf(user_widget_class))
        {
            return false;
        }

        if (widget_class->HasAnyClassFlags(Unreal::CLASS_Abstract))
        {
            return false;
        }

        if (!class_implements_interface(widget_class, interface_class))
        {
            return false;
        }

        if (class_path.empty())
        {
            class_path = package_name + L"." + ensure_generated_asset_name(asset_name);
        }

        if (!seen_class_paths->insert(class_path).second)
        {
            return false;
        }

        JSValue item = JS_NewObject(ctx);
        set_js_wstring_property(ctx, item, "classPath", class_path);
        set_js_wstring_property(ctx, item, "packageName", package_name);
        set_js_wstring_property(ctx, item, "assetName", asset_name);
        set_js_wstring_property(ctx, item, "objectPath", object_path);
        set_js_wstring_property(ctx, item, "assetClass", asset_class_name);
        JS_SetPropertyUint32(ctx, result, (*result_index)++, item);
        return true;
    }

    static int seh_append_widget_asset_if_matches_inner(
        JSContext* ctx,
        Unreal::FAssetData* asset_data,
        Unreal::UClass* interface_class,
        Unreal::UClass* user_widget_class,
        const wchar_t* root_path_ptr,
        std::unordered_set<std::wstring>* seen_class_paths,
        JSValue result,
        uint32_t* result_index)
    {
        __try
        {
            return append_widget_asset_if_matches_impl(
                ctx,
                asset_data,
                interface_class,
                user_widget_class,
                root_path_ptr,
                seen_class_paths,
                result,
                result_index) ? 1 : 2;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"ScanBlueprintWidgetsByInterface.Asset", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    static JSValue js_scan_blueprint_widgets_by_interface_impl(JSContext* ctx, const wchar_t* interface_path_ptr, const wchar_t* root_path_ptr)
    {
        std::wstring interface_path = interface_path_ptr ? interface_path_ptr : L"";
        std::wstring root_path = (root_path_ptr && root_path_ptr[0]) ? root_path_ptr : L"/Game";

        try
        {
            Unreal::UClass* interface_class = load_UClass_from_object_path(interface_path);
            if (!interface_class)
            {
                return JS_ThrowReferenceError(ctx, "Interface class not found");
            }

            Unreal::UClass* user_widget_class = load_UClass_from_object_path(STR("/Script/UMG.UserWidget"));
            auto* asset_registry = static_cast<Unreal::UAssetRegistry*>(Unreal::UAssetRegistryHelpers::GetAssetRegistry().ObjectPointer);
            if (!asset_registry)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface: AssetRegistry unavailable\n"));
                return JS_NewArray(ctx);
            }

            std::unordered_set<std::wstring> seen_class_paths{};
            JSValue result = JS_NewArray(ctx);
            uint32_t result_index = 0;
            uint32_t skipped_asset_count = 0;

            const wchar_t* widget_class_names[] = {
                L"WidgetBlueprint",
                L"WidgetBlueprintGeneratedClass",
            };

            for (const wchar_t* widget_class_name : widget_class_names)
            {
                Unreal::TArray<Unreal::FAssetData> matched_assets{nullptr, 0, 0};
                if (!asset_registry->GetAssetsByClass(Unreal::FName(widget_class_name, Unreal::FNAME_Add), matched_assets, true))
                {
                    continue;
                }

                Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface: class '{}' yielded {} candidate asset(s)\n"),
                    widget_class_name,
                    matched_assets.Num());

                for (int32_t asset_index = 0; asset_index < matched_assets.Num(); ++asset_index)
                {
                    int append_result = seh_append_widget_asset_if_matches_inner(
                        ctx,
                        &matched_assets[asset_index],
                        interface_class,
                        user_widget_class,
                        root_path.c_str(),
                        &seen_class_paths,
                        result,
                        &result_index);
                    if (append_result == 0)
                    {
                        ++skipped_asset_count;
                    }
                }
            }

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface '{}' found {} widget(s), skipped {} crashing asset(s)\n"),
                interface_path,
                result_index,
                skipped_asset_count);

            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "ScanBlueprintWidgetsByInterface failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface unknown exception\n"));
            return JS_ThrowInternalError(ctx, "ScanBlueprintWidgetsByInterface failed due to unknown exception");
        }
    }

    static int seh_js_scan_blueprint_widgets_by_interface_inner(
        JSContext* ctx,
        const wchar_t* interface_path_ptr,
        const wchar_t* root_path_ptr,
        JSValue* out_result)
    {
        __try
        {
            *out_result = js_scan_blueprint_widgets_by_interface_impl(ctx, interface_path_ptr, root_path_ptr);
            return 1;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"ScanBlueprintWidgetsByInterface", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    // ============================================
    // print()
    // ============================================

    JSValue js_print(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        std::wstring output;

        for (int i = 0; i < argc; i++)
        {
            if (i > 0) output += L" ";

            const char* str = JS_ToCString(ctx, argv[i]);
            if (str)
            {
                output += utf8_to_wide(std::string(str));
                JS_FreeCString(ctx, str);
            }
        }

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] {}\n"), output);
        return JS_UNDEFINED;
    }

    // ============================================
    // UObject lookup functions (with SEH)
    // ============================================

    JSValue js_find_first_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "FindFirstOf requires a class name argument");

        const char* class_name = JS_ToCString(ctx, argv[0]);
        if (!class_name)
            return JS_ThrowTypeError(ctx, "Invalid class name");

        std::wstring wide_name(class_name, class_name + strlen(class_name));
        JS_FreeCString(ctx, class_name);

        try
        {
            Unreal::UObject* found_obj = nullptr;
            if (!safe_find_first_of(wide_name, found_obj))
            {
                set_last_object_search_failure(ctx, "FindFirstOf", wide_name, Seh::GetLastSehCode());
                return JS_NULL;
            }
            if (!found_obj) return JS_NULL;
            return JSUObject::create(ctx, found_obj);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindFirstOf exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindFirstOf unknown exception\n"));
            return JS_NULL;
        }
    }

    JSValue js_find_all_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "FindAllOf requires a class name argument");

        const char* class_name = JS_ToCString(ctx, argv[0]);
        if (!class_name)
            return JS_ThrowTypeError(ctx, "Invalid class name");

        std::wstring wide_name(class_name, class_name + strlen(class_name));
        JS_FreeCString(ctx, class_name);

        try
        {
            std::vector<Unreal::UObject*> found_objects;
            if (!safe_find_all_of(wide_name, found_objects))
            {
                set_last_object_search_failure(ctx, "FindAllOf", wide_name, Seh::GetLastSehCode());
                return JS_NewArray(ctx);
            }

            JSValue result = JS_NewArray(ctx);
            for (size_t i = 0; i < found_objects.size(); i++)
            {
                JS_SetPropertyUint32(ctx, result, static_cast<uint32_t>(i),
                    JSUObject::create(ctx, found_objects[i]));
            }
            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindAllOf exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NewArray(ctx);
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindAllOf unknown exception\n"));
            return JS_NewArray(ctx);
        }
    }

    static int seh_actor_implements_interface(Unreal::UObject* object, Unreal::UClass* interface_class)
    {
        __try
        {
            if (!object || !interface_class)
            {
                return 2;
            }

            Unreal::UClass* object_class = object->GetClassPrivate();
            return class_implements_interface(object_class, interface_class) ? 1 : 2;
        }
        __except (Seh::FilterAndLog(L"JavaScript", L"FindAllActorsWithInterface.Actor", GetExceptionCode(), GetExceptionInformation()))
        {
            return 0;
        }
    }

    JSValue js_find_all_actors_with_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "FindAllActorsWithInterface requires an interface path argument");
        }

        const char* interface_path_utf8 = JS_ToCString(ctx, argv[0]);
        if (!interface_path_utf8)
        {
            return JS_ThrowTypeError(ctx, "Invalid interface path");
        }

        std::wstring interface_path = utf8_to_wide(std::string(interface_path_utf8));
        JS_FreeCString(ctx, interface_path_utf8);

        try
        {
            Unreal::UClass* interface_class = load_UClass_from_object_path(interface_path);
            if (!interface_class)
            {
                return JS_ThrowReferenceError(ctx, "Interface class not found");
            }

            std::vector<Unreal::UObject*> found_actors{};
            if (!safe_find_all_of(L"Actor", found_actors))
            {
                return JS_NewArray(ctx);
            }

            JSValue result = JS_NewArray(ctx);
            std::unordered_set<uintptr_t> seen_actor_addresses{};
            uint32_t result_index = 0;
            uint32_t skipped_actor_count = 0;

            for (Unreal::UObject* actor : found_actors)
            {
                if (!actor)
                {
                    continue;
                }

                uintptr_t actor_address = reinterpret_cast<uintptr_t>(actor);
                if (!seen_actor_addresses.insert(actor_address).second)
                {
                    continue;
                }

                int match_result = seh_actor_implements_interface(actor, interface_class);
                if (match_result == 1)
                {
                    JS_SetPropertyUint32(ctx, result, result_index++, JSUObject::create(ctx, actor));
                }
                else if (match_result == 0)
                {
                    ++skipped_actor_count;
                }
            }

            Output::send<LogLevel::Normal>(
                STR("[UE4SSL.JavaScript] FindAllActorsWithInterface '{}' found {} actor(s), skipped {} crashing actor(s)\n"),
                interface_path,
                result_index,
                skipped_actor_count);

            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindAllActorsWithInterface exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NewArray(ctx);
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] FindAllActorsWithInterface unknown exception\n"));
            return JS_NewArray(ctx);
        }
    }

    JSValue js_static_find_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "StaticFindObject requires an object path argument");

        const char* object_path = JS_ToCString(ctx, argv[0]);
        if (!object_path)
            return JS_ThrowTypeError(ctx, "Invalid object path");

        std::wstring wide_path(object_path, object_path + strlen(object_path));
        JS_FreeCString(ctx, object_path);

        try
        {
            Unreal::UObject* found_obj = safe_static_find_object(wide_path);
            if (!found_obj) return JS_NULL;
            return JSUObject::create(ctx, found_obj);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] StaticFindObject exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] StaticFindObject unknown exception\n"));
            return JS_NULL;
        }
    }

    JSValue js_load_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "LoadObject requires an object path argument");
        }

        const char* object_path_utf8 = JS_ToCString(ctx, argv[0]);
        if (!object_path_utf8)
        {
            return JS_ThrowTypeError(ctx, "Invalid object path");
        }

        std::wstring object_path = utf8_to_wide(std::string(object_path_utf8));
        JS_FreeCString(ctx, object_path_utf8);

        try
        {
            Unreal::UObject* found_object = load_uobject_from_object_path(object_path);
            if (!found_object)
            {
                return JS_NULL;
            }

            return JSUObject::create(ctx, found_object);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] LoadObject exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] LoadObject unknown exception\n"));
            return JS_NULL;
        }
    }

    JSValue js_scan_blueprint_widgets_by_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "ScanBlueprintWidgetsByInterface requires an interface path argument");
        }

        const char* interface_path_utf8 = JS_ToCString(ctx, argv[0]);
        if (!interface_path_utf8)
        {
            return JS_ThrowTypeError(ctx, "Invalid interface path");
        }

        std::wstring interface_path = utf8_to_wide(std::string(interface_path_utf8));
        JS_FreeCString(ctx, interface_path_utf8);

        std::wstring root_path = L"/Game";
        if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
        {
            const char* root_path_utf8 = JS_ToCString(ctx, argv[1]);
            if (!root_path_utf8)
            {
                return JS_ThrowTypeError(ctx, "Second argument must be a string when provided");
            }

            root_path = utf8_to_wide(std::string(root_path_utf8));
            JS_FreeCString(ctx, root_path_utf8);
        }

        JSValue result = JS_UNDEFINED;
        if (!seh_js_scan_blueprint_widgets_by_interface_inner(ctx, interface_path.c_str(), root_path.c_str(), &result))
        {
            Output::send<LogLevel::Warning>(
                STR("[UE4SSL.JavaScript] ScanBlueprintWidgetsByInterface hit SEH, returning empty result for '{}'\n"),
                interface_path);
            return JS_NewArray(ctx);
        }

        return result;
    }

    // ============================================
    // Hook registration JS functions
    // ============================================

    static bool read_hook_sync_option(JSContext* ctx, int argc, JSValueConst* argv, int option_index)
    {
        if (argc <= option_index || !JS_IsObject(argv[option_index]))
        {
            return false;
        }

        JSValue sync_value = JS_GetPropertyStr(ctx, argv[option_index], "sync");
        bool sync = JS_IsBool(sync_value) && JS_ToBool(ctx, sync_value) != 0;
        JS_FreeValue(ctx, sync_value);
        return sync;
    }

    JSValue js_register_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "RegisterHook requires at least 2 arguments: function_name, pre_callback [, post_callback]");

        const char* func_name = JS_ToCString(ctx, argv[0]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "Invalid function name");

        JSValue pre_callback = JS_UNDEFINED;
        JSValue post_callback = JS_UNDEFINED;

        if (JS_IsFunction(ctx, argv[1]))
            pre_callback = argv[1];
        else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
        {
            JS_FreeCString(ctx, func_name);
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");
        }

        if (argc >= 3)
        {
            if (JS_IsFunction(ctx, argv[2]))
                post_callback = argv[2];
            else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
            {
                JS_FreeCString(ctx, func_name);
                return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
            }
        }

        std::wstring wide_func_name(func_name, func_name + strlen(func_name));
        JS_FreeCString(ctx, func_name);
        bool force_sync = read_hook_sync_option(ctx, argc, argv, 3);

        try
        {
            Unreal::UFunction* unreal_function = Unreal::UObjectGlobals::StaticFindObject<Unreal::UFunction*>(
                nullptr, nullptr, wide_func_name);

            if (!unreal_function)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] RegisterHook: UFunction not found: {}\n"), wide_func_name);
                return JS_ThrowReferenceError(ctx, "UFunction not found");
            }

            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
            if (mod->is_safe_mode())
                return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
            if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
                return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");

            auto [pre_id, post_id] = mod->register_ufunction_hook(ctx, unreal_function, pre_callback, post_callback, force_sync);

            JSValue result = JS_NewArray(ctx);
            JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
            JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] RegisterHook exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "RegisterHook failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] RegisterHook unknown exception\n"));
            return JS_ThrowInternalError(ctx, "RegisterHook failed due to unknown exception");
        }
    }

    JSValue js_register_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "RegisterBindHook requires at least 2 arguments: function_name, pre_callback [, post_callback]");

        const char* func_name = JS_ToCString(ctx, argv[0]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "Invalid function name");

        JSValue pre_callback = JS_UNDEFINED;
        JSValue post_callback = JS_UNDEFINED;

        if (JS_IsFunction(ctx, argv[1]))
            pre_callback = argv[1];
        else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
        {
            JS_FreeCString(ctx, func_name);
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");
        }

        if (argc >= 3)
        {
            if (JS_IsFunction(ctx, argv[2]))
                post_callback = argv[2];
            else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
            {
                JS_FreeCString(ctx, func_name);
                return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
            }
        }

        std::wstring wide_func_name(func_name, func_name + strlen(func_name));
        JS_FreeCString(ctx, func_name);

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
            if (mod->is_safe_mode())
                return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
            if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
                return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");

            auto [bind_id_a, bind_id_b] = mod->register_bind_hook(ctx, wide_func_name, pre_callback, post_callback);

            JSValue result = JS_NewArray(ctx);
            JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, bind_id_a));
            JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, bind_id_b));
            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] RegisterBindHook exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "RegisterBindHook failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] RegisterBindHook unknown exception\n"));
            return JS_ThrowInternalError(ctx, "RegisterBindHook failed due to unknown exception");
        }
    }

    static bool js_option_string(JSContext* ctx, JSValueConst options, const char* key, std::wstring& out)
    {
        JSValue value = JS_GetPropertyStr(ctx, options, key);
        if (JS_IsUndefined(value) || JS_IsNull(value))
        {
            JS_FreeValue(ctx, value);
            return false;
        }

        const char* cstr = JS_ToCString(ctx, value);
        if (!cstr)
        {
            JS_FreeValue(ctx, value);
            return false;
        }

        out = utf8_to_wide(std::string(cstr));
        JS_FreeCString(ctx, cstr);
        JS_FreeValue(ctx, value);
        return true;
    }

    static bool js_option_int32(JSContext* ctx, JSValueConst options, const char* key, int32_t& out)
    {
        JSValue value = JS_GetPropertyStr(ctx, options, key);
        if (JS_IsUndefined(value) || JS_IsNull(value))
        {
            JS_FreeValue(ctx, value);
            return false;
        }

        bool ok = JS_ToInt32(ctx, &out, value) == 0;
        JS_FreeValue(ctx, value);
        return ok;
    }

    static auto native_method_arg_from_js(JSContext* ctx, JSValueConst value) -> JSMod::NativeMethodArg
    {
        JSMod::NativeMethodArg arg{};

        if (JS_IsNull(value) || JS_IsUndefined(value))
        {
            arg.type = JSMod::NativeMethodArg::Type::Null;
            return arg;
        }

        if (JS_IsBool(value))
        {
            arg.type = JSMod::NativeMethodArg::Type::Bool;
            arg.bool_value = JS_ToBool(ctx, value) != 0;
            return arg;
        }

        if (JS_IsNumber(value))
        {
            arg.type = JSMod::NativeMethodArg::Type::Float;
            double number = 0.0;
            JS_ToFloat64(ctx, &number, value);
            arg.float_value = number;
            arg.int_value = static_cast<int64_t>(number);
            return arg;
        }

        if (JS_IsString(value))
        {
            arg.type = JSMod::NativeMethodArg::Type::String;
            const char* cstr = JS_ToCString(ctx, value);
            if (cstr)
            {
                arg.string_value = utf8_to_wide(std::string(cstr));
                JS_FreeCString(ctx, cstr);
            }
            return arg;
        }

        return arg;
    }

    static void parse_native_method_args(JSContext* ctx, JSValueConst options, JSMod::NativeObjectMethodHookData& hook_data)
    {
        JSValue args = JS_GetPropertyStr(ctx, options, "args");
        if (!JS_IsObject(args))
        {
            JS_FreeValue(ctx, args);
            return;
        }

        JSValue length_value = JS_GetPropertyStr(ctx, args, "length");
        uint32_t length = 0;
        JS_ToUint32(ctx, &length, length_value);
        JS_FreeValue(ctx, length_value);

        constexpr uint32_t max_args = 16;
        if (length > max_args)
        {
            length = max_args;
        }

        for (uint32_t i = 0; i < length; i++)
        {
            JSValue item = JS_GetPropertyUint32(ctx, args, i);
            hook_data.args.push_back(native_method_arg_from_js(ctx, item));
            JS_FreeValue(ctx, item);
        }

        JS_FreeValue(ctx, args);
    }

    JSValue js_register_native_object_method_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
        {
            return JS_ThrowTypeError(ctx, "RegisterNativeObjectMethodHook requires 2 arguments: functionPath, options");
        }

        const char* function_path = JS_ToCString(ctx, argv[0]);
        if (!function_path)
        {
            return JS_ThrowTypeError(ctx, "First argument must be a UFunction path string");
        }

        if (!JS_IsObject(argv[1]))
        {
            JS_FreeCString(ctx, function_path);
            return JS_ThrowTypeError(ctx, "Second argument must be an options object");
        }

        std::wstring wide_function_path = utf8_to_wide(std::string(function_path));
        JS_FreeCString(ctx, function_path);

        std::wstring method_name{};
        bool has_method = js_option_string(ctx, argv[1], "method", method_name) && !method_name.empty();
        JSValue diagnose_value = JS_GetPropertyStr(ctx, argv[1], "diagnose");
        bool diagnose = JS_IsBool(diagnose_value) && JS_ToBool(ctx, diagnose_value) != 0;
        JS_FreeValue(ctx, diagnose_value);
        if (!has_method && !diagnose)
        {
            return JS_ThrowTypeError(ctx, "options.method must be a method name string unless options.diagnose is true");
        }

        Unreal::UFunction* hook_function = nullptr;
        try
        {
            hook_function = Unreal::UObjectGlobals::StaticFindObject<Unreal::UFunction*>(
                nullptr, nullptr, wide_function_path);
        }
        catch (...)
        {
            hook_function = nullptr;
        }

        if (!hook_function)
        {
            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] RegisterNativeObjectMethodHook: UFunction not found: {}\n"), wide_function_path);
            return JS_ThrowReferenceError(ctx, "UFunction not found");
        }

        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        if (mod->is_safe_mode())
            return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
            return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");

        auto hook_data = std::make_unique<JSMod::NativeObjectMethodHookData>();
        hook_data->hook_function = hook_function;
        hook_data->method_name = method_name;
        hook_data->diagnose_params = diagnose;
        (void)js_option_string(ctx, argv[1], "label", hook_data->diagnostic_label);

        std::wstring phase{};
        if (js_option_string(ctx, argv[1], "phase", phase))
        {
            hook_data->run_pre = phase == L"pre" || phase == L"both";
            hook_data->run_post = phase == L"post" || phase == L"both";
        }
        else
        {
            hook_data->run_pre = false;
            hook_data->run_post = true;
        }

        if (!hook_data->run_pre && !hook_data->run_post)
        {
            hook_data->run_post = true;
        }

        std::wstring target{};
        if (js_option_string(ctx, argv[1], "target", target))
        {
            hook_data->target_context = target != L"param";
        }

        int32_t param_index = 0;
        if (js_option_int32(ctx, argv[1], "paramIndex", param_index) || js_option_int32(ctx, argv[1], "targetParamIndex", param_index))
        {
            hook_data->target_param_index = param_index;
        }

        (void)js_option_string(ctx, argv[1], "fullNameContains", hook_data->target_filter);
        parse_native_method_args(ctx, argv[1], *hook_data);

        auto [pre_id, post_id] = mod->register_native_object_method_hook(std::move(hook_data));

        JSValue result = JS_NewArray(ctx);
        JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
        JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
        return result;
    }

    JSValue js_register_pe_watch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "RegisterProcessEventWatch requires 2 arguments: function_name, callback");

        const char* func_name = JS_ToCString(ctx, argv[0]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "First argument must be a string");

        if (!JS_IsFunction(ctx, argv[1]))
        {
            JS_FreeCString(ctx, func_name);
            return JS_ThrowTypeError(ctx, "Second argument must be a function");
        }

        auto* mod = get_js_mod(ctx);
        if (!mod)
        {
            JS_FreeCString(ctx, func_name);
            return JS_ThrowInternalError(ctx, "No mod context");
        }

        std::wstring wide_name(func_name, func_name + strlen(func_name));
        JS_FreeCString(ctx, func_name);

        int idx = mod->register_process_event_watch(ctx, wide_name, argv[1]);
        return JS_NewInt32(ctx, idx);
    }

    static JSValue js_register_load_map_hook_common(JSContext* ctx, int argc, JSValueConst* argv, bool is_pre)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(
                ctx,
                is_pre
                    ? "RegisterLoadMapPreHook requires 1 argument: callback"
                    : "RegisterLoadMapPostHook requires 1 argument: callback");
        }

        if (!JS_IsFunction(ctx, argv[0]))
        {
            return JS_ThrowTypeError(ctx, "First argument must be a callback function");
        }

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
            if (mod->is_safe_mode())
                return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
            if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
                return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");

            int32_t callback_id = mod->register_load_map_hook(ctx, is_pre, argv[0]);
            if (callback_id <= 0)
            {
                return JS_ThrowInternalError(ctx, "LoadMap callback registration failed");
            }

            return JS_NewInt32(ctx, callback_id);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(
                STR("[UE4SSL.JavaScript] RegisterLoadMap{}Hook exception: {}\n"),
                is_pre ? STR("Pre") : STR("Post"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "LoadMap callback registration failed due to exception");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "LoadMap callback registration failed due to unknown exception");
        }
    }

    JSValue js_register_load_map_pre_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        return js_register_load_map_hook_common(ctx, argc, argv, true);
    }

    JSValue js_register_load_map_post_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        return js_register_load_map_hook_common(ctx, argc, argv, false);
    }

    JSValue js_hook_ufunction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "HookUFunction requires at least 2 arguments: ufunction, pre_callback [, post_callback]");

        Unreal::UFunction* function = nullptr;
        void* obj = JSUObject::get_uobject(ctx, argv[0]);
        if (obj)
            function = static_cast<Unreal::UFunction*>(static_cast<Unreal::UObject*>(obj));
        else
            return JS_ThrowTypeError(ctx, "First argument must be a UFunction object");

        if (!function)
            return JS_ThrowTypeError(ctx, "Invalid UFunction object");

        JSValue pre_callback = JS_UNDEFINED;
        JSValue post_callback = JS_UNDEFINED;

        if (JS_IsFunction(ctx, argv[1]))
            pre_callback = argv[1];
        else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");

        if (argc >= 3)
        {
            if (JS_IsFunction(ctx, argv[2]))
                post_callback = argv[2];
            else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
                return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
        }
        bool force_sync = read_hook_sync_option(ctx, argc, argv, 3);

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
            if (mod->is_safe_mode())
                return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
            if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Hook))
                return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");

            auto [pre_id, post_id] = mod->register_ufunction_hook(ctx, function, pre_callback, post_callback, force_sync);

            JSValue result = JS_NewArray(ctx);
            JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
            JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
            return result;
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] HookUFunction exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "HookUFunction failed due to exception");
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] HookUFunction unknown exception\n"));
            return JS_ThrowInternalError(ctx, "HookUFunction failed due to unknown exception");
        }
    }

    JSValue js_unregister_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "UnregisterHook requires 2 arguments: pre_id, post_id");

        int32_t pre_id = 0, post_id = 0;
        if (JS_ToInt32(ctx, &pre_id, argv[0]) != 0)
            return JS_ThrowTypeError(ctx, "First argument must be a number (pre_id)");
        if (JS_ToInt32(ctx, &post_id, argv[1]) != 0)
            return JS_ThrowTypeError(ctx, "Second argument must be a number (post_id)");

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

            bool success = mod->unregister_ufunction_hook(pre_id, post_id);
            return JS_NewBool(ctx, success);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] UnregisterHook exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "UnregisterHook failed due to exception");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "UnregisterHook failed due to unknown exception");
        }
    }

    JSValue js_unregister_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "UnregisterBindHook requires 2 arguments: bind_id, bind_id");

        int32_t first_id = 0, second_id = 0;
        if (JS_ToInt32(ctx, &first_id, argv[0]) != 0)
            return JS_ThrowTypeError(ctx, "First argument must be a number (bind_id)");
        if (JS_ToInt32(ctx, &second_id, argv[1]) != 0)
            return JS_ThrowTypeError(ctx, "Second argument must be a number (bind_id)");

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

            bool success = mod->unregister_bind_hook(first_id, second_id);
            return JS_NewBool(ctx, success);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] UnregisterBindHook exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "UnregisterBindHook failed due to exception");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "UnregisterBindHook failed due to unknown exception");
        }
    }

    JSValue js_unregister_load_map_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
        {
            return JS_ThrowTypeError(ctx, "UnregisterLoadMapHook requires 1 argument: callbackId");
        }

        int32_t callback_id = 0;
        if (JS_ToInt32(ctx, &callback_id, argv[0]) != 0)
        {
            return JS_ThrowTypeError(ctx, "First argument must be a number (callbackId)");
        }

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

            bool success = mod->unregister_load_map_hook(callback_id);
            return JS_NewBool(ctx, success);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] UnregisterLoadMapHook exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "UnregisterLoadMapHook failed due to exception");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "UnregisterLoadMapHook failed due to unknown exception");
        }
    }

    JSValue js_notify_on_new_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "NotifyOnNewObject requires 2 arguments: class_name, callback");

        const char* class_name = JS_ToCString(ctx, argv[0]);
        if (!class_name)
            return JS_ThrowTypeError(ctx, "Invalid class name");

        if (!JS_IsFunction(ctx, argv[1]))
        {
            JS_FreeCString(ctx, class_name);
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function");
        }

        std::wstring wide_class_name(class_name, class_name + strlen(class_name));
        JS_FreeCString(ctx, class_name);

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] NotifyOnNewObject: Registered for class {}\n"), wide_class_name);
        return JS_UNDEFINED;
    }

    // ============================================
    // Key Binding JS function
    // ============================================

    static Input::Key string_to_key(const char* key_name)
    {
        if (strlen(key_name) == 1 && key_name[0] >= 'A' && key_name[0] <= 'Z')
            return static_cast<Input::Key>(key_name[0]);
        if (strlen(key_name) == 1 && key_name[0] >= 'a' && key_name[0] <= 'z')
            return static_cast<Input::Key>(key_name[0] - 32);
        if (strlen(key_name) == 1 && key_name[0] >= '0' && key_name[0] <= '9')
            return static_cast<Input::Key>(key_name[0]);

        if (key_name[0] == 'F' || key_name[0] == 'f')
        {
            int num = atoi(key_name + 1);
            if (num >= 1 && num <= 12)
                return static_cast<Input::Key>(Input::Key::F1 + num - 1);
        }

        if (_stricmp(key_name, "ESCAPE") == 0 || _stricmp(key_name, "ESC") == 0) return Input::Key::ESCAPE;
        if (_stricmp(key_name, "SPACE") == 0) return Input::Key::SPACE;
        if (_stricmp(key_name, "ENTER") == 0 || _stricmp(key_name, "RETURN") == 0) return Input::Key::RETURN;
        if (_stricmp(key_name, "TAB") == 0) return Input::Key::TAB;
        if (_stricmp(key_name, "BACKSPACE") == 0) return Input::Key::BACKSPACE;
        if (_stricmp(key_name, "DELETE") == 0 || _stricmp(key_name, "DEL") == 0) return Input::Key::DEL;
        if (_stricmp(key_name, "INSERT") == 0 || _stricmp(key_name, "INS") == 0) return Input::Key::INS;
        if (_stricmp(key_name, "HOME") == 0) return Input::Key::HOME;
        if (_stricmp(key_name, "END") == 0) return Input::Key::END;
        if (_stricmp(key_name, "PAGEUP") == 0) return Input::Key::PAGE_UP;
        if (_stricmp(key_name, "PAGEDOWN") == 0) return Input::Key::PAGE_DOWN;
        if (_stricmp(key_name, "UP") == 0) return Input::Key::UP_ARROW;
        if (_stricmp(key_name, "DOWN") == 0) return Input::Key::DOWN_ARROW;
        if (_stricmp(key_name, "LEFT") == 0) return Input::Key::LEFT_ARROW;
        if (_stricmp(key_name, "RIGHT") == 0) return Input::Key::RIGHT_ARROW;
        if (_stricmp(key_name, "NUMLOCK") == 0) return Input::Key::NUM_LOCK;
        if (_stricmp(key_name, "CAPSLOCK") == 0) return Input::Key::CAPS_LOCK;
        if (_stricmp(key_name, "SCROLLLOCK") == 0) return Input::Key::SCROLL_LOCK;
        if (_stricmp(key_name, "PAUSE") == 0) return Input::Key::PAUSE;
        if (_stricmp(key_name, "PRINTSCREEN") == 0) return Input::Key::PRINT_SCREEN;

        if (_strnicmp(key_name, "NUM", 3) == 0 && strlen(key_name) == 4)
        {
            char c = key_name[3];
            if (c >= '0' && c <= '9')
                return static_cast<Input::Key>(Input::Key::NUM_ZERO + (c - '0'));
        }

        return static_cast<Input::Key>(0);
    }

    JSValue js_register_key_bind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "RegisterKeyBind requires at least 2 arguments: key, callback [, modifiers]");

        const char* key_name = JS_ToCString(ctx, argv[0]);
        if (!key_name)
            return JS_ThrowTypeError(ctx, "First argument must be a key name string");

        Input::Key key = string_to_key(key_name);
        JS_FreeCString(ctx, key_name);

        if (key == 0)
            return JS_ThrowTypeError(ctx, "Invalid key name");

        if (!JS_IsFunction(ctx, argv[1]))
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function");

        bool with_ctrl = false, with_shift = false, with_alt = false;
        if (argc >= 3 && JS_IsObject(argv[2]))
        {
            JSValue ctrl_val = JS_GetPropertyStr(ctx, argv[2], "ctrl");
            JSValue shift_val = JS_GetPropertyStr(ctx, argv[2], "shift");
            JSValue alt_val = JS_GetPropertyStr(ctx, argv[2], "alt");

            if (JS_IsBool(ctrl_val)) with_ctrl = JS_ToBool(ctx, ctrl_val);
            if (JS_IsBool(shift_val)) with_shift = JS_ToBool(ctx, shift_val);
            if (JS_IsBool(alt_val)) with_alt = JS_ToBool(ctx, alt_val);

            JS_FreeValue(ctx, ctrl_val);
            JS_FreeValue(ctx, shift_val);
            JS_FreeValue(ctx, alt_val);
        }

        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

            bool success = mod->register_key_bind(ctx, static_cast<uint8_t>(key), argv[1], with_ctrl, with_shift, with_alt);
            return JS_NewBool(ctx, success);
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] RegisterKeyBind exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "RegisterKeyBind failed due to exception");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "RegisterKeyBind failed due to unknown exception");
        }
    }

    // ============================================
    // CallFunction (with SEH on param fill)
    // ============================================

    // SEH probe: verify the UFunction is still readable before iterating its properties.
    // Separated to satisfy MSVC C2712 (no C++ objects in __try scope).
    static int seh_probe_ufunction(Unreal::UFunction* func)
    {
        __try
        {
            volatile auto psize = func->GetParmsSize();
            (void)psize;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool fill_call_params(JSContext* ctx, Unreal::UFunction* function, void* params_memory,
        int argc, JSValueConst* argv, int js_arg_start, std::vector<wchar_t*>& raw_string_buffers)
    {
        if (!seh_probe_ufunction(function))
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] CallFunction: SEH during parameter fill (stale UFunction)\n"));
            return false;
        }

        int js_arg_index = js_arg_start;
        for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
        {
            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm)) continue;
            if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm)) continue;
            if (js_arg_index >= argc) break;

            void* prop_addr = prop->ContainerPtrToValuePtr<void>(params_memory);

            if (prop->IsA<Unreal::FStrProperty>())
            {
                const char* str_val = JS_ToCString(ctx, argv[js_arg_index]);
                if (str_val)
                {
                    std::wstring wide_str = utf8_to_wide(std::string(str_val));
                    JS_FreeCString(ctx, str_val);
                    int32_t num = static_cast<int32_t>(wide_str.length()) + 1;
                    wchar_t* buffer = static_cast<wchar_t*>(malloc(num * sizeof(wchar_t)));
                    if (buffer)
                    {
                        std::memcpy(buffer, wide_str.c_str(), wide_str.length() * sizeof(wchar_t));
                        buffer[wide_str.length()] = L'\0';
                        raw_string_buffers.push_back(buffer);
                        struct RawFString { wchar_t* Data; int32_t Num; int32_t Max; };
                        auto* raw_fstr = static_cast<RawFString*>(prop_addr);
                        raw_fstr->Data = buffer;
                        raw_fstr->Num = num;
                        raw_fstr->Max = num;
                    }
                }
            }
            else if (prop->IsA<Unreal::FAnsiStrProperty>())
            {
                const char* str_val = JS_ToCString(ctx, argv[js_arg_index]);
                if (str_val)
                {
                    size_t len = strlen(str_val) + 1;
                    char* buffer = static_cast<char*>(malloc(len));
                    if (buffer)
                    {
                        std::memcpy(buffer, str_val, len);
                        raw_string_buffers.push_back(reinterpret_cast<wchar_t*>(buffer));
                        struct RawFAnsiString { char* Data; int32_t Num; int32_t Max; };
                        auto* raw_astr = static_cast<RawFAnsiString*>(prop_addr);
                        raw_astr->Data = buffer;
                        raw_astr->Num = static_cast<int32_t>(len);
                        raw_astr->Max = static_cast<int32_t>(len);
                    }
                    JS_FreeCString(ctx, str_val);
                }
            }
            else if (prop->IsA<Unreal::FTextProperty>())
            {
                const char* str_val = JS_ToCString(ctx, argv[js_arg_index]);
                if (str_val)
                {
                    std::wstring wide = utf8_to_wide(std::string(str_val));
                    JS_FreeCString(ctx, str_val);
                    new (prop_addr) Unreal::FText(wide.c_str());
                }
            }
            else if (prop->IsA<Unreal::FNameProperty>())
            {
                const char* str_val = JS_ToCString(ctx, argv[js_arg_index]);
                if (str_val)
                {
                    std::wstring wide = utf8_to_wide(std::string(str_val));
                    JS_FreeCString(ctx, str_val);
                    new (prop_addr) Unreal::FName(wide);
                }
            }
            else if (prop->IsA<Unreal::FBoolProperty>())
            {
                static_cast<Unreal::FBoolProperty*>(prop)->SetPropertyValue(prop_addr, JS_ToBool(ctx, argv[js_arg_index]));
            }
            else if (prop->IsA<Unreal::FNumericProperty>())
            {
                auto* num_prop = static_cast<Unreal::FNumericProperty*>(prop);
                if (num_prop->IsFloatingPoint())
                {
                    double val; JS_ToFloat64(ctx, &val, argv[js_arg_index]);
                    num_prop->SetFloatingPointPropertyValue(prop_addr, val);
                }
                else if (num_prop->IsInteger())
                {
                    int64_t val; JS_ToInt64(ctx, &val, argv[js_arg_index]);
                    num_prop->SetIntPropertyValue(prop_addr, val);
                }
            }
            else if (prop->IsA<Unreal::FObjectProperty>())
            {
                void* arg_obj = JSUObject::get_uobject(ctx, argv[js_arg_index]);
                if (arg_obj)
                    *static_cast<Unreal::UObject**>(prop_addr) = static_cast<Unreal::UObject*>(arg_obj);
            }
            else if (prop->IsA<Unreal::FStructProperty>())
            {
                if (JS_IsObject(argv[js_arg_index]))
                    jsvalue_to_property(ctx, prop, prop_addr, argv[js_arg_index]);
            }
            else if (prop->IsA<Unreal::FEnumProperty>())
            {
                auto* enum_prop = static_cast<Unreal::FEnumProperty*>(prop);
                auto* underlying = enum_prop->GetUnderlyingProperty();
                if (underlying)
                {
                    int64_t val = 0;
                    JS_ToInt64(ctx, &val, argv[js_arg_index]);
                    underlying->SetIntPropertyValue(prop_addr, static_cast<uint64_t>(val));
                }
            }
            else if (prop->IsA<Unreal::FByteProperty>())
            {
                int64_t val = 0;
                JS_ToInt64(ctx, &val, argv[js_arg_index]);
                *static_cast<uint8_t*>(prop_addr) = static_cast<uint8_t>(val);
            }

            js_arg_index++;
        }
        return true;
    }

    static void free_raw_string_buffers(std::vector<wchar_t*>& raw_string_buffers)
    {
        for (auto* buffer : raw_string_buffers)
        {
            free(buffer);
        }
        raw_string_buffers.clear();
    }

    static JSValue collect_function_outputs(JSContext* ctx, Unreal::UFunction* function, void* params_memory)
    {
        JSValue outputs = JS_NewObject(ctx);
        bool has_output = false;

        if (!function || !params_memory)
        {
            JS_SetPropertyStr(ctx, outputs, "__success", JS_NewBool(ctx, true));
            return outputs;
        }

        for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
        {
            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
            {
                continue;
            }

            if (!prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_OutParm | Unreal::EPropertyFlags::CPF_ReturnParm))
            {
                continue;
            }

            void* prop_addr = prop->ContainerPtrToValuePtr<void>(params_memory);
            std::string prop_name = wide_to_utf8(prop->GetName());
            JS_SetPropertyStr(ctx, outputs, prop_name.c_str(), property_to_jsvalue(ctx, prop, prop_addr));
            has_output = true;
        }

        JS_SetPropertyStr(ctx, outputs, "__success", JS_NewBool(ctx, true));
        JS_SetPropertyStr(ctx, outputs, "__hasOutputs", JS_NewBool(ctx, has_output));
        return outputs;
    }

    JSValue js_call_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "CallFunction requires at least 2 arguments: object, functionName [, ...args]");

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* func_name = JS_ToCString(ctx, argv[1]);
        if (!func_name)
            return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
        std::wstring wide_func_name(func_name, func_name + strlen(func_name));
        JS_FreeCString(ctx, func_name);

        void* params_memory = nullptr;
        std::vector<wchar_t*> raw_string_buffers;
        try
        {
            Unreal::UFunction* function = object->GetFunctionByNameInChain(wide_func_name.c_str());
            if (!function)
                return JS_ThrowReferenceError(ctx, "Function not found on object");

            int32_t params_size = function->GetParmsSize();
            if (params_size > 0)
            {
                params_memory = calloc(1, params_size);
                if (!params_memory)
                    return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
            }

            if (!fill_call_params(ctx, function, params_memory, argc, argv, 2, raw_string_buffers))
            {
                free_raw_string_buffers(raw_string_buffers);
                if (params_memory) free(params_memory);
                return JS_ThrowInternalError(ctx, "CallFunction: parameter fill crashed (SEH)");
            }

            bool has_net_flags = function->HasAnyFunctionFlags(Unreal::EFunctionFlags::FUNC_Net);

            if (has_net_flags)
            {
                JSMod* mod = get_js_mod(ctx);
                if (mod)
                {
                    if (!mod->m_game_thread_callback_registered)
                        mod->setup_game_thread_dispatcher();

                    if (mod->m_game_thread_callback_registered)
                    {
                        JSMod::PendingGameThreadCall pending;
                        pending.object = object;
                        pending.function = function;
                        pending.params_memory = params_memory;
                        pending.raw_string_buffers = std::move(raw_string_buffers);

                        size_t queued_depth = 0;
                        {
                            std::lock_guard<std::mutex> lock(mod->m_pending_game_thread_mutex);
                            mod->m_pending_game_thread_calls.push_back(std::move(pending));
                            queued_depth = mod->m_pending_game_thread_calls.size();
                        }
                        if (queued_depth > 8)
                        {
                            Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] Game thread RPC queue depth is high: {}\n"),
                                queued_depth);
                        }
                        return JS_TRUE;
                    }
                }
            }

            bool process_event_ok = safe_process_event(object, function, params_memory);

            if (!process_event_ok)
            {
                Output::send<LogLevel::Error>(
                    STR("[UE4SSL.JavaScript] CallFunction ProcessEvent crashed: object={} function={}\n"),
                    object->GetFullName(),
                    function->GetFullName());
                free_raw_string_buffers(raw_string_buffers);
                if (params_memory) free(params_memory);
                return JS_ThrowInternalError(ctx, "ProcessEvent crashed (SEH exception caught)");
            }

            JSValue ret_val = JS_TRUE;
            if (params_memory)
            {
                for (Unreal::FProperty* ret_prop : Unreal::TFieldRange<Unreal::FProperty>(function, Unreal::EFieldIterationFlags::IncludeDeprecated))
                {
                    if (ret_prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
                    {
                        void* ret_addr = ret_prop->ContainerPtrToValuePtr<void>(params_memory);
                        ret_val = property_to_jsvalue(ctx, ret_prop, ret_addr);
                        break;
                    }
                }
            }

            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);

            return ret_val;
        }
        catch (const std::exception& e)
        {
            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] CallFunction exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "CallFunction failed due to exception");
        }
        catch (...)
        {
            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);
            return JS_ThrowInternalError(ctx, "CallFunction failed due to unknown exception");
        }
    }

    JSValue js_with_exec_budget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
        {
            return JS_ThrowTypeError(ctx, "__withExecBudget requires 2 arguments: budgetMs, callback");
        }

        double budget_ms = 0.0;
        if (JS_ToFloat64(ctx, &budget_ms, argv[0]) != 0)
        {
            return JS_ThrowTypeError(ctx, "First argument must be a number (budgetMs)");
        }

        if (!JS_IsFunction(ctx, argv[1]))
        {
            return JS_ThrowTypeError(ctx, "Second argument must be a callback function");
        }

        JSMod* mod = get_js_mod(ctx);
        if (!mod)
        {
            return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
        }

        const JSMod::ExecBudgetSnapshot snapshot = mod->capture_exec_budget();
        if (budget_ms > 0.0)
        {
            mod->set_exec_budget_ms(budget_ms);
        }
        else
        {
            mod->end_exec_budget();
        }

        JSValue result = JS_UNDEFINED;
        const bool ok = safe_js_call(ctx, argv[1], JS_UNDEFINED, 0, nullptr, &result);

        mod->restore_exec_budget(snapshot);

        if (!ok)
        {
            return JS_ThrowInternalError(ctx, "__withExecBudget failed due to exception");
        }

        return result;
    }

    JSValue js_call_function_ex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
        {
            return JS_ThrowTypeError(ctx, "CallFunctionEx requires at least 2 arguments: object, functionName [, ...args]");
        }

        void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
        if (!obj_ptr)
        {
            return JS_ThrowTypeError(ctx, "First argument must be a UObject");
        }
        Unreal::UObject* object = static_cast<Unreal::UObject*>(obj_ptr);

        const char* func_name = JS_ToCString(ctx, argv[1]);
        if (!func_name)
        {
            return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
        }
        std::wstring wide_func_name(func_name, func_name + strlen(func_name));
        JS_FreeCString(ctx, func_name);

        void* params_memory = nullptr;
        std::vector<wchar_t*> raw_string_buffers{};

        try
        {
            Unreal::UFunction* function = object->GetFunctionByNameInChain(wide_func_name.c_str());
            if (!function)
            {
                return JS_ThrowReferenceError(ctx, "Function not found on object");
            }

            if (function->HasAnyFunctionFlags(Unreal::EFunctionFlags::FUNC_Net))
            {
                return JS_ThrowInternalError(ctx, "CallFunctionEx does not support net functions");
            }

            int32_t params_size = function->GetParmsSize();
            if (params_size > 0)
            {
                params_memory = calloc(1, params_size);
                if (!params_memory)
                {
                    return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
                }
            }

            if (!fill_call_params(ctx, function, params_memory, argc, argv, 2, raw_string_buffers))
            {
                free_raw_string_buffers(raw_string_buffers);
                if (params_memory) free(params_memory);
                return JS_ThrowInternalError(ctx, "CallFunctionEx: parameter fill crashed (SEH)");
            }

            if (!safe_process_event(object, function, params_memory))
            {
                free_raw_string_buffers(raw_string_buffers);
                if (params_memory) free(params_memory);
                return JS_ThrowInternalError(ctx, "ProcessEvent crashed (SEH exception caught)");
            }

            JSValue outputs = collect_function_outputs(ctx, function, params_memory);

            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);
            return outputs;
        }
        catch (const std::exception& e)
        {
            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] CallFunctionEx exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "CallFunctionEx failed due to exception");
        }
        catch (...)
        {
            free_raw_string_buffers(raw_string_buffers);
            if (params_memory) free(params_memory);
            return JS_ThrowInternalError(ctx, "CallFunctionEx failed due to unknown exception");
        }
    }

} // namespace RC::JSScript
