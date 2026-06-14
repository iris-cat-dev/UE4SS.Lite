#ifndef UE4SS_REWRITTEN_FILEDEVICE_HPP
#define UE4SS_REWRITTEN_FILEDEVICE_HPP

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <DynamicOutput/Common.hpp>
#include <DynamicOutput/Macros.hpp>
#include <DynamicOutput/OutputDevice.hpp>

extern "C"
{
    auto ue4ssl_native_file_prepare_append(const uint16_t* path, uint8_t truncate_existing) -> uint8_t;
    auto ue4ssl_native_file_append_utf16(const uint16_t* path, const uint16_t* data, size_t len) -> uint8_t;
}

namespace RC::Output
{
    // Note: For FileDevice, 'Output::sends()' must only be called after 'FileDevice::set_file_name_and_path()' has been called

    // Less simple class that outputs to a file on a drive
    // Behavior defined as:
    // Create all necessary directories
    // Create file if it doesn't exist
    // Open a file in append mode and keep it open until ~FileDevice
    // Whether to allow the file to be opened by other applications is not defined
    // Write one std::wstring to the file
    class FileDevice : public OutputDevice
    {
      private:
        std::filesystem::path m_file_name_and_path;

      protected:
        bool m_always_create_file{};

      public:
#if ENABLE_OUTPUT_DEVICE_DEBUG_MODE
        FileDevice()
        {
            std::puts("FileDevice opening...");
        }

        ~FileDevice() override
        {
            std::puts("FileDevice closing...");
        }
#else
        ~FileDevice() override
        {
        }
#endif

  private:
    auto native_path() const -> const uint16_t*
    {
        return reinterpret_cast<const uint16_t*>(m_file_name_and_path.c_str());
    }

    auto start_device() const -> void
    {
        static_assert(sizeof(std::filesystem::path::value_type) == sizeof(uint16_t));
        if (ue4ssl_native_file_prepare_append(native_path(), static_cast<uint8_t>(m_always_create_file)) == 0)
        {
            THROW_INTERNAL_OUTPUT_ERROR("[FileDevice::start_device] Failed to prepare log file for appending")
        }

        m_is_device_ready = true;
    }

  public:
    // OutputDevice Interface -> START
    // Due to the design of the Output system the opening of the file is done in receive instead of in the constructor
    // It's opened only once and stays open until the Output object (not the device) leaves scope
    // The destructor is responsible for closing the file
    auto receive(RC::StringViewType fmt) const -> void override
    {
        if (!m_is_device_ready)
        {
            start_device();
        }

        // Do file output stuff here
        // File should already be open & be ready for writing (happens in constructor)

        const auto formatted = m_formatter(fmt);
        if (ue4ssl_native_file_append_utf16(native_path(), reinterpret_cast<const uint16_t*>(formatted.data()), formatted.size()) == 0)
        {
            THROW_INTERNAL_OUTPUT_ERROR("[FileDevice::receive] Failed to append to log file")
        }
    }
    // OutputDevice Interface -> END

    auto set_file_name_and_path(const RC::StringType& file_name_and_path) -> void
    {
        m_file_name_and_path = file_name_and_path;
    }
};
} // namespace RC::Output

#endif // UE4SS_REWRITTEN_FILEDEVICE_HPP
