#ifndef UE4SS_REWRITTEN_FILEDEVICE_HPP
#define UE4SS_REWRITTEN_FILEDEVICE_HPP

#include <DynamicOutput/Common.hpp>
#include <DynamicOutput/Macros.hpp>
#include <DynamicOutput/OutputDevice.hpp>
#include <Compat/RustSupportFFI.hpp>

namespace RC::Output
{
    // Rust opens lazily on the first write and owns the file until close.
    class FileDevice : public OutputDevice
    {
        uint64_t m_sink{};
      protected:
        bool m_always_create_file{};
      public:
        FileDevice() : m_sink(ue4ssl_native_log_file_new())
        {
            if (!m_sink)
            {
                THROW_INTERNAL_OUTPUT_ERROR("[FileDevice] Failed to create log device")
            }
        }
        FileDevice(const FileDevice&) = delete;
        auto operator=(const FileDevice&) -> FileDevice& = delete;
        ~FileDevice() override
        {
            // Destruction never throws; flush/close failures use the existing error flag.
            if (!ue4ssl_native_log_sink_close(m_sink)) { Internal::internal_error = true; }
        }
        auto receive(RC::StringViewType content) const -> void override
        {
            const auto formatted = m_formatter(content);
            if (!ue4ssl_native_log_file_write(m_sink, reinterpret_cast<const uint16_t*>(formatted.data()), formatted.size()))
            {
                THROW_INTERNAL_OUTPUT_ERROR("[FileDevice::receive] Failed to append to log file")
            }
        }
        auto set_file_name_and_path(const RC::StringType& path) -> void
        {
            if (!ue4ssl_native_log_file_set_path(m_sink, reinterpret_cast<const uint16_t*>(path.c_str()), static_cast<uint8_t>(m_always_create_file)))
            {
                THROW_INTERNAL_OUTPUT_ERROR("[FileDevice::set_file_name_and_path] Failed to configure log file")
            }
        }
    };
}

#endif // UE4SS_REWRITTEN_FILEDEVICE_HPP
