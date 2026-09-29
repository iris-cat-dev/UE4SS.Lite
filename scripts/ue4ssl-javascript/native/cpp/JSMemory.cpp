#include "JSInternal.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <SigScanner/SinglePassSigScanner.hpp>

namespace RC::JSScript
{
    JSValue js_sig_scan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "SigScan requires a signature string argument");

        const char* sig_str = JS_ToCString(ctx, argv[0]);
        if (!sig_str)
            return JS_ThrowTypeError(ctx, "Invalid signature string");

        std::string sig_copy(sig_str);
        JS_FreeCString(ctx, sig_str);

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] SigScan: scanning for pattern '{}'\n"),
            std::wstring(sig_copy.begin(), sig_copy.end()));

        try
        {
            intptr_t address = 0;
            SignatureContainer signature_container{
                {{sig_copy}},
                [&](const SignatureContainer& self) {
                    address = reinterpret_cast<intptr_t>(self.get_match_address());
                    return true;
                },
                [](SignatureContainer& self) {},
            };

            SinglePassScanner::SignatureContainerMap signature_containers = {
                {ScanTarget::MainExe, {signature_container}},
            };

            SinglePassScanner::start_scan(signature_containers);

            if (address == 0)
            {
                Output::send<LogLevel::Warning>(STR("[UE4SSL.JavaScript] SigScan: pattern not found\n"));
                return JS_NULL;
            }

            Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] SigScan: found at 0x{:X}\n"), static_cast<uintptr_t>(address));
            return JS_NewBigInt64(ctx, static_cast<int64_t>(address));
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] SigScan exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "SigScan exception: %s", e.what());
        }
        catch (...)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] SigScan: unknown exception\n"));
            return JS_ThrowInternalError(ctx, "SigScan: unknown exception during scan");
        }
    }

    // SEH-isolated byte patch: read old + write new (no C++ objects - satisfies MSVC C2712)
    static int seh_patch_byte_inner(uint8_t* target, uint8_t new_val, uint8_t* out_old_val)
    {
        __try
        {
            *out_old_val = *target;
            *target = new_val;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    JSValue js_patch_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        JSMod* mod = get_js_mod(ctx);
        if (mod && mod->is_safe_mode())
            return JS_ThrowInternalError(ctx, "PatchByte is disabled while engine is in safe mode");

        if (argc < 2)
            return JS_ThrowTypeError(ctx, "PatchByte requires 2 arguments: address (BigInt), value (number)");

        int64_t addr64;
        if (JS_ToBigInt64(ctx, &addr64, argv[0]))
            return JS_ThrowTypeError(ctx, "PatchByte: first argument must be a BigInt address");

        int32_t value;
        if (JS_ToInt32(ctx, &value, argv[1]))
            return JS_ThrowTypeError(ctx, "PatchByte: second argument must be a number (0-255)");

        if (value < 0 || value > 255)
            return JS_ThrowRangeError(ctx, "PatchByte: value must be in range 0-255");

        auto* target = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(addr64));

        DWORD old_protect;
        if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old_protect))
            return JS_ThrowInternalError(ctx, "PatchByte: VirtualProtect failed at 0x%llX", static_cast<unsigned long long>(addr64));

        uint8_t old_value = 0;
        if (!seh_patch_byte_inner(target, static_cast<uint8_t>(value), &old_value))
        {
            VirtualProtect(target, 1, old_protect, &old_protect);
            return JS_ThrowInternalError(ctx, "PatchByte: access violation at 0x%llX", static_cast<unsigned long long>(addr64));
        }

        VirtualProtect(target, 1, old_protect, &old_protect);

        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] PatchByte: 0x{:X}  0x{:02X} -> 0x{:02X}\n"),
            static_cast<uintptr_t>(addr64), old_value, static_cast<uint8_t>(value));

        return JS_TRUE;
    }

    // SEH-protected byte read from arbitrary address
    JSValue js_read_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "ReadByte requires 1 argument: address (BigInt)");

        int64_t addr64;
        if (JS_ToBigInt64(ctx, &addr64, argv[0]))
            return JS_ThrowTypeError(ctx, "ReadByte: argument must be a BigInt address");

        auto* target = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(addr64));

        __try
        {
            return JS_NewInt32(ctx, *target);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return JS_ThrowInternalError(ctx, "ReadByte: access violation at 0x%llX", static_cast<unsigned long long>(addr64));
        }
    }

} // namespace RC::JSScript
