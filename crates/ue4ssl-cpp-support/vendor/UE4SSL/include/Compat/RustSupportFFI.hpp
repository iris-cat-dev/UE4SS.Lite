#pragma once

#ifndef UE4SSL_SUPPORT_FFI_HPP
#define UE4SSL_SUPPORT_FFI_HPP

#include <cstddef>
#include <cstdint>

#include <Compat/GeneratedRustCoreAbi.hpp>
extern "C" {
RC::Compat::RustCore::OwnedString ue4ssl_native_file_read_to_string(const uint16_t*);
void ue4ssl_native_file_free_string(RC::Compat::RustCore::OwnedString);
uint8_t ue4ssl_native_file_prepare_append(const uint16_t*, uint8_t);
uint8_t ue4ssl_native_file_append_utf16(const uint16_t*, const uint16_t*, size_t);
uint8_t ue4ssl_native_input_is_key_down(int32_t);
uint8_t ue4ssl_native_input_foreground_class_matches(const uint16_t*);
uintptr_t ue4ssl_native_input_owner_enter(uintptr_t);
uintptr_t ue4ssl_native_input_current_owner();
void ue4ssl_native_input_owner_leave(uintptr_t);
void* ue4ssl_native_input_handler_new();
void ue4ssl_native_input_handler_destroy(void*);
void ue4ssl_native_input_handler_add_window_class(void*, const uint16_t*);
void ue4ssl_native_input_handler_register_keydown_event(void*, uint8_t, const uint8_t*, size_t, void (*)(void*), void*);
uint64_t ue4ssl_native_input_handler_register_keydown_event_v2(void*, uint8_t, const uint8_t*, size_t, uintptr_t, uint8_t, void (*)(void*), void*, void (*)(void*));
uint8_t ue4ssl_native_input_handler_unregister_event(void*, uint64_t);
size_t ue4ssl_native_input_handler_unregister_owner(void*, uintptr_t);
size_t ue4ssl_native_input_handler_unregister_kind(void*, uint8_t);
uint8_t ue4ssl_native_input_handler_is_keydown_event_registered(void*, uint8_t, const uint8_t*, size_t);
void ue4ssl_native_input_handler_process_event(void*);
uint8_t ue4ssl_native_input_handler_get_allow_input(void*);
void ue4ssl_native_input_handler_set_allow_input(void*, uint8_t);
uint64_t ue4ssl_native_log_group_new();
uint64_t ue4ssl_native_log_default_group();
void ue4ssl_native_log_group_destroy(uint64_t);
uint8_t ue4ssl_native_log_group_clear(uint64_t);
uint8_t ue4ssl_native_log_group_add(uint64_t, void*, uint8_t (*)(void*, const uint16_t*, size_t, int32_t), void (*)(void*));
uint8_t ue4ssl_native_log_group_send(uint64_t, const uint16_t*, size_t, int32_t);
void* ue4ssl_native_log_group_find(uint64_t, void*, void* (*)(void*, void*));
void ue4ssl_native_log_set_default_level(int32_t);
int32_t ue4ssl_native_log_get_default_level();
uint64_t ue4ssl_native_log_file_new();
uint8_t ue4ssl_native_log_file_set_path(uint64_t, const uint16_t*, uint8_t);
uint8_t ue4ssl_native_log_file_write(uint64_t, const uint16_t*, size_t);
uint8_t ue4ssl_native_log_sink_close(uint64_t);
uint64_t ue4ssl_native_log_console_new();
uint8_t ue4ssl_native_log_console_write(uint64_t, const uint16_t*, size_t, int32_t);
}
#endif
