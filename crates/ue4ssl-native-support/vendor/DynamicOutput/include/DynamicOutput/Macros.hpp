#ifndef DYNAMIC_OUTPUT_MACROS_HPP
#define DYNAMIC_OUTPUT_MACROS_HPP

#include <atomic>
#include <stdexcept>

#define ENABLE_OUTPUT_DEVICE_DEBUG_MODE 0

namespace RC::Output::Internal
{
    inline std::atomic_bool internal_error{};
}

#define THROW_INTERNAL_OUTPUT_ERROR(msg)                                                                                                                        \
    RC::Output::Internal::internal_error = true;                                                                                                                \
    throw std::runtime_error{msg};


#endif // DYNAMIC_OUTPUT_MACROS_HPP
