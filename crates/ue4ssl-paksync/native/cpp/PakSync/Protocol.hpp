#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace RC::PakSync
{
    enum class FrameKind : uint16_t
    {
        Manifest = 1,
        Chunk = 2,
        Ack = 3,
        Nak = 4,
        Resume = 5,
        Done = 6,
        ManifestEnd = 7,
        NoWork = 8,
    };

#pragma pack(push, 1)
    struct FrameHeader
    {
        uint32_t magic{};
        uint16_t version{};
        uint16_t kind{};
        uint64_t session_id{};
        std::array<uint8_t, 32> pak_hash{};
        uint32_t seq{};
        uint32_t total{};
        uint32_t payload_size{};
        uint32_t payload_crc{};
    };
#pragma pack(pop)

    static_assert(sizeof(FrameHeader) == 64, "FrameHeader must stay wire-stable");

    auto crc32(std::span<const uint8_t> bytes) -> uint32_t;
    auto encode_frame(
            FrameKind kind,
            uint64_t session_id,
            const std::array<uint8_t, 32>& pak_hash,
            uint32_t seq,
            uint32_t total,
            std::span<const uint8_t> payload) -> std::vector<uint8_t>;
    auto decode_frame(std::span<const uint8_t> bytes) -> std::optional<std::pair<FrameHeader, std::span<const uint8_t>>>;
    auto find_paksync_frame(std::span<const uint8_t> bytes) -> std::optional<std::span<const uint8_t>>;
}
