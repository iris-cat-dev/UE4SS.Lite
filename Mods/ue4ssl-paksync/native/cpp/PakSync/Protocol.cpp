#include "PakSync/Protocol.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace RC::PakSync
{
    namespace
    {
        constexpr uint32_t FrameMagic = 0x4B535055; // UPSK, little-endian marker for UE4SSL PakSync
        constexpr uint16_t FrameVersion = 1;

        auto read_u32_le(const uint8_t* ptr) -> uint32_t
        {
            return static_cast<uint32_t>(ptr[0]) |
                   (static_cast<uint32_t>(ptr[1]) << 8u) |
                   (static_cast<uint32_t>(ptr[2]) << 16u) |
                   (static_cast<uint32_t>(ptr[3]) << 24u);
        }
    }

    auto crc32(std::span<const uint8_t> bytes) -> uint32_t
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (const uint8_t byte : bytes)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                const uint32_t mask = 0u - (crc & 1u);
                crc = (crc >> 1u) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    auto encode_frame(
            FrameKind kind,
            uint64_t session_id,
            const std::array<uint8_t, 32>& pak_hash,
            uint32_t seq,
            uint32_t total,
            std::span<const uint8_t> payload) -> std::vector<uint8_t>
    {
        FrameHeader header{};
        header.magic = FrameMagic;
        header.version = FrameVersion;
        header.kind = static_cast<uint16_t>(kind);
        header.session_id = session_id;
        header.pak_hash = pak_hash;
        header.seq = seq;
        header.total = total;
        header.payload_size = static_cast<uint32_t>(payload.size());
        header.payload_crc = crc32(payload);

        std::vector<uint8_t> out(sizeof(FrameHeader) + payload.size());
        std::memcpy(out.data(), &header, sizeof(header));
        if (!payload.empty())
        {
            std::memcpy(out.data() + sizeof(header), payload.data(), payload.size());
        }
        return out;
    }

    auto decode_frame(std::span<const uint8_t> bytes) -> std::optional<std::pair<FrameHeader, std::span<const uint8_t>>>
    {
        if (bytes.size() < sizeof(FrameHeader))
        {
            return std::nullopt;
        }

        FrameHeader header{};
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.magic != FrameMagic || header.version != FrameVersion)
        {
            return std::nullopt;
        }

        const auto payload_size = static_cast<size_t>(header.payload_size);
        if (bytes.size() < sizeof(FrameHeader) + payload_size)
        {
            return std::nullopt;
        }

        auto payload = bytes.subspan(sizeof(FrameHeader), payload_size);
        if (crc32(payload) != header.payload_crc)
        {
            return std::nullopt;
        }

        return std::pair{header, payload};
    }

    auto find_paksync_frame(std::span<const uint8_t> bytes) -> std::optional<std::span<const uint8_t>>
    {
        const auto max_scan = std::min<size_t>(bytes.size(), 256);
        for (size_t offset = 0; offset + sizeof(FrameHeader) <= max_scan; ++offset)
        {
            if (read_u32_le(bytes.data() + offset) != FrameMagic)
            {
                continue;
            }

            auto candidate = bytes.subspan(offset);
            if (decode_frame(candidate))
            {
                return candidate;
            }
        }
        return std::nullopt;
    }
}
