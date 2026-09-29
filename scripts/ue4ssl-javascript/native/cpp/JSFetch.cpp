#include "JSInternal.hpp"

#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <DynamicOutput/DynamicOutput.hpp>

namespace RC::JSScript
{
    static void fetch_do_one_request(int64_t id, const std::string& url, const std::string& method,
        const std::string& headers_str, const std::string& body,
        int& out_status, std::string& out_body, std::string& out_error)
    {
        (void)id;
        out_status = 0;
        out_body.clear();
        out_error.clear();
        std::wstring wurl = utf8_to_wide(url);
        if (wurl.empty()) { out_error = "Invalid URL encoding"; return; }

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host_buf[256] = {};
        wchar_t path_buf[2048] = {};
        uc.lpszHostName = host_buf;
        uc.dwHostNameLength = (DWORD)std::size(host_buf);
        uc.lpszUrlPath = path_buf;
        uc.dwUrlPathLength = (DWORD)std::size(path_buf);
        if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        {
            out_error = "Failed to parse URL";
            return;
        }
        bool use_ssl = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        int port = uc.nPort;
        if (port == 0) port = use_ssl ? 443 : 80;

        HINTERNET hSession = WinHttpOpen(L"UE4SS-JS/1.0", 0, nullptr, nullptr, 0);
        if (!hSession) { out_error = "WinHttpOpen failed"; return; }

        WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 30000);

        HINTERNET hConnect = WinHttpConnect(hSession, host_buf, (INTERNET_PORT)port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); out_error = "WinHttpConnect failed"; return; }
        DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
        std::wstring wpath = path_buf[0] ? path_buf : L"/";
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, utf8_to_wide(method).c_str(), wpath.c_str(), nullptr, nullptr, nullptr, flags);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); out_error = "WinHttpOpenRequest failed"; return; }

        if (use_ssl)
        {
            DWORD secure_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secure_flags, sizeof(secure_flags));
        }

        if (!headers_str.empty())
        {
            std::wstring wheaders = utf8_to_wide(headers_str);
            if (!wheaders.empty())
                WinHttpAddRequestHeaders(hRequest, wheaders.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        }
        if (method == "POST" || method == "PUT" || method == "PATCH")
        {
            if (!body.empty())
            {
                if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0))
                { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); out_error = "WinHttpSendRequest failed"; return; }
            }
            else if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); out_error = "WinHttpSendRequest failed"; return; }
        }
        else
        {
            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); out_error = "WinHttpSendRequest failed"; return; }
        }
        if (!WinHttpReceiveResponse(hRequest, nullptr))
        { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); out_error = "WinHttpReceiveResponse failed"; return; }

        DWORD status = 0;
        DWORD status_len = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &status_len, nullptr);
        out_status = (int)status;

        std::vector<char> buf(4096);
        DWORD read_len = 0;
        for (;;)
        {
            if (!WinHttpReadData(hRequest, buf.data(), (DWORD)buf.size(), &read_len) || read_len == 0)
                break;
            out_body.append(buf.data(), read_len);
        }
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    }

    static bool wants_stream_response(const std::string& headers_str)
    {
        std::string lowered = headers_str;
        for (auto& c : lowered)
        {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }

        return lowered.find("accept:") != std::string::npos &&
               lowered.find("text/event-stream") != std::string::npos;
    }

    static void queue_fetch_event(JSMod* mod, JSMod::CompletedFetchResult result)
    {
        std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
        mod->m_fetch_result_queue.push_back(std::move(result));
    }

    static void fetch_do_stream_request(JSMod* mod, int64_t id, const std::string& url, const std::string& method,
        const std::string& headers_str, const std::string& body)
    {
        std::wstring wurl = utf8_to_wide(url);
        if (wurl.empty())
        {
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "Invalid URL encoding" });
            return;
        }

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host_buf[256] = {};
        wchar_t path_buf[2048] = {};
        uc.lpszHostName = host_buf;
        uc.dwHostNameLength = (DWORD)std::size(host_buf);
        uc.lpszUrlPath = path_buf;
        uc.dwUrlPathLength = (DWORD)std::size(path_buf);
        if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
        {
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "Failed to parse URL" });
            return;
        }

        bool use_ssl = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        int port = uc.nPort;
        if (port == 0) port = use_ssl ? 443 : 80;

        HINTERNET hSession = WinHttpOpen(L"UE4SS-JS/1.0", 0, nullptr, nullptr, 0);
        if (!hSession)
        {
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpOpen failed" });
            return;
        }

        WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 30000);

        HINTERNET hConnect = WinHttpConnect(hSession, host_buf, (INTERNET_PORT)port, 0);
        if (!hConnect)
        {
            WinHttpCloseHandle(hSession);
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpConnect failed" });
            return;
        }

        DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
        std::wstring wpath = path_buf[0] ? path_buf : L"/";
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, utf8_to_wide(method).c_str(), wpath.c_str(), nullptr, nullptr, nullptr, flags);
        if (!hRequest)
        {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpOpenRequest failed" });
            return;
        }

        if (use_ssl)
        {
            DWORD secure_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secure_flags, sizeof(secure_flags));
        }

        if (!headers_str.empty())
        {
            std::wstring wheaders = utf8_to_wide(headers_str);
            if (!wheaders.empty())
                WinHttpAddRequestHeaders(hRequest, wheaders.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        }

        if (method == "POST" || method == "PUT" || method == "PATCH")
        {
            if (!body.empty())
            {
                if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0))
                {
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    WinHttpCloseHandle(hSession);
                    queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpSendRequest failed" });
                    return;
                }
            }
            else if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpSendRequest failed" });
                return;
            }
        }
        else
        {
            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpSendRequest failed" });
                return;
            }
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr))
        {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, 0, {}, "WinHttpReceiveResponse failed" });
            return;
        }

        DWORD status = 0;
        DWORD status_len = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &status_len, nullptr);
        queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamHeaders, static_cast<int>(status), {}, {} });

        std::vector<char> buf(4096);
        DWORD read_len = 0;
        for (;;)
        {
            if (mod->m_fetch_worker_stop.load())
                break;
            if (!WinHttpReadData(hRequest, buf.data(), (DWORD)buf.size(), &read_len))
            {
                queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamError, static_cast<int>(status), {}, "WinHttpReadData failed" });
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return;
            }
            if (read_len == 0)
                break;
            queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamChunk, static_cast<int>(status), std::string(buf.data(), read_len), {} });
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        queue_fetch_event(mod, { id, JSMod::CompletedFetchResult::Kind::StreamEnd, static_cast<int>(status), {}, {} });
    }

    // Helper: parse fetch options from JS argv[1]
    static void parse_fetch_options(JSContext* ctx, int argc, JSValueConst* argv,
        std::string& method, std::string& headers_str, std::string& body)
    {
        method = "GET";
        headers_str.clear();
        body.clear();
        if (argc < 2 || JS_IsUndefined(argv[1]) || JS_IsNull(argv[1])) return;

        JSValue opts = argv[1];
        JSValue method_val = JS_GetPropertyStr(ctx, opts, "method");
        if (!JS_IsUndefined(method_val))
        {
            const char* m = JS_ToCString(ctx, method_val);
            if (m) { method = m; for (auto& c : method) c = (char)toupper((unsigned char)c); JS_FreeCString(ctx, m); }
        }
        JS_FreeValue(ctx, method_val);

        JSValue headers_val = JS_GetPropertyStr(ctx, opts, "headers");
        if (JS_IsObject(headers_val))
        {
            JSPropertyEnum* names = nullptr;
            uint32_t len = 0;
            if (JS_GetOwnPropertyNames(ctx, &names, &len, headers_val, JS_GPN_STRING_MASK) == 0)
            {
                for (uint32_t i = 0; i < len; i++)
                {
                    const char* k = JS_AtomToCString(ctx, names[i].atom);
                    JSValue v = JS_GetProperty(ctx, headers_val, names[i].atom);
                    const char* vc = JS_ToCString(ctx, v);
                    if (k && vc) { headers_str += std::string(k) + ": " + vc + "\r\n"; }
                    if (k) JS_FreeCString(ctx, k);
                    if (vc) JS_FreeCString(ctx, vc);
                    JS_FreeValue(ctx, v);
                }
                js_free(ctx, names);
            }
        }
        JS_FreeValue(ctx, headers_val);

        JSValue body_val = JS_GetPropertyStr(ctx, opts, "body");
        if (!JS_IsUndefined(body_val) && !JS_IsNull(body_val))
        {
            const char* b = JS_ToCString(ctx, body_val);
            if (b) body = b; JS_FreeCString(ctx, b);
        }
        JS_FreeValue(ctx, body_val);
    }

    static bool safe_fetch_callback_call(JSMod* mod, JSContext* ctx, JSValueConst callback,
                                         int argc, JSValueConst* argv, const wchar_t* operation)
    {
        JSValue call_result = JS_UNDEFINED;
        if (!safe_js_call(ctx, callback, JS_UNDEFINED, argc, argv, &call_result))
        {
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, operation, L"SEH exception");
            return false;
        }

        if (JS_IsException(call_result))
        {
            mod->log_exception(ctx, operation);
            JS_FreeValue(ctx, call_result);
            mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, operation, L"JS exception");
            return false;
        }

        JS_FreeValue(ctx, call_result);
        mod->report_subsystem_success(JSMod::GuardedSubsystem::Fetch);
        return true;
    }

    void fetch_worker_run(JSMod* mod)
    {
        while (!mod->m_fetch_worker_stop.load())
        {
            try
            {
                JSMod::FetchRequest req;
                bool has_work = false;
                {
                    std::unique_lock<std::mutex> lock(mod->m_fetch_mutex);
                    mod->m_fetch_request_cv.wait(lock, [mod]() {
                        return mod->m_fetch_worker_stop.load() || !mod->m_fetch_request_queue.empty();
                    });
                    if (mod->m_fetch_worker_stop.load()) break;
                    if (!mod->m_fetch_request_queue.empty())
                    {
                        req = std::move(mod->m_fetch_request_queue.front());
                        mod->m_fetch_request_queue.erase(mod->m_fetch_request_queue.begin());
                        has_work = true;
                    }
                }
                if (!has_work) continue;
                int64_t id = req.id;
                int status = 0;
                std::string resp_body, error_msg;

                try
                {
                    if (req.stream_response)
                    {
                        fetch_do_stream_request(mod, id, req.url, req.method, req.headers_str, req.body);
                        continue;
                    }
                    fetch_do_one_request(id, req.url, req.method, req.headers_str, req.body, status, resp_body, error_msg);
                }
                catch (const std::exception& e)
                {
                    error_msg = std::string("fetch exception: ") + e.what();
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] fetch_do_one_request exception: {}\n"),
                        std::wstring(e.what(), e.what() + strlen(e.what())));
                }
                catch (...)
                {
                    error_msg = "fetch: unknown exception during HTTP request";
                    Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] fetch_do_one_request: unknown exception\n"));
                }

                {
                    std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
                    mod->m_fetch_result_queue.push_back({ id, JSMod::CompletedFetchResult::Kind::Complete, status, std::move(resp_body), std::move(error_msg) });
                }
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] fetch worker loop exception: {}\n"),
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] fetch worker loop: unknown exception, continuing\n"));
            }
        }
        Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] fetch worker thread exiting normally\n"));
    }

    JSValue js_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "No JSMod");
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Fetch))
            return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");

        if (argc < 1) return JS_ThrowTypeError(ctx, "fetch requires at least 1 argument (url)");
        const char* url_cstr = JS_ToCString(ctx, argv[0]);
        if (!url_cstr) return JS_ThrowTypeError(ctx, "fetch: url must be a string");
        std::string url(url_cstr);
        JS_FreeCString(ctx, url_cstr);

        std::string method, headers_str, body;
        parse_fetch_options(ctx, argc, argv, method, headers_str, body);

        JSValue resolving_funcs[2];
        JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(promise)) return promise;
        JSValue resolve_func = JS_DupValue(ctx, resolving_funcs[0]);
        JSValue reject_func = JS_DupValue(ctx, resolving_funcs[1]);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);

        int64_t id;
        {
            std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
            id = mod->m_fetch_next_id++;
            mod->m_fetch_pending[id] = { resolve_func, reject_func };
            mod->m_fetch_request_queue.push_back({
                id,
                std::move(url),
                std::move(method),
                headers_str,
                std::move(body),
                wants_stream_response(headers_str)
            });
        }
        mod->m_fetch_request_cv.notify_one();
        return promise;
    }

    JSValue js_fetch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        JSMod* mod = get_js_mod(ctx);
        if (!mod) return JS_ThrowInternalError(ctx, "No JSMod");
        if (mod->is_subsystem_disabled(JSMod::GuardedSubsystem::Fetch))
            return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");

        if (argc < 1) return JS_ThrowTypeError(ctx, "fetchSync requires at least 1 argument (url)");
        const char* url_cstr = JS_ToCString(ctx, argv[0]);
        if (!url_cstr) return JS_ThrowTypeError(ctx, "fetchSync: url must be a string");
        std::string url(url_cstr);
        JS_FreeCString(ctx, url_cstr);

        std::string method, headers_str, body;
        parse_fetch_options(ctx, argc, argv, method, headers_str, body);

        int status = 0;
        std::string resp_body, error_msg;

        try
        {
            fetch_do_one_request(0, url, method, headers_str, body, status, resp_body, error_msg);
        }
        catch (const std::exception& e)
        {
            return JS_ThrowInternalError(ctx, "fetchSync exception: %s", e.what());
        }
        catch (...)
        {
            return JS_ThrowInternalError(ctx, "fetchSync: unknown exception during HTTP request");
        }

        if (!error_msg.empty())
        {
            return JS_ThrowInternalError(ctx, "%s", error_msg.c_str());
        }

        JSValue resp = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
        JS_SetPropertyStr(ctx, resp, "status", JS_NewInt32(ctx, status));
        JS_SetPropertyStr(ctx, resp, "body", JS_NewString(ctx, resp_body.c_str()));
        return resp;
    }

    static bool get_fetch_id_from_object(JSContext* ctx, JSValueConst obj, int64_t& out_id)
    {
        JSValue id_val = JS_GetPropertyStr(ctx, obj, "__fetchId");
        bool ok = !JS_IsUndefined(id_val) && !JS_IsNull(id_val) && JS_ToInt64(ctx, &out_id, id_val) == 0;
        JS_FreeValue(ctx, id_val);
        return ok;
    }

    static JSValue make_fetch_stream_read_result(JSContext* ctx, bool done, const std::string& chunk)
    {
        JSValue result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, result, "done", JS_NewBool(ctx, done));
        if (done)
        {
            JS_SetPropertyStr(ctx, result, "value", JS_UNDEFINED);
        }
        else
        {
            JS_SetPropertyStr(ctx, result, "value", JS_NewUint8ArrayCopy(ctx, reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()));
        }
        return result;
    }

    static JSValue make_fetch_error(JSContext* ctx, const std::string& message)
    {
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, message.c_str()));
        return err;
    }

    static void free_fetch_callbacks(JSContext* ctx, JSMod::PendingFetchCallbacks& cb)
    {
        JS_FreeValue(ctx, cb.resolve_func);
        JS_FreeValue(ctx, cb.reject_func);
        cb.resolve_func = JS_UNDEFINED;
        cb.reject_func = JS_UNDEFINED;
    }

    static void resolve_fetch_stream_read(JSMod* mod, JSContext* ctx, JSMod::PendingFetchCallbacks& cb, bool done, const std::string& chunk)
    {
        JSValue result = make_fetch_stream_read_result(ctx, done, chunk);
        JSValue args[1] = { result };
        safe_fetch_callback_call(mod, ctx, cb.resolve_func, 1, args, L"FetchStreamReadResolve");
        JS_FreeValue(ctx, result);
        free_fetch_callbacks(ctx, cb);
    }

    static void reject_fetch_stream_read(JSMod* mod, JSContext* ctx, JSMod::PendingFetchCallbacks& cb, const std::string& message)
    {
        JSValue err = make_fetch_error(ctx, message);
        JSValue args[1] = { err };
        safe_fetch_callback_call(mod, ctx, cb.reject_func, 1, args, L"FetchStreamReadReject");
        JS_FreeValue(ctx, err);
        free_fetch_callbacks(ctx, cb);
    }

    static JSValue create_fetch_body_stream(JSContext* ctx, int64_t id)
    {
        JSValue body = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, body, "__fetchId", JS_NewInt64(ctx, id));
        JS_SetPropertyStr(ctx, body, "getReader", JS_NewCFunction(ctx, js_fetch_body_get_reader, "getReader", 0));
        return body;
    }

    JSValue js_fetch_body_get_reader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        int64_t id = 0;
        if (!get_fetch_id_from_object(ctx, this_val, id))
        {
            return JS_ThrowTypeError(ctx, "ReadableStream.getReader called on an invalid fetch body");
        }

        JSValue reader = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, reader, "__fetchId", JS_NewInt64(ctx, id));
        JS_SetPropertyStr(ctx, reader, "read", JS_NewCFunction(ctx, js_fetch_stream_reader_read, "read", 0));
        return reader;
    }

    JSValue js_fetch_stream_reader_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        JSMod* mod = get_js_mod(ctx);
        int64_t id = 0;
        std::string chunk;
        std::string error_msg;
        bool should_wait = false;
        bool done = false;
        bool reject_now = false;

        if (!mod)
            return JS_ThrowInternalError(ctx, "No JSMod");
        if (!get_fetch_id_from_object(ctx, this_val, id))
            return JS_ThrowTypeError(ctx, "ReadableStream reader is missing fetch id");

        JSValue resolving_funcs[2];
        JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(p)) return p;

        JSMod::PendingFetchCallbacks cb{
            JS_DupValue(ctx, resolving_funcs[0]),
            JS_DupValue(ctx, resolving_funcs[1])
        };
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);

        {
            std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
            auto it = mod->m_fetch_streams.find(id);
            if (it == mod->m_fetch_streams.end())
            {
                reject_now = true;
                error_msg = "fetch stream not found";
            }
            else if (!it->second.error_msg.empty())
            {
                reject_now = true;
                error_msg = it->second.error_msg;
            }
            else if (!it->second.chunks.empty())
            {
                chunk = std::move(it->second.chunks.front());
                for (size_t i = 1; i < it->second.chunks.size(); ++i)
                    chunk.append(it->second.chunks[i]);
                it->second.chunks.clear();
            }
            else if (it->second.closed)
            {
                done = true;
            }
            else
            {
                should_wait = true;
                it->second.pending_reads.push_back(cb);
            }
        }

        if (should_wait)
            return p;

        if (reject_now)
            reject_fetch_stream_read(mod, ctx, cb, error_msg);
        else
            resolve_fetch_stream_read(mod, ctx, cb, done, chunk);
        return p;
    }

    JSValue js_text_decoder_decode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)this_val;
        if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
            return JS_NewString(ctx, "");

        size_t byte_offset = 0;
        size_t byte_length = 0;
        size_t bytes_per_element = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &byte_offset, &byte_length, &bytes_per_element);
        if (!JS_IsException(buffer))
        {
            size_t buffer_size = 0;
            uint8_t* data = JS_GetArrayBuffer(ctx, &buffer_size, buffer);
            if (data && byte_offset <= buffer_size && byte_length <= buffer_size - byte_offset)
            {
                JSValue text = JS_NewStringLen(ctx, reinterpret_cast<const char*>(data + byte_offset), byte_length);
                JS_FreeValue(ctx, buffer);
                return text;
            }
            JS_FreeValue(ctx, buffer);
        }

        if (JS_IsArrayBuffer(argv[0]))
        {
            size_t buffer_size = 0;
            uint8_t* data = JS_GetArrayBuffer(ctx, &buffer_size, argv[0]);
            if (data)
                return JS_NewStringLen(ctx, reinterpret_cast<const char*>(data), buffer_size);
        }

        size_t text_length = 0;
        const char* text = JS_ToCStringLen(ctx, &text_length, argv[0]);
        if (!text)
            return JS_ThrowTypeError(ctx, "TextDecoder.decode expects a Uint8Array, ArrayBuffer, or string");

        JSValue result = JS_NewStringLen(ctx, text, text_length);
        JS_FreeCString(ctx, text);
        return result;
    }

    JSValue js_text_decoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv)
    {
        (void)new_target;
        (void)argc;
        (void)argv;
        JSValue decoder = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, decoder, "encoding", JS_NewString(ctx, "utf-8"));
        JS_SetPropertyStr(ctx, decoder, "fatal", JS_NewBool(ctx, false));
        JS_SetPropertyStr(ctx, decoder, "ignoreBOM", JS_NewBool(ctx, false));
        JS_SetPropertyStr(ctx, decoder, "decode", JS_NewCFunction(ctx, js_text_decoder_decode, "decode", 1));
        return decoder;
    }

    JSValue js_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        JSMod* mod = get_js_mod(ctx);
        JSValue body_val = JS_GetPropertyStr(ctx, this_val, "body");
        int64_t stream_id = 0;
        if (mod && get_fetch_id_from_object(ctx, body_val, stream_id))
        {
            std::string body_text;
            {
                std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
                auto it = mod->m_fetch_streams.find(stream_id);
                if (it != mod->m_fetch_streams.end())
                    body_text = it->second.body;
            }
            JS_FreeValue(ctx, body_val);
            body_val = JS_NewString(ctx, body_text.c_str());
        }
        JSValue resolving_funcs[2];
        JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(p)) { JS_FreeValue(ctx, body_val); return p; }

        JSValue args[1] = { body_val };
        if (mod)
        {
            safe_fetch_callback_call(mod, ctx, resolving_funcs[0], 1, args, L"ResponseTextResolve");
        }
        else
        {
            JSValue call_result = JS_UNDEFINED;
            safe_js_call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, args, &call_result);
            JS_FreeValue(ctx, call_result);
        }

        JS_FreeValue(ctx, body_val);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return p;
    }

    JSValue js_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
    {
        (void)argc; (void)argv;
        JSMod* mod = get_js_mod(ctx);
        JSValue body_val = JS_GetPropertyStr(ctx, this_val, "body");
        int64_t stream_id = 0;
        if (mod && get_fetch_id_from_object(ctx, body_val, stream_id))
        {
            std::string body_text;
            {
                std::lock_guard<std::mutex> lock(mod->m_fetch_mutex);
                auto it = mod->m_fetch_streams.find(stream_id);
                if (it != mod->m_fetch_streams.end())
                    body_text = it->second.body;
            }
            JS_FreeValue(ctx, body_val);
            body_val = JS_NewString(ctx, body_text.c_str());
        }
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue json_obj = JS_GetPropertyStr(ctx, global, "JSON");
        JSValue parse_fn = JS_GetPropertyStr(ctx, json_obj, "parse");
        JSValue parsed = JS_UNDEFINED;
        JSValue parse_args[1] = { body_val };
        if (!safe_js_call(ctx, parse_fn, json_obj, 1, parse_args, &parsed))
        {
            JS_FreeValue(ctx, body_val);
            JS_FreeValue(ctx, parse_fn);
            JS_FreeValue(ctx, json_obj);
            JS_FreeValue(ctx, global);
            if (mod)
            {
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, L"ResponseJsonParse", L"SEH exception");
            }
            return JS_ThrowInternalError(ctx, "Response.json parse failed with SEH");
        }
        JS_FreeValue(ctx, body_val);
        JS_FreeValue(ctx, parse_fn);
        JS_FreeValue(ctx, json_obj);
        JS_FreeValue(ctx, global);
        if (JS_IsException(parsed))
        {
            if (mod)
            {
                mod->report_subsystem_failure(JSMod::GuardedSubsystem::Fetch, L"ResponseJsonParse", L"JSON.parse exception");
            }
            return parsed;
        }
        if (mod)
        {
            mod->report_subsystem_success(JSMod::GuardedSubsystem::Fetch);
        }
        JSValue resolving_funcs[2];
        JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
        if (JS_IsException(p)) { JS_FreeValue(ctx, parsed); return p; }

        JSValue args[1] = { parsed };
        if (mod)
        {
            safe_fetch_callback_call(mod, ctx, resolving_funcs[0], 1, args, L"ResponseJsonResolve");
        }
        else
        {
            JSValue call_result = JS_UNDEFINED;
            safe_js_call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, args, &call_result);
            JS_FreeValue(ctx, call_result);
        }

        JS_FreeValue(ctx, parsed);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return p;
    }

    static JSValue create_fetch_response(JSContext* ctx, int64_t id, int status, const std::string& body, bool stream_response)
    {
        JSValue resp = JS_NewObject(ctx);
        const char* status_text = (status == 200) ? "OK" : (status == 404) ? "Not Found" : (status >= 500) ? "Server Error" : "";
        JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
        JS_SetPropertyStr(ctx, resp, "status", JS_NewInt32(ctx, status));
        JS_SetPropertyStr(ctx, resp, "statusText", JS_NewString(ctx, status_text));
        if (stream_response)
            JS_SetPropertyStr(ctx, resp, "body", create_fetch_body_stream(ctx, id));
        else
            JS_SetPropertyStr(ctx, resp, "body", JS_NewString(ctx, body.c_str()));
        JS_SetPropertyStr(ctx, resp, "text", JS_NewCFunction(ctx, js_response_text, "text", 0));
        JS_SetPropertyStr(ctx, resp, "json", JS_NewCFunction(ctx, js_response_json, "json", 0));
        return resp;
    }

    auto JSMod::process_fetch_results() -> void
    {
        std::vector<CompletedFetchResult> results;
        {
            std::lock_guard<std::mutex> lock(m_fetch_mutex);
            results.swap(m_fetch_result_queue);
        }
        JSContext* ctx = m_main_ctx;
        if (!ctx) return;

        const bool fetch_disabled = is_subsystem_disabled(GuardedSubsystem::Fetch);
        for (auto& r : results)
        {
            try
            {
                if (r.kind == CompletedFetchResult::Kind::Complete)
                {
                    PendingFetchCallbacks cb{ JS_UNDEFINED, JS_UNDEFINED };
                    {
                        std::lock_guard<std::mutex> lock(m_fetch_mutex);
                        auto it = m_fetch_pending.find(r.id);
                        if (it == m_fetch_pending.end()) continue;
                        cb = it->second;
                        m_fetch_pending.erase(it);
                    }

                    if (fetch_disabled)
                    {
                        free_fetch_callbacks(ctx, cb);
                        continue;
                    }

                    if (!r.error_msg.empty())
                    {
                        JSValue err = make_fetch_error(ctx, r.error_msg);
                        JSValue args[1] = { err };
                        safe_fetch_callback_call(this, ctx, cb.reject_func, 1, args, L"FetchRejectCallback");
                        JS_FreeValue(ctx, err);
                    }
                    else
                    {
                        JSValue resp = create_fetch_response(ctx, r.id, r.status, r.body, false);
                        JSValue args[1] = { resp };
                        safe_fetch_callback_call(this, ctx, cb.resolve_func, 1, args, L"FetchResolveCallback");
                        JS_FreeValue(ctx, resp);
                    }
                    free_fetch_callbacks(ctx, cb);
                }
                else if (r.kind == CompletedFetchResult::Kind::StreamHeaders)
                {
                    PendingFetchCallbacks cb{ JS_UNDEFINED, JS_UNDEFINED };
                    {
                        std::lock_guard<std::mutex> lock(m_fetch_mutex);
                        auto it = m_fetch_pending.find(r.id);
                        if (it == m_fetch_pending.end()) continue;
                        cb = it->second;
                        m_fetch_pending.erase(it);
                        FetchStreamState state;
                        state.status = r.status;
                        m_fetch_streams[r.id] = std::move(state);
                    }

                    if (fetch_disabled)
                    {
                        free_fetch_callbacks(ctx, cb);
                        continue;
                    }

                    JSValue resp = create_fetch_response(ctx, r.id, r.status, {}, true);
                    JSValue args[1] = { resp };
                    safe_fetch_callback_call(this, ctx, cb.resolve_func, 1, args, L"FetchResolveCallback");
                    JS_FreeValue(ctx, resp);
                    free_fetch_callbacks(ctx, cb);
                }
                else if (r.kind == CompletedFetchResult::Kind::StreamChunk)
                {
                    PendingFetchCallbacks read_cb{ JS_UNDEFINED, JS_UNDEFINED };
                    bool has_read = false;
                    {
                        std::lock_guard<std::mutex> lock(m_fetch_mutex);
                        auto it = m_fetch_streams.find(r.id);
                        if (it == m_fetch_streams.end()) continue;
                        it->second.body.append(r.body);
                        if (!it->second.pending_reads.empty())
                        {
                            read_cb = it->second.pending_reads.front();
                            it->second.pending_reads.erase(it->second.pending_reads.begin());
                            has_read = true;
                        }
                        else
                        {
                            if (!it->second.chunks.empty())
                                it->second.chunks.back().append(r.body);
                            else
                                it->second.chunks.push_back(std::move(r.body));
                        }
                    }

                    if (has_read && !fetch_disabled)
                        resolve_fetch_stream_read(this, ctx, read_cb, false, r.body);
                    else if (has_read)
                        free_fetch_callbacks(ctx, read_cb);
                }
                else if (r.kind == CompletedFetchResult::Kind::StreamEnd)
                {
                    std::vector<PendingFetchCallbacks> pending_reads;
                    {
                        std::lock_guard<std::mutex> lock(m_fetch_mutex);
                        auto it = m_fetch_streams.find(r.id);
                        if (it == m_fetch_streams.end()) continue;
                        it->second.closed = true;
                        pending_reads.swap(it->second.pending_reads);
                    }

                    for (auto& read_cb : pending_reads)
                    {
                        if (!fetch_disabled)
                            resolve_fetch_stream_read(this, ctx, read_cb, true, {});
                        else
                            free_fetch_callbacks(ctx, read_cb);
                    }
                }
                else if (r.kind == CompletedFetchResult::Kind::StreamError)
                {
                    PendingFetchCallbacks response_cb{ JS_UNDEFINED, JS_UNDEFINED };
                    bool has_response_cb = false;
                    std::vector<PendingFetchCallbacks> pending_reads;
                    {
                        std::lock_guard<std::mutex> lock(m_fetch_mutex);
                        auto pending_it = m_fetch_pending.find(r.id);
                        if (pending_it != m_fetch_pending.end())
                        {
                            response_cb = pending_it->second;
                            m_fetch_pending.erase(pending_it);
                            has_response_cb = true;
                        }
                        auto stream_it = m_fetch_streams.find(r.id);
                        if (stream_it != m_fetch_streams.end())
                        {
                            stream_it->second.error_msg = r.error_msg;
                            stream_it->second.closed = true;
                            pending_reads.swap(stream_it->second.pending_reads);
                        }
                    }

                    if (has_response_cb)
                    {
                        if (!fetch_disabled)
                        {
                            JSValue err = make_fetch_error(ctx, r.error_msg);
                            JSValue args[1] = { err };
                            safe_fetch_callback_call(this, ctx, response_cb.reject_func, 1, args, L"FetchRejectCallback");
                            JS_FreeValue(ctx, err);
                        }
                        free_fetch_callbacks(ctx, response_cb);
                    }

                    for (auto& read_cb : pending_reads)
                    {
                        if (!fetch_disabled)
                            reject_fetch_stream_read(this, ctx, read_cb, r.error_msg);
                        else
                            free_fetch_callbacks(ctx, read_cb);
                    }
                }
            }
            catch (const std::exception& e)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_fetch_results exception for id {}: {}\n"),
                    r.id, std::wstring(e.what(), e.what() + strlen(e.what())));
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] process_fetch_results unknown exception for id {}\n"), r.id);
            }
        }
    }

} // namespace RC::JSScript
