#define NOMINMAX
#define RC_UNREAL_DISABLE_CLASS_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS

#include <Windows.h>

#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include <Mod/CppUserModBase.hpp>
#include <Unreal/Core/HAL/UnrealMemory.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace MeowChat
{
    using namespace RC;
    using namespace RC::Unreal;

    // UE4 FString 的原始 ABI 兼容视图（24 字节）。RPC Parms 缓冲区里是游戏内存中的
    // FString，直接用 SDK FString 赋值会牵涉析构/共享，因此只改 Data/Num/Max。
    struct RawFString
    {
        wchar_t* Data;
        int32_t Num;
        int32_t Max;
    };

    static HMODULE g_hmod = nullptr;
    static std::wstring g_cfg_path;
    static bool g_cfg_enabled = true;
    static bool g_cfg_sender_enabled = false;
    static std::wstring g_cfg_suffix = L"\u55B5";
    static ULONGLONG g_cfg_check_tick = 0;
    static bool g_cfg_had_file = false;
    static uint64_t g_cfg_last_write = 0;

    static bool g_replace_enabled = false;
    static std::vector<std::pair<std::wstring, std::wstring>> g_replacements;

    static void InitConfigPath()
    {
        wchar_t buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameW(g_hmod, buf, MAX_PATH);
        std::wstring p(buf, n);
        auto pos = p.rfind(L'\\');
        if (pos != std::wstring::npos)
        {
            p = p.substr(0, pos);
        }
        g_cfg_path = p + L"\\config.txt";
    }

    static std::wstring TrimW(const std::wstring& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && iswspace(s[b]))
        {
            ++b;
        }
        while (e > b && iswspace(s[e - 1]))
        {
            --e;
        }
        return s.substr(b, e - b);
    }

    static std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
        {
            return {};
        }
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0)
        {
            return {};
        }
        std::wstring w((size_t)n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    static bool ParseBoolValue(const std::wstring& v, bool fallback)
    {
        if (v.empty())
        {
            return fallback;
        }
        if (_wcsicmp(v.c_str(), L"true") == 0 || v == L"1" || _wcsicmp(v.c_str(), L"yes") == 0 ||
            _wcsicmp(v.c_str(), L"on") == 0)
        {
            return true;
        }
        if (_wcsicmp(v.c_str(), L"false") == 0 || v == L"0" || _wcsicmp(v.c_str(), L"no") == 0 ||
            _wcsicmp(v.c_str(), L"off") == 0)
        {
            return false;
        }
        return fallback;
    }

    static void ResetConfigDefaults()
    {
        g_cfg_enabled = true;
        g_cfg_sender_enabled = false;
        g_cfg_suffix = L"\u55B5";
        g_replace_enabled = false;
        g_replacements.clear();
    }

    static bool ParseReplacePair(const std::wstring& val, std::wstring& outFrom, std::wstring& outTo)
    {
        size_t sep = val.find(L"=>");
        if (sep == std::wstring::npos)
        {
            sep = val.find(L"->");
        }
        if (sep == std::wstring::npos)
        {
            sep = val.find(L"=");
        }
        if (sep == std::wstring::npos)
        {
            return false;
        }
        outFrom = TrimW(val.substr(0, sep));
        size_t skip = 1;
        if (sep + 1 < val.size() && val[sep] == L'=' && val[sep + 1] == L'>')
        {
            skip = 2;
        }
        else if (sep + 1 < val.size() && val[sep] == L'-' && val[sep + 1] == L'>')
        {
            skip = 2;
        }
        outTo = TrimW(val.substr(sep + skip));
        return !outFrom.empty();
    }

    static void LoadConfig()
    {
        ResetConfigDefaults();
        HANDLE h = CreateFileW(
                g_cfg_path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD size = GetFileSize(h, nullptr);
        std::string buf(size ? size : 1, 0);
        DWORD read = 0;
        BOOL ok = (size > 0) && ReadFile(h, &buf[0], size, &read, nullptr);
        CloseHandle(h);
        if (!ok)
        {
            return;
        }

        size_t off = (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
                      (unsigned char)buf[2] == 0xBF)
                             ? 3
                             : 0;
        std::wstring text = Utf8ToWide(buf.substr(off));

        std::vector<std::pair<std::wstring, std::wstring>> tmpReplacements;

        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t nl = text.find(L'\n', pos);
            std::wstring line = TrimW(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            pos = (nl == std::wstring::npos) ? text.size() + 1 : nl + 1;
            if (line.empty() || line[0] == L'#' || line[0] == L';')
            {
                continue;
            }
            if (line[0] == 0xFEFF)
            {
                line = line.substr(1);
            }
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos)
            {
                continue;
            }
            std::wstring key = TrimW(line.substr(0, eq));
            std::wstring val = TrimW(line.substr(eq + 1));
            if (_wcsicmp(key.c_str(), L"Enabled") == 0)
            {
                g_cfg_enabled = ParseBoolValue(val, g_cfg_enabled);
            }
            else if (_wcsicmp(key.c_str(), L"Suffix") == 0)
            {
                g_cfg_suffix = val;
            }
            else if (_wcsicmp(key.c_str(), L"SenderEnabled") == 0)
            {
                g_cfg_sender_enabled = ParseBoolValue(val, g_cfg_sender_enabled);
            }
            else if (_wcsicmp(key.c_str(), L"ReplaceEnabled") == 0)
            {
                g_replace_enabled = ParseBoolValue(val, g_replace_enabled);
            }
            else if (_wcsnicmp(key.c_str(), L"Replace", 7) == 0)
            {
                std::wstring from, to;
                if (ParseReplacePair(val, from, to))
                {
                    tmpReplacements.emplace_back(from, to);
                }
            }
        }

        for (auto& kv : tmpReplacements)
        {
            const std::wstring& from = kv.first;
            const std::wstring& to = kv.second;
            if (!g_cfg_suffix.empty() && from.find(g_cfg_suffix) != std::wstring::npos)
            {
                continue;
            }
            g_replacements.emplace_back(from, to);
        }
    }

    static void TryReloadConfig()
    {
        ULONGLONG now = GetTickCount64();
        if (now - g_cfg_check_tick < 1000)
        {
            return;
        }
        g_cfg_check_tick = now;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(g_cfg_path.c_str(), GetFileExInfoStandard, &fad))
        {
            if (g_cfg_had_file)
            {
                g_cfg_had_file = false;
                ResetConfigDefaults();
            }
            return;
        }
        uint64_t ft = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
        if (g_cfg_had_file && ft == g_cfg_last_write)
        {
            return;
        }
        g_cfg_had_file = true;
        g_cfg_last_write = ft;
        LoadConfig();
    }

    static bool IsMiaoPunc(wchar_t c)
    {
        switch (c)
        {
        case 0xFF01: /* ！ */
        case 0xFF1F: /* ？ */
        case 0x3002: /* 。 */
        case 0xFF0C: /* ， */
        case 0xFF5E: /* ～ */
        case L'!':
        case L'?':
        case L'.':
        case L',':
        case L'~':
        case 0x2026: /* … */
        case 0x3001: /* 、 */
            return true;
        default:
            return false;
        }
    }

    static bool IsPurePlaceholders(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] == L'{')
            {
                size_t j = i + 1;
                while (j < s.size() && (iswalnum(s[j]) || s[j] == L'_'))
                {
                    ++j;
                }
                if (j > i + 1 && j < s.size() && s[j] == L'}')
                {
                    i = j + 1;
                    continue;
                }
            }
            out.push_back(s[i]);
            ++i;
        }
        for (wchar_t c : out)
        {
            if (!iswspace(c))
            {
                return false;
            }
        }
        return true;
    }

    static std::wstring EnsureMiao(const std::wstring& str)
    {
        if (!g_cfg_enabled || g_cfg_suffix.empty())
        {
            return str;
        }
        if (str.empty())
        {
            return str;
        }

        std::wstring suffixCore = g_cfg_suffix;
        {
            size_t cpos = suffixCore.size();
            while (cpos > 0)
            {
                wchar_t ch = suffixCore[cpos - 1];
                if (ch == L' ' || IsMiaoPunc(ch))
                {
                    --cpos;
                }
                else
                {
                    break;
                }
            }
            if (cpos == 0)
            {
                return str;
            }
            suffixCore.resize(cpos);
        }

        std::wstring work = str;
        while (!work.empty() && iswspace(work.back()))
        {
            work.pop_back();
        }
        if (work.empty())
        {
            return str;
        }

        size_t pos = work.size();
        while (pos > 0)
        {
            wchar_t ch = work[pos - 1];
            if (ch == L' ' || IsMiaoPunc(ch))
            {
                --pos;
            }
            else
            {
                break;
            }
        }

        std::wstring main = work.substr(0, pos);
        std::wstring tail = work.substr(pos);

        if (!main.empty() && main.size() >= suffixCore.size() &&
            main.compare(main.size() - suffixCore.size(), suffixCore.size(), suffixCore) == 0)
        {
            return str;
        }

        if (IsPurePlaceholders(work))
        {
            return str;
        }

        return main + g_cfg_suffix + tail;
    }

    static std::wstring ReadRawFString(const RawFString& fs)
    {
        if (!fs.Data || fs.Num <= 0)
        {
            return {};
        }
        return std::wstring(fs.Data, (size_t)(fs.Num - 1));
    }

    static void ApplyReplacements(std::wstring& s)
    {
        if (!g_replace_enabled)
        {
            return;
        }
        if (!g_cfg_suffix.empty() && s.find(g_cfg_suffix) != std::wstring::npos)
        {
            return;
        }
        for (const auto& kv : g_replacements)
        {
            const std::wstring& from = kv.first;
            const std::wstring& to = kv.second;
            if (from.empty())
            {
                continue;
            }
            if (!g_cfg_suffix.empty() && from.find(g_cfg_suffix) != std::wstring::npos)
            {
                continue;
            }
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::wstring::npos)
            {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
        }
    }

    static void WriteRawFString(RawFString& fs, const std::wstring& text)
    {
        int32_t len = (int32_t)text.size();
        int32_t newNum = len + 1;
        wchar_t* buf = (wchar_t*)FMemory::Malloc((SIZE_T)newNum * sizeof(wchar_t), alignof(wchar_t));
        if (!buf)
        {
            return;
        }
        if (len > 0)
        {
            memcpy(buf, text.c_str(), (size_t)len * sizeof(wchar_t));
        }
        buf[len] = 0;
        fs.Data = buf;
        fs.Num = newNum;
        fs.Max = newNum;
    }

    static void ApplyMeowFText(const std::wstring& miaoed, FText& msg)
    {
        msg.SetString(FString(miaoed));
    }

    static void ApplyMeowFStringIfNeeded(RawFString& fs)
    {
        std::wstring raw = ReadRawFString(fs);
        if (raw.empty())
        {
            return;
        }

        std::wstring replaced = raw;
        ApplyReplacements(replaced);

        std::wstring miaoed = EnsureMiao(replaced);
        if (miaoed != raw)
        {
            WriteRawFString(fs, miaoed);
        }
    }

    static void ApplyMeowFTextIfNeeded(FText& msg)
    {
        std::wstring tpl = msg.ToString();
        if (tpl.empty())
        {
            return;
        }

        std::wstring replaced = tpl;
        ApplyReplacements(replaced);

        std::wstring miaoed = EnsureMiao(replaced);
        if (miaoed != tpl)
        {
            ApplyMeowFText(miaoed, msg);
        }
    }

    static bool g_hooksReady = false;

    static CallbackId g_serverHookId = 0;
    static CallbackId g_clientHookId = 0;
    static CallbackId g_localizedHookId = 0;
    static bool g_triedServer = false;
    static bool g_triedClient = false;
    static bool g_triedLocalized = false;
    static bool g_triedChatStruct = false;
    static bool g_triedLocStruct = false;

    static UFunction* g_fnServerNewMessage = nullptr;
    static UFunction* g_fnClientNewMessage = nullptr;
    static UFunction* g_fnClientNewLocalized = nullptr;

    static int32_t g_offServerText = -1;
    static int32_t g_offClientMsgStruct = -1;
    static int32_t g_offClientMsgField = -1;
    static int32_t g_offClientSenderField = -1;
    static int32_t g_offLocalizedMsgStruct = -1;
    static int32_t g_offLocalizedMsgField = -1;
    static int32_t g_offLocalizedSenderField = -1;

    static int32_t FindPropOffset(UStruct* s, const wchar_t* name)
    {
        if (!s)
        {
            return -1;
        }
        FProperty* prop = s->FindProperty(FName(name, FNAME_Find));
        if (!prop)
        {
            return -1;
        }
        return prop->GetOffset_Internal();
    }

    static void TryMeowMulticastParms(UFunction* fn, void* parms)
    {
        if (!fn || !parms)
        {
            return;
        }
        uint8_t* p = (uint8_t*)parms;
        if (fn == g_fnClientNewMessage && g_offClientMsgStruct >= 0 && g_offClientMsgField >= 0)
        {
            uint8_t* msgStruct = p + g_offClientMsgStruct;
            if (g_cfg_sender_enabled && g_offClientSenderField >= 0)
            {
                ApplyMeowFStringIfNeeded(*(RawFString*)(msgStruct + g_offClientSenderField));
            }
            ApplyMeowFStringIfNeeded(*(RawFString*)(msgStruct + g_offClientMsgField));
        }
        else if (fn == g_fnClientNewLocalized && g_offLocalizedMsgStruct >= 0 && g_offLocalizedMsgField >= 0)
        {
            uint8_t* msgStruct = p + g_offLocalizedMsgStruct;
            if (g_cfg_sender_enabled && g_offLocalizedSenderField >= 0)
            {
                ApplyMeowFStringIfNeeded(*(RawFString*)(msgStruct + g_offLocalizedSenderField));
            }
            ApplyMeowFTextIfNeeded(*(FText*)(msgStruct + g_offLocalizedMsgField));
        }
    }

    static void ChatRewriteCallbackImpl(UObject*, UFunction* fn, void* parms)
    {
        TryMeowMulticastParms(fn, parms);
    }

    static void ChatRewriteCallback(UObject* ctx, UFunction* fn, void* parms)
    {
        __try
        {
            ChatRewriteCallbackImpl(ctx, fn, parms);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void OnServerNewMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (g_offServerText < 0)
        {
            return;
        }
        uint8_t* parms = ctx.TheStack.Locals();
        if (!parms)
        {
            return;
        }
        RawFString& text = *(RawFString*)(parms + g_offServerText);
        std::wstring raw = ReadRawFString(text);
        if (raw.empty())
        {
            return;
        }

        std::wstring replaced = raw;
        ApplyReplacements(replaced);
        std::wstring miaoed = EnsureMiao(replaced);
        if (miaoed != raw)
        {
            WriteRawFString(text, miaoed);
        }
    }

    static void OnClientNewMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (g_offClientMsgStruct < 0 || g_offClientMsgField < 0)
        {
            return;
        }
        uint8_t* parms = ctx.TheStack.Locals();
        if (!parms)
        {
            return;
        }
        uint8_t* msgStruct = parms + g_offClientMsgStruct;
        if (g_cfg_sender_enabled && g_offClientSenderField >= 0)
        {
            ApplyMeowFStringIfNeeded(*(RawFString*)(msgStruct + g_offClientSenderField));
        }
        RawFString& msg = *(RawFString*)(msgStruct + g_offClientMsgField);
        std::wstring raw = ReadRawFString(msg);
        if (raw.empty())
        {
            return;
        }

        std::wstring miaoed = EnsureMiao(raw);
        if (miaoed != raw)
        {
            WriteRawFString(msg, miaoed);
        }
    }

    static void OnClientNewLocalizedImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (g_offLocalizedMsgStruct < 0 || g_offLocalizedMsgField < 0)
        {
            return;
        }
        uint8_t* parms = ctx.TheStack.Locals();
        if (!parms)
        {
            return;
        }
        uint8_t* msgStruct = parms + g_offLocalizedMsgStruct;
        if (g_cfg_sender_enabled && g_offLocalizedSenderField >= 0)
        {
            ApplyMeowFStringIfNeeded(*(RawFString*)(msgStruct + g_offLocalizedSenderField));
        }
        FText& msg = *(FText*)(msgStruct + g_offLocalizedMsgField);
        std::wstring tpl = msg.ToString();
        if (tpl.empty())
        {
            return;
        }

        std::wstring replaced = tpl;
        ApplyReplacements(replaced);
        std::wstring miaoed = EnsureMiao(replaced);
        if (miaoed != tpl)
        {
            ApplyMeowFText(miaoed, msg);
        }
    }

    static void OnServerNewMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try
        {
            OnServerNewMessageImpl(ctx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void OnClientNewMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try
        {
            OnClientNewMessageImpl(ctx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void OnClientNewLocalized(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try
        {
            OnClientNewLocalizedImpl(ctx);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    static void TryRegisterHooks()
    {
        if (g_hooksReady)
        {
            return;
        }

        if (!g_triedServer)
        {
            g_triedServer = true;
            g_fnServerNewMessage = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/FSD.FSDPlayerController:Server_NewMessage"));
            if (g_fnServerNewMessage)
            {
                g_offServerText = FindPropOffset(g_fnServerNewMessage, STR("Text"));
                if (g_offServerText >= 0)
                {
                    g_serverHookId = g_fnServerNewMessage->RegisterPreHook(OnServerNewMessage);
                }
            }
        }

        if (!g_triedClient)
        {
            g_triedClient = true;
            g_fnClientNewMessage = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/FSD.FSDGameState:ClientNewMessage"));
            if (g_fnClientNewMessage)
            {
                g_offClientMsgStruct = FindPropOffset(g_fnClientNewMessage, STR("Msg"));
                if (g_offClientMsgStruct >= 0)
                {
                    g_clientHookId = g_fnClientNewMessage->RegisterPreHook(OnClientNewMessage);
                }
            }
        }

        if (!g_triedLocalized)
        {
            g_triedLocalized = true;
            g_fnClientNewLocalized = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, STR("/Script/FSD.FSDGameState:Client_NewLocalizedMessage"));
            if (g_fnClientNewLocalized)
            {
                g_offLocalizedMsgStruct = FindPropOffset(g_fnClientNewLocalized, STR("Msg"));
                if (g_offLocalizedMsgStruct >= 0)
                {
                    g_localizedHookId = g_fnClientNewLocalized->RegisterPreHook(OnClientNewLocalized);
                }
            }
        }

        if (!g_triedChatStruct)
        {
            g_triedChatStruct = true;
            UScriptStruct* chatStruct = UObjectGlobals::StaticFindObject<UScriptStruct*>(
                    nullptr, nullptr, STR("/Script/FSD.FSDChatMessage"));
            if (chatStruct)
            {
                g_offClientMsgField = FindPropOffset(chatStruct, STR("Msg"));
                g_offClientSenderField = FindPropOffset(chatStruct, STR("Sender"));
            }
        }

        if (!g_triedLocStruct)
        {
            g_triedLocStruct = true;
            UScriptStruct* locStruct = UObjectGlobals::StaticFindObject<UScriptStruct*>(
                    nullptr, nullptr, STR("/Script/FSD.FSDLocalizedChatMessage"));
            if (locStruct)
            {
                g_offLocalizedMsgField = FindPropOffset(locStruct, STR("Msg"));
                g_offLocalizedSenderField = FindPropOffset(locStruct, STR("Sender"));
            }
        }

        bool serverOk = g_fnServerNewMessage && g_offServerText >= 0;
        bool clientOk = g_fnClientNewMessage && g_offClientMsgStruct >= 0 && g_offClientMsgField >= 0;
        bool locOk = g_fnClientNewLocalized && g_offLocalizedMsgStruct >= 0 && g_offLocalizedMsgField >= 0;
        if (serverOk && clientOk && locOk)
        {
            g_hooksReady = true;
        }
    }

    static bool g_inSetup = false;
    static uint64_t g_lastSetupTick = 0;

    static void GameThreadSetupImpl()
    {
        TryReloadConfig();
        if (!g_hooksReady)
        {
            TryRegisterHooks();
        }
    }

    static void GameThreadSetupCallback(UObject*, UFunction*, void*)
    {
        uint64_t now = GetTickCount64();
        if (now - g_lastSetupTick < 500)
        {
            return;
        }
        if (g_inSetup)
        {
            return;
        }
        g_inSetup = true;
        g_lastSetupTick = now;
        __try
        {
            GameThreadSetupImpl();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        g_inSetup = false;
    }

    class MyMod : public RC::CppUserModBase
    {
    public:
        MyMod()
        {
            ModName = STR("CPP_MeowChat");
            ModVersion = STR("0.1");
            ModDescription = STR(
                    "Appends a configurable suffix to chat and sender names; performs configurable text replacements on message content before the suffix is applied.");
            ModAuthors = STR("Sakura (original), JiuDuo (modified)");
            InitConfigPath();
            TryReloadConfig();
        }

        ~MyMod() override
        {
            if (g_serverHookId && g_fnServerNewMessage)
            {
                g_fnServerNewMessage->UnregisterHook(g_serverHookId);
            }
            if (g_clientHookId && g_fnClientNewMessage)
            {
                g_fnClientNewMessage->UnregisterHook(g_clientHookId);
            }
            if (g_localizedHookId && g_fnClientNewLocalized)
            {
                g_fnClientNewLocalized->UnregisterHook(g_localizedHookId);
            }
        }

        auto on_program_start() -> void override
        {
            RC::Unreal::Hook::RegisterProcessEventPreCallback(GameThreadSetupCallback);
            RC::Unreal::Hook::RegisterProcessEventPreCallback(ChatRewriteCallback);
        }
    };
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        MeowChat::g_hmod = hModule;
    }
    return TRUE;
}

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod()
    {
        return new MeowChat::MyMod();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
