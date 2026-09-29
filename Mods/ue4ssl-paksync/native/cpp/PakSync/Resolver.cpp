#define NOMINMAX

#include "PakSync/Resolver.hpp"

#include "PakSync/RuntimeUtils.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

#include <Windows.h>
#include <winnt.h>

#include <DynamicOutput/DynamicOutput.hpp>

namespace RC::PakSync
{
    namespace
    {
        auto bounded_strlen(const char* value, size_t max_len) -> size_t
        {
            size_t len = 0;
            while (len < max_len && value[len] != '\0')
            {
                ++len;
            }
            return len;
        }

        auto matches_signature(const uint8_t* at, const std::vector<int>& signature) -> bool
        {
            for (size_t i = 0; i < signature.size(); ++i)
            {
                if (signature[i] >= 0 && at[i] != static_cast<uint8_t>(signature[i]))
                {
                    return false;
                }
            }
            return true;
        }

        struct ScanResult
        {
            void* first{};
            void* second{};
            uint32_t count{};
        };

        auto scan_text(const ModuleSections& sections, const PatternSpec& spec) -> ScanResult
        {
            ScanResult result{};
            if (!sections.text || sections.text_size < spec.signature.size() || spec.signature.empty())
            {
                return result;
            }

            const auto scan_size = sections.text_size - spec.signature.size();
            for (size_t offset = 0; offset <= scan_size; ++offset)
            {
                auto* candidate = sections.text + offset;
                if (matches_signature(candidate, spec.signature))
                {
                    ++result.count;
                    if (!result.first)
                    {
                        result.first = candidate;
                    }
                    else if (!result.second)
                    {
                        result.second = candidate;
                    }
                }
            }

            return result;
        }

        auto resolve_pattern(const ModuleSections& sections, const PatternSpec& spec) -> ResolveResult
        {
            ResolveResult result{&spec};
            if (!sections.base || spec.signature.empty())
            {
                return result;
            }

            auto* rva_candidate = sections.base + spec.pdb_rva;
            if (spec.pdb_rva < sections.image_size && matches_signature(rva_candidate, spec.signature))
            {
                result.address = rva_candidate;
                result.method = "pdb-rva-signature";
                result.confidence = 90;
                result.match_count = 1;
                return result;
            }

            const auto scanned = scan_text(sections, spec);
            if (scanned.first)
            {
                result.address = scanned.first;
                result.second_address = scanned.second;
                result.method = "text-signature";
                result.confidence = scanned.count == 1 ? 80 : 55;
                result.match_count = scanned.count;
                return result;
            }

            return result;
        }

        auto resolver_specs() -> const std::vector<PatternSpec>&
        {
            static const std::vector<PatternSpec> specs{
                    {"FControlChannelOutBunch::ctor", 0x365DDD0, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x80, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x10}},
                    {"FOutBunch::ctor", 0x191BDF0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48}},
                    {"FInBunch::ctor", 0x191B880, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9}},
                    {"UControlChannel::ReceivedBunch", 0x367CA90, {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x78, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x88, 0x01, 0x00, 0x00, 0x33, 0xF6, 0x48, 0x8B, 0xDA}},
                    {"UChannel::ReceivedRawBunch", 0x367EAD0, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83}},
                    {"UChannel::SendBunch", 0x3682A60, {0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0x20, 0xFF, 0xFF, 0xFF}},
                    {"UControlChannel::SendBunch", 0x3683690, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0x41, 0x78, 0x49, 0x8B, 0xF0}},
                    {"UNetConnection::SendRawBunch", 0x385B5B0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x01, 0x45, 0x0F, 0xB6, 0xF8}},
                    {"UNetConnection::CreateChannelByName", 0x0, {}},
                    {"FPakPlatformFile::Mount", 0x33C4A10, {0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x49, 0x8D, 0xAB, 0xA8, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x40, 0x03, 0x00, 0x00}},
                    {"FPakPlatformFile::MountAllPakFiles", 0x33C5B80, {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}},
                    {"FPackageName::RegisterMountPoint", 0x0B52CD0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9}},
                    {"FPlatformFileManager::FindPlatformFile", 0x0A213B0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x19, 0x48, 0x8B, 0xFA}},
                    {"FPlatformFileManager::Get", 0x1C77C80, {0x48, 0x8D, 0x05, -1, -1, -1, -1, 0xC3}},
            };
            return specs;
        }
    }

    auto get_module_sections() -> ModuleSections
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
        {
            return {};
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return {};
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return {};
        }

        ModuleSections sections{};
        sections.base = base;
        sections.image_size = nt->OptionalHeader.SizeOfImage;

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            const auto* raw_name = reinterpret_cast<const char*>(section->Name);
            const std::string_view name{raw_name, bounded_strlen(raw_name, 8)};
            if (name == ".text")
            {
                sections.text = base + section->VirtualAddress;
                sections.text_size = section->Misc.VirtualSize;
                break;
            }
        }

        return sections;
    }

    auto resolve_functions() -> FunctionTable
    {
        const auto sections = get_module_sections();
        const auto& specs = resolver_specs();
        std::unordered_map<std::string_view, ResolveResult> map{};

        for (const auto& spec : specs)
        {
            const auto result = resolve_pattern(sections, spec);
            map.emplace(spec.key, result);
        }

        FunctionTable table{};
        table.f_control_channel_out_bunch_ctor = map.at("FControlChannelOutBunch::ctor");
        table.f_out_bunch_ctor = map.at("FOutBunch::ctor");
        table.f_in_bunch_ctor = map.at("FInBunch::ctor");
        table.u_control_channel_received_bunch = map.at("UControlChannel::ReceivedBunch");
        table.u_channel_received_raw_bunch = map.at("UChannel::ReceivedRawBunch");
        table.u_channel_send_bunch = map.at("UChannel::SendBunch");
        table.u_control_channel_send_bunch = map.at("UControlChannel::SendBunch");
        table.u_net_connection_send_raw_bunch = map.at("UNetConnection::SendRawBunch");
        table.u_net_connection_create_channel_by_name = map.at("UNetConnection::CreateChannelByName");
        table.f_pak_platform_file_mount = map.at("FPakPlatformFile::Mount");
        table.f_pak_platform_file_mount_all_pak_files = map.at("FPakPlatformFile::MountAllPakFiles");
        table.f_package_name_register_mount_point = map.at("FPackageName::RegisterMountPoint");
        table.f_platform_file_manager_find_platform_file = map.at("FPlatformFileManager::FindPlatformFile");
        table.f_platform_file_manager_get = map.at("FPlatformFileManager::Get");
        return table;
    }

    void log_resolve_result(const ResolveResult& result)
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const auto rva = result.address && base
                                 ? static_cast<uintptr_t>(reinterpret_cast<uint8_t*>(result.address) - base)
                                 : uintptr_t{};
        const auto second_rva = result.second_address && base
                                        ? static_cast<uintptr_t>(reinterpret_cast<uint8_t*>(result.second_address) - base)
                                        : uintptr_t{};
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] resolver {}: addr=0x{:016X} rva=0x{:X} matches={} second_rva=0x{:X} confidence={} method={}\n"),
                widen(result.spec ? result.spec->key : "<null>"),
                reinterpret_cast<uintptr_t>(result.address),
                rva,
                result.match_count,
                second_rva,
                result.confidence,
                widen(result.method ? result.method : "missing"));
    }

    void log_module_sections()
    {
        const auto sections = get_module_sections();
        Output::send<LogLevel::Normal>(
                STR("[UE4SSL.PakSync] module base=0x{:016X} image_size=0x{:X} text=0x{:016X} text_size=0x{:X}\n"),
                reinterpret_cast<uintptr_t>(sections.base),
                sections.image_size,
                reinterpret_cast<uintptr_t>(sections.text),
                sections.text_size);
    }

    auto all_required_resolved(const FunctionTable& table) -> bool
    {
        return table.u_control_channel_received_bunch.address &&
               table.u_control_channel_send_bunch.address &&
               table.u_net_connection_send_raw_bunch.address;
    }
}
