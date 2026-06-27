#pragma once

#include "PakSync/RuntimeTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace RC::Unreal
{
    class FString;
}

namespace RC::PakSync
{
    auto widen(std::string_view value) -> std::wstring;
    auto bytes_to_hex(std::span<const uint8_t> bytes) -> std::wstring;
    auto fstring_to_wstring(const Unreal::FString& value) -> std::wstring;
    auto get_current_module_path() -> std::filesystem::path;
    auto utf8_from_wide(std::wstring_view value) -> std::string;
    auto wide_from_utf8(std::span<const uint8_t> value) -> std::wstring;
    auto sanitize_filename(std::wstring value) -> std::wstring;
    auto sync_phase_name(SyncPhase phase) -> const wchar_t*;
    auto manifest_payload(const PakManifest& manifest) -> std::vector<uint8_t>;
    auto session_id_for_hash(const std::array<uint8_t, 32>& hash) -> uint64_t;
    auto chunk_payload(const PakManifest& manifest, uint32_t seq) -> std::optional<std::vector<uint8_t>>;
    auto sha256_file(const std::filesystem::path& path) -> std::optional<std::array<uint8_t, 32>>;
    auto build_manifest(const std::filesystem::path& path, uint32_t chunk_size) -> std::optional<PakManifest>;
    auto is_readable_memory(const void* ptr, size_t size) -> bool;
    auto copy_bunch_payload_bytes(void* bunch) -> std::optional<std::vector<uint8_t>>;
    auto append_bunch_bits(void* bunch, std::span<const uint8_t> bytes) -> bool;

    template <typename T>
    void append_le(std::vector<uint8_t>& out, T value)
    {
        static_assert(std::is_integral_v<T>, "append_le expects an integral type");
        using U = std::make_unsigned_t<T>;
        U v = static_cast<U>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xffu));
        }
    }

    template <typename T>
    auto read_le_value(std::span<const uint8_t> bytes, size_t& offset) -> std::optional<T>
    {
        static_assert(std::is_integral_v<T>, "read_le_value expects an integral type");
        if (offset > bytes.size() || bytes.size() - offset < sizeof(T))
        {
            return std::nullopt;
        }

        using U = std::make_unsigned_t<T>;
        U value{};
        for (size_t i = 0; i < sizeof(T); ++i)
        {
            value |= static_cast<U>(bytes[offset + i]) << (i * 8);
        }
        offset += sizeof(T);
        return static_cast<T>(value);
    }
}
