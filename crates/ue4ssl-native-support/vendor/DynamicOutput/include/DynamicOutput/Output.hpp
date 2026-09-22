#ifndef UE4SS_REWRITTEN_OUTPUT_HPP
#define UE4SS_REWRITTEN_OUTPUT_HPP

#include <format>
#include <memory>
#include <typeinfo>
#include <utility>
#include <DynamicOutput/Common.hpp>
#include <DynamicOutput/Macros.hpp>
#include <DynamicOutput/OutputDevice.hpp>
#include <Compat/RustSupportFFI.hpp>

namespace RC::Output
{
    namespace detail
    {
        template <typename... FmtArgs>
        auto format_message(RC::StringViewType content, FmtArgs&&... args) -> RC::StringType
        {
#if RC_IS_ANSI == 1
            return std::vformat(content, std::make_format_args(args...));
#else
            return std::vformat(content, std::make_wformat_args(args...));
#endif
        }
        RC_DYNOUT_API auto add_device(uint64_t group, std::unique_ptr<OutputDevice> device) -> OutputDevice&;
        RC_DYNOUT_API auto send_to_group(uint64_t group, RC::StringViewType content, int32_t level) -> void;

        template <typename DeviceType>
        auto get_device(uint64_t group) -> DeviceType&
        {
            auto* device = static_cast<DeviceType*>(ue4ssl_native_log_group_find(group, nullptr, [](void* data, void*) noexcept -> void* {
                return dynamic_cast<DeviceType*>(static_cast<OutputDevice*>(data));
            }));
            if (!device)
            {
                THROW_INTERNAL_OUTPUT_ERROR(std::format("[Output::get_device] Unable to find device of type: {}", typeid(DeviceType).name()))
            }
            return *device;
        }
    }

    template <typename SupposedEnum>
    concept EnumType = std::is_enum_v<SupposedEnum>;

    auto RC_DYNOUT_API has_internal_error() -> bool;

    class DefaultTargets
    {
      public:
        RC_DYNOUT_API auto static get_group() -> uint64_t;
        RC_DYNOUT_API auto static set_default_log_level(int32_t level) -> void;
        RC_DYNOUT_API auto static get_default_log_level() -> int32_t;
        RC_DYNOUT_API auto static close_all_default_devices() -> void;
    };

    template <typename OutputDeviceType, typename... OutputDeviceTypes>
    class Targets
    {
        uint64_t m_group{};
      public:
        Targets() : m_group(ue4ssl_native_log_group_new())
        {
            try
            {
                detail::add_device(m_group, std::make_unique<OutputDeviceType>());
                (detail::add_device(m_group, std::make_unique<OutputDeviceTypes>()), ...);
            }
            catch (...)
            {
                ue4ssl_native_log_group_destroy(m_group);
                throw;
            }
        }
        Targets(const Targets&) = delete;
        auto operator=(const Targets&) -> Targets& = delete;
        ~Targets() { ue4ssl_native_log_group_destroy(m_group); }

        template <typename... FmtArgs>
        auto send(RC::StringViewType content, FmtArgs... args) -> void
        {
            detail::send_to_group(m_group, detail::format_message(content, args...), 0);
        }
        template <EnumType OptionalArg, typename... FmtArgs>
        auto send(RC::StringViewType content, OptionalArg level, FmtArgs... args) -> void
        {
            if constexpr (sizeof...(args) == 0) { detail::send_to_group(m_group, content, static_cast<int32_t>(level)); }
            else { detail::send_to_group(m_group, detail::format_message(content, args...), static_cast<int32_t>(level)); }
        }
        auto send(const RC::StringType& content) -> void { detail::send_to_group(m_group, content, 0); }
        template <int32_t level, typename... FmtArgs>
        auto send(RC::StringViewType content, FmtArgs... args) -> void
        {
            if constexpr (sizeof...(args) == 0) { detail::send_to_group(m_group, content, level); }
            else { detail::send_to_group(m_group, detail::format_message(content, args...), level); }
        }
        template <typename DeviceType>
        auto get_device() -> DeviceType& { return detail::get_device<DeviceType>(m_group); }
    };

    template <typename DeviceType>
    auto set_default_devices() -> DeviceType&
    {
        return static_cast<DeviceType&>(detail::add_device(DefaultTargets::get_group(), std::make_unique<DeviceType>()));
    }
    template <typename DeviceType, typename DeviceTypeWorkaround, typename... DeviceTypes>
    auto set_default_devices() -> void
    {
        set_default_devices<DeviceType>();
        set_default_devices<DeviceTypeWorkaround, DeviceTypes...>();
    }
    auto inline clear_all_default_devices() -> void { DefaultTargets::close_all_default_devices(); }
    template <int32_t level>
    auto set_default_log_level() -> void { DefaultTargets::set_default_log_level(level); }

    auto RC_DYNOUT_API send(RC::StringViewType content) -> void;
    template <typename... FmtArgs>
    auto send(RC::StringViewType content, FmtArgs... args) -> void
    {
        detail::send_to_group(DefaultTargets::get_group(), detail::format_message(content, args...), 0);
    }
    template <EnumType OptionalArg, typename... FmtArgs>
    auto send(RC::StringViewType content, OptionalArg level, FmtArgs... args) -> void
    {
        if constexpr (sizeof...(args) == 0) { detail::send_to_group(DefaultTargets::get_group(), content, static_cast<int32_t>(level)); }
        else { detail::send_to_group(DefaultTargets::get_group(), detail::format_message(content, args...), static_cast<int32_t>(level)); }
    }
    template <int32_t level, typename... FmtArgs>
    auto send(RC::StringViewType content, FmtArgs... args) -> void
    {
        if constexpr (sizeof...(args) == 0) { detail::send_to_group(DefaultTargets::get_group(), content, level); }
        else { detail::send_to_group(DefaultTargets::get_group(), detail::format_message(content, args...), level); }
    }
    template <typename DeviceType>
    auto get_device() -> DeviceType& { return detail::get_device<DeviceType>(DefaultTargets::get_group()); }
    auto RC_DYNOUT_API close_all_default_devices() -> void;

    class RC_DYNOUT_API Lock
    {
        const OutputDevice* m_output_device{};
      public:
        Lock();
        explicit Lock(const OutputDevice* device);
        Lock(const Lock&) = delete;
        auto operator=(const Lock&) -> Lock& = delete;
        ~Lock();
    };
}

#endif // UE4SS_REWRITTEN_OUTPUT_HPP
