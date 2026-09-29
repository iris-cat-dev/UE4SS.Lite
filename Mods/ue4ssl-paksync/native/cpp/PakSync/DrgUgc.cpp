#define NOMINMAX

#include "PakSync/DrgUgc.hpp"

#include "PakSync/AssetPackages.hpp"
#include "PakSync/Protocol.hpp"
#include "PakSync/RuntimeUtils.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <SehFramework.hpp>
#include <Unreal\CoreUObject\UObject\Class.hpp>
#include <Unreal\CoreUObject\UObject\FStrProperty.hpp>
#include <Unreal\CoreUObject\UObject\UnrealType.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace fs = std::filesystem;

namespace RC::PakSync
{
    namespace
    {
        constexpr size_t UugcPackageWindowsSize = 288;
        constexpr size_t UugcPackageWindowsIdPtrOffset = 280;
static auto sanitize_ue_object_name(std::wstring value) -> std::wstring
{
    for (auto& ch : value)
    {
        if (!(std::iswalnum(ch) || ch == L'_'))
        {
            ch = L'_';
        }
    }
    while (!value.empty() && value.front() == L'_')
    {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == L'_')
    {
        value.pop_back();
    }
    if (value.empty())
    {
        value = L"PakSyncPak";
    }
    if (std::iswdigit(value.front()))
    {
        value.insert(value.begin(), L'P');
    }
    return value;
}

static auto stable_paksync_mod_id_for_pak(const std::wstring& pak_path) -> std::wstring
{
    const auto hash = sha256_file(fs::path{pak_path});
    if (!hash)
    {
        const auto fallback = static_cast<uint64_t>(crc32(std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(pak_path.data()),
                pak_path.size() * sizeof(wchar_t)}));
        return std::to_wstring(900000000000000000ULL + fallback);
    }

    uint64_t value = 0;
    for (size_t index = 0; index < sizeof(value); ++index)
    {
        value = (value << 8) | static_cast<uint64_t>((*hash)[index]);
    }

    value &= 0x0FFFFFFFFFFFFFFFULL;
    return std::to_wstring(900000000000000000ULL + (value % 99999999999999999ULL));
}

static bool set_fstring_property(Unreal::UObject* object, const CharType* property_name, const std::wstring& value)
{
    if (!object)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FStrProperty>(property))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected FString property {} missing on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    *property->ContainerPtrToValuePtr<Unreal::FString>(object) = Unreal::FString(value.c_str());
    return true;
}

    }

bool set_bool_property(Unreal::UObject* object, const CharType* property_name, bool value)
{
    if (!object)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(property);
    if (!bool_property)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected bool property {} missing on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    bool_property->SetPropertyValueInContainer(object, value);
    return true;
}

static bool set_numeric_property_u64(Unreal::UObject* object, const CharType* property_name, uint64_t value)
{
    if (!object)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (auto* enum_property = Unreal::CastField<Unreal::FEnumProperty>(property))
    {
        if (auto* underlying = enum_property->GetUnderlyingProperty())
        {
            underlying->SetIntPropertyValue(property->ContainerPtrToValuePtr<void>(object), value);
            return true;
        }
    }

    if (auto* numeric_property = Unreal::CastField<Unreal::FNumericProperty>(property))
    {
        if (numeric_property->IsInteger())
        {
            numeric_property->SetIntPropertyValue(property->ContainerPtrToValuePtr<void>(object), value);
            return true;
        }
    }

    Output::send<LogLevel::Warning>(
            STR("[UE4SSL.PakSync] reflected numeric property {} missing on {}\n"),
            property_name,
            object->GetFullName());
    return false;
}

static bool set_fstring_array_property(
        Unreal::UObject* object,
        const CharType* property_name,
        const std::vector<std::wstring>& values)
{
    if (!object)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FArrayProperty>(property))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected FString array property {} missing on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    auto* array = property->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FString>>(object);
    array->Empty(static_cast<int32_t>(values.size()));
    for (const auto& value : values)
    {
        array->Add(Unreal::FString(value.c_str()));
    }
    return true;
}

static bool clear_int64_array_property(Unreal::UObject* object, const CharType* property_name)
{
    if (!object)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    auto* array_property = Unreal::CastField<Unreal::FArrayProperty>(property);
    if (!array_property)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected int64 array property {} missing on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    auto* inner = array_property->GetInner();
    if (!Unreal::CastField<Unreal::FInt64Property>(inner))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected array property {} has unexpected inner type on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    Unreal::FScriptArrayHelper helper(array_property, property->ContainerPtrToValuePtr<void>(object));
    helper.EmptyValues(0);
    return true;
}

static std::optional<std::wstring> get_fstring_property(Unreal::UObject* object, const CharType* property_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FStrProperty>(property))
    {
        return std::nullopt;
    }

    return fstring_to_wstring(*property->ContainerPtrToValuePtr<Unreal::FString>(object));
}

static std::optional<int32_t> get_int_property_i32(Unreal::UObject* object, const CharType* property_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (auto* numeric_property = Unreal::CastField<Unreal::FNumericProperty>(property))
    {
        if (numeric_property->IsInteger())
        {
            return static_cast<int32_t>(
                    numeric_property->GetSignedIntPropertyValue(property->ContainerPtrToValuePtr<void>(object)));
        }
    }
    return std::nullopt;
}

std::optional<bool> get_bool_property(Unreal::UObject* object, const CharType* property_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(property))
    {
        return bool_property->GetPropertyValueInContainer(object);
    }
    return std::nullopt;
}

static std::optional<std::vector<std::wstring>> get_fstring_array_property(
        Unreal::UObject* object,
        const CharType* property_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FArrayProperty>(property))
    {
        return std::nullopt;
    }

    auto* array = property->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FString>>(object);
    std::vector<std::wstring> values{};
    values.reserve(static_cast<size_t>(array->Num()));
    for (int32_t index = 0; index < array->Num(); ++index)
    {
        values.push_back(fstring_to_wstring((*array)[index]));
    }
    return values;
}

static std::optional<int64_t> invoke_int64_return_function(Unreal::UObject* object, const CharType* function_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* function = object->GetFunctionByNameInChain(function_name);
    if (!function || function->GetParmsSize() <= 0 || function->GetParmsSize() > 64)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
    Unreal::FProperty* return_property = nullptr;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (prop && prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            return_property = prop;
            break;
        }
    }
    if (!return_property)
    {
        return std::nullopt;
    }
    if (!Seh::SafeProcessEvent(object, function, params.data()))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} failed during SafeProcessEvent on {}\n"),
                function_name,
                object->GetFullName());
        return std::nullopt;
    }
    if (auto* numeric_property = Unreal::CastField<Unreal::FNumericProperty>(return_property))
    {
        if (numeric_property->IsInteger())
        {
            return numeric_property->GetSignedIntPropertyValue(
                    return_property->ContainerPtrToValuePtr<void>(params.data()));
        }
    }
    return std::nullopt;
}

static std::optional<std::wstring> invoke_fstring_return_function(
        Unreal::UObject* object,
        const CharType* function_name)
{
    if (!object)
    {
        return std::nullopt;
    }

    auto* function = object->GetFunctionByNameInChain(function_name);
    if (!function || function->GetParmsSize() <= 0 || function->GetParmsSize() > 128)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
    Unreal::FString* return_value = nullptr;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (prop &&
            prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm) &&
            Unreal::CastField<Unreal::FStrProperty>(prop))
        {
            return_value = prop->ContainerPtrToValuePtr<Unreal::FString>(params.data());
            new (return_value) Unreal::FString();
            break;
        }
    }
    if (!return_value)
    {
        return std::nullopt;
    }
    if (!Seh::SafeProcessEvent(object, function, params.data()))
    {
        return_value->~FString();
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} failed during SafeProcessEvent on {}\n"),
                function_name,
                object->GetFullName());
        return std::nullopt;
    }

    auto out = fstring_to_wstring(*return_value);
    return_value->~FString();
    return out;
}

static bool add_package_to_array_property(
        Unreal::UObject* object,
        const CharType* property_name,
        Unreal::UObject* package)
{
    if (!object || !package)
    {
        return false;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FArrayProperty>(property))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] reflected package array property {} missing on {}\n"),
                property_name,
                object->GetFullName());
        return false;
    }

    auto* array = property->ContainerPtrToValuePtr<Unreal::TArray<Unreal::UObject*>>(object);
    for (int32_t index = 0; index < array->Num(); ++index)
    {
        if ((*array)[index] == package)
        {
            return true;
        }
    }
    array->Add(package);
    return true;
}

static auto get_object_array_property(Unreal::UObject* object, const CharType* property_name)
        -> Unreal::TArray<Unreal::UObject*>*
{
    if (!object)
    {
        return nullptr;
    }

    auto* property = object->GetPropertyByNameInChain(property_name);
    if (!Unreal::CastField<Unreal::FArrayProperty>(property))
    {
        return nullptr;
    }
    return property->ContainerPtrToValuePtr<Unreal::TArray<Unreal::UObject*>>(object);
}

static auto count_package_asset_overlap(
        Unreal::UObject* package,
        const std::vector<std::wstring>& package_names) -> size_t
{
    const auto assets = get_fstring_array_property(package, STR("PakFileAssets"));
    if (!assets || assets->empty() || package_names.empty())
    {
        return 0;
    }

    size_t overlap = 0;
    for (const auto& candidate : package_names)
    {
        if (std::find(assets->begin(), assets->end(), candidate) != assets->end())
        {
            ++overlap;
        }
    }
    return overlap;
}

static bool set_bool_param(Unreal::FProperty* property, void* params, bool value)
{
    if (auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(property))
    {
        bool_property->SetPropertyValueInContainer(params, value);
        return true;
    }
    if (property && property->GetSize() == sizeof(bool))
    {
        *property->ContainerPtrToValuePtr<bool>(params) = value;
        return true;
    }
    return false;
}

static auto find_ugc_package_class() -> Unreal::UClass*
{
    if (auto* package_class = Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/SimpleUGC.UGCPackage_Windows")))
    {
        return package_class;
    }
    if (auto* package_class = static_cast<Unreal::UClass*>(
                Unreal::UObjectGlobals::FindObjectByClassAndName(STR("Class"), STR("UGCPackage_Windows"))))
    {
        return package_class;
    }
    if (auto* package_class = Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(
                nullptr,
                nullptr,
                STR("/Script/SimpleUGC.UGCPackage")))
    {
        return package_class;
    }

    return static_cast<Unreal::UClass*>(Unreal::UObjectGlobals::FindObjectByClassAndName(STR("Class"), STR("UGCPackage")));
}

static bool is_ugc_package_windows_object(Unreal::UObject* package)
{
    if (!package)
    {
        return false;
    }

    const auto full_name = package->GetFullName();
    return full_name.find(STR("UGCPackage_Windows")) != decltype(full_name)::npos;
}

static bool install_synthetic_ugc_windows_id(Unreal::UObject* package, uint64_t mod_id)
{
    if (!package || !is_ugc_package_windows_object(package))
    {
        return false;
    }
    if (!is_readable_memory(package, UugcPackageWindowsSize))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC Windows id install skipped: object memory not readable package={} size={}\n"),
                package->GetFullName(),
                UugcPackageWindowsSize);
        return false;
    }

    auto** id_ptr = reinterpret_cast<uint64_t**>(
            static_cast<uint8_t*>(static_cast<void*>(package)) + UugcPackageWindowsIdPtrOffset);
    if (*id_ptr && is_readable_memory(*id_ptr, sizeof(uint64_t)))
    {
        **id_ptr = mod_id;
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] updated existing UGC Windows id package={} id={}\n"),
                package->GetFullName(),
                mod_id);
        return true;
    }

    auto* storage = static_cast<uint64_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(uint64_t)));
    if (!storage)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC Windows id install failed: HeapAlloc returned null package={}\n"),
                package->GetFullName());
        return false;
    }

    *storage = mod_id;
    *id_ptr = storage;
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] installed synthetic UGC Windows id package={} id={} storage=0x{:016X}\n"),
            package->GetFullName(),
            mod_id,
            reinterpret_cast<uintptr_t>(storage));
    return true;
}

auto find_existing_ugc_package_for_pak(
        Unreal::UObject* registry,
        const std::wstring& pak_path,
        const std::vector<std::wstring>& package_names) -> Unreal::UObject*
{
    auto* packages = get_object_array_property(registry, STR("UGCPackages"));
    if (!packages)
    {
        return nullptr;
    }

    const auto pak_fs_path = fs::path{pak_path};
    const auto pak_filename = pak_fs_path.filename().wstring();
    const auto pak_stem = pak_fs_path.stem().wstring();
    Unreal::UObject* best_package = nullptr;
    size_t best_overlap = 0;
    bool best_path_match = false;

    for (int32_t index = 0; index < packages->Num(); ++index)
    {
        auto* package = (*packages)[index];
        if (!package)
        {
            continue;
        }

        const auto package_pak = get_fstring_property(package, STR("PakFileLocation")).value_or(L"");
        const bool path_match =
                !package_pak.empty() &&
                (package_pak == pak_path ||
                 fs::path{package_pak}.filename().wstring() == pak_filename);
        const auto overlap = count_package_asset_overlap(package, package_names);
        const auto name = get_fstring_property(package, STR("Name")).value_or(L"");
        const bool weak_name_match = !pak_stem.empty() && name == pak_stem;

        if (path_match || overlap > 0 || weak_name_match)
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] existing UGC package candidate package={} path_match={} asset_overlap={} weak_name_match={} pak={}\n"),
                    package->GetFullName(),
                    path_match,
                    overlap,
                    weak_name_match,
                    package_pak.empty() ? L"<none>" : package_pak);
        }

        const bool better =
                (!best_package && (path_match || overlap > 0 || weak_name_match)) ||
                (path_match && !best_path_match) ||
                (path_match == best_path_match && overlap > best_overlap);
        if (better)
        {
            best_package = package;
            best_overlap = overlap;
            best_path_match = path_match;
        }
    }

    if (best_package)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] reusing existing UGC package package={} path_match={} asset_overlap={} pak={}\n"),
                best_package->GetFullName(),
                best_path_match,
                best_overlap,
                pak_path);
    }
    return best_package;
}

bool invoke_package_param_function(
        Unreal::UObject* object,
        const CharType* function_name,
        const CharType* package_param_name,
        Unreal::UObject* package)
{
    if (!object || !package)
    {
        return false;
    }

    auto* function = object->GetFunctionByNameInChain(function_name);
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 256)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} skipped: unexpected parms_size={} on {}\n"),
                function_name,
                params_size,
                object->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    bool wrote_package = false;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm) ||
            prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            continue;
        }

        ++reflected_param_count;
        if (prop->GetName() == package_param_name && prop->GetSize() == sizeof(Unreal::UObject*))
        {
            *prop->ContainerPtrToValuePtr<Unreal::UObject*>(params.data()) = package;
            wrote_package = true;
        }
        else
        {
            unexpected_param = true;
        }
    }

    if (!wrote_package || reflected_param_count != 1 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} skipped: package_param={} count={} unexpected={}\n"),
                function_name,
                wrote_package,
                reflected_param_count,
                unexpected_param);
        return false;
    }

    if (!Seh::SafeProcessEvent(object, function, params.data()))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] {} failed during SafeProcessEvent on {}\n"),
                function_name,
                object->GetFullName());
        return false;
    }
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked {} on {} package={}\n"),
            function_name,
            object->GetFullName(),
            package->GetFullName());
    return true;
}

bool invoke_mount_ugc_package(
        Unreal::UObject* registry,
        Unreal::UObject* package,
        bool from_joining)
{
    if (!registry || !package)
    {
        return false;
    }

    auto* function = registry->GetFunctionByNameInChain(STR("MountUGCPackage"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 256)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] MountUGCPackage skipped: unexpected parms_size={} on {}\n"),
                params_size,
                registry->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    bool wrote_package = false;
    bool wrote_from_joining = false;
    Unreal::FProperty* return_property = nullptr;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
        {
            continue;
        }

        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            if (prop->GetName() == STR("ReturnValue") && prop->GetSize() == sizeof(bool))
            {
                return_property = prop;
            }
            else
            {
                unexpected_param = true;
            }
            continue;
        }

        ++reflected_param_count;
        const auto prop_name = prop->GetName();
        if (prop_name == STR("Package") && prop->GetSize() == sizeof(Unreal::UObject*))
        {
            *prop->ContainerPtrToValuePtr<Unreal::UObject*>(params.data()) = package;
            wrote_package = true;
        }
        else if (prop_name == STR("FromJoining") && set_bool_param(prop, params.data(), from_joining))
        {
            wrote_from_joining = true;
        }
        else
        {
            unexpected_param = true;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] MountUGCPackage unexpected param {} size={}\n"),
                    prop_name,
                    prop->GetSize());
        }
    }

    if (!wrote_package || !wrote_from_joining || !return_property ||
        reflected_param_count != 2 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] MountUGCPackage skipped: Package={} FromJoining={} ReturnValue={} count={} unexpected={}\n"),
                wrote_package,
                wrote_from_joining,
                return_property != nullptr,
                reflected_param_count,
                unexpected_param);
        return false;
    }

    if (!Seh::SafeProcessEvent(registry, function, params.data()))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] MountUGCPackage failed during SafeProcessEvent on {} package={}\n"),
                registry->GetFullName(),
                package->GetFullName());
        return false;
    }

    bool mounted = false;
    if (auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(return_property))
    {
        mounted = bool_property->GetPropertyValueInContainer(params.data());
    }
    else
    {
        mounted = *return_property->ContainerPtrToValuePtr<bool>(params.data());
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked MountUGCPackage on {} package={} FromJoining={} result={}\n"),
            registry->GetFullName(),
            package->GetFullName(),
            from_joining,
            mounted);
    return mounted;
}

bool invoke_get_all_classes_in_package(Unreal::UObject* registry, Unreal::UObject* package)
{
    if (!registry || !package)
    {
        return false;
    }

    auto* function = registry->GetFunctionByNameInChain(STR("GetAllClassesInPackage"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 512)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetAllClassesInPackage skipped: unexpected parms_size={} on {}\n"),
                params_size,
                registry->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    Unreal::TArray<Unreal::UObject*>* classes = nullptr;
    Unreal::FProperty* return_property = nullptr;
    bool wrote_package = false;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
        {
            continue;
        }

        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            return_property = prop;
            continue;
        }

        ++reflected_param_count;
        const auto prop_name = prop->GetName();
        if (prop_name == STR("Package") && prop->GetSize() == sizeof(Unreal::UObject*))
        {
            *prop->ContainerPtrToValuePtr<Unreal::UObject*>(params.data()) = package;
            wrote_package = true;
        }
        else if (prop_name == STR("Classes") && Unreal::CastField<Unreal::FArrayProperty>(prop))
        {
            classes = prop->ContainerPtrToValuePtr<Unreal::TArray<Unreal::UObject*>>(params.data());
            new (classes) Unreal::TArray<Unreal::UObject*>();
        }
        else
        {
            unexpected_param = true;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] GetAllClassesInPackage unexpected param {} size={}\n"),
                    prop_name,
                    prop->GetSize());
        }
    }

    if (!wrote_package || !classes || !return_property || reflected_param_count != 2 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetAllClassesInPackage skipped: Package={} Classes={} ReturnValue={} count={} unexpected={}\n"),
                wrote_package,
                classes != nullptr,
                return_property != nullptr,
                reflected_param_count,
                unexpected_param);
        if (classes)
        {
            classes->~TArray<Unreal::UObject*>();
        }
        return false;
    }

    const auto ok = Seh::SafeProcessEvent(registry, function, params.data());
    bool result = false;
    if (ok)
    {
        if (auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(return_property))
        {
            result = bool_property->GetPropertyValueInContainer(params.data());
        }
        else if (return_property->GetSize() == sizeof(bool))
        {
            result = *return_property->ContainerPtrToValuePtr<bool>(params.data());
        }
    }

    const int32_t class_count = classes->Num();
    if (!ok)
    {
        classes->~TArray<Unreal::UObject*>();
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetAllClassesInPackage failed during SafeProcessEvent on {} package={}\n"),
                registry->GetFullName(),
                package->GetFullName());
        return false;
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked GetAllClassesInPackage on {} package={} result={} classes={}\n"),
            registry->GetFullName(),
            package->GetFullName(),
            result,
            class_count);
    for (int32_t index = 0; index < std::min<int32_t>(class_count, 16); ++index)
    {
        auto* klass = (*classes)[index];
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   UGC class[{}]={}\n"),
                index,
                klass ? klass->GetFullName() : STR("<null>"));
    }
    classes->~TArray<Unreal::UObject*>();
    return result;
}

bool invoke_get_maps_in_package(Unreal::UObject* registry, Unreal::UObject* package)
{
    if (!registry || !package)
    {
        return false;
    }

    auto* function = registry->GetFunctionByNameInChain(STR("GetMapsInPackage"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 512)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetMapsInPackage skipped: unexpected parms_size={} on {}\n"),
                params_size,
                registry->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    Unreal::TArray<Unreal::FName>* maps = nullptr;
    Unreal::FProperty* return_property = nullptr;
    bool wrote_package = false;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm))
        {
            continue;
        }

        if (prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            return_property = prop;
            continue;
        }

        ++reflected_param_count;
        const auto prop_name = prop->GetName();
        if (prop_name == STR("Package") && prop->GetSize() == sizeof(Unreal::UObject*))
        {
            *prop->ContainerPtrToValuePtr<Unreal::UObject*>(params.data()) = package;
            wrote_package = true;
        }
        else if (prop_name == STR("Maps") && Unreal::CastField<Unreal::FArrayProperty>(prop))
        {
            maps = prop->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FName>>(params.data());
            new (maps) Unreal::TArray<Unreal::FName>();
        }
        else
        {
            unexpected_param = true;
            Output::send<LogLevel::Warning>(
                    STR("[UE4SSL.PakSync] GetMapsInPackage unexpected param {} size={}\n"),
                    prop_name,
                    prop->GetSize());
        }
    }

    if (!wrote_package || !maps || !return_property || reflected_param_count != 2 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetMapsInPackage skipped: Package={} Maps={} ReturnValue={} count={} unexpected={}\n"),
                wrote_package,
                maps != nullptr,
                return_property != nullptr,
                reflected_param_count,
                unexpected_param);
        if (maps)
        {
            maps->~TArray<Unreal::FName>();
        }
        return false;
    }

    const auto ok = Seh::SafeProcessEvent(registry, function, params.data());
    bool result = false;
    if (ok)
    {
        if (auto* bool_property = Unreal::CastField<Unreal::FBoolProperty>(return_property))
        {
            result = bool_property->GetPropertyValueInContainer(params.data());
        }
        else if (return_property->GetSize() == sizeof(bool))
        {
            result = *return_property->ContainerPtrToValuePtr<bool>(params.data());
        }
    }

    const int32_t map_count = maps->Num();
    if (!ok)
    {
        maps->~TArray<Unreal::FName>();
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] GetMapsInPackage failed during SafeProcessEvent on {} package={}\n"),
                registry->GetFullName(),
                package->GetFullName());
        return false;
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked GetMapsInPackage on {} package={} result={} maps={}\n"),
            registry->GetFullName(),
            package->GetFullName(),
            result,
            map_count);
    for (int32_t index = 0; index < std::min<int32_t>(map_count, 16); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   UGC map[{}]={}\n"),
                index,
                (*maps)[index].ToString());
    }
    maps->~TArray<Unreal::FName>();
    return result;
}

bool invoke_set_packages_as_recently_installed(Unreal::UObject* subsystem, Unreal::UObject* package)
{
    if (!subsystem || !package)
    {
        return false;
    }

    auto* function = subsystem->GetFunctionByNameInChain(STR("SetPackagesAsRecentlyInstalled"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 256)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] SetPackagesAsRecentlyInstalled skipped: unexpected parms_size={} on {}\n"),
                params_size,
                subsystem->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    Unreal::TArray<Unreal::UObject*>* constructed_array = nullptr;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm) ||
            prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            continue;
        }

        ++reflected_param_count;
        if (prop->GetName() == STR("RecentMods") && Unreal::CastField<Unreal::FArrayProperty>(prop))
        {
            auto* recent_mods = prop->ContainerPtrToValuePtr<Unreal::TArray<Unreal::UObject*>>(params.data());
            new (recent_mods) Unreal::TArray<Unreal::UObject*>();
            recent_mods->Add(package);
            constructed_array = recent_mods;
        }
        else
        {
            unexpected_param = true;
        }
    }

    if (!constructed_array || reflected_param_count != 1 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] SetPackagesAsRecentlyInstalled skipped: array_param={} count={} unexpected={}\n"),
                constructed_array != nullptr,
                reflected_param_count,
                unexpected_param);
        if (constructed_array)
        {
            constructed_array->~TArray<Unreal::UObject*>();
        }
        return false;
    }

    subsystem->ProcessEvent(function, params.data());
    constructed_array->~TArray<Unreal::UObject*>();
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked SetPackagesAsRecentlyInstalled on {} package={}\n"),
            subsystem->GetFullName(),
            package->GetFullName());
    return true;
}

bool invoke_set_mods_as_recently_installed(Unreal::UObject* subsystem, const std::wstring& mod_id)
{
    if (!subsystem || mod_id.empty())
    {
        return false;
    }

    auto* function = subsystem->GetFunctionByNameInChain(STR("SetModsAsRecentlyInstalled"));
    if (!function)
    {
        return false;
    }

    const int32_t params_size = function->GetParmsSize();
    if (params_size <= 0 || params_size > 256)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] SetModsAsRecentlyInstalled skipped: unexpected parms_size={} on {}\n"),
                params_size,
                subsystem->GetFullName());
        return false;
    }

    std::vector<uint8_t> params(static_cast<size_t>(params_size), 0);
    Unreal::TArray<Unreal::FString>* constructed_array = nullptr;
    uint32_t reflected_param_count = 0;
    bool unexpected_param = false;
    for (Unreal::FProperty* prop : Unreal::TFieldRange<Unreal::FProperty>(
                 function,
                 Unreal::EFieldIterationFlags::IncludeDeprecated))
    {
        if (!prop || !prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_Parm) ||
            prop->HasAnyPropertyFlags(Unreal::EPropertyFlags::CPF_ReturnParm))
        {
            continue;
        }

        ++reflected_param_count;
        if (prop->GetName() == STR("RecentMods") && Unreal::CastField<Unreal::FArrayProperty>(prop))
        {
            auto* recent_mods = prop->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FString>>(params.data());
            new (recent_mods) Unreal::TArray<Unreal::FString>();
            recent_mods->Add(Unreal::FString(mod_id.c_str()));
            constructed_array = recent_mods;
        }
        else
        {
            unexpected_param = true;
        }
    }

    if (!constructed_array || reflected_param_count != 1 || unexpected_param)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] SetModsAsRecentlyInstalled skipped: array_param={} count={} unexpected={}\n"),
                constructed_array != nullptr,
                reflected_param_count,
                unexpected_param);
        if (constructed_array)
        {
            constructed_array->~TArray<Unreal::FString>();
        }
        return false;
    }

    const auto ok = Seh::SafeProcessEvent(subsystem, function, params.data());
    constructed_array->~TArray<Unreal::FString>();
    if (!ok)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] SetModsAsRecentlyInstalled failed during SafeProcessEvent on {} mod_id={}\n"),
                subsystem->GetFullName(),
                mod_id);
        return false;
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] invoked SetModsAsRecentlyInstalled on {} mod_id={}\n"),
            subsystem->GetFullName(),
            mod_id);
    return true;
}

void log_synthesized_ugc_package_identity(Unreal::UObject* package)
{
    if (!package)
    {
        return;
    }

    const auto name = get_fstring_property(package, STR("Name")).value_or(L"<missing>");
    const auto mod_url = get_fstring_property(package, STR("ModURL")).value_or(L"<missing>");
    const auto pak_location = get_fstring_property(package, STR("PakFileLocation")).value_or(L"<missing>");
    const auto is_mounted = get_bool_property(package, STR("IsMounted"));
    const auto reflected_id = invoke_fstring_return_function(package, STR("GetIdAsString"));
    const auto reflected_id_int = invoke_int64_return_function(package, STR("GetIdAsInt"));
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] UGC package identity package={} name={} reflected_id={} reflected_id_int={} mod_url={} is_mounted={} pak={}\n"),
            package->GetFullName(),
            name,
            reflected_id ? (reflected_id->empty() ? L"<empty>" : *reflected_id) : L"<failed>",
            reflected_id_int ? std::to_wstring(*reflected_id_int) : L"<failed>",
            mod_url,
            is_mounted ? (*is_mounted ? STR("true") : STR("false")) : STR("<missing>"),
            pak_location);
}

std::wstring effective_ugc_mod_id(Unreal::UObject* package)
{
    if (!package)
    {
        return {};
    }

    return get_fstring_property(package, STR("ModURL")).value_or(L"");
}

std::wstring reflected_ugc_mod_id(Unreal::UObject* package)
{
    const auto reflected_id = invoke_fstring_return_function(package, STR("GetIdAsString"));
    if (!reflected_id || reflected_id->empty())
    {
        return {};
    }
    return *reflected_id;
}

bool is_safe_for_native_ugc_mount(Unreal::UObject* package)
{
    (void)package;
    // Verified unsafe in DRG during join with synthesized packages: ProcessEvent can SEH at MountUGCPackage.
    return false;
}

static Unreal::UObject* find_ugc_settings_object(Unreal::UObject* subsystem)
{
    if (subsystem)
    {
        if (auto* property = subsystem->GetPropertyByNameInChain(STR("UGCSettings"));
            property && property->GetSize() == sizeof(Unreal::UObject*))
        {
            if (auto* settings = *property->ContainerPtrToValuePtr<Unreal::UObject*>(subsystem))
            {
                return settings;
            }
        }
    }

    std::vector<Unreal::UObject*> settings_objects{};
    Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("UGCSettings"), settings_objects);
    for (auto* settings : settings_objects)
    {
        if (settings)
        {
            return settings;
        }
    }
    return nullptr;
}

static const CharType* ugc_slot_property_for_number(int32_t selected_slot)
{
    switch (selected_slot)
    {
    case 1:
        return STR("slot1");
    case 2:
        return STR("slot2");
    case 3:
        return STR("slot3");
    case 4:
        return STR("slot4");
    default:
        return STR("slot1");
    }
}

bool add_mod_id_to_selected_ugc_slot(Unreal::UObject* subsystem, const std::wstring& mod_id)
{
    if (mod_id.empty())
    {
        return false;
    }

    auto* settings = find_ugc_settings_object(subsystem);
    if (!settings)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGCSettings selected slot update skipped: settings object not found mod_id={}\n"),
                mod_id);
        return false;
    }

    const auto selected_slot = get_int_property_i32(settings, STR("SelectedSlot")).value_or(1);
    const auto* slot_name = ugc_slot_property_for_number(selected_slot);
    auto* property = settings->GetPropertyByNameInChain(slot_name);
    auto* array_property = Unreal::CastField<Unreal::FArrayProperty>(property);
    if (!array_property || !Unreal::CastField<Unreal::FStrProperty>(array_property->GetInner()))
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGCSettings selected slot update skipped: {} is not FString array on {}\n"),
                slot_name,
                settings->GetFullName());
        return false;
    }

    auto* slot = property->ContainerPtrToValuePtr<Unreal::TArray<Unreal::FString>>(settings);
    for (int32_t index = 0; index < slot->Num(); ++index)
    {
        if (fstring_to_wstring((*slot)[index]) == mod_id)
        {
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] UGCSettings selected slot already contains mod_id={} object={} selected_slot={} property={}\n"),
                    mod_id,
                    settings->GetFullName(),
                    selected_slot,
                    slot_name);
            return true;
        }
    }

    slot->Add(Unreal::FString(mod_id.c_str()));
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] added mod_id to UGCSettings selected slot mod_id={} object={} selected_slot={} property={} count={}\n"),
            mod_id,
            settings->GetFullName(),
            selected_slot,
            slot_name,
            slot->Num());
    return true;
}

void log_ugc_settings_state(
        Unreal::UObject* subsystem,
        Unreal::UObject* package,
        const std::wstring& effective_mod_id)
{
    std::vector<Unreal::UObject*> settings_objects{};
    if (subsystem)
    {
        if (auto* property = subsystem->GetPropertyByNameInChain(STR("UGCSettings"));
            property && property->GetSize() == sizeof(Unreal::UObject*))
        {
            if (auto* settings = *property->ContainerPtrToValuePtr<Unreal::UObject*>(subsystem))
            {
                settings_objects.push_back(settings);
            }
        }
    }
    if (settings_objects.empty())
    {
        Unreal::UObjectGlobals::FindAllInstancesOfClass(STR("UGCSettings"), settings_objects);
    }

    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] UGCSettings candidates={} effective_mod_id={}\n"),
            settings_objects.size(),
            effective_mod_id.empty() ? L"<none>" : effective_mod_id);

    for (auto* settings : settings_objects)
    {
        if (!settings)
        {
            continue;
        }

        const auto selected_slot = get_int_property_i32(settings, STR("SelectedSlot"));
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] UGCSettings state object={} SelectedSlot={}\n"),
                settings->GetFullName(),
                selected_slot ? std::to_wstring(*selected_slot) : L"<missing>");

        for (const auto* slot_name : {STR("slot1"), STR("slot2"), STR("slot3"), STR("slot4")})
        {
            const auto values = get_fstring_array_property(settings, slot_name);
            if (!values)
            {
                Output::send<LogLevel::Warning>(
                        STR("[UE4SSL.PakSync] UGCSettings {} missing on {}\n"),
                        slot_name,
                        settings->GetFullName());
                continue;
            }

            bool contains_id = false;
            for (const auto& value : *values)
            {
                if (!effective_mod_id.empty() && value == effective_mod_id)
                {
                    contains_id = true;
                    break;
                }
            }
            Output::send<LogLevel::Normal>(
                    STR("[UE4SSL.PakSync] UGCSettings {} count={} contains_effective_mod_id={}\n"),
                    slot_name,
                    values->size(),
                    contains_id);
            const size_t log_count = std::min<size_t>(values->size(), 8);
            for (size_t index = 0; index < log_count; ++index)
            {
                Output::send<LogLevel::Normal>(
                        STR("[UE4SSL.PakSync]   {}[{}]={}\n"),
                        slot_name,
                        index,
                        (*values)[index]);
            }
        }
    }
}

Unreal::UObject* create_synthesized_ugc_package(
        Unreal::UObject* registry,
        const std::wstring& pak_path,
        const std::vector<std::wstring>& package_names)
{
    if (!registry || package_names.empty())
    {
        return nullptr;
    }

    auto* package_class = find_ugc_package_class();
    if (!package_class)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package synthesis skipped: /Script/SimpleUGC.UGCPackage class not found\n"));
        return nullptr;
    }

    const fs::path pak_fs_path{pak_path};
    const auto display_name = sanitize_ue_object_name(pak_fs_path.stem().wstring());
    const auto stable_mod_id = stable_paksync_mod_id_for_pak(pak_path);
    const auto stable_mod_id_int = std::wcstoull(stable_mod_id.c_str(), nullptr, 10);
    const auto object_name = L"PakSync_" + display_name + L"_" + std::to_wstring(GetTickCount64());
    auto* package = Unreal::UObjectGlobals::NewObject<Unreal::UObject>(
            registry,
            package_class,
            Unreal::FName(object_name.c_str(), Unreal::FNAME_Add),
            Unreal::RF_Transient);
    if (!package)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package synthesis failed: NewObject returned null pak={}\n"),
                pak_path);
        return nullptr;
    }

    bool fields_ok = true;
    fields_ok &= set_fstring_property(package, STR("Name"), display_name);
    fields_ok &= set_fstring_property(package, STR("Version"), L"PakSync");
    fields_ok &= set_fstring_property(package, STR("ModURL"), stable_mod_id);
    fields_ok &= set_fstring_property(package, STR("Categories"), L"PakSync");
    fields_ok &= set_numeric_property_u64(package, STR("Status"), 0);
    fields_ok &= set_numeric_property_u64(package, STR("DownloadVersion"), 1);
    fields_ok &= set_fstring_property(package, STR("ModPath"), pak_fs_path.parent_path().wstring());
    fields_ok &= set_fstring_property(package, STR("PakFileLocation"), pak_path);
    fields_ok &= set_fstring_property(package, STR("Author"), L"PakSync");
    fields_ok &= set_fstring_property(package, STR("AuthorURL"), L"");
    fields_ok &= set_fstring_property(package, STR("Description"), L"PakSync received package");
    const auto asset_references = build_ugc_asset_reference_variants(package_names);
    fields_ok &= set_fstring_array_property(package, STR("PakFileAssets"), asset_references);
    fields_ok &= set_bool_property(package, STR("IsMounted"), false);
    fields_ok &= set_bool_property(package, STR("MountingToBeApplied"), false);
    fields_ok &= set_bool_property(package, STR("DeprecatedLocation"), false);
    fields_ok &= set_bool_property(package, STR("ShowStatusForAudioCosmetic"), false);
    fields_ok &= clear_int64_array_property(package, STR("Dependencies"));
    fields_ok &= set_bool_property(package, STR("DependencyRemoved"), false);
    fields_ok &= set_bool_property(package, STR("PackagedForLatestVersion"), true);
    fields_ok &= set_bool_property(package, STR("OverridePackedForLatestVersion"), true);
    const bool windows_id_ok = install_synthetic_ugc_windows_id(package, stable_mod_id_int);

    if (!fields_ok)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package synthesis incomplete: reflected fields did not match package={}\n"),
                package->GetFullName());
        return nullptr;
    }

    const bool added_to_registry = add_package_to_array_property(registry, STR("UGCPackages"), package);
    const bool added_to_join_list = add_package_to_array_property(registry, STR("UGCPackagesInstalledDuringJoin"), package);
    (void)set_bool_property(registry, STR("PackageChange"), true);
    Output::send<LogLevel::Normal>(
            STR("[UE4SSL.PakSync] synthesized UGC package={} pak={} stable_mod_id={} windows_class={} windows_id={} package_assets={} pak_file_assets={} registry_array={} join_array={}\n"),
            package->GetFullName(),
            pak_path,
            stable_mod_id,
            is_ugc_package_windows_object(package),
            windows_id_ok,
            package_names.size(),
            asset_references.size(),
            added_to_registry,
            added_to_join_list);
    for (size_t index = 0; index < std::min<size_t>(asset_references.size(), 16); ++index)
    {
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync]   synthesized PakFileAssets[{}]={}\n"),
                index,
                asset_references[index]);
    }
    if (!added_to_registry)
    {
        Output::send<LogLevel::Warning>(
                STR("[UE4SSL.PakSync] UGC package synthesis aborted: UGCPackages array update failed package={}\n"),
                package->GetFullName());
        return nullptr;
    }
    return package;
}

}
