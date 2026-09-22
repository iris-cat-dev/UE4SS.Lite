#pragma once

#ifndef UE4SSL_RUNTIME_FFI_HPP
#define UE4SSL_RUNTIME_FFI_HPP

#include <cstddef>
#include <cstdint>

#include <Compat/GeneratedRustCoreAbi.hpp>
extern "C" {
uint8_t ue4ssl_runtime_start(const RC::Compat::RustCore::RuntimeConfig*);
void ue4ssl_runtime_request_shutdown();
uint8_t ue4ssl_install_dll_notifications();
void ue4ssl_stop_dll_notifications();
uint8_t ue4ssl_runtime_shutdown();
uint8_t ue4ssl_runtime_reinstall();
uint8_t ue4ssl_runtime_queue_event(RC::Compat::RustCore::RuntimeEvent);
uint8_t ue4ssl_runtime_queue_empty();
uintptr_t ue4ssl_runtime_current_owner();
void ue4ssl_runtime_cancel_owner(uintptr_t);
void ue4ssl_runtime_dll_load(RC::Compat::RustCore::SliceU16);
void* ue4ssl_runtime_find_mod(RC::Compat::RustCore::SliceU16, uint8_t, uint8_t);
RC::Compat::RustCore::CppModRuntimeStatus ue4ssl_runtime_mod_status(uint64_t);
void ue4ssl_runtime_mod_action(uint64_t, uint32_t, uint8_t);
void ue4ssl_runtime_mod_dll_load(uint64_t, RC::Compat::RustCore::SliceU16);
}
#endif
