#define NOMINMAX

#include "PakSync/RuntimeUtils.hpp"

#include "PakSync/Protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

#include <Windows.h>
#include <bcrypt.h>
#include <winnt.h>

#include <Unreal/FString.hpp>

namespace RC::PakSync
{
    auto widen(std::string_view value) -> std::wstring
    {
        return std::wstring{value.begin(), value.end()};
    }

    auto bytes_to_hex(std::span<const uint8_t> bytes) -> std::wstring
    {
        static constexpr wchar_t Hex[] = L"0123456789abcdef";
        std::wstring out{};
        out.reserve(bytes.size() * 2);
        for (const auto byte : bytes)
        {
            out.push_back(Hex[(byte >> 4) & 0x0f]);
            out.push_back(Hex[byte & 0x0f]);
        }
        return out;
    }

    auto fstring_to_wstring(const Unreal::FString& value) -> std::wstring
    {
        const auto length = value.Len();
        if (length <= 0)
        {
            return {};
        }

        const auto* chars = *value;
        if (!chars)
        {
            return {};
        }

        return std::wstring(chars, chars + length);
    }

    auto get_current_module_path() -> std::filesystem::path
    {
        HMODULE module{};
        if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&get_current_module_path),
                    &module))
        {
            return {};
        }

        std::wstring buffer(MAX_PATH, L'\0');
        DWORD size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        while (size == buffer.size())
        {
            buffer.resize(buffer.size() * 2);
            size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        }
        buffer.resize(size);
        return std::filesystem::path{buffer};
    }

    auto utf8_from_wide(std::wstring_view value) -> std::string
    {
        if (value.empty())
        {
            return {};
        }

        const int needed = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
        if (needed <= 0)
        {
            return {};
        }

        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                needed,
                nullptr,
                nullptr);
        return out;
    }

    auto wide_from_utf8(std::span<const uint8_t> value) -> std::wstring
    {
        if (value.empty())
        {
            return {};
        }

        const int needed = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(value.data()),
                static_cast<int>(value.size()),
                nullptr,
                0);
        if (needed <= 0)
        {
            return widen(std::string_view{reinterpret_cast<const char*>(value.data()), value.size()});
        }

        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(value.data()),
                static_cast<int>(value.size()),
                out.data(),
                needed);
        return out;
    }

    auto sanitize_filename(std::wstring value) -> std::wstring
    {
        for (auto& ch : value)
        {
            if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' ||
                ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            {
                ch = L'_';
            }
        }
        if (value.empty())
        {
            value = L"received.pak";
        }
        if (!value.ends_with(L".pak"))
        {
            value += L".pak";
        }
        return value;
    }

    auto sync_phase_name(SyncPhase phase) -> const wchar_t*
    {
        switch (phase)
        {
        case SyncPhase::WaitingForConnection:
            return L"WaitingForConnection";
        case SyncPhase::AwaitingHostManifest:
            return L"AwaitingHostManifest";
        case SyncPhase::ControlChannelReady:
            return L"ControlChannelReady";
        case SyncPhase::ManifestAnnounced:
            return L"ManifestAnnounced";
        case SyncPhase::TransferPending:
            return L"TransferPending";
        case SyncPhase::Receiving:
            return L"Receiving";
        case SyncPhase::MountPending:
            return L"MountPending";
        case SyncPhase::AssetRegistryPending:
            return L"AssetRegistryPending";
        case SyncPhase::ReadyToTravel:
            return L"ReadyToTravel";
        case SyncPhase::NoPakWork:
            return L"NoPakWork";
        case SyncPhase::MountedPartial:
            return L"MountedPartial";
        default:
            return L"Unknown";
        }
    }

    auto manifest_payload(const PakManifest& manifest) -> std::vector<uint8_t>
    {
        const auto name_utf8 = utf8_from_wide(manifest.name);
        std::vector<uint8_t> payload{};
        payload.reserve(2 + name_utf8.size() + sizeof(uint64_t) + sizeof(uint32_t) * 2);
        append_le<uint16_t>(payload, static_cast<uint16_t>(std::min<size_t>(name_utf8.size(), 0xffff)));
        payload.insert(payload.end(), name_utf8.begin(), name_utf8.begin() + std::min<size_t>(name_utf8.size(), 0xffff));
        append_le<uint64_t>(payload, manifest.size);
        append_le<uint32_t>(payload, manifest.chunk_size);
        append_le<uint32_t>(payload, manifest.chunk_count);
        return payload;
    }

    auto session_id_for_hash(const std::array<uint8_t, 32>& hash) -> uint64_t
    {
        return static_cast<uint64_t>(crc32(std::span<const uint8_t>{hash.data(), hash.size()}));
    }

    auto chunk_payload(const PakManifest& manifest, uint32_t seq) -> std::optional<std::vector<uint8_t>>
    {
        if (seq >= manifest.chunk_count)
        {
            return std::nullopt;
        }

        std::ifstream file{manifest.path, std::ios::binary};
        if (!file)
        {
            return std::nullopt;
        }

        const uint64_t offset = static_cast<uint64_t>(seq) * manifest.chunk_size;
        const uint64_t remaining = manifest.size > offset ? manifest.size - offset : 0;
        const auto bytes_to_read = static_cast<size_t>(std::min<uint64_t>(remaining, manifest.chunk_size));
        std::vector<uint8_t> payload(bytes_to_read);
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (file.gcount() != static_cast<std::streamsize>(payload.size()))
        {
            return std::nullopt;
        }
        return payload;
    }

    auto sha256_file(const std::filesystem::path& path) -> std::optional<std::array<uint8_t, 32>>
    {
        std::ifstream file{path, std::ios::binary};
        if (!file)
        {
            return std::nullopt;
        }

        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        DWORD object_length{};
        DWORD data_length{};
        std::vector<uint8_t> hash_object{};
        std::array<uint8_t, 32> digest{};

        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        {
            return std::nullopt;
        }

        auto close_algorithm = [&]() {
            if (algorithm)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
            }
        };

        if (BCryptGetProperty(algorithm,
                              BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_length),
                              sizeof(object_length),
                              &data_length,
                              0) < 0)
        {
            close_algorithm();
            return std::nullopt;
        }

        hash_object.resize(object_length);
        if (BCryptCreateHash(algorithm, &hash, hash_object.data(), object_length, nullptr, 0, 0) < 0)
        {
            close_algorithm();
            return std::nullopt;
        }

        std::array<char, 64 * 1024> buffer{};
        while (file)
        {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto read = file.gcount();
            if (read > 0)
            {
                if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(read), 0) < 0)
                {
                    BCryptDestroyHash(hash);
                    close_algorithm();
                    return std::nullopt;
                }
            }
        }

        const auto ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
        BCryptDestroyHash(hash);
        close_algorithm();
        if (!ok)
        {
            return std::nullopt;
        }

        return digest;
    }

    auto build_manifest(const std::filesystem::path& path, uint32_t chunk_size) -> std::optional<PakManifest>
    {
        std::error_code ec{};
        const auto size = std::filesystem::file_size(path, ec);
        if (ec)
        {
            return std::nullopt;
        }

        auto hash = sha256_file(path);
        if (!hash)
        {
            return std::nullopt;
        }

        PakManifest manifest{};
        manifest.path = path;
        manifest.name = path.filename().wstring();
        manifest.size = size;
        manifest.chunk_size = chunk_size;
        manifest.chunk_count = static_cast<uint32_t>((size + chunk_size - 1) / chunk_size);
        manifest.sha256 = *hash;
        return manifest;
    }

    auto is_readable_memory(const void* ptr, size_t size) -> bool
    {
        if (!ptr || size == 0)
        {
            return false;
        }

        auto* cursor = static_cast<const uint8_t*>(ptr);
        size_t remaining = size;
        while (remaining > 0)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(cursor, &mbi, sizeof(mbi)))
            {
                return false;
            }
            if (mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            {
                return false;
            }

            const auto region_end = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
            const auto available = static_cast<size_t>(region_end - cursor);
            const auto consumed = std::min(remaining, available);
            if (consumed == 0)
            {
                return false;
            }
            cursor += consumed;
            remaining -= consumed;
        }
        return true;
    }

    auto copy_bunch_payload_bytes(void* bunch) -> std::optional<std::vector<uint8_t>>
    {
        constexpr size_t BunchDataOffset = 0x98;
        constexpr size_t BunchNumBitsOffset = 0xA8;
        constexpr size_t MaxFrameBytes = 4 * 1024 * 1024;

        if (!bunch || !is_readable_memory(bunch, BunchNumBitsOffset + sizeof(uint64_t)))
        {
            return std::nullopt;
        }

        const auto* base = static_cast<const uint8_t*>(bunch);
        auto* data = *reinterpret_cast<uint8_t* const*>(base + BunchDataOffset);
        const auto num_bits = *reinterpret_cast<const uint64_t*>(base + BunchNumBitsOffset);
        const auto num_bytes = static_cast<size_t>((num_bits + 7u) / 8u);
        if (!data || num_bytes < sizeof(FrameHeader) || num_bytes > MaxFrameBytes ||
            !is_readable_memory(data, num_bytes))
        {
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(num_bytes);
        std::memcpy(bytes.data(), data, bytes.size());
        return bytes;
    }

    auto append_bunch_bits(void* bunch, std::span<const uint8_t> bytes) -> bool
    {
        using SerializeBitsFn = void(__fastcall*)(void* archive, void* bits, int64_t length_bits);
        if (!bunch || bytes.empty() || !is_readable_memory(bunch, sizeof(void*)))
        {
            return false;
        }

        auto** vtable = *reinterpret_cast<void***>(bunch);
        if (!vtable || !is_readable_memory(vtable, 0x160))
        {
            return false;
        }

        auto serialize_bits = reinterpret_cast<SerializeBitsFn>(vtable[0x158 / sizeof(void*)]);
        if (!serialize_bits)
        {
            return false;
        }

        serialize_bits(bunch, const_cast<uint8_t*>(bytes.data()), static_cast<int64_t>(bytes.size() * 8));
        return true;
    }
}
