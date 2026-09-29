#include "JSInternal.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>

namespace RC::JSScript
{
    namespace
    {
        auto normalize_utf8_path(std::string value) -> std::string
        {
            for (auto& ch : value)
            {
                if (ch == '\\')
                {
                    ch = '/';
                }
            }
            return value;
        }
    } // namespace

    JSValue js_read_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 1)
            return JS_ThrowTypeError(ctx, "readFile requires 1 argument: path");

        const char* path_str = JS_ToCString(ctx, argv[0]);
        if (!path_str)
            return JS_ThrowTypeError(ctx, "Invalid path argument");

        std::string path(path_str);
        JS_FreeCString(ctx, path_str);

        try
        {
            std::filesystem::path file_path(path);
            if (!std::filesystem::exists(file_path))
                return JS_NULL;

            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open())
                return JS_NULL;

            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            return JS_NewStringLen(ctx, content.c_str(), content.size());
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] readFile exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NULL;
        }
        catch (...)
        {
            return JS_NULL;
        }
    }

    JSValue js_write_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "writeFile requires 2 arguments: path, content");

        const char* path_str = JS_ToCString(ctx, argv[0]);
        if (!path_str)
            return JS_ThrowTypeError(ctx, "Invalid path argument");

        size_t content_len = 0;
        const char* content_str = JS_ToCStringLen(ctx, &content_len, argv[1]);
        if (!content_str)
        {
            JS_FreeCString(ctx, path_str);
            return JS_ThrowTypeError(ctx, "Invalid content argument");
        }

        std::string path(path_str);
        JS_FreeCString(ctx, path_str);

        try
        {
            std::filesystem::path file_path(path);
            auto parent = file_path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent))
            {
                std::filesystem::create_directories(parent);
            }

            std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                JS_FreeCString(ctx, content_str);
                return JS_NewBool(ctx, false);
            }

            file.write(content_str, static_cast<std::streamsize>(content_len));
            JS_FreeCString(ctx, content_str);
            file.close();

            return JS_NewBool(ctx, true);
        }
        catch (const std::exception& e)
        {
            JS_FreeCString(ctx, content_str);
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] writeFile exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_NewBool(ctx, false);
        }
        catch (...)
        {
            JS_FreeCString(ctx, content_str);
            return JS_NewBool(ctx, false);
        }
    }

    JSValue js_get_mods_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        try
        {
            JSMod* mod = get_js_mod(ctx);
            if (!mod)
                return JS_ThrowInternalError(ctx, "Could not get JSMod instance");

            std::string utf8 = wide_to_utf8(mod->get_mods_directory().wstring());
            // Normalize to forward slashes for JS consistency
            for (auto& c : utf8)
            {
                if (c == '\\') c = '/';
            }
            return JS_NewString(ctx, utf8.c_str());
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "getModsDirectory failed");
        }
    }

    JSValue js_get_game_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        try
        {
            auto exe_dir_str = UE4SSProgram::get_program().get_game_executable_directory();
            // exe_dir is typically FSD/Binaries/Win64/ - go up 2 levels to get FSD/
            std::filesystem::path exe_dir(exe_dir_str);
            std::filesystem::path game_dir = exe_dir.parent_path().parent_path();

            std::string utf8 = wide_to_utf8(game_dir.wstring());
            for (auto& c : utf8)
            {
                if (c == '\\') c = '/';
            }
            return JS_NewString(ctx, utf8.c_str());
        }
        catch (const std::exception& e)
        {
            Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] getGameDirectory exception: {}\n"),
                std::wstring(e.what(), e.what() + strlen(e.what())));
            return JS_ThrowInternalError(ctx, "getGameDirectory failed");
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "getGameDirectory failed");
        }
    }

} // namespace RC::JSScript
