# Lua / JavaScript 逐 API 冻结契约
提交 `fff1f21dce7f671c8736a7907ff41ccb9c2faf32`；所有条目是源码取证，运行状态均 **unverified**。
## 阅读规则与覆盖范围

这里记录项目额外公开的全局函数、userdata/table 方法、operator，以及 JS 返回对象上的 callable。Lua 5.4.7 标准库与 QuickJS ECMAScript 内建不重新枚举为项目 API；其语义来自冻结 VM 实现。Lua `prepare_mod` 开放标准库的具体列表见 LuaMod.cpp:5402 起。动态 UObject 属性名、UFunction 参数及 enum 名来自实际游戏反射，不能建立与游戏无关的固定名字表；它们由这里列出的索引/调用 operator 和 property API 定义。

每项“参数/返回证据”给出实际参数读取、overload 检查、Lua 栈写入/返回个数、JS 值构造、错误分支及原行号；不是从 arity 推断签名。读取顺序即 Lua pop/get 参数次序；方法的首个 userdata 为 self。Lua `return N` 是返回栈顶 N 个值，不能把源码 `return bool` 当作正确压入 boolean 的证明。`lua.is_*` 的省略索引使用当前栈读取位置，精确行为沿用 LuaMadeSimple，错误经 luaL_error，不承诺参数不足时无副作用。

JS `arity` 仅 `function.length`。`JS_ToCString`/ToInt32 等可能接受可转换类型；不得把它们写成严格 typeof 校验。JS_NewObject 后 JS_SetPropertyStr 给出返回对象 schema；return JS_NULL/UNDEFINED/Bool 是普通返回，JS_EXCEPTION 是 pending exception。异步完成与 callback 参数还须看同文件 worker/tick，摘要后的编号证据不能替代完整控制流。

证据是为迁移逐项对照而冻结的**源码级契约**，不是从运行测得的安全保证。路径用 `git show 提交:路径` 取原文；`baseline-sources.json` 提供内容 SHA256。由于只摘取输入/输出语句，未展示的锁、清理或分支不能推断不存在。共享 helper 的参数/错误也在文末列明来源；任何实际行为差异登记 known-defects，不能按这些片段机械补类型检查。

## JavaScript callable 清单（75 项）

| 名称 | arity | 实现 | 源位置 |
|---|---:|---|---|
| `print` | 1 | `js_print` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:630` |
| `FindFirstOf` | 1 | `js_find_first_of` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:654` |
| `FindAllOf` | 1 | `js_find_all_of` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:690` |
| `FindAllActorsWithInterface` | 1 | `js_find_all_actors_with_interface` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:750` |
| `StaticFindObject` | 1 | `js_static_find_object` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:830` |
| `LoadObject` | 1 | `js_load_object` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:861` |
| `ScanBlueprintWidgetsByInterface` | 2 | `js_scan_blueprint_widgets_by_interface` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:900` |
| `RegisterHook` | 3 | `js_register_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:958` |
| `RegisterBindHook` | 3 | `js_register_bind_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1031` |
| `RegisterNativeObjectMethodHook` | 2 | `js_register_native_object_method_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1202` |
| `RegisterLoadMapPreHook` | 1 | `js_register_load_map_pre_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1380` |
| `RegisterLoadMapPostHook` | 1 | `js_register_load_map_post_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1385` |
| `HookUFunction` | 3 | `js_hook_ufunction` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1390` |
| `UnregisterHook` | 2 | `js_unregister_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1451` |
| `UnregisterBindHook` | 2 | `js_unregister_bind_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1482` |
| `UnregisterLoadMapHook` | 1 | `js_unregister_load_map_hook` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1513` |
| `NotifyOnNewObject` | 2 | `js_notify_on_new_object` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1546` |
| `RegisterKeyBind` | 3 | `js_register_key_bind` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1619` |
| `CallFunction` | 3 | `js_call_function` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1868` |
| `CallFunctionEx` | 3 | `js_call_function_ex` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:2036` |
| `__withExecBudget` | 2 | `js_with_exec_budget` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1989` |
| `setTimeout` | 2 | `js_set_timeout` | `crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:148` |
| `setInterval` | 2 | `js_set_interval` | `crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:169` |
| `clearTimeout` | 1 | `js_clear_timeout` | `crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:190` |
| `clearInterval` | 1 | `js_clear_interval` | `crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:204` |
| `fetch` | 2 | `js_fetch` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:395` |
| `fetchSync` | 2 | `js_fetch_sync` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:438` |
| `SigScan` | 1 | `js_sig_scan` | `crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:8` |
| `PatchByte` | 2 | `js_patch_byte` | `crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:78` |
| `ReadByte` | 1 | `js_read_byte` | `crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:120` |
| `readFile` | 1 | `js_read_file` | `crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:27` |
| `writeFile` | 2 | `js_write_file` | `crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:68` |
| `getModsDirectory` | 0 | `js_get_mods_directory` | `crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:124` |
| `getGameDirectory` | 0 | `js_get_game_directory` | `crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:147` |
| `downloadFile` | 3 | `js_download_file` | `crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:453` |
| `downloadFileSync` | 3 | `js_download_file_sync` | `crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:516` |
| `playSoundFile` | 2 | `js_play_sound_file` | `crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:630` |
| `stopSound` | 0 | `js_stop_sound` | `crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:681` |
| `GetProperty` | 2 | `js_get_property` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:156` |
| `SetProperty` | 3 | `js_set_property` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:206` |
| `ExportPropertyText` | 2 | `js_export_property_text` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:303` |
| `GetPropertyPath` | 2 | `js_get_property_path` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:355` |
| `SetPropertyPath` | 3 | `js_set_property_path` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:360` |
| `ApplyObjectPatch` | 2 | `js_apply_object_patch` | `crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:365` |
| `BindDelegate` | 4 | `js_bind_delegate` | `crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:467` |
| `UnbindDelegate` | 4 | `js_unbind_delegate` | `crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:539` |
| `ClearDelegate` | 2 | `js_clear_delegate` | `crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:592` |
| `RegisterProcessEventWatch` | 2 | `js_register_pe_watch` | `crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1304` |
| `BindDelegateCallback` | 3 | `js_bind_delegate_callback` | `crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:981` |
| `UnbindDelegateCallback` | 1 | `js_unbind_delegate_callback` | `crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:1182` |
| `NewUObject` | 2 | `js_new_uobject` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:413` |
| `__umgDispatchSync` | 1 | `js_umg_dispatch_sync` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:746` |
| `__umgDispatchAsync` | 1 | `js_umg_dispatch_async` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:852` |
| `__umgCreateUserWidget` | 3 | `js_umg_create_user_widget` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:499` |
| `__umgCloneUserWidget` | 2 | `js_umg_clone_user_widget` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:631` |
| `__umgSetUserWidgetRoot` | 2 | `js_umg_set_user_widget_root` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:942` |
| `__umgConstructWidget` | 2 | `js_umg_construct_widget` | `crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:1020` |
| `getReader` | 0 | `js_fetch_body_get_reader` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:547` |
| `read` | 0 | `js_fetch_stream_reader_read` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:562` |
| `decode` | 1 | `js_text_decoder_decode` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:630` |
| `text` | 0 | `js_response_text` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:684` |
| `json` | 0 | `js_response_json` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:724` |
| `JSPropertyUtils.get` | 0 | `js_param_ref_get` | `crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:174` |
| `JSPropertyUtils.set` | 1 | `js_param_ref_set` | `crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:190` |
| `JSPropertyUtils.toString` | 0 | `js_param_ref_tostring` | `crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:209` |
| `JSPropertyUtils.isValid` | 0 | `js_param_ref_is_valid` | `crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:230` |
| `JSUObject.GetFullName` | 0 | `js_uobject_get_full_name` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:90` |
| `JSUObject.GetClass` | 0 | `js_uobject_get_class` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:115` |
| `JSUObject.IsA` | 1 | `js_uobject_is_a` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:136` |
| `JSUObject.GetAddress` | 0 | `js_uobject_get_address` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:172` |
| `JSUObject.IsValid` | 0 | `js_uobject_is_valid` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:184` |
| `JSUObject.GetName` | 0 | `js_uobject_get_name` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:193` |
| `JSUObject.AddToRoot` | 0 | `js_uobject_add_to_root` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:239` |
| `JSUObject.RemoveFromRoot` | 0 | `js_uobject_remove_from_root` | `crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:259` |
| `TextDecoder` | 1 | `js_text_decoder_constructor` | `crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:671` |

### JS `print`

- 注册 arity：1；实现：`js_print`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:630`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
630: JSValue js_print(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
631: {
634: for (int i = 0; i < argc; i++)
635: {
638: const char* str = JS_ToCString(ctx, argv[i]);
647: return JS_UNDEFINED;
```

### JS `FindFirstOf`

- 注册 arity：1；实现：`js_find_first_of`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:654`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
654: JSValue js_find_first_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
655: {
656: if (argc < 1)
657: return JS_ThrowTypeError(ctx, "FindFirstOf requires a class name argument");
659: const char* class_name = JS_ToCString(ctx, argv[0]);
661: return JS_ThrowTypeError(ctx, "Invalid class name");
672: return JS_NULL;
674: if (!found_obj) return JS_NULL;
675: return JSUObject::create(ctx, found_obj);
681: return JS_NULL;
686: return JS_NULL;
```

### JS `FindAllOf`

- 注册 arity：1；实现：`js_find_all_of`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:690`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
690: JSValue js_find_all_of(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
691: {
692: if (argc < 1)
693: return JS_ThrowTypeError(ctx, "FindAllOf requires a class name argument");
695: const char* class_name = JS_ToCString(ctx, argv[0]);
697: return JS_ThrowTypeError(ctx, "Invalid class name");
708: return JS_NewArray(ctx);
711: JSValue result = JS_NewArray(ctx);
717: return result;
723: return JS_NewArray(ctx);
728: return JS_NewArray(ctx);
```

### JS `FindAllActorsWithInterface`

- 注册 arity：1；实现：`js_find_all_actors_with_interface`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:750`。
- 错误类别：ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
750: JSValue js_find_all_actors_with_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
751: {
752: if (argc < 1)
753: {
754: return JS_ThrowTypeError(ctx, "FindAllActorsWithInterface requires an interface path argument");
757: const char* interface_path_utf8 = JS_ToCString(ctx, argv[0]);
760: return JS_ThrowTypeError(ctx, "Invalid interface path");
771: return JS_ThrowReferenceError(ctx, "Interface class not found");
777: return JS_NewArray(ctx);
780: JSValue result = JS_NewArray(ctx);
815: return result;
821: return JS_NewArray(ctx);
826: return JS_NewArray(ctx);
```

### JS `StaticFindObject`

- 注册 arity：1；实现：`js_static_find_object`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:830`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
830: JSValue js_static_find_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
831: {
832: if (argc < 1)
833: return JS_ThrowTypeError(ctx, "StaticFindObject requires an object path argument");
835: const char* object_path = JS_ToCString(ctx, argv[0]);
837: return JS_ThrowTypeError(ctx, "Invalid object path");
845: if (!found_obj) return JS_NULL;
846: return JSUObject::create(ctx, found_obj);
852: return JS_NULL;
857: return JS_NULL;
```

### JS `LoadObject`

- 注册 arity：1；实现：`js_load_object`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:861`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
861: JSValue js_load_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
862: {
863: if (argc < 1)
864: {
865: return JS_ThrowTypeError(ctx, "LoadObject requires an object path argument");
868: const char* object_path_utf8 = JS_ToCString(ctx, argv[0]);
871: return JS_ThrowTypeError(ctx, "Invalid object path");
882: return JS_NULL;
885: return JSUObject::create(ctx, found_object);
891: return JS_NULL;
896: return JS_NULL;
```

### JS `ScanBlueprintWidgetsByInterface`

- 注册 arity：2；实现：`js_scan_blueprint_widgets_by_interface`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:900`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
900: JSValue js_scan_blueprint_widgets_by_interface(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
901: {
902: if (argc < 1)
903: {
904: return JS_ThrowTypeError(ctx, "ScanBlueprintWidgetsByInterface requires an interface path argument");
907: const char* interface_path_utf8 = JS_ToCString(ctx, argv[0]);
910: return JS_ThrowTypeError(ctx, "Invalid interface path");
917: if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
918: {
919: const char* root_path_utf8 = JS_ToCString(ctx, argv[1]);
922: return JS_ThrowTypeError(ctx, "Second argument must be a string when provided");
935: return JS_NewArray(ctx);
938: return result;
```

### JS `RegisterHook`

- 注册 arity：3；实现：`js_register_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:958`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
958: JSValue js_register_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
959: {
960: if (argc < 2)
961: return JS_ThrowTypeError(ctx, "RegisterHook requires at least 2 arguments: function_name, pre_callback [, post_callback]");
963: const char* func_name = JS_ToCString(ctx, argv[0]);
965: return JS_ThrowTypeError(ctx, "Invalid function name");
970: if (JS_IsFunction(ctx, argv[1]))
971: pre_callback = argv[1];
972: else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
973: {
975: return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");
978: if (argc >= 3)
979: {
980: if (JS_IsFunction(ctx, argv[2]))
981: post_callback = argv[2];
982: else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
983: {
985: return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
991: bool force_sync = read_hook_sync_option(ctx, argc, argv, 3);
1001: return JS_ThrowReferenceError(ctx, "UFunction not found");
1005: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1007: return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
1009: return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");
1013: JSValue result = JS_NewArray(ctx);
1014: JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
1015: JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
1016: return result;
1022: return JS_ThrowInternalError(ctx, "RegisterHook failed due to exception");
1027: return JS_ThrowInternalError(ctx, "RegisterHook failed due to unknown exception");
```

### JS `RegisterBindHook`

- 注册 arity：3；实现：`js_register_bind_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1031`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1031: JSValue js_register_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1032: {
1033: if (argc < 2)
1034: return JS_ThrowTypeError(ctx, "RegisterBindHook requires at least 2 arguments: function_name, pre_callback [, post_callback]");
1036: const char* func_name = JS_ToCString(ctx, argv[0]);
1038: return JS_ThrowTypeError(ctx, "Invalid function name");
1043: if (JS_IsFunction(ctx, argv[1]))
1044: pre_callback = argv[1];
1045: else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
1046: {
1048: return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");
1051: if (argc >= 3)
1052: {
1053: if (JS_IsFunction(ctx, argv[2]))
1054: post_callback = argv[2];
1055: else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
1056: {
1058: return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
1068: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1070: return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
1072: return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");
1076: JSValue result = JS_NewArray(ctx);
1077: JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, bind_id_a));
1078: JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, bind_id_b));
1079: return result;
1085: return JS_ThrowInternalError(ctx, "RegisterBindHook failed due to exception");
1090: return JS_ThrowInternalError(ctx, "RegisterBindHook failed due to unknown exception");
```

### JS `RegisterNativeObjectMethodHook`

- 注册 arity：2；实现：`js_register_native_object_method_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1202`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1202: JSValue js_register_native_object_method_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1203: {
1204: if (argc < 2)
1205: {
1206: return JS_ThrowTypeError(ctx, "RegisterNativeObjectMethodHook requires 2 arguments: functionPath, options");
1209: const char* function_path = JS_ToCString(ctx, argv[0]);
1212: return JS_ThrowTypeError(ctx, "First argument must be a UFunction path string");
1215: if (!JS_IsObject(argv[1]))
1216: {
1218: return JS_ThrowTypeError(ctx, "Second argument must be an options object");
1225: bool has_method = js_option_string(ctx, argv[1], "method", method_name) && !method_name.empty();
1226: JSValue diagnose_value = JS_GetPropertyStr(ctx, argv[1], "diagnose");
1227: bool diagnose = JS_IsBool(diagnose_value) && JS_ToBool(ctx, diagnose_value) != 0;
1231: return JS_ThrowTypeError(ctx, "options.method must be a method name string unless options.diagnose is true");
1248: return JS_ThrowReferenceError(ctx, "UFunction not found");
1252: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1254: return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
1256: return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");
1262: (void)js_option_string(ctx, argv[1], "label", hook_data->diagnostic_label);
1265: if (js_option_string(ctx, argv[1], "phase", phase))
1266: {
1282: if (js_option_string(ctx, argv[1], "target", target))
1283: {
1288: if (js_option_int32(ctx, argv[1], "paramIndex", param_index) || js_option_int32(ctx, argv[1], "targetParamIndex", param_index))
1289: {
1293: (void)js_option_string(ctx, argv[1], "fullNameContains", hook_data->target_filter);
1294: parse_native_method_args(ctx, argv[1], *hook_data);
1298: JSValue result = JS_NewArray(ctx);
1299: JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
1300: JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
1301: return result;
```

### JS `RegisterLoadMapPreHook`

- 注册 arity：1；实现：`js_register_load_map_pre_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1380`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1380: JSValue js_register_load_map_pre_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1381: {
1382: return js_register_load_map_hook_common(ctx, argc, argv, true);
```

### JS `RegisterLoadMapPostHook`

- 注册 arity：1；实现：`js_register_load_map_post_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1385`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1385: JSValue js_register_load_map_post_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1386: {
1387: return js_register_load_map_hook_common(ctx, argc, argv, false);
```

### JS `HookUFunction`

- 注册 arity：3；实现：`js_hook_ufunction`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1390`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1390: JSValue js_hook_ufunction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1391: {
1392: if (argc < 2)
1393: return JS_ThrowTypeError(ctx, "HookUFunction requires at least 2 arguments: ufunction, pre_callback [, post_callback]");
1396: void* obj = JSUObject::get_uobject(ctx, argv[0]);
1400: return JS_ThrowTypeError(ctx, "First argument must be a UFunction object");
1403: return JS_ThrowTypeError(ctx, "Invalid UFunction object");
1408: if (JS_IsFunction(ctx, argv[1]))
1409: pre_callback = argv[1];
1410: else if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]))
1411: return JS_ThrowTypeError(ctx, "Second argument must be a callback function or null");
1413: if (argc >= 3)
1414: {
1415: if (JS_IsFunction(ctx, argv[2]))
1416: post_callback = argv[2];
1417: else if (!JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2]))
1418: return JS_ThrowTypeError(ctx, "Third argument must be a callback function or null");
1420: bool force_sync = read_hook_sync_option(ctx, argc, argv, 3);
1425: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1427: return JS_ThrowInternalError(ctx, "Hook registration disabled in safe mode");
1429: return JS_ThrowInternalError(ctx, "Hook subsystem disabled by circuit breaker");
1433: JSValue result = JS_NewArray(ctx);
1434: JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, pre_id));
1435: JS_SetPropertyUint32(ctx, result, 1, JS_NewInt32(ctx, post_id));
1436: return result;
1442: return JS_ThrowInternalError(ctx, "HookUFunction failed due to exception");
1447: return JS_ThrowInternalError(ctx, "HookUFunction failed due to unknown exception");
```

### JS `UnregisterHook`

- 注册 arity：2；实现：`js_unregister_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1451`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1451: JSValue js_unregister_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1452: {
1453: if (argc < 2)
1454: return JS_ThrowTypeError(ctx, "UnregisterHook requires 2 arguments: pre_id, post_id");
1457: if (JS_ToInt32(ctx, &pre_id, argv[0]) != 0)
1458: return JS_ThrowTypeError(ctx, "First argument must be a number (pre_id)");
1459: if (JS_ToInt32(ctx, &post_id, argv[1]) != 0)
1460: return JS_ThrowTypeError(ctx, "Second argument must be a number (post_id)");
1465: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1468: return JS_NewBool(ctx, success);
1474: return JS_ThrowInternalError(ctx, "UnregisterHook failed due to exception");
1478: return JS_ThrowInternalError(ctx, "UnregisterHook failed due to unknown exception");
```

### JS `UnregisterBindHook`

- 注册 arity：2；实现：`js_unregister_bind_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1482`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1482: JSValue js_unregister_bind_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1483: {
1484: if (argc < 2)
1485: return JS_ThrowTypeError(ctx, "UnregisterBindHook requires 2 arguments: bind_id, bind_id");
1488: if (JS_ToInt32(ctx, &first_id, argv[0]) != 0)
1489: return JS_ThrowTypeError(ctx, "First argument must be a number (bind_id)");
1490: if (JS_ToInt32(ctx, &second_id, argv[1]) != 0)
1491: return JS_ThrowTypeError(ctx, "Second argument must be a number (bind_id)");
1496: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1499: return JS_NewBool(ctx, success);
1505: return JS_ThrowInternalError(ctx, "UnregisterBindHook failed due to exception");
1509: return JS_ThrowInternalError(ctx, "UnregisterBindHook failed due to unknown exception");
```

### JS `UnregisterLoadMapHook`

- 注册 arity：1；实现：`js_unregister_load_map_hook`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1513`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1513: JSValue js_unregister_load_map_hook(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1514: {
1515: if (argc < 1)
1516: {
1517: return JS_ThrowTypeError(ctx, "UnregisterLoadMapHook requires 1 argument: callbackId");
1521: if (JS_ToInt32(ctx, &callback_id, argv[0]) != 0)
1522: {
1523: return JS_ThrowTypeError(ctx, "First argument must be a number (callbackId)");
1529: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1532: return JS_NewBool(ctx, success);
1538: return JS_ThrowInternalError(ctx, "UnregisterLoadMapHook failed due to exception");
1542: return JS_ThrowInternalError(ctx, "UnregisterLoadMapHook failed due to unknown exception");
```

### JS `NotifyOnNewObject`

- 注册 arity：2；实现：`js_notify_on_new_object`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1546`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1546: JSValue js_notify_on_new_object(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1547: {
1548: if (argc < 2)
1549: return JS_ThrowTypeError(ctx, "NotifyOnNewObject requires 2 arguments: class_name, callback");
1551: const char* class_name = JS_ToCString(ctx, argv[0]);
1553: return JS_ThrowTypeError(ctx, "Invalid class name");
1555: if (!JS_IsFunction(ctx, argv[1]))
1556: {
1558: return JS_ThrowTypeError(ctx, "Second argument must be a callback function");
1565: return JS_UNDEFINED;
```

### JS `RegisterKeyBind`

- 注册 arity：3；实现：`js_register_key_bind`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1619`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1619: JSValue js_register_key_bind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1620: {
1621: if (argc < 2)
1622: return JS_ThrowTypeError(ctx, "RegisterKeyBind requires at least 2 arguments: key, callback [, modifiers]");
1624: const char* key_name = JS_ToCString(ctx, argv[0]);
1626: return JS_ThrowTypeError(ctx, "First argument must be a key name string");
1632: return JS_ThrowTypeError(ctx, "Invalid key name");
1634: if (!JS_IsFunction(ctx, argv[1]))
1635: return JS_ThrowTypeError(ctx, "Second argument must be a callback function");
1638: if (argc >= 3 && JS_IsObject(argv[2]))
1639: {
1640: JSValue ctrl_val = JS_GetPropertyStr(ctx, argv[2], "ctrl");
1641: JSValue shift_val = JS_GetPropertyStr(ctx, argv[2], "shift");
1642: JSValue alt_val = JS_GetPropertyStr(ctx, argv[2], "alt");
1644: if (JS_IsBool(ctrl_val)) with_ctrl = JS_ToBool(ctx, ctrl_val);
1645: if (JS_IsBool(shift_val)) with_shift = JS_ToBool(ctx, shift_val);
1646: if (JS_IsBool(alt_val)) with_alt = JS_ToBool(ctx, alt_val);
1656: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1658: bool success = mod->register_key_bind(ctx, static_cast<uint8_t>(key), argv[1], with_ctrl, with_shift, with_alt);
1659: return JS_NewBool(ctx, success);
1665: return JS_ThrowInternalError(ctx, "RegisterKeyBind failed due to exception");
1669: return JS_ThrowInternalError(ctx, "RegisterKeyBind failed due to unknown exception");
```

### JS `CallFunction`

- 注册 arity：3；实现：`js_call_function`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1868`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1868: JSValue js_call_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1869: {
1870: if (argc < 2)
1871: return JS_ThrowTypeError(ctx, "CallFunction requires at least 2 arguments: object, functionName [, ...args]");
1873: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
1875: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
1878: const char* func_name = JS_ToCString(ctx, argv[1]);
1880: return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
1890: return JS_ThrowReferenceError(ctx, "Function not found on object");
1897: return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
1900: if (!fill_call_params(ctx, function, params_memory, argc, argv, 2, raw_string_buffers))
1901: {
1904: return JS_ThrowInternalError(ctx, "CallFunction: parameter fill crashed (SEH)");
1936: return JS_TRUE;
1951: return JS_ThrowInternalError(ctx, "ProcessEvent crashed (SEH exception caught)");
1971: return ret_val;
1979: return JS_ThrowInternalError(ctx, "CallFunction failed due to exception");
1985: return JS_ThrowInternalError(ctx, "CallFunction failed due to unknown exception");
```

### JS `CallFunctionEx`

- 注册 arity：3；实现：`js_call_function_ex`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:2036`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
2036: JSValue js_call_function_ex(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
2037: {
2038: if (argc < 2)
2039: {
2040: return JS_ThrowTypeError(ctx, "CallFunctionEx requires at least 2 arguments: object, functionName [, ...args]");
2043: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
2046: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
2050: const char* func_name = JS_ToCString(ctx, argv[1]);
2053: return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
2066: return JS_ThrowReferenceError(ctx, "Function not found on object");
2071: return JS_ThrowInternalError(ctx, "CallFunctionEx does not support net functions");
2080: return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
2084: if (!fill_call_params(ctx, function, params_memory, argc, argv, 2, raw_string_buffers))
2085: {
2088: return JS_ThrowInternalError(ctx, "CallFunctionEx: parameter fill crashed (SEH)");
2095: return JS_ThrowInternalError(ctx, "ProcessEvent crashed (SEH exception caught)");
2102: return outputs;
2110: return JS_ThrowInternalError(ctx, "CallFunctionEx failed due to exception");
2116: return JS_ThrowInternalError(ctx, "CallFunctionEx failed due to unknown exception");
```

### JS `__withExecBudget`

- 注册 arity：2；实现：`js_with_exec_budget`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1989`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1989: JSValue js_with_exec_budget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1990: {
1991: if (argc < 2)
1992: {
1993: return JS_ThrowTypeError(ctx, "__withExecBudget requires 2 arguments: budgetMs, callback");
1997: if (JS_ToFloat64(ctx, &budget_ms, argv[0]) != 0)
1998: {
1999: return JS_ThrowTypeError(ctx, "First argument must be a number (budgetMs)");
2002: if (!JS_IsFunction(ctx, argv[1]))
2003: {
2004: return JS_ThrowTypeError(ctx, "Second argument must be a callback function");
2010: return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
2024: const bool ok = safe_js_call(ctx, argv[1], JS_UNDEFINED, 0, nullptr, &result);
2030: return JS_ThrowInternalError(ctx, "__withExecBudget failed due to exception");
2033: return result;
```

### JS `setTimeout`

- 注册 arity：2；实现：`js_set_timeout`；来源：`crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:148`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
148: JSValue js_set_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
149: {
150: if (argc < 2)
151: return JS_ThrowTypeError(ctx, "setTimeout requires 2 arguments: callback, delay");
152: if (!JS_IsFunction(ctx, argv[0]))
153: return JS_ThrowTypeError(ctx, "First argument must be a callback function");
156: if (JS_ToFloat64(ctx, &delay_ms, argv[1]) != 0)
157: return JS_ThrowTypeError(ctx, "Second argument must be a number (delay in ms)");
161: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
163: return JS_ThrowInternalError(ctx, "Timer subsystem disabled by circuit breaker");
165: int32_t timer_id = mod->add_timer(ctx, argv[0], delay_ms, false);
166: return JS_NewInt32(ctx, timer_id);
```

### JS `setInterval`

- 注册 arity：2；实现：`js_set_interval`；来源：`crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:169`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
169: JSValue js_set_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
170: {
171: if (argc < 2)
172: return JS_ThrowTypeError(ctx, "setInterval requires 2 arguments: callback, interval");
173: if (!JS_IsFunction(ctx, argv[0]))
174: return JS_ThrowTypeError(ctx, "First argument must be a callback function");
177: if (JS_ToFloat64(ctx, &interval_ms, argv[1]) != 0)
178: return JS_ThrowTypeError(ctx, "Second argument must be a number (interval in ms)");
182: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
184: return JS_ThrowInternalError(ctx, "Timer subsystem disabled by circuit breaker");
186: int32_t timer_id = mod->add_timer(ctx, argv[0], interval_ms, true);
187: return JS_NewInt32(ctx, timer_id);
```

### JS `clearTimeout`

- 注册 arity：1；实现：`js_clear_timeout`；来源：`crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:190`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
190: JSValue js_clear_timeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
191: {
192: if (argc < 1) return JS_UNDEFINED;
195: if (JS_ToInt32(ctx, &timer_id, argv[0]) != 0)
196: return JS_UNDEFINED;
201: return JS_UNDEFINED;
```

### JS `clearInterval`

- 注册 arity：1；实现：`js_clear_interval`；来源：`crates/ue4ssl-javascript/native/cpp/JSTimer.cpp:204`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
204: JSValue js_clear_interval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
205: {
206: return js_clear_timeout(ctx, this_val, argc, argv);
```

### JS `fetch`

- 注册 arity：2；实现：`js_fetch`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:395`。
- 错误类别：InternalError; TypeError; Promise rejection path。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
395: JSValue js_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
396: {
399: if (!mod) return JS_ThrowInternalError(ctx, "No JSMod");
401: return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");
403: if (argc < 1) return JS_ThrowTypeError(ctx, "fetch requires at least 1 argument (url)");
404: const char* url_cstr = JS_ToCString(ctx, argv[0]);
405: if (!url_cstr) return JS_ThrowTypeError(ctx, "fetch: url must be a string");
410: parse_fetch_options(ctx, argc, argv, method, headers_str, body);
413: JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
414: if (JS_IsException(promise)) return promise;
415: JSValue resolve_func = JS_DupValue(ctx, resolving_funcs[0]);
416: JSValue reject_func = JS_DupValue(ctx, resolving_funcs[1]);
424: mod->m_fetch_pending[id] = { resolve_func, reject_func };
435: return promise;
```

### JS `fetchSync`

- 注册 arity：2；实现：`js_fetch_sync`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:438`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
438: JSValue js_fetch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
439: {
442: if (!mod) return JS_ThrowInternalError(ctx, "No JSMod");
444: return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");
446: if (argc < 1) return JS_ThrowTypeError(ctx, "fetchSync requires at least 1 argument (url)");
447: const char* url_cstr = JS_ToCString(ctx, argv[0]);
448: if (!url_cstr) return JS_ThrowTypeError(ctx, "fetchSync: url must be a string");
453: parse_fetch_options(ctx, argc, argv, method, headers_str, body);
464: return JS_ThrowInternalError(ctx, "fetchSync exception: %s", e.what());
468: return JS_ThrowInternalError(ctx, "fetchSync: unknown exception during HTTP request");
473: return JS_ThrowInternalError(ctx, "%s", error_msg.c_str());
476: JSValue resp = JS_NewObject(ctx);
477: JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
478: JS_SetPropertyStr(ctx, resp, "status", JS_NewInt32(ctx, status));
479: JS_SetPropertyStr(ctx, resp, "body", JS_NewString(ctx, resp_body.c_str()));
480: return resp;
```

### JS `SigScan`

- 注册 arity：1；实现：`js_sig_scan`；来源：`crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:8`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
8: JSValue js_sig_scan(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
9: {
10: if (argc < 1)
11: return JS_ThrowTypeError(ctx, "SigScan requires a signature string argument");
13: const char* sig_str = JS_ToCString(ctx, argv[0]);
15: return JS_ThrowTypeError(ctx, "Invalid signature string");
30: return true;
44: return JS_NULL;
48: return JS_NewBigInt64(ctx, static_cast<int64_t>(address));
54: return JS_ThrowInternalError(ctx, "SigScan exception: %s", e.what());
59: return JS_ThrowInternalError(ctx, "SigScan: unknown exception during scan");
```

### JS `PatchByte`

- 注册 arity：2；实现：`js_patch_byte`；来源：`crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:78`。
- 错误类别：InternalError; RangeError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
78: JSValue js_patch_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
79: {
82: return JS_ThrowInternalError(ctx, "PatchByte is disabled while engine is in safe mode");
84: if (argc < 2)
85: return JS_ThrowTypeError(ctx, "PatchByte requires 2 arguments: address (BigInt), value (number)");
88: if (JS_ToBigInt64(ctx, &addr64, argv[0]))
89: return JS_ThrowTypeError(ctx, "PatchByte: first argument must be a BigInt address");
92: if (JS_ToInt32(ctx, &value, argv[1]))
93: return JS_ThrowTypeError(ctx, "PatchByte: second argument must be a number (0-255)");
96: return JS_ThrowRangeError(ctx, "PatchByte: value must be in range 0-255");
102: return JS_ThrowInternalError(ctx, "PatchByte: VirtualProtect failed at 0x%llX", static_cast<unsigned long long>(addr64));
108: return JS_ThrowInternalError(ctx, "PatchByte: access violation at 0x%llX", static_cast<unsigned long long>(addr64));
116: return JS_TRUE;
```

### JS `ReadByte`

- 注册 arity：1；实现：`js_read_byte`；来源：`crates/ue4ssl-javascript/native/cpp/JSMemory.cpp:120`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
120: JSValue js_read_byte(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
121: {
122: if (argc < 1)
123: return JS_ThrowTypeError(ctx, "ReadByte requires 1 argument: address (BigInt)");
126: if (JS_ToBigInt64(ctx, &addr64, argv[0]))
127: return JS_ThrowTypeError(ctx, "ReadByte: argument must be a BigInt address");
133: return JS_NewInt32(ctx, *target);
137: return JS_ThrowInternalError(ctx, "ReadByte: access violation at 0x%llX", static_cast<unsigned long long>(addr64));
```

### JS `readFile`

- 注册 arity：1；实现：`js_read_file`；来源：`crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:27`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
27: JSValue js_read_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
28: {
29: if (argc < 1)
30: return JS_ThrowTypeError(ctx, "readFile requires 1 argument: path");
32: const char* path_str = JS_ToCString(ctx, argv[0]);
34: return JS_ThrowTypeError(ctx, "Invalid path argument");
43: return JS_NULL;
47: return JS_NULL;
54: return JS_NewStringLen(ctx, content.c_str(), content.size());
60: return JS_NULL;
64: return JS_NULL;
```

### JS `writeFile`

- 注册 arity：2；实现：`js_write_file`；来源：`crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:68`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
68: JSValue js_write_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
69: {
70: if (argc < 2)
71: return JS_ThrowTypeError(ctx, "writeFile requires 2 arguments: path, content");
73: const char* path_str = JS_ToCString(ctx, argv[0]);
75: return JS_ThrowTypeError(ctx, "Invalid path argument");
78: const char* content_str = JS_ToCStringLen(ctx, &content_len, argv[1]);
82: return JS_ThrowTypeError(ctx, "Invalid content argument");
101: return JS_NewBool(ctx, false);
108: return JS_NewBool(ctx, true);
115: return JS_NewBool(ctx, false);
120: return JS_NewBool(ctx, false);
```

### JS `getModsDirectory`

- 注册 arity：0；实现：`js_get_mods_directory`；来源：`crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:124`。
- 错误类别：InternalError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
124: JSValue js_get_mods_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
125: {
126: (void)argc; (void)argv;
131: return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
139: return JS_NewString(ctx, utf8.c_str());
143: return JS_ThrowInternalError(ctx, "getModsDirectory failed");
```

### JS `getGameDirectory`

- 注册 arity：0；实现：`js_get_game_directory`；来源：`crates/ue4ssl-javascript/native/cpp/JSFileIO.cpp:147`。
- 错误类别：InternalError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
147: JSValue js_get_game_directory(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
148: {
149: (void)argc; (void)argv;
162: return JS_NewString(ctx, utf8.c_str());
168: return JS_ThrowInternalError(ctx, "getGameDirectory failed");
172: return JS_ThrowInternalError(ctx, "getGameDirectory failed");
```

### JS `downloadFile`

- 注册 arity：3；实现：`js_download_file`；来源：`crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:453`。
- 错误类别：InternalError; TypeError; Promise rejection path。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
453: JSValue js_download_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
454: {
460: return JS_ThrowInternalError(ctx, "No JSMod");
464: return JS_ThrowInternalError(ctx, "Fetch subsystem disabled by circuit breaker");
467: if (argc < 2)
468: {
469: return JS_ThrowTypeError(ctx, "downloadFile requires 2 arguments: url, path");
472: std::string url = js_value_to_utf8_string(ctx, argv[0]);
475: return JS_ThrowTypeError(ctx, "downloadFile: url must be a string");
478: std::string path = js_value_to_utf8_string(ctx, argv[1]);
481: return JS_ThrowTypeError(ctx, "downloadFile: path must be a string");
485: parse_download_options(ctx, argc, argv, options);
488: JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
489: if (JS_IsException(promise))
490: {
491: return promise;
494: JSValue resolve_func = JS_DupValue(ctx, resolving_funcs[0]);
495: JSValue reject_func = JS_DupValue(ctx, resolving_funcs[1]);
503: mod->m_download_pending[id] = { resolve_func, reject_func };
513: return promise;
```

### JS `downloadFileSync`

- 注册 arity：3；实现：`js_download_file_sync`；来源：`crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:516`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
516: JSValue js_download_file_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
517: {
520: if (argc < 2)
521: {
522: return JS_ThrowTypeError(ctx, "downloadFileSync requires 2 arguments: url, path");
525: std::string url = js_value_to_utf8_string(ctx, argv[0]);
528: return JS_ThrowTypeError(ctx, "downloadFileSync: url must be a string");
531: std::string path = js_value_to_utf8_string(ctx, argv[1]);
534: return JS_ThrowTypeError(ctx, "downloadFileSync: path must be a string");
538: parse_download_options(ctx, argc, argv, options);
557: return make_download_result(ctx, path, result);
```

### JS `playSoundFile`

- 注册 arity：2；实现：`js_play_sound_file`；来源：`crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:630`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
630: JSValue js_play_sound_file(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
631: {
634: if (argc < 1)
635: {
636: return JS_ThrowTypeError(ctx, "playSoundFile requires 1 argument: path");
639: std::string path = js_value_to_utf8_string(ctx, argv[0]);
642: return JS_ThrowTypeError(ctx, "playSoundFile: path must be a string");
648: if (argc >= 2 && JS_IsObject(argv[1]))
649: {
650: async = get_bool_option(ctx, argv[1], "async", true);
651: loop = get_bool_option(ctx, argv[1], "loop", false);
652: no_stop = get_bool_option(ctx, argv[1], "noStop", false);
663: return JS_ThrowTypeError(ctx, "playSoundFile: invalid path encoding");
678: return JS_NewBool(ctx, success == TRUE);
```

### JS `stopSound`

- 注册 arity：0；实现：`js_stop_sound`；来源：`crates/ue4ssl-javascript/native/cpp/JSAudio.cpp:681`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
681: JSValue js_stop_sound(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
682: {
684: (void)argc;
688: return JS_NewBool(ctx, success == TRUE);
```

### JS `GetProperty`

- 注册 arity：2；实现：`js_get_property`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:156`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
156: JSValue js_get_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
157: {
158: if (argc < 2)
159: return JS_ThrowTypeError(ctx, "GetProperty requires 2 arguments: uobject, propertyName");
161: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
163: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
166: const char* prop_name = JS_ToCString(ctx, argv[1]);
168: return JS_ThrowTypeError(ctx, "Second argument must be a property name string");
178: return JS_NULL;
183: if (!seh_resolve_property_path(object, parts, &prop, &data))
184: {
185: return JS_NULL;
189: return JS_NULL;
192: return property_to_jsvalue(ctx, prop, data);
198: return JS_NULL;
202: return JS_NULL;
```

### JS `SetProperty`

- 注册 arity：3；实现：`js_set_property`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:206`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
206: JSValue js_set_property(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
207: {
208: if (argc < 3)
209: return JS_ThrowTypeError(ctx, "SetProperty requires 3 arguments: uobject, propertyName, value");
211: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
213: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
216: const char* prop_name = JS_ToCString(ctx, argv[1]);
218: return JS_ThrowTypeError(ctx, "Second argument must be a property name string");
228: return JS_NewBool(ctx, false);
233: if (!seh_resolve_property_path(object, parts, &prop, &data))
234: {
235: return JS_NewBool(ctx, false);
239: return JS_NewBool(ctx, false);
242: jsvalue_to_property(ctx, prop, data, argv[2]);
243: return JS_NewBool(ctx, true);
249: return JS_NewBool(ctx, false);
253: return JS_NewBool(ctx, false);
```

### JS `ExportPropertyText`

- 注册 arity：2；实现：`js_export_property_text`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:303`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
303: JSValue js_export_property_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
304: {
305: if (argc < 2)
306: return JS_ThrowTypeError(ctx, "ExportPropertyText requires 2 arguments: uobject, propertyName");
308: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
310: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
313: const char* prop_name = JS_ToCString(ctx, argv[1]);
315: return JS_ThrowTypeError(ctx, "Second argument must be a property name string");
325: return JS_NULL;
330: if (!seh_resolve_property_path(object, parts, &prop, &data) || !prop || !data)
331: {
332: return JS_NULL;
338: return JS_NULL;
341: return JS_NewString(ctx, wide_to_utf8(exported).c_str());
347: return JS_NULL;
351: return JS_NULL;
```

### JS `GetPropertyPath`

- 注册 arity：2；实现：`js_get_property_path`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:355`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
355: JSValue js_get_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
356: {
357: return js_get_property(ctx, this_val, argc, argv);
```

### JS `SetPropertyPath`

- 注册 arity：3；实现：`js_set_property_path`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:360`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
360: JSValue js_set_property_path(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
361: {
362: return js_set_property(ctx, this_val, argc, argv);
```

### JS `ApplyObjectPatch`

- 注册 arity：2；实现：`js_apply_object_patch`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyAccess.cpp:365`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
365: JSValue js_apply_object_patch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
366: {
367: if (argc < 2)
368: return JS_ThrowTypeError(ctx, "ApplyObjectPatch requires 2 arguments: uobject, patchObject");
370: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
372: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
373: if (!JS_IsObject(argv[1]))
374: return JS_ThrowTypeError(ctx, "Second argument must be an object");
381: if (JS_GetOwnPropertyNames(ctx, &names, &len, argv[1], JS_GPN_STRING_MASK) != 0)
382: {
383: return JS_ThrowInternalError(ctx, "ApplyObjectPatch could not enumerate patch object");
389: JSValue value = JS_GetProperty(ctx, argv[1], names[i].atom);
390: if (!key || JS_IsUndefined(value))
391: {
397: JSValue path = JS_NewString(ctx, key);
398: JSValueConst set_args[3] = { argv[0], path, value };
400: if (JS_ToBool(ctx, set_result))
401: applied++;
413: JSValue result = JS_NewObject(ctx);
414: JS_SetPropertyStr(ctx, result, "applied", JS_NewUint32(ctx, applied));
415: JS_SetPropertyStr(ctx, result, "failed", JS_NewUint32(ctx, failed));
416: JS_SetPropertyStr(ctx, result, "__success", JS_NewBool(ctx, failed == 0));
417: return result;
```

### JS `BindDelegate`

- 注册 arity：4；实现：`js_bind_delegate`；来源：`crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:467`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
467: JSValue js_bind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
468: {
469: if (argc < 4)
470: return JS_ThrowTypeError(ctx,
471: "BindDelegate requires 4 arguments: delegateOwner, delegateName, targetObject, functionName");
473: void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
475: return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
478: const char* delegate_name_c = JS_ToCString(ctx, argv[1]);
480: return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");
482: void* target_ptr = JSUObject::get_uobject(ctx, argv[2]);
485: return JS_ThrowTypeError(ctx, "Third argument must be a UObject (target object)");
489: const char* func_name_c = JS_ToCString(ctx, argv[3]);
492: return JS_ThrowTypeError(ctx, "Fourth argument must be a string (function name)");
508: return JS_NewBool(ctx, false);
522: return JS_NewBool(ctx, true);
529: return JS_NewBool(ctx, false);
533: return JS_NewBool(ctx, false);
```

### JS `UnbindDelegate`

- 注册 arity：4；实现：`js_unbind_delegate`；来源：`crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:539`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
539: JSValue js_unbind_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
540: {
541: if (argc < 4)
542: return JS_ThrowTypeError(ctx,
543: "UnbindDelegate requires 4 arguments: delegateOwner, delegateName, targetObject, functionName");
545: void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
547: return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
550: const char* delegate_name_c = JS_ToCString(ctx, argv[1]);
552: return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");
554: void* target_ptr = JSUObject::get_uobject(ctx, argv[2]);
557: return JS_ThrowTypeError(ctx, "Third argument must be a UObject (target object)");
561: const char* func_name_c = JS_ToCString(ctx, argv[3]);
564: return JS_ThrowTypeError(ctx, "Fourth argument must be a string (function name)");
576: return JS_NewBool(ctx, false);
582: return JS_NewBool(ctx, true);
586: return JS_NewBool(ctx, false);
```

### JS `ClearDelegate`

- 注册 arity：2；实现：`js_clear_delegate`；来源：`crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:592`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
592: JSValue js_clear_delegate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
593: {
594: if (argc < 2)
595: return JS_ThrowTypeError(ctx, "ClearDelegate requires 2 arguments: delegateOwner, delegateName");
597: void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
599: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
602: const char* name_c = JS_ToCString(ctx, argv[1]);
604: return JS_ThrowTypeError(ctx, "Second argument must be a string");
613: return JS_NewBool(ctx, false);
616: return JS_NewBool(ctx, true);
620: return JS_NewBool(ctx, false);
```

### JS `RegisterProcessEventWatch`

- 注册 arity：2；实现：`js_register_pe_watch`；来源：`crates/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp:1304`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1304: JSValue js_register_pe_watch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1305: {
1306: if (argc < 2)
1307: return JS_ThrowTypeError(ctx, "RegisterProcessEventWatch requires 2 arguments: function_name, callback");
1309: const char* func_name = JS_ToCString(ctx, argv[0]);
1311: return JS_ThrowTypeError(ctx, "First argument must be a string");
1313: if (!JS_IsFunction(ctx, argv[1]))
1314: {
1316: return JS_ThrowTypeError(ctx, "Second argument must be a function");
1323: return JS_ThrowInternalError(ctx, "No mod context");
1329: int idx = mod->register_process_event_watch(ctx, wide_name, argv[1]);
1330: return JS_NewInt32(ctx, idx);
```

### JS `BindDelegateCallback`

- 注册 arity：3；实现：`js_bind_delegate_callback`；来源：`crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:981`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
981: JSValue js_bind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
982: {
985: return JS_NewInt32(ctx, -1);
987: return JS_NewInt32(ctx, -1);
989: return JS_NewInt32(ctx, -1);
991: if (argc < 3)
992: return JS_ThrowTypeError(ctx,
993: "BindDelegateCallback requires 3 arguments: delegateOwner, delegateName, callback");
995: void* owner_ptr = JSUObject::get_uobject(ctx, argv[0]);
997: return JS_ThrowTypeError(ctx, "First argument must be a UObject (delegate owner)");
1000: const char* name_c = JS_ToCString(ctx, argv[1]);
1002: return JS_ThrowTypeError(ctx, "Second argument must be a string (delegate name)");
1004: if (!JS_IsFunction(ctx, argv[2])) {
1006: return JS_ThrowTypeError(ctx, "Third argument must be a function (callback)");
1023: return JS_NewInt32(ctx, -1);
1033: return JS_NewInt32(ctx, -1);
1047: return JS_NewInt32(ctx, -1);
1064: return JS_NewInt32(ctx, -1);
1088: return JS_NewInt32(ctx, -1);
1113: JS_DupValue(ctx, argv[2]),
1114: owner,
1115: wide_name,
1116: sig_func_name,
1117: shared_hook->id,
1118: });
1125: return JS_NewInt32(ctx, callback_id);
1151: return JS_NewInt32(ctx, -1);
1176: return JS_NewInt32(ctx, -1);
```

### JS `UnbindDelegateCallback`

- 注册 arity：1；实现：`js_unbind_delegate_callback`；来源：`crates/ue4ssl-javascript/native/cpp/JSDelegate.cpp:1182`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1182: JSValue js_unbind_delegate_callback(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1183: {
1185: if (argc < 1)
1186: return JS_ThrowTypeError(ctx,
1187: "UnbindDelegateCallback requires 1 argument: callbackId");
1190: if (JS_ToInt32(ctx, &callback_id, argv[0]))
1191: return JS_NewBool(ctx, false);
1218: if (it->ctx && !JS_IsUndefined(it->callback))
1219: {
1231: return JS_NewBool(ctx, true);
1235: return JS_NewBool(ctx, false);
```

### JS `NewUObject`

- 注册 arity：2；实现：`js_new_uobject`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:413`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
413: JSValue js_new_uobject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
414: {
415: if (argc < 2)
416: return JS_ThrowTypeError(ctx, "NewUObject requires at least 2 arguments: class (string or UObject), outer");
419: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
425: if (JS_IsString(argv[0]))
426: {
427: const char* class_path = JS_ToCString(ctx, argv[0]);
429: return JS_ThrowTypeError(ctx, "Invalid class path string");
440: return JS_ThrowReferenceError(ctx, "Class not found");
446: void* cls_ptr = JSUObject::get_uobject(ctx, argv[0]);
448: return JS_ThrowTypeError(ctx, "First argument must be a class path string or UClass object");
450: return JS_ThrowInternalError(ctx, "NewUObject: UClass pointer is stale");
454: void* outer_ptr = JSUObject::get_uobject(ctx, argv[1]);
456: return JS_ThrowInternalError(ctx, "NewUObject: outer UObject pointer is stale");
466: return JS_ThrowInternalError(ctx, "StaticConstructObject_Internal failed (SEH or null result)");
467: return JSUObject::create(ctx, new_obj);
475: return nullptr;
478: return static_cast<void*>(new_obj);
482: return JS_ThrowInternalError(ctx, "StaticConstructObject_Internal failed on game thread");
484: return JSUObject::create(ctx, result);
490: return JS_ThrowInternalError(ctx, "NewUObject failed due to exception");
495: return JS_ThrowInternalError(ctx, "NewUObject failed due to unknown exception");
```

### JS `__umgDispatchSync`

- 注册 arity：1；实现：`js_umg_dispatch_sync`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:746`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
746: JSValue js_umg_dispatch_sync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
747: {
748: if (argc < 2)
749: return JS_ThrowTypeError(ctx, "__umgDispatchSync requires: object, functionName [, ...args]");
752: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
755: return JS_ThrowInternalError(ctx, "__umgDispatchSync: UMG dispatcher failed to register");
757: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
759: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
761: return JS_ThrowInternalError(ctx, "__umgDispatchSync: UObject pointer is stale");
764: const char* func_name = JS_ToCString(ctx, argv[1]);
766: return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
774: return JS_ThrowReferenceError(ctx, "Function not found on object");
782: return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
794: if (js_arg_index >= argc) break;
797: jsvalue_to_property(ctx, prop, prop_addr, argv[js_arg_index]);
807: return nullptr;
811: return nullptr;
813: return make_dispatch_success_token(params_memory);
817: return JS_ThrowInternalError(ctx, "__umgDispatchSync: ProcessEvent failed on game thread");
833: return ret;
839: return JS_ThrowInternalError(ctx, "__umgDispatchSync failed due to exception");
844: return JS_ThrowInternalError(ctx, "__umgDispatchSync failed due to unknown exception");
```

### JS `__umgDispatchAsync`

- 注册 arity：1；实现：`js_umg_dispatch_async`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:852`。
- 错误类别：InternalError; ReferenceError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
852: JSValue js_umg_dispatch_async(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
853: {
854: if (argc < 2)
855: return JS_ThrowTypeError(ctx, "__umgDispatchAsync requires: object, functionName [, ...args]");
858: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
861: return JS_ThrowInternalError(ctx, "__umgDispatchAsync: UMG dispatcher failed to register");
863: void* obj_ptr = JSUObject::get_uobject(ctx, argv[0]);
865: return JS_ThrowTypeError(ctx, "First argument must be a UObject");
867: return JS_ThrowInternalError(ctx, "__umgDispatchAsync: UObject pointer is stale");
870: const char* func_name = JS_ToCString(ctx, argv[1]);
872: return JS_ThrowTypeError(ctx, "Second argument must be a function name string");
881: return JS_ThrowReferenceError(ctx, "Function not found on object");
888: return JS_ThrowInternalError(ctx, "Failed to allocate params memory");
896: if (js_arg_index >= argc) break;
899: jsvalue_to_property(ctx, prop, prop_addr, argv[js_arg_index]);
911: return;
921: return JS_UNDEFINED;
928: return JS_ThrowInternalError(ctx, "__umgDispatchAsync failed due to exception");
934: return JS_ThrowInternalError(ctx, "__umgDispatchAsync failed due to unknown exception");
```

### JS `__umgCreateUserWidget`

- 注册 arity：3；实现：`js_umg_create_user_widget`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:499`。
- 错误类别：InternalError; TypeError; pending JS_EXCEPTION。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
499: JSValue js_umg_create_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
500: {
502: if (argc < 1)
503: {
504: return JS_ThrowTypeError(ctx, "__umgCreateUserWidget requires: widgetClass [, worldContextObject [, owningPlayer]]");
510: return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
513: Unreal::UClass* widget_class = resolve_uclass_argument(ctx, argv[0], "__umgCreateUserWidget");
516: return JS_EXCEPTION;
519: void* world_context_ptr = argc >= 2 ? JSUObject::get_uobject(ctx, argv[1]) : nullptr;
522: return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: world context UObject pointer is stale");
525: void* owning_player_ptr = argc >= 3 ? JSUObject::get_uobject(ctx, argv[2]) : nullptr;
528: return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: owning player UObject pointer is stale");
553: return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: could not resolve a world context object");
558: return JS_ThrowInternalError(ctx, "__umgCreateUserWidget: UMG dispatcher failed to register");
571: return nullptr;
574: Unreal::UObject* library_cdo = resolve_widget_blueprint_library_cdo();
577: Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCreateUserWidget: failed to resolve WidgetBlueprintLibrary CDO\n"));
578: return nullptr;
585: return nullptr;
592: return nullptr;
599: return nullptr;
607: return nullptr;
615: return nullptr;
620: return created_widget;
625: return JS_ThrowInternalError(ctx, "__umgCreateUserWidget failed");
628: return JSUObject::create(ctx, result);
```

### JS `__umgCloneUserWidget`

- 注册 arity：2；实现：`js_umg_clone_user_widget`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:631`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
631: JSValue js_umg_clone_user_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
632: {
634: if (argc < 1)
635: {
636: return JS_ThrowTypeError(ctx, "__umgCloneUserWidget requires: sourceWidget [, outerObject]");
642: return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
645: void* source_ptr = JSUObject::get_uobject(ctx, argv[0]);
648: return JS_ThrowTypeError(ctx, "__umgCloneUserWidget: sourceWidget must be a UObject");
652: return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: sourceWidget UObject pointer is stale");
655: void* outer_ptr = argc >= 2 ? JSUObject::get_uobject(ctx, argv[1]) : nullptr;
658: return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: outer UObject pointer is stale");
666: return JS_ThrowInternalError(ctx, "__umgCloneUserWidget: UMG dispatcher failed to register");
676: return nullptr;
683: return nullptr;
709: Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] __umgCloneUserWidget: failed to resolve clone outer\n"));
710: return nullptr;
717: return nullptr;
723: return nullptr;
726: return cloned_widget;
731: return JS_ThrowInternalError(ctx, "__umgCloneUserWidget failed");
734: return JSUObject::create(ctx, result);
```

### JS `__umgSetUserWidgetRoot`

- 注册 arity：2；实现：`js_umg_set_user_widget_root`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:942`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
942: JSValue js_umg_set_user_widget_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
943: {
945: if (argc < 2)
946: return JS_ThrowTypeError(ctx, "__umgSetUserWidgetRoot requires: userWidget, rootWidget");
949: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
951: void* user_ptr = JSUObject::get_uobject(ctx, argv[0]);
953: return JS_ThrowTypeError(ctx, "First argument must be a UObject (UserWidget)");
955: return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot: UserWidget pointer is stale");
956: void* root_ptr = JSUObject::get_uobject(ctx, argv[1]);
958: return JS_ThrowTypeError(ctx, "Second argument must be a UObject (root widget)");
960: return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot: root widget pointer is stale");
973: return reinterpret_cast<void*>(static_cast<uintptr_t>(1));
984: return reinterpret_cast<void*>(static_cast<uintptr_t>(2));
992: return reinterpret_cast<void*>(static_cast<uintptr_t>(3));
1001: return reinterpret_cast<void*>(static_cast<uintptr_t>(4));
1005: return nullptr;
1011: return JS_ThrowInternalError(ctx, "__umgSetUserWidgetRoot failed (code %d)", (int)err);
1013: return JS_UNDEFINED;
```

### JS `__umgConstructWidget`

- 注册 arity：2；实现：`js_umg_construct_widget`；来源：`crates/ue4ssl-javascript/native/cpp/JSGameThreadDispatcher.cpp:1020`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
1020: JSValue js_umg_construct_widget(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
1021: {
1023: if (argc < 2)
1024: return JS_ThrowTypeError(ctx, "__umgConstructWidget requires: userWidget, widgetClassName");
1027: if (!mod) return JS_ThrowInternalError(ctx, "Could not get JSMod instance");
1029: void* user_ptr = JSUObject::get_uobject(ctx, argv[0]);
1031: return JS_ThrowTypeError(ctx, "First argument must be a UObject (UserWidget)");
1033: return JS_ThrowInternalError(ctx, "__umgConstructWidget: UserWidget pointer is stale");
1035: const char* class_name = JS_ToCString(ctx, argv[1]);
1037: return JS_ThrowTypeError(ctx, "Second argument must be a widget class name string");
1052: return nullptr;
1063: return nullptr;
1071: return nullptr;
1082: return nullptr;
1091: return nullptr;
1095: return static_cast<void*>(new_widget);
1099: return JS_ThrowInternalError(ctx, "__umgConstructWidget failed");
1101: return JSUObject::create(ctx, result);
```

### JS `getReader`

- 注册 arity：0；实现：`js_fetch_body_get_reader`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:547`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
547: JSValue js_fetch_body_get_reader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
548: {
549: (void)argc; (void)argv;
553: return JS_ThrowTypeError(ctx, "ReadableStream.getReader called on an invalid fetch body");
556: JSValue reader = JS_NewObject(ctx);
557: JS_SetPropertyStr(ctx, reader, "__fetchId", JS_NewInt64(ctx, id));
558: JS_SetPropertyStr(ctx, reader, "read", JS_NewCFunction(ctx, js_fetch_stream_reader_read, "read", 0));
559: return reader;
```

### JS `read`

- 注册 arity：0；实现：`js_fetch_stream_reader_read`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:562`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
562: JSValue js_fetch_stream_reader_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
563: {
564: (void)argc; (void)argv;
571: bool reject_now = false;
574: return JS_ThrowInternalError(ctx, "No JSMod");
576: return JS_ThrowTypeError(ctx, "ReadableStream reader is missing fetch id");
579: JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
580: if (JS_IsException(p)) return p;
594: reject_now = true;
599: reject_now = true;
621: return p;
623: if (reject_now)
624: reject_fetch_stream_read(mod, ctx, cb, error_msg);
626: resolve_fetch_stream_read(mod, ctx, cb, done, chunk);
627: return p;
```

### JS `decode`

- 注册 arity：1；实现：`js_text_decoder_decode`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:630`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
630: JSValue js_text_decoder_decode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
631: {
633: if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]))
634: return JS_NewString(ctx, "");
639: JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[0], &byte_offset, &byte_length, &bytes_per_element);
640: if (!JS_IsException(buffer))
641: {
646: JSValue text = JS_NewStringLen(ctx, reinterpret_cast<const char*>(data + byte_offset), byte_length);
648: return text;
653: if (JS_IsArrayBuffer(argv[0]))
654: {
656: uint8_t* data = JS_GetArrayBuffer(ctx, &buffer_size, argv[0]);
658: return JS_NewStringLen(ctx, reinterpret_cast<const char*>(data), buffer_size);
662: const char* text = JS_ToCStringLen(ctx, &text_length, argv[0]);
664: return JS_ThrowTypeError(ctx, "TextDecoder.decode expects a Uint8Array, ArrayBuffer, or string");
666: JSValue result = JS_NewStringLen(ctx, text, text_length);
668: return result;
```

### JS `text`

- 注册 arity：0；实现：`js_response_text`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:684`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
684: JSValue js_response_text(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
685: {
686: (void)argc; (void)argv;
688: JSValue body_val = JS_GetPropertyStr(ctx, this_val, "body");
700: body_val = JS_NewString(ctx, body_text.c_str());
703: JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
704: if (JS_IsException(p)) { JS_FreeValue(ctx, body_val); return p; }
721: return p;
```

### JS `json`

- 注册 arity：0；实现：`js_response_json`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:724`。
- 错误类别：InternalError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
724: JSValue js_response_json(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
725: {
726: (void)argc; (void)argv;
728: JSValue body_val = JS_GetPropertyStr(ctx, this_val, "body");
740: body_val = JS_NewString(ctx, body_text.c_str());
743: JSValue json_obj = JS_GetPropertyStr(ctx, global, "JSON");
744: JSValue parse_fn = JS_GetPropertyStr(ctx, json_obj, "parse");
757: return JS_ThrowInternalError(ctx, "Response.json parse failed with SEH");
763: if (JS_IsException(parsed))
764: {
769: return parsed;
776: JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
777: if (JS_IsException(p)) { JS_FreeValue(ctx, parsed); return p; }
794: return p;
```

### JS `JSPropertyUtils.get`

- 注册 arity：0；实现：`js_param_ref_get`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:174`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
174: JSValue js_param_ref_get(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
175: {
176: (void)argc; (void)argv;
178: if (!ref || !ref->prop || !ref->data) return JS_NULL;
181: return property_to_jsvalue(ctx, ref->prop, ref->data);
186: return JS_NULL;
```

### JS `JSPropertyUtils.set`

- 注册 arity：1；实现：`js_param_ref_set`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:190`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
190: JSValue js_param_ref_set(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
191: {
192: if (argc < 1) return JS_ThrowTypeError(ctx, "ParamRef.set requires 1 argument");
195: return JS_NewBool(ctx, false);
198: jsvalue_to_property(ctx, ref->prop, ref->data, argv[0]);
199: return JS_NewBool(ctx, true);
205: return JS_NewBool(ctx, false);
```

### JS `JSPropertyUtils.toString`

- 注册 arity：0；实现：`js_param_ref_tostring`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:209`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
209: JSValue js_param_ref_tostring(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
210: {
211: (void)argc; (void)argv;
213: if (!ref || !ref->prop || !ref->data) return JS_NewString(ctx, "[ParamRef invalid]");
217: const char* s = JS_ToCString(ctx, v);
218: JSValue result = JS_NewString(ctx, s ? s : "");
221: return result;
226: return JS_NewString(ctx, "[ParamRef invalid]");
```

### JS `JSPropertyUtils.isValid`

- 注册 arity：0；实现：`js_param_ref_is_valid`；来源：`crates/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp:230`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
230: JSValue js_param_ref_is_valid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
231: {
232: (void)argc; (void)argv;
234: return JS_NewBool(ctx, ref && ref->prop && ref->data);
```

### JS `JSUObject.GetFullName`

- 注册 arity：0；实现：`js_uobject_get_full_name`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:90`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
90: JSValue js_uobject_get_full_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
91: {
94: return JS_ThrowTypeError(ctx, "Invalid UObject");
96: return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");
102: return JS_NewString(ctx, utf8_name.c_str());
106: return JS_ThrowInternalError(ctx, "GetFullName: %s", e.what());
110: return JS_ThrowInternalError(ctx, "GetFullName: unknown exception");
```

### JS `JSUObject.GetClass`

- 注册 arity：0；实现：`js_uobject_get_class`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:115`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
115: JSValue js_uobject_get_class(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
116: {
119: return JS_ThrowTypeError(ctx, "Invalid UObject");
121: return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");
126: if (!obj_class) return JS_NULL;
127: return JSUObject::create(ctx, obj_class);
131: return JS_ThrowInternalError(ctx, "GetClass: exception during access");
```

### JS `JSUObject.IsA`

- 注册 arity：1；实现：`js_uobject_is_a`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:136`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
136: JSValue js_uobject_is_a(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
137: {
138: if (argc < 1)
139: return JS_ThrowTypeError(ctx, "IsA requires a class name argument");
143: return JS_ThrowTypeError(ctx, "Invalid UObject");
145: return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");
147: const char* class_name = JS_ToCString(ctx, argv[0]);
149: return JS_ThrowTypeError(ctx, "Invalid class name");
161: return JS_TRUE;
163: return JS_FALSE;
167: return JS_ThrowInternalError(ctx, "IsA: exception during class check");
```

### JS `JSUObject.GetAddress`

- 注册 arity：0；实现：`js_uobject_get_address`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:172`。
- 错误类别：TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
172: JSValue js_uobject_get_address(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
173: {
177: return JS_ThrowTypeError(ctx, "Invalid UObject");
180: return JS_NewInt64(ctx, reinterpret_cast<int64_t>(data->object));
```

### JS `JSUObject.IsValid`

- 注册 arity：0；实现：`js_uobject_is_valid`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:184`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
184: JSValue js_uobject_is_valid(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
185: {
188: return JS_FALSE;
189: return JS_NewBool(ctx, seh_probe_uobject(data->object));
```

### JS `JSUObject.GetName`

- 注册 arity：0；实现：`js_uobject_get_name`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:193`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
193: JSValue js_uobject_get_name(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
194: {
197: return JS_ThrowTypeError(ctx, "Invalid UObject");
199: return JS_ThrowInternalError(ctx, "UObject pointer is stale (access violation)");
205: return JS_NewString(ctx, utf8_name.c_str());
209: return JS_ThrowInternalError(ctx, "GetName: exception during access");
```

### JS `JSUObject.AddToRoot`

- 注册 arity：0；实现：`js_uobject_add_to_root`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:239`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
239: JSValue js_uobject_add_to_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
240: {
243: return JS_ThrowTypeError(ctx, "Invalid UObject");
245: return JS_ThrowInternalError(ctx, "UObject pointer is stale");
254: return JS_ThrowInternalError(ctx, "AddToRoot: SEH exception");
256: return JS_UNDEFINED;
```

### JS `JSUObject.RemoveFromRoot`

- 注册 arity：0；实现：`js_uobject_remove_from_root`；来源：`crates/ue4ssl-javascript/native/cpp/JSType/JSUObject.cpp:259`。
- 错误类别：InternalError; TypeError。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
259: JSValue js_uobject_remove_from_root(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
260: {
263: return JS_ThrowTypeError(ctx, "Invalid UObject");
265: return JS_ThrowInternalError(ctx, "UObject pointer is stale");
272: return JS_UNDEFINED;
```

### JS `TextDecoder`

- 注册 arity：1；实现：`js_text_decoder_constructor`；来源：`crates/ue4ssl-javascript/native/cpp/JSFetch.cpp:671`。
- 错误类别：No explicit throw in binding; conversion/helper failures remain possible。
- 参数、选项、返回及错误分支（原始语句证据）：

```cpp
671: JSValue js_text_decoder_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv)
672: {
674: (void)argc;
676: JSValue decoder = JS_NewObject(ctx);
677: JS_SetPropertyStr(ctx, decoder, "encoding", JS_NewString(ctx, "utf-8"));
678: JS_SetPropertyStr(ctx, decoder, "fatal", JS_NewBool(ctx, false));
679: JS_SetPropertyStr(ctx, decoder, "ignoreBOM", JS_NewBool(ctx, false));
680: JS_SetPropertyStr(ctx, decoder, "decode", JS_NewCFunction(ctx, js_text_decoder_decode, "decode", 1));
681: return decoder;
```

## Lua callable 注册/运算符清单（280 个注册位置）

同名 `type`、继承方法及不同 delegate/property 实现分别列出，不去重抹掉行为。`operator:Call/Index/NewIndex/Length/Equal` 分别对应构造/函数调用、读取、写入、长度、比较，最终映射见 LuaMadeSimple MetaMethod 定义。

| 名称 | 注册类/文件 | 原行号 |
|---|---|---:|
| `GetWorld` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp` | 54 |
| `GetLevel` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp` | 60 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp` | 68 |
| `ToString` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 119 |
| `GetComparisonIndex` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 127 |
| `Equals` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 135 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 149 |
| `Log` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFOutputDevice.cpp` | 54 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFOutputDevice.cpp` | 75 |
| `GetAssetPathName` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp` | 54 |
| `GetSubPathString` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp` | 60 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp` | 68 |
| `ToString` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp` | 92 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp` | 103 |
| `Get` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp` | 52 |
| `get` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp` | 58 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp` | 66 |
| `SetSharedVariable` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp` | 42 |
| `GetSharedVariable` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp` | 151 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp` | 221 |
| `GetArrayAddress` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 79 |
| `GetArrayNum` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 87 |
| `GetArrayMax` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 95 |
| `GetArrayDataAddress` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 103 |
| `Empty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 111 |
| `ForEach` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 121 |
| `IsValid` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 182 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 192 |
| `IsValid` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 76 |
| `Find` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 85 |
| `Add` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 91 |
| `Contains` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 97 |
| `Remove` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 103 |
| `Empty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 109 |
| `ForEach` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 115 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 175 |
| `IsValid` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 70 |
| `Add` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 79 |
| `Contains` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 85 |
| `Remove` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 91 |
| `Empty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 97 |
| `ForEach` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 103 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 111 |
| `GetWeakPtr` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp` | 55 |
| `GetTagAtLastTest` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp` | 61 |
| `GetObjectID` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp` | 67 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp` | 75 |
| `ToString` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp` | 65 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp` | 77 |
| `GetCDO` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp` | 56 |
| `IsChildOf` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp` | 72 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp` | 83 |
| `IsValid` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 62 |
| `GetRowStruct` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 69 |
| `GetRowMap` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 91 |
| `FindRow` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 135 |
| `AddRow` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 141 |
| `RemoveRow` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 147 |
| `EmptyTable` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 153 |
| `GetRowNames` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 159 |
| `GetAllRows` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 165 |
| `ForEachRow` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 171 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 225 |
| `GetNameByValue` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 56 |
| `ForEachName` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 74 |
| `GetEnumNameByIndex` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 113 |
| `InsertIntoNames` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 135 |
| `EditNameAt` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 204 |
| `EditValueAt` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 251 |
| `RemoveFromNamesAt` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 296 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` | 350 |
| `GetFunctionFlags` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp` | 78 |
| `SetFunctionFlags` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp` | 84 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp` | 97 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUInterface.cpp` | 61 |
| `Get` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` | 2452 |
| `get` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` | 2457 |
| `set` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` | 2462 |
| `Set` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` | 2467 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` | 2473 |
| `GetBaseAddress` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 74 |
| `GetStructAddress` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 80 |
| `GetPropertyAddress` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 89 |
| `IsValid` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 97 |
| `IsMappedToObject` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 112 |
| `IsMappedToProperty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 127 |
| `GetProperty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 142 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 150 |
| `GetSuperStruct` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp` | 59 |
| `ForEachFunction` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp` | 67 |
| `ForEachProperty` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp` | 98 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp` | 131 |
| `SpawnActor` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUWorld.cpp` | 62 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUWorld.cpp` | 147 |
| `GetInner` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXArrayProperty.cpp` | 55 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXArrayProperty.cpp` | 65 |
| `GetByteMask` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` | 57 |
| `GetByteOffset` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` | 63 |
| `GetFieldMask` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` | 69 |
| `GetFieldSize` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` | 75 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` | 83 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 63 |
| `Add` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 155 |
| `Remove` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 186 |
| `Broadcast` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 217 |
| `GetBindings` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 279 |
| `Clear` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 320 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 338 |
| `Add` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 430 |
| `Remove` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 461 |
| `Broadcast` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 492 |
| `GetBindings` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 554 |
| `Clear` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 595 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp` | 613 |
| `GetEnum` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXEnumProperty.cpp` | 55 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXEnumProperty.cpp` | 65 |
| `GetFName` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXFieldClass.cpp` | 51 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXFieldClass.cpp` | 59 |
| `GetInterfaceClass` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXInterfaceProperty.cpp` | 57 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXInterfaceProperty.cpp` | 65 |
| `GetPropertyClass` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXObjectProperty.cpp` | 57 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXObjectProperty.cpp` | 65 |
| `GetClass` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 97 |
| `GetFullName` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 104 |
| `GetFName` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 124 |
| `GetOffset_Internal` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 130 |
| `IsA` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 136 |
| `ContainerPtrToValuePtr` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 186 |
| `ImportText` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 215 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` | 266 |
| `GetStruct` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXStructProperty.cpp` | 57 |
| `type` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaXStructProperty.cpp` | 68 |
| `print` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1409 |
| `LoadExport` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1410 |
| `CreateInvalidObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1412 |
| `StaticFindObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1417 |
| `FindFirstOf` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1520 |
| `FindAllOf` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1555 |
| `IsKeyBindRegistered` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1623 |
| `RegisterKeyBindAsync` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1685 |
| `RegisterKeyBind` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1803 |
| `UnregisterHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 1920 |
| `DumpAllObjects` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2013 |
| `GenerateSDK` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2024 |
| `GenerateLuaTypes` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2035 |
| `GenerateUHTCompatibleHeaders` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2046 |
| `DumpStaticMeshes` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2052 |
| `DumpAllActors` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2057 |
| `DumpUSMAP` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2062 |
| `StaticConstructObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2068 |
| `RegisterCustomProperty` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2178 |
| `ForEachUObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2435 |
| `NotifyOnNewObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2456 |
| `RegisterCustomEvent` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2514 |
| `UnregisterCustomEvent` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2554 |
| `RegisterLoadMapPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2572 |
| `RegisterLoadMapPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2599 |
| `RegisterInitGameStatePreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2626 |
| `RegisterInitGameStatePostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2654 |
| `RegisterBeginPlayPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2682 |
| `RegisterBeginPlayPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2710 |
| `RegisterEndPlayPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2738 |
| `RegisterEndPlayPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2766 |
| `IterateGameDirectories` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 2794 |
| `CreateLogicModsDirectory` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3054 |
| `ExecuteAsync` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3125 |
| `ExecuteWithDelay` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3146 |
| `LoopAsync` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3178 |
| `RegisterProcessConsoleExecPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3210 |
| `RegisterProcessConsoleExecPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3231 |
| `RegisterCallFunctionByNameWithArgumentsPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3252 |
| `RegisterCallFunctionByNameWithArgumentsPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3273 |
| `RegisterULocalPlayerExecPreHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3294 |
| `RegisterULocalPlayerExecPostHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3315 |
| `RegisterConsoleCommandGlobalHandler` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3336 |
| `RegisterConsoleCommandHandler` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3371 |
| `LoadAsset` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3406 |
| `FindObject` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3454 |
| `FindObjects` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3619 |
| `GetCurrentThreadId` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3778 |
| `GetMainModThreadId` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3789 |
| `GetAsyncThreadId` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3801 |
| `GetGameThreadId` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3813 |
| `IsInMainModThread` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3824 |
| `IsInAsyncThread` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3836 |
| `IsInGameThread` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 3848 |
| `RegisterHook` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4050 |
| `ExecuteInGameThread` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4175 |
| `ExecuteInGameThreadWithDelay` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4241 |
| `RetriggerableExecuteInGameThreadWithDelay` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4365 |
| `ExecuteInGameThreadAfterFrames` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4455 |
| `LoopInGameThreadWithDelay` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4501 |
| `LoopInGameThreadAfterFrames` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4572 |
| `ResetDelayedActionTimer` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4619 |
| `SetDelayedActionTimer` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4663 |
| `PauseDelayedAction` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4711 |
| `UnpauseDelayedAction` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4756 |
| `CancelDelayedAction` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4793 |
| `IsValidDelayedActionHandle` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4828 |
| `IsDelayedActionActive` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4859 |
| `IsDelayedActionPaused` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4890 |
| `GetDelayedActionTimeRemaining` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4921 |
| `GetDelayedActionTimeElapsed` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 4974 |
| `GetDelayedActionRate` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5028 |
| `ClearAllDelayedActions` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5068 |
| `MakeActionHandle` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5097 |
| `RestartCurrentMod` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5108 |
| `UninstallCurrentMod` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5120 |
| `RestartMod` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5133 |
| `UninstallMod` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5150 |
| `GetVersion` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5214 |
| `GetMajor` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5227 |
| `GetMinor` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5232 |
| `IsEqual` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5237 |
| `IsAtLeast` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5248 |
| `IsAtMost` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5259 |
| `IsBelow` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5270 |
| `IsAbove` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5281 |
| `FString` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5312 |
| `FUtf8String` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5327 |
| `FAnsiString` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5342 |
| `IsShortPackageName` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5359 |
| `IsValidLongPackageName` | `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` | 5376 |
| `GetAddress` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 171 |
| `IsValid` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 177 |
| `type` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 194 |
| `GetFullName` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 511 |
| `GetFName` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 531 |
| `GetClass` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 536 |
| `GetOuter` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 541 |
| `IsAnyClass` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 549 |
| `Reflection` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 557 |
| `GetProperty` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 566 |
| `GetPropertyValue` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 599 |
| `SetPropertyValue` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 604 |
| `IsClass` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 609 |
| `GetWorld` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 619 |
| `CallFunction` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 625 |
| `IsA` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 629 |
| `HasAllFlags` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 633 |
| `HasAnyFlags` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 651 |
| `HasAnyInternalFlags` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 669 |
| `ProcessConsoleExec` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 687 |
| `IsValid` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 721 |
| `type` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 739 |
| `Get` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 841 |
| `get` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 846 |
| `Set` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 851 |
| `set` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 856 |
| `type` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 862 |
| `ToString` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 68 |
| `Empty` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 74 |
| `Clear` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 80 |
| `Len` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 86 |
| `IsEmpty` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 92 |
| `Append` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 98 |
| `Find` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 119 |
| `StartsWith` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 142 |
| `EndsWith` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 157 |
| `ToUpper` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 172 |
| `ToLower` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 179 |
| `type` | `crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp` | 188 |
| `operator:Call` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 48 |
| `operator:Equal` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` | 103 |
| `operator:Call` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp` | 47 |
| `operator:Equal` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp` | 76 |
| `operator:Call` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaFURL.cpp` | 44 |
| `operator:Index` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 59 |
| `operator:NewIndex` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 64 |
| `operator:Length` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` | 69 |
| `operator:Length` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` | 64 |
| `operator:Length` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp` | 58 |
| `operator:Equal` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp` | 49 |
| `operator:Length` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp` | 50 |
| `operator:Call` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp` | 70 |
| `operator:Index` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 58 |
| `operator:NewIndex` | `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` | 63 |
| `operator:Index` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 483 |
| `operator:NewIndex` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 488 |
| `operator:Call` | `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` | 493 |

### Lua `LuaAActor.GetWorld` (54)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp:54`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
55: const auto& lua_object = lua.get_userdata<AActor>();
57: return 1;
```

### Lua `LuaAActor.GetLevel` (60)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp:60`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
61: const auto& lua_object = lua.get_userdata<AActor>();
63: return 1;
```

### Lua `LuaAActor.type` (68)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp:68`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
69: lua.set_string(ClassName::ToString());
70: return 1;
```

### Lua `LuaFName.ToString` (119)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:119`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
120: auto& lua_object = lua.get_userdata<FName>();
122: lua.set_string(to_string(lua_object.get_local_cpp_object().ToString()));
124: return 1;
```

### Lua `LuaFName.GetComparisonIndex` (127)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:127`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
128: auto& lua_object = lua.get_userdata<FName>();
130: lua.set_integer(lua_object.get_local_cpp_object().GetComparisonIndex());
132: return 1;
```

### Lua `LuaFName.Equals` (135)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:135`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
136: if (!lua.is_userdata(1) || !lua.is_userdata(2))
137: {
138: lua.throw_error("FName.Equals called but there was not two userdata to compare (use ':' to call, not '.')");
141: auto name_a = lua.get_userdata<LuaType::FName>();
142: auto name_b = lua.get_userdata<LuaType::FName>();
144: return name_a.get_local_cpp_object().Equals(name_b.get_local_cpp_object());
```

### Lua `LuaFName.type` (149)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:149`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
150: lua.set_string(ClassName::ToString());
151: return 1;
```

### Lua `LuaFOutputDevice.Log` (54)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFOutputDevice.cpp:54`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
57: Overloads:
58: #1: Log(string Message))"};
60: const auto& lua_object = lua.get_userdata<FOutputDevice>();
62: if (!lua.is_string())
63: {
66: auto message = lua.get_string();
70: return 0;
```

### Lua `LuaFOutputDevice.type` (75)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFOutputDevice.cpp:75`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
76: lua.set_string(ClassName::ToString());
77: return 1;
```

### Lua `LuaFSoftObjectPath.GetAssetPathName` (54)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp:54`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
55: auto& lua_object = lua.get_userdata<FSoftObjectPath>();
56: FName::construct(lua, lua_object.get_local_cpp_object().AssetPathName);
57: return 1;
```

### Lua `LuaFSoftObjectPath.GetSubPathString` (60)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp:60`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
61: auto& lua_object = lua.get_userdata<FSoftObjectPath>();
62: FString::construct(lua, &lua_object.get_local_cpp_object().SubPathString);
63: return 1;
```

### Lua `LuaFSoftObjectPath.type` (68)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp:68`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
69: lua.set_string("FSoftObjectPathUserdata");
70: return 1;
```

### Lua `LuaFText.ToString` (92)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp:92`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
93: auto& lua_object = lua.get_userdata<FText>();
96: lua.set_string(to_string(*fstring));
98: return 1;
```

### Lua `LuaFText.type` (103)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp:103`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
104: lua.set_string(ClassName::ToString());
105: return 1;
```

### Lua `LuaFWeakObjectPtr.Get` (52)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp:52`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
53: auto& lua_object = lua.get_userdata<FWeakObjectPtr>();
55: return 1;
```

### Lua `LuaFWeakObjectPtr.get` (58)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp:58`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
59: auto& lua_object = lua.get_userdata<FWeakObjectPtr>();
61: return 1;
```

### Lua `LuaFWeakObjectPtr.type` (66)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp:66`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
67: lua.set_string(ClassName::ToString());
68: return 1;
```

### Lua `LuaModRef.SetSharedVariable` (42)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp:42`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
45: Overloads:
46: #1: SetSharedVariable(string VariableName, any Value))"};
49: if (!lua.is_string())
50: {
53: auto variable_name = std::string{lua.get_string()};
61: auto type = lua_type(lua.get_lua_state(), 1);
73: RC::LuaMod::m_shared_lua_variables[variable_name] = RC::LuaMod::SharedLuaVariable{type, new bool{lua.get_bool()}, false};
83: RC::LuaMod::SharedLuaVariable{type, new RC::LuaMod::SharedLuaVariable::UserdataContainer{lua_touserdata(lua.get_lua_state(), 1)}, false};
87: if (lua_isinteger(lua.get_lua_state(), 1))
88: {
94: RC::LuaMod::m_shared_lua_variables[variable_name] = RC::LuaMod::SharedLuaVariable{type, new int64_t{lua.get_integer()}, true};
103: RC::LuaMod::m_shared_lua_variables[variable_name] = RC::LuaMod::SharedLuaVariable{type, new double{lua.get_number()}, false};
113: RC::LuaMod::m_shared_lua_variables[variable_name] = RC::LuaMod::SharedLuaVariable{type, new std::string{lua.get_string()}, false};
117: lua.throw_error("Tried to set shared variable but the variable is unsupported type: 'table'.");
121: lua.throw_error("Tried to set shared variable but the variable is unsupported type: 'function'.");
130: auto& userdata = lua.get_userdata<LuaType::UE4SSBaseObject>();
140: lua.throw_error("Tried to set shared variable but the variable is unsupported type: 'userdata' other than UObject derivative.");
145: lua.throw_error("Tried to set shared variable but the variable is unsupported type: 'thread'.");
148: return 0;
```

### Lua `LuaModRef.GetSharedVariable` (151)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp:151`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
154: Overloads:
155: #1: GetSharedVariable(string VariableName))"};
158: if (!lua.is_string())
159: {
162: auto variable_name = std::string{lua.get_string()};
169: lua.set_nil();
173: lua.set_bool(*static_cast<bool*>(value));
177: lua_pushlightuserdata(lua.get_lua_state(), static_cast<RC::LuaMod::SharedLuaVariable::UserdataContainer*>(value)->userdata);
183: lua.set_integer(*static_cast<int64_t*>(value));
187: lua.set_number(*static_cast<double*>(value));
192: lua.set_string(static_cast<std::string*>(value)->c_str());
196: lua.throw_error("Tried to get shared variable but the variable is unsupported type: 'table'.");
200: lua.throw_error("Tried to get shared variable but the variable is unsupported type: 'function'.");
208: lua.throw_error("Tried to get shared variable but the variable is unsupported type: 'thread'.");
213: lua.set_nil();
216: return 1;
```

### Lua `LuaModRef.type` (221)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp:221`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
222: lua.set_string(ClassName::ToString());
223: return 1;
```

### Lua `LuaTArray.GetArrayAddress` (79)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:79`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
80: auto& lua_object = lua.get_userdata<TArray>();
82: lua.set_integer(reinterpret_cast<uintptr_t>(lua_object.get_remote_cpp_object()));
84: return 1;
```

### Lua `LuaTArray.GetArrayNum` (87)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:87`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
88: auto& lua_object = lua.get_userdata<TArray>();
90: lua.set_integer(lua_object.get_remote_cpp_object()->Num());
92: return 1;
```

### Lua `LuaTArray.GetArrayMax` (95)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:95`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
96: auto& lua_object = lua.get_userdata<TArray>();
98: lua.set_integer(lua_object.get_remote_cpp_object()->Max());
100: return 1;
```

### Lua `LuaTArray.GetArrayDataAddress` (103)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:103`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
104: auto& lua_object = lua.get_userdata<TArray>();
106: lua.set_integer(reinterpret_cast<uintptr_t>(lua_object.get_remote_cpp_object()->GetData()));
108: return 1;
```

### Lua `LuaTArray.Empty` (111)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:111`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
112: auto& lua_object = lua.get_userdata<TArray>();
116: return 0;
```

### Lua `LuaTArray.ForEach` (121)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:121`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
122: auto& lua_object = lua.get_userdata<TArray>();
135: lua_pushvalue(lua.get_lua_state(), 1);
138: lua.set_integer(i + 1); // Adding 1 here to account for that fact that Lua tables are 1-indexed
139:
140: // Set the 'elem' parameter for the Lua function (P2)
141: // TODO: Fix crash that occurs here.
142: //       It appears that the Lua stack is getting corrupted somehow, or lua_object is getting GC'd by Lua.
143: //       It seems to only affect large arrays, and I don't know how to fix it.
144: void* property_value = array_data + (i * lua_object.m_inner_property->GetElementSize());
152: // Call function passing index & the element, expecting 1 return value
153: // The element is read-only for all trivial types
154: // The element is writable if it's a UObject
155: lua.call_function(2, 1);
158: if (lua.is_bool(2) && lua.get_bool(2))
159: {
164: // There's a 'nil' on the stack because we told Lua that we expect a return value.
165: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
166: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEach' loop.
167: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
168: lua.discard_value(2);
174: lua.throw_error(std::format("[TArray:ForEach] Tried iterating an array but the unreal property has no registered handler (via ArrayProperty). "
175: "Property type '{}' not supported.",
176: to_string(property_type_name.ToString())));
179: return 0;
```

### Lua `LuaTArray.IsValid` (182)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:182`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
183: auto& lua_object = lua.get_userdata<TArray>();
185: lua.set_bool(lua_object.get_remote_cpp_object());
187: return 1;
```

### Lua `LuaTArray.type` (192)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:192`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
193: lua.set_string(ClassName::ToString());
194: return 1;
```

### Lua `LuaTMap.IsValid` (76)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:76`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
78: auto& lua_object = lua.get_userdata<TMap>();
80: lua.set_bool(lua_object.get_remote_cpp_object());
82: return 1;
```

### Lua `LuaTMap.Find` (85)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:85`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
87: prepare_to_handle(MapOperation::Find, lua);
88: return 1;
```

### Lua `LuaTMap.Add` (91)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:91`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
93: prepare_to_handle(MapOperation::Add, lua);
94: return 1;
```

### Lua `LuaTMap.Contains` (97)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:97`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
99: prepare_to_handle(MapOperation::Contains, lua);
100: return 1;
```

### Lua `LuaTMap.Remove` (103)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:103`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
105: prepare_to_handle(MapOperation::Remove, lua);
106: return 1;
```

### Lua `LuaTMap.Empty` (109)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:109`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
111: prepare_to_handle(MapOperation::Empty, lua);
112: return 1;
```

### Lua `LuaTMap.ForEach` (115)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:115`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
117: TMap& lua_object = lua.get_userdata<TMap>();
120: info.validate_pushers(lua);
133: lua_pushvalue(lua.get_lua_state(), 1);
151: // Call function passing key & value, expecting 1 return value
152: // Mutating the key is undefined behavior
153: lua.call_function(2, 1);
156: if (lua.is_bool(2) && lua.get_bool(2))
157: {
162: // There's a 'nil' on the stack because we told Lua that we expect a return value.
163: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
164: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEach' loop.
165: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
166: lua.discard_value(2);
170: return 1;
```

### Lua `LuaTMap.type` (175)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:175`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
177: lua.set_string(ClassName::ToString());
178: return 1;
```

### Lua `LuaTSet.IsValid` (70)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:70`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
72: auto& lua_object = lua.get_userdata<TSet>();
74: lua.set_bool(lua_object.get_remote_cpp_object());
76: return 1;
```

### Lua `LuaTSet.Add` (79)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:79`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
81: prepare_to_handle(SetOperation::Add, lua);
82: return 1;
```

### Lua `LuaTSet.Contains` (85)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:85`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
87: prepare_to_handle(SetOperation::Contains, lua);
88: return 1;
```

### Lua `LuaTSet.Remove` (91)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:91`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
93: prepare_to_handle(SetOperation::Remove, lua);
94: return 1;
```

### Lua `LuaTSet.Empty` (97)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:97`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
99: prepare_to_handle(SetOperation::Empty, lua);
100: return 1;
```

### Lua `LuaTSet.ForEach` (103)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:103`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
105: prepare_to_handle(SetOperation::ForEach, lua);
106: return 1;
```

### Lua `LuaTSet.type` (111)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:111`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
113: lua.set_string(ClassName::ToString());
114: return 1;
```

### Lua `LuaTSoftObjectPtr.GetWeakPtr` (55)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp:55`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
56: auto& lua_object = lua.get_userdata<TSoftObjectPtr>();
57: FWeakObjectPtr::construct(lua, lua_object.get_local_cpp_object().WeakPtr);
58: return 1;
```

### Lua `LuaTSoftObjectPtr.GetTagAtLastTest` (61)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp:61`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
62: auto& lua_object = lua.get_userdata<TSoftObjectPtr>();
63: lua.set_integer(lua_object.get_local_cpp_object().TagAtLastTest);
64: return 1;
```

### Lua `LuaTSoftObjectPtr.GetObjectID` (67)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp:67`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
68: auto& lua_object = lua.get_userdata<TSoftObjectPtr>();
69: FSoftObjectPath::construct(lua, lua_object.get_local_cpp_object().ObjectID);
70: return 1;
```

### Lua `LuaTSoftObjectPtr.type` (75)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp:75`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
76: lua.set_string("TSoftObjectPtrUserdata");
77: return 1;
```

### Lua `LuaThreadId.ToString` (65)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp:65`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: auto& lua_object = lua.get_userdata<ThreadId>();
70: lua.set_string(thread_id_stream.str());
72: return 1;
```

### Lua `LuaThreadId.type` (77)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp:77`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
78: lua.set_string(ClassName::ToString());
79: return 1;
```

### Lua `LuaUClass.GetCDO` (56)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp:56`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
57: const auto& lua_object = lua.get_userdata<UClass>();
61: LuaType::UObject::construct(lua, nullptr);
66: LuaType::UObject::construct(lua, lua_object.get_remote_cpp_object()->GetClassDefaultObject());
69: return 1;
```

### Lua `LuaUClass.IsChildOf` (72)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp:72`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
73: const auto& lua_object = lua.get_userdata<UClass>();
75: const auto& param_1 = lua.get_userdata<UClass>();
76: lua.set_bool(lua_object.get_remote_cpp_object()->IsChildOf(param_1.get_remote_cpp_object()));
78: return 1;
```

### Lua `LuaUClass.type` (83)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp:83`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
84: lua.set_string(ClassName::ToString());
85: return 1;
```

### Lua `LuaUDataTable.IsValid` (62)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:62`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
64: auto& lua_object = lua.get_userdata<UDataTable>();
65: lua.set_bool(lua_object.get_remote_cpp_object() != nullptr);
66: return 1;
```

### Lua `LuaUDataTable.GetRowStruct` (69)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:69`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
71: auto& lua_object = lua.get_userdata<UDataTable>();
76: lua.set_nil();
77: return 1;
83: lua.set_nil();
84: return 1;
87: LuaType::UObject::construct(lua, row_struct);
88: return 1;
```

### Lua `LuaUDataTable.GetRowMap` (91)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:91`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
93: auto& lua_object = lua.get_userdata<UDataTable>();
98: lua.set_nil();
99: return 1;
106: lua.set_nil();
107: return 1;
113: auto lua_table = lua.prepare_new_table();
126: UScriptStruct::construct(lua, row_wrapper);
132: return 1;
```

### Lua `LuaUDataTable.FindRow` (135)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:135`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
137: prepare_to_handle(DataTableOperation::FindRow, lua);
138: return 1;
```

### Lua `LuaUDataTable.AddRow` (141)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:141`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
143: prepare_to_handle(DataTableOperation::AddRow, lua);
144: return 1;
```

### Lua `LuaUDataTable.RemoveRow` (147)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:147`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
149: prepare_to_handle(DataTableOperation::RemoveRow, lua);
150: return 1;
```

### Lua `LuaUDataTable.EmptyTable` (153)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:153`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
155: prepare_to_handle(DataTableOperation::EmptyTable, lua);
156: return 1;
```

### Lua `LuaUDataTable.GetRowNames` (159)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:159`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
161: prepare_to_handle(DataTableOperation::GetRowNames, lua);
162: return 1;
```

### Lua `LuaUDataTable.GetAllRows` (165)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:165`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
167: prepare_to_handle(DataTableOperation::GetAllRows, lua);
168: return 1;
```

### Lua `LuaUDataTable.ForEachRow` (171)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:171`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
173: UDataTable& lua_object = lua.get_userdata<UDataTable>();
178: lua.throw_error("DataTable is null");
182: info.validate_row_struct(lua);
189: lua_pushvalue(lua.get_lua_state(), 1);
192: lua.set_string(to_string(Pair.Key.ToString()));
200: UScriptStruct::construct(lua, row_wrapper);
202: // Call function with row name and row data, expecting 1 return value
203: lua.call_function(2, 1);
206: if (lua.is_bool(2) && lua.get_bool(2))
207: {
212: // There's a 'nil' on the stack because we told Lua that we expect a return value.
213: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
214: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEachRow' loop.
215: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
216: lua.discard_value(2);
220: return 0;
```

### Lua `LuaUDataTable.type` (225)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:225`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
227: lua.set_string(ClassName::ToString());
228: return 1;
```

### Lua `LuaUEnum.GetNameByValue` (56)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:56`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
59: Overloads:
60: #1: GetNameByValue(integer Value))"};
62: auto& lua_object = lua.get_userdata<UEnum>();
64: if (!lua.is_integer())
65: {
66: lua.throw_error(error_overload_not_found);
69: auto value = lua.get_integer();
70: LuaType::FName::construct(lua, lua_object.get_remote_cpp_object()->GetNameByValue(value));
71: return 1;
```

### Lua `LuaUEnum.ForEachName` (74)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:74`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
77: Overloads:
78: #1: ForEachName(LuaFunction Callback))"};
80: auto& lua_object = lua.get_userdata<UEnum>();
85: lua_pushvalue(lua.get_lua_state(), 1);
88: LuaType::FName::construct(lua, name);
91: lua.set_integer(value);
93: lua.call_function(2, 1);
96: if (lua.is_bool(2) && lua.get_bool(2))
97: {
102: // There's a 'nil' on the stack because we told Lua that we expect a return value.
103: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
104: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEachFunction' loop.
105: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
106: lua.discard_value(2);
110: return 0;
```

### Lua `LuaUEnum.GetEnumNameByIndex` (113)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:113`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
116: Overloads:
117: #1: GetNameByValue(integer Value))"};
119: auto& lua_object = lua.get_userdata<UEnum>();
121: if (!lua.is_integer())
122: {
123: lua.throw_error(error_overload_not_found);
126: auto value = lua.get_integer();
129: LuaType::FName::construct(lua, enum_pair.Key);
130: lua.set_integer(enum_pair.Value);
132: return 2;
```

### Lua `LuaUEnum.InsertIntoNames` (135)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:135`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
137: // Param #1: Enum key name
138: // Param #2: Enum value
139: // Param #3: Enum index
140: // (Optional) Param #4: Shift values
141: std::string error_overload_not_found{R"(
142: No overload found for function 'UEnum.InsertIntoNames'.
143: Overloads:
144: #1: InsertIntoNames(string Name, integer Value, integer Index) // shiftValues = false
145: #1: InsertIntoNames(string Name, integer Value, integer Index, bool shiftValues)"};
147: auto& lua_object = lua.get_userdata<UEnum>();
149: int32_t stack_size = lua.get_stack_size();
153: lua.throw_error("Function 'UEnum.InsertIntoNames' cannot be called with 0 parameters.");
162: if (lua.is_string())
163: {
164: param_name = ensure_str(lua.get_string());
168: lua.throw_error("'UEnum.InsertIntoNames' could not load parameter for \"Name\"");
172: if (lua.is_integer())
173: {
174: param_value = lua.get_integer();
178: lua.throw_error("'UEnum.InsertIntoNames' could not load parameter for \"Value\"");
182: if (lua.is_integer())
183: {
184: param_index = lua.get_integer();
188: lua.throw_error("'UEnum.InsertIntoNames' could not load parameter for \"Index\"");
192: if (lua.is_bool())
193: {
194: param_shift = lua.get_bool();
201: return 1;
```

### Lua `LuaUEnum.EditNameAt` (204)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:204`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
206: // Param #1: Enum key index
207: // Param #2: New name for index
208: std::string error_overload_not_found{R"(
209: No overload found for function 'UEnum.EditNameAt'.
210: Overloads:
211: #1: EditNameAt(integer Index, string NewName))"};
213: auto& lua_object = lua.get_userdata<UEnum>();
215: int32_t stack_size = lua.get_stack_size();
219: lua.throw_error("Function 'UEnum.EditValueAt' cannot be called with 0 parameters.");
226: if (lua.is_integer())
227: {
228: param_index = lua.get_integer();
232: lua.throw_error("'UEnum.EditNameAt' could not load parameter for \"Index\"");
236: if (lua.is_string())
237: {
238: param_new_name = lua.get_string();
242: lua.throw_error("'UEnum.EditNameAt' could not load parameter for \"NewName\"");
248: return 0;
```

### Lua `LuaUEnum.EditValueAt` (251)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:251`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
253: // Param #1: Enum key Index
254: // Param #2: New value for Index
255: std::string error_overload_not_found{R"(
256: No overload found for function 'UEnum.EditValueAt'.
257: Overloads:
258: #1: EditValueAt(integer Index, integer NewValue))"};
260: auto& lua_object = lua.get_userdata<UEnum>();
262: int32_t stack_size = lua.get_stack_size();
266: lua.throw_error("Function 'UEnum.EditValueAt' cannot be called with 0 parameters.");
273: if (lua.is_integer())
274: {
275: param_index = lua.get_integer();
279: lua.throw_error("'UEnum.EditValueAt' Could not load parameter for \"Index\"");
283: if (lua.is_integer())
284: {
285: param_new_value = lua.get_integer();
289: lua.throw_error("'UEnum.EditValueAt' Could not load parameter for \"NewValue\"");
293: return 0;
```

### Lua `LuaUEnum.RemoveFromNamesAt` (296)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:296`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
298: // Param #1: Enum index
299: // (Optional) Param #2: Enum entry count
300: // (Optional) Param #3: Allow shrinking after removal
301: std::string error_overload_not_found{R"(
302: No overload found for function 'UEnum.RemoveFromNamesAt'.
303: Overloads:
304: #1: RemoveFromNamesAt(integer Index) // Count = 1, AllowShrinking = Default
305: #2: RemoveFromNamesAt(integer Index, integer Count) // AllowShrinking = Default
306: #3: RemoveFromNamesAt(integer Index, integer Count, bool AllowShrinking))"};
308: auto& lua_object = lua.get_userdata<UEnum>();
310: int32_t stack_size = lua.get_stack_size();
314: lua.throw_error("Function 'UEnum.RemoveFromNamesAt' cannot be called with 0 parameters.");
322: if (lua.is_integer())
323: {
324: param_index = lua.get_integer();
328: lua.throw_error("'UEnum.RemoveFromNamesAt' Could not load parameter for \"Index\"");
332: if (stack_size >= 2 && lua.is_integer())
333: {
334: param_count = lua.get_integer();
338: if (stack_size >= 3 && lua.is_bool())
339: {
340: bool allow_shrinking = lua.get_bool();
345: return 0;
```

### Lua `LuaUEnum.type` (350)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp:350`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
351: lua.set_string(ClassName::ToString());
352: return 1;
```

### Lua `LuaUFunction.GetFunctionFlags` (78)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp:78`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
79: const auto& lua_object = lua.get_userdata<LuaType::UFunction>();
80: lua.set_integer(lua_object.get_remote_cpp_object()->GetFunctionFlags());
81: return 1;
```

### Lua `LuaUFunction.SetFunctionFlags` (84)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp:84`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
85: const auto& lua_object = lua.get_userdata<LuaType::UFunction>();
86: if (!lua.is_integer())
87: {
90: auto new_function_flags = static_cast<Unreal::EFunctionFlags>(lua.get_integer());
92: return 0;
```

### Lua `LuaUFunction.type` (97)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp:97`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
98: lua.set_string(ClassName::ToString());
99: return 1;
```

### Lua `LuaUInterface.type` (61)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUInterface.cpp:61`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
62: lua.set_string(ClassName::ToString());
63: return 1;
```

### Lua `LuaUObject.Get` (2452)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2452`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2453: prepare_to_handle(Operation::Get, lua);
2454: return 1;
```

### Lua `LuaUObject.get` (2457)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2457`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2458: prepare_to_handle(Operation::Get, lua);
2459: return 1;
```

### Lua `LuaUObject.set` (2462)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2462`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2463: prepare_to_handle(Operation::Set, lua);
2464: return 0;
```

### Lua `LuaUObject.Set` (2467)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2467`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2468: prepare_to_handle(Operation::Set, lua);
2469: return 0;
```

### Lua `LuaUObject.type` (2473)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2473`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2474: lua.set_string("RemoteUnrealParam");
2475: return 1;
```

### Lua `LuaUScriptStruct.GetBaseAddress` (74)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:74`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
76: lua.throw_error("WARNING! Use of deprecated & removed function 'UScriptStruct::GetBaseAddress'!");
77: return 0;
```

### Lua `LuaUScriptStruct.GetStructAddress` (80)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:80`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
81: auto& lua_object = lua.get_userdata<UScriptStruct>();
84: lua.set_integer(std::bit_cast<uintptr_t>(data));
86: return 1;
```

### Lua `LuaUScriptStruct.GetPropertyAddress` (89)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:89`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
90: auto& lua_object = lua.get_userdata<UScriptStruct>();
92: lua.set_integer(std::bit_cast<uintptr_t>(lua_object.get_local_cpp_object().property));
94: return 1;
```

### Lua `LuaUScriptStruct.IsValid` (97)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:97`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
98: auto& lua_object = lua.get_userdata<UScriptStruct>();
102: lua.set_bool(true);
106: lua.set_bool(false);
109: return 1;
```

### Lua `LuaUScriptStruct.IsMappedToObject` (112)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:112`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
113: auto& lua_object = lua.get_userdata<UScriptStruct>();
117: lua.set_bool(true);
121: lua.set_bool(false);
124: return 1;
```

### Lua `LuaUScriptStruct.IsMappedToProperty` (127)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:127`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
128: auto& lua_object = lua.get_userdata<UScriptStruct>();
132: lua.set_bool(true);
136: lua.set_bool(false);
139: return 1;
```

### Lua `LuaUScriptStruct.GetProperty` (142)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:142`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
143: auto& lua_object = lua.get_userdata<UScriptStruct>();
144: XStructProperty::construct(lua, lua_object.get_local_cpp_object().property);
145: return 1;
```

### Lua `LuaUScriptStruct.type` (150)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:150`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
151: lua.set_string(ClassName::ToString());
152: return 1;
```

### Lua `LuaUStruct.GetSuperStruct` (59)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp:59`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
60: const auto& lua_object = lua.get_userdata<UStruct>();
62: LuaType::UStruct::construct(lua, static_cast<Unreal::UClass*>(lua_object.get_remote_cpp_object()->GetSuperStruct()));
64: return 1;
```

### Lua `LuaUStruct.ForEachFunction` (67)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp:67`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
68: const auto& lua_object = lua.get_userdata<UStruct>();
73: lua_pushvalue(lua.get_lua_state(), 1);
76: LuaType::UFunction::construct(lua, lua_object.get_remote_cpp_object(), function);
78: lua.call_function(1, 1);
81: if (lua.is_bool(2) && lua.get_bool(2))
82: {
87: // There's a 'nil' on the stack because we told Lua that we expect a return value.
88: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
89: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEachFunction' loop.
90: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
91: lua.discard_value(2);
95: return 1;
```

### Lua `LuaUStruct.ForEachProperty` (98)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp:98`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
99: const auto& lua_object = lua.get_userdata<UStruct>();
104: lua_pushvalue(lua.get_lua_state(), 1);
109: lua.call_function(1, 1);
112: if (lua.is_bool(2) && lua.get_bool(2))
113: {
118: // There's a 'nil' on the stack because we told Lua that we expect a return value.
119: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
120: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEachFunction' loop.
121: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
122: lua.discard_value(2);
126: return 1;
```

### Lua `LuaUStruct.type` (131)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp:131`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
132: lua.set_string(ClassName::ToString());
133: return 1;
```

### Lua `LuaUWorld.SpawnActor` (62)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUWorld.cpp:62`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
63: const auto& lua_object = lua.get_userdata<UWorld>();
67: Overloads:
68: #1: SpawnActor(UClass Class, table Location, table Rotation))"};
70: if (!lua.is_userdata())
71: {
72: lua.throw_error(error_overload_not_found);
75: const auto& actor_class = lua.get_userdata<UClass>();
78: if (lua.is_userdata())
79: {
80: // location = lua.get_userdata<FVector>().get_remote_cpp_object();
81: lua.throw_error(error_overload_not_found);
83: else if (lua.is_table())
84: {
100: return false;
105: lua.throw_error(error_overload_not_found);
109: if (lua.is_userdata())
110: {
111: // location = lua.get_userdata<FRotator>().get_remote_cpp_object();
112: lua.throw_error(error_overload_not_found);
114: else if (lua.is_table())
115: {
131: return false;
136: lua.throw_error(error_overload_not_found);
140: LuaType::AActor::construct(lua, actor);
142: return 1;
```

### Lua `LuaUWorld.type` (147)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUWorld.cpp:147`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
148: lua.set_string(ClassName::ToString());
149: return 1;
```

### Lua `LuaXArrayProperty.GetInner` (55)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXArrayProperty.cpp:55`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
56: auto& lua_object = lua.get_userdata<XArrayProperty>();
58: return 1;
```

### Lua `LuaXArrayProperty.type` (65)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXArrayProperty.cpp:65`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: lua.set_string(ClassName::ToString());
67: return 1;
```

### Lua `LuaXBoolProperty.GetByteMask` (57)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp:57`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
58: const auto& lua_object = lua.get_userdata<XBoolProperty>();
59: lua.set_integer(lua_object.get_remote_cpp_object()->GetByteMask());
60: return 1;
```

### Lua `LuaXBoolProperty.GetByteOffset` (63)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp:63`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
64: const auto& lua_object = lua.get_userdata<XBoolProperty>();
65: lua.set_integer(lua_object.get_remote_cpp_object()->GetByteOffset());
66: return 1;
```

### Lua `LuaXBoolProperty.GetFieldMask` (69)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp:69`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
70: const auto& lua_object = lua.get_userdata<XBoolProperty>();
71: lua.set_integer(lua_object.get_remote_cpp_object()->GetFieldMask());
72: return 1;
```

### Lua `LuaXBoolProperty.GetFieldSize` (75)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp:75`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
76: const auto& lua_object = lua.get_userdata<XBoolProperty>();
77: lua.set_integer(lua_object.get_remote_cpp_object()->GetFieldSize());
78: return 1;
```

### Lua `LuaXBoolProperty.type` (83)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp:83`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
84: lua.set_string(ClassName::ToString());
85: return 1;
```

### Lua `LuaXDelegateProperty.type` (63)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:63`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
65: lua.set_string(ClassName::ToString());
66: return 1;
```

### Lua `LuaXDelegateProperty.Add` (155)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:155`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
157: const auto& lua_object = lua.get_userdata<XMulticastDelegateProperty>();
163: auto* target_object = lua.get_userdata<UObject>(1).get_remote_cpp_object();
166: if (lua.is_string(1))
167: {
168: fname = Unreal::FName(to_wstring(lua.get_string(1)), Unreal::FNAME_Add);
172: fname = lua.get_userdata<FName>(1).get_local_cpp_object();
182: return 0;
```

### Lua `LuaXDelegateProperty.Remove` (186)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:186`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
188: const auto& lua_object = lua.get_userdata<XMulticastDelegateProperty>();
194: auto* target_object = lua.get_userdata<UObject>(1).get_remote_cpp_object();
197: if (lua.is_string(1))
198: {
199: fname = Unreal::FName(to_wstring(lua.get_string(1)), Unreal::FNAME_Add);
203: fname = lua.get_userdata<FName>(1).get_local_cpp_object();
213: return 0;
```

### Lua `LuaXDelegateProperty.Broadcast` (217)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:217`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
219: const auto& lua_object = lua.get_userdata<XMulticastDelegateProperty>();
228: return 0; // No bindings, nothing to broadcast
229: }
234: lua.throw_error("Delegate signature function not found");
275: return 0;
```

### Lua `LuaXDelegateProperty.GetBindings` (279)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:279`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
281: const auto& lua_object = lua.get_userdata<XMulticastDelegateProperty>();
290: lua.set_nil();
291: return 1;
294: LuaMadeSimple::Lua::Table lua_table = lua.prepare_new_table();
301: LuaMadeSimple::Lua::Table delegate_entry = lua.prepare_new_table();
308: FName::construct(lua, delegate_value->InvocationList[i].GetFunctionName());
316: return 1;
```

### Lua `LuaXDelegateProperty.Clear` (320)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:320`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
322: const auto& lua_object = lua.get_userdata<XMulticastDelegateProperty>();
333: return 0;
```

### Lua `LuaXDelegateProperty.type` (338)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:338`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
340: lua.set_string(ClassName::ToString());
341: return 1;
```

### Lua `LuaXDelegateProperty.Add` (430)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:430`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
432: const auto& lua_object = lua.get_userdata<XMulticastSparseDelegateProperty>();
438: auto* target_object = lua.get_userdata<UObject>(1).get_remote_cpp_object();
441: if (lua.is_string(1))
442: {
443: fname = Unreal::FName(to_wstring(lua.get_string(1)), Unreal::FNAME_Add);
447: fname = lua.get_userdata<FName>(1).get_local_cpp_object();
457: return 0;
```

### Lua `LuaXDelegateProperty.Remove` (461)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:461`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
463: const auto& lua_object = lua.get_userdata<XMulticastSparseDelegateProperty>();
469: auto* target_object = lua.get_userdata<UObject>(1).get_remote_cpp_object();
472: if (lua.is_string(1))
473: {
474: fname = Unreal::FName(to_wstring(lua.get_string(1)), Unreal::FNAME_Add);
478: fname = lua.get_userdata<FName>(1).get_local_cpp_object();
488: return 0;
```

### Lua `LuaXDelegateProperty.Broadcast` (492)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:492`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
494: const auto& lua_object = lua.get_userdata<XMulticastSparseDelegateProperty>();
503: return 0; // No bindings, nothing to broadcast
504: }
509: lua.throw_error("Delegate signature function not found");
550: return 0;
```

### Lua `LuaXDelegateProperty.GetBindings` (554)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:554`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
556: const auto& lua_object = lua.get_userdata<XMulticastSparseDelegateProperty>();
565: lua.set_nil();
566: return 1;
569: LuaMadeSimple::Lua::Table lua_table = lua.prepare_new_table();
576: LuaMadeSimple::Lua::Table delegate_entry = lua.prepare_new_table();
583: FName::construct(lua, delegate_value->InvocationList[i].GetFunctionName());
591: return 1;
```

### Lua `LuaXDelegateProperty.Clear` (595)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:595`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
597: const auto& lua_object = lua.get_userdata<XMulticastSparseDelegateProperty>();
608: return 0;
```

### Lua `LuaXDelegateProperty.type` (613)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXDelegateProperty.cpp:613`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
615: lua.set_string(ClassName::ToString());
616: return 1;
```

### Lua `LuaXEnumProperty.GetEnum` (55)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXEnumProperty.cpp:55`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
56: auto& lua_object = lua.get_userdata<XEnumProperty>();
57: LuaType::UEnum::construct(lua, lua_object.get_remote_cpp_object()->GetEnum());
58: return 1;
```

### Lua `LuaXEnumProperty.type` (65)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXEnumProperty.cpp:65`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: lua.set_string(ClassName::ToString());
67: return 1;
```

### Lua `LuaXFieldClass.GetFName` (51)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXFieldClass.cpp:51`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
52: auto& lua_object = lua.get_userdata<XFieldClass>();
53: FName::construct(lua, lua_object.get_local_cpp_object().GetFName());
54: return 1;
```

### Lua `LuaXFieldClass.type` (59)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXFieldClass.cpp:59`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
60: lua.set_string(ClassName::ToString());
61: return 1;
```

### Lua `LuaXInterfaceProperty.GetInterfaceClass` (57)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXInterfaceProperty.cpp:57`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
58: const auto& lua_object = lua.get_userdata<XInterfaceProperty>();
59: LuaType::UClass::construct(lua, lua_object.get_remote_cpp_object()->GetInterfaceClass());
60: return 1;
```

### Lua `LuaXInterfaceProperty.type` (65)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXInterfaceProperty.cpp:65`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: lua.set_string(ClassName::ToString());
67: return 1;
```

### Lua `LuaXObjectProperty.GetPropertyClass` (57)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXObjectProperty.cpp:57`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
58: const auto& lua_object = lua.get_userdata<XObjectProperty>();
59: LuaType::UClass::construct(lua, lua_object.get_remote_cpp_object()->GetPropertyClass());
60: return 1;
```

### Lua `LuaXObjectProperty.type` (65)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXObjectProperty.cpp:65`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: lua.set_string(ClassName::ToString());
67: return 1;
```

### Lua `LuaXProperty.GetClass` (97)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:97`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
98: const auto& lua_object = lua.get_userdata<XProperty>();
100: LuaType::XFieldClass::construct(lua, lua_object.get_remote_cpp_object()->GetClass());
101: return 1;
```

### Lua `LuaXProperty.GetFullName` (104)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:104`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
108: const auto& lua_object = lua.get_userdata<XProperty>();
112: // Set the return value to the ansi version of the full name
113: lua.set_string(to_string(lua_object.get_remote_cpp_object()->GetFullName()).c_str());
117: // We have a nullptr, lets return 'nil' for easy object verification in Lua
118: lua.set_nil();
121: return 1;
```

### Lua `LuaXProperty.GetFName` (124)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:124`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
125: const auto& lua_object = lua.get_userdata<XProperty>();
126: LuaType::FName::construct(lua, lua_object.get_remote_cpp_object()->GetFName());
127: return 1;
```

### Lua `LuaXProperty.GetOffset_Internal` (130)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:130`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
131: const auto& lua_object = lua.get_userdata<XProperty>();
132: lua.set_integer(lua_object.get_remote_cpp_object()->GetOffset_Internal());
133: return 1;
```

### Lua `LuaXProperty.IsA` (136)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:136`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
139: Overloads:
140: #1: IsA(PropertyTypes PropertyType)"};
142: const auto& lua_object = lua.get_userdata<XProperty>();
143: if (lua.is_table())
144: {
151: lua.throw_error("Table value for key 'FFieldClassPointer' must be integer");
154: return true;
157: return false;
162: lua.throw_error("Could not find FFieldClassPointer");
168: lua.set_bool(lua_object.get_remote_cpp_object()->IsA(ffield_class));
173: lua.set_bool(lua_object.get_remote_cpp_object()->IsA(ffield_class));
175: return 1;
179: lua.throw_error(error_overload_not_found);
182: lua.set_bool(false);
183: return 1;
```

### Lua `LuaXProperty.ContainerPtrToValuePtr` (186)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:186`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
189: Overloads:
190: #1: ContainerPtrToValuePtr(UObjectDerivative Container, integer ArrayIndex = 0))"};
192: const auto& lua_object = lua.get_userdata<XProperty>();
194: if (!lua.is_userdata())
195: {
198: const auto& container = lua.get_userdata<UObject>();
201: if (lua.is_integer())
202: {
203: array_index = static_cast<int32_t>(lua.get_integer());
205: else if (lua.is_nil())
206: {
211: lua_pushlightuserdata(lua.get_lua_state(), data);
212: return 1;
```

### Lua `LuaXProperty.ImportText` (215)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:215`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
218: Overloads:
219: #1: ImportText(string Buffer, lightuserdata Data, integer PortFlags, UObject OwnerObject))"};
221: const auto& lua_object = lua.get_userdata<XProperty>();
224: if (lua.is_string())
225: {
226: buffer = ensure_str(lua.get_string());
234: if (lua_islightuserdata(lua.get_lua_state(), 1))
235: {
236: data = lua_touserdata(lua.get_lua_state(), 1);
237: lua_remove(lua.get_lua_state(), 1);
245: if (lua.is_integer())
246: {
247: port_flags = static_cast<int32_t>(lua.get_integer());
254: if (!lua.is_userdata())
255: {
258: auto* owner_object = lua.get_userdata<UObject>().get_remote_cpp_object();
261: return 0;
```

### Lua `LuaXProperty.type` (266)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp:266`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
267: lua.set_string(ClassName::ToString());
268: return 1;
```

### Lua `LuaXStructProperty.GetStruct` (57)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXStructProperty.cpp:57`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
58: auto& lua_object = lua.get_userdata<XStructProperty>();
60: LuaType::UScriptStruct::construct(lua, script_struct_wrapper);
61: return 1;
```

### Lua `LuaXStructProperty.type` (68)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaXStructProperty.cpp:68`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
69: lua.set_string(ClassName::ToString());
70: return 1;
```

### Lua `LuaLibrary.print` (36)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp:36`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
49: int32_t stack_size = lua.get_stack_size();
55: const char* raw_string = luaL_tolstring(lua.get_lua_state(), i, nullptr);
110: return 0;
```

### Lua `LuaLibrary.LoadExport` (136)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp:136`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
138: if (lua.get_stack_size() != 1 || !lua.is_string())
139: {
141: lua.set_nil();
142: return 1;
145: const auto symbol_name = std::string{lua.get_string()};
147: lua.set_integer(std::bit_cast<intptr_t>(Unreal::UnrealInitializer::LoadExport(symbol_name)));
148: return 1;
```

### Lua `LuaMod.CreateInvalidObject` (1412)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1412`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1414: return 1;
```

### Lua `LuaMod.StaticFindObject` (1417)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1417`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1419: int32_t stack_size = lua.get_stack_size();
1423: lua.throw_error("Function 'StaticFindObject' cannot be called with 0 parameters.");
1428: Overloads:
1429: #1: StaticFindObject(string name)
1430: #2: StaticFindObject(UClass* Class, UObject* InOuter, string name, bool ExactClass = false))"};
1433: // P1: string name
1434: // Ignores any params after P1
1435: if (lua.is_string())
1436: {
1437: Unreal::UObject* object = Unreal::UObjectGlobals::StaticFindObject(nullptr, nullptr, ensure_str(lua.get_string()));
1443: return 1;
1447: // P1: UClass* Class
1448: // P2: UObject* InOuter
1449: // P3: string Name
1450: // P4: bool ExactClass = false
1451: // Full definition of StaticFindObject, including default values
1452: // Ignores any params after P4
1453: if (stack_size < 3)
1454: {
1456: lua.throw_error(error_overload_not_found);
1465: if (lua.is_userdata())
1466: {
1467: auto& lua_object = lua.get_userdata<LuaType::UClass>();
1470: else if (lua.is_nil())
1471: {
1476: lua.throw_error(error_overload_not_found);
1480: if (lua.is_userdata())
1481: {
1482: auto& lua_object = lua.get_userdata<LuaType::UObject>();
1485: else if (lua.is_nil())
1486: {
1491: lua.throw_error(error_overload_not_found);
1495: if (lua.is_string())
1496: {
1497: param_name = ensure_str(lua.get_string());
1501: lua.throw_error(error_overload_not_found);
1505: if (lua.is_bool())
1506: {
1507: param_exact_class = lua.get_bool();
1517: return 1;
```

### Lua `LuaMod.FindFirstOf` (1520)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1520`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1522: int32_t stack_size = lua.get_stack_size();
1526: lua.throw_error("Function 'FindFirstOf' cannot be called with 0 parameters.");
1531: Overloads:
1532: #1: FindFirstOf(string short_class_name))"};
1535: // P1: string short_name
1536: // Ignores any params after P1
1537: if (lua.is_string())
1538: {
1539: Unreal::UObject* object = Unreal::UObjectGlobals::FindFirstOf(ensure_str(lua.get_string()));
1545: return 1;
1549: lua.throw_error(error_overload_not_found);
1552: return 0;
```

### Lua `LuaMod.FindAllOf` (1555)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1555`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1557: int32_t stack_size = lua.get_stack_size();
1561: lua.throw_error("Function 'FindAllOf' cannot be called with 0 parameters.");
1566: Overloads:
1567: #1: FindAllOf(string short_class_name))"};
1570: // P1: string short_name
1571: // Ignores any params after P1
1572: if (lua.is_string())
1573: {
1581: Unreal::UObjectGlobals::FindAllOf(lua.get_string(), found_unreal_objects);
1585: LuaMadeSimple::Lua::Table table = lua.prepare_new_table(elements_to_reserve);
1604: lua.set_nil();
1607: return 1;
1611: lua.throw_error(error_overload_not_found);
1617: lua.set_nil();
1618: return 1;
```

### Lua `LuaMod.IsKeyBindRegistered` (1623)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1623`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1626: Overloads:
1627: #1: IsKeyBindRegistered(integer key)
1628: #2: IsKeyBindRegistered(integer key, table modifier_key_integers))"};
1630: if (!lua.is_integer())
1631: {
1632: lua.throw_error(error_overload_not_found);
1635: auto key_from_lua = lua.get_integer();
1638: lua.throw_error("Parameter #1 for function 'IsKeyBindRegistered' must be an integer between 0 and 255");
1644: if (lua.is_table())
1645: {
1652: lua.throw_error(
1653: "Lua function 'IsKeyBindRegistered', overload #2, requires a table of 1-byte large integers as the second parameter");
1659: lua.throw_error(
1660: "Lua function 'IsKeyBindRegistered', overload #2, requires a table of 1-byte large integers as the second parameter");
1665: return false;
1670: lua.set_bool(mod->m_program.is_keydown_event_registered(key_to_check, modifier_keys));
1674: lua.set_bool(mod->m_program.is_keydown_event_registered(key_to_check));
1679: lua.set_bool(mod->m_program.is_keydown_event_registered(key_to_check));
1682: return 1;
```

### Lua `LuaMod.RegisterKeyBindAsync` (1685)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1685`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1688: Overloads:
1689: #1: RegisterKeyBindAsync(integer key)
1690: #2: RegisterKeyBindAsync(integer key, table modifier_key_integers))"};
1694: if (!lua.is_integer())
1695: {
1696: lua.throw_error(error_overload_not_found);
1699: int64_t key_from_lua = lua.get_integer();
1702: lua.throw_error("Parameter #1 for function 'RegisterKeyBindAsync' must be an integer between 0 and 255");
1711: lua.call_function(0, 0);
1715: Output::send<LogLevel::Error>(STR("{}\n"), ensure_str(lua.handle_error(e.what())));
1719: if (lua.is_function())
1720: {
1722: // P1: Key to register
1723: // P2: Callback
1724:
1725: // Duplicate the Lua function to the top of the stack for luaL_ref
1726: lua_pushvalue(lua.get_lua_state(), 1);
1740: else if (lua.is_table())
1741: {
1743: // P1: Key to register
1744: // P2: Table of modifier keys
1745: // P3: Callback
1746:
1747: Input::Handler::ModifierKeyArray modifier_keys{};
1753: lua.throw_error(
1754: "Lua function 'RegisterKeyBindAsync', overload #2, requires a table of 1-byte large integers as the second parameter");
1760: lua.throw_error(
1761: "Lua function 'RegisterKeyBindAsync', overload #2, requires a table of 1-byte large integers as the second parameter");
1766: return false;
1770: lua_pushvalue(lua.get_lua_state(), 1);
1797: lua.throw_error(error_overload_not_found);
1800: return 0;
```

### Lua `LuaMod.RegisterKeyBind` (1803)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1803`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1806: Overloads:
1807: #1: RegisterKeyBind(integer key)
1808: #2: RegisterKeyBind(integer key, table modifier_key_integers))"};
1812: if (!lua.is_integer())
1813: {
1814: lua.throw_error(error_overload_not_found);
1817: int64_t key_from_lua = lua.get_integer();
1820: lua.throw_error("Parameter #1 for function 'RegisterKeyBind' must be an integer between 0 and 255");
1830: lua.call_function(0, 0);
1834: Output::send<LogLevel::Error>(STR("{}\n"), ensure_str(lua.handle_error(e.what())));
1838: if (lua.is_function())
1839: {
1841: // P1: Key to register
1842: // P2: Callback
1843:
1844: // Duplicate the Lua function to the top of the stack for luaL_ref
1845: lua_pushvalue(lua.get_lua_state(), 1);
1859: else if (lua.is_table())
1860: {
1862: // P1: Key to register
1863: // P2: Table of modifier keys
1864: // P3: Callback
1865:
1866: Input::Handler::ModifierKeyArray modifier_keys{};
1872: lua.throw_error("Lua function 'RegisterKeyBind', overload #2, requires a table of 1-byte large integers as the second parameter");
1878: lua.throw_error("Lua function 'RegisterKeyBind', overload #2, requires a table of 1-byte large integers as the second parameter");
1883: return false;
1887: lua_pushvalue(lua.get_lua_state(), 1);
1914: lua.throw_error(error_overload_not_found);
1917: return 0;
```

### Lua `LuaMod.UnregisterHook` (1920)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:1920`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
1925: Overloads:
1926: #1: UnregisterHook(string UFunction_Name, integer PreCallbackId, integer PostCallbackId))"};
1928: if (!lua.is_string())
1929: {
1930: lua.throw_error(error_overload_not_found);
1933: auto function_name_no_prefix = get_function_name_without_prefix(ensure_str(lua.get_string()));
1938: lua.throw_error(std::format("Tried to unregister a hook with Lua function 'UnregisterHook' but no UFunction with the specified name "
1939: "was found.\n>FunctionName: {}",
1940: to_string(function_name_no_prefix)));
1943: if (!lua.is_integer())
1944: {
1945: lua.throw_error(error_overload_not_found);
1947: const auto pre_id = lua.get_integer();
1949: if (!lua.is_integer())
1950: {
1951: lua.throw_error(error_overload_not_found);
1953: const auto post_id = lua.get_integer();
1957: lua.throw_error(std::format("Tried to unregister a hook with Lua function 'UnregisterHook' but the PreCallbackId supplied was too "
1958: "large (>int32)\n>FunctionName: {}",
1959: to_string(function_name_no_prefix)));
1964: lua.throw_error(std::format("Tried to unregister a hook with Lua function 'UnregisterHook' but the PostCallbackId supplied was too "
1965: "large (>int32)\n>FunctionName: {}",
1966: to_string(function_name_no_prefix)));
1975: return elem->post_callback_id == post_id && elem->pre_callback_id == pre_id;
2007: lua.throw_error(error_message);
2010: return 0;
```

### Lua `LuaMod.DumpAllObjects` (2013)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2013`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2017: lua.throw_error("Couldn't dump objects and properties because the pointer to 'Mod' was nullptr");
2021: return 0;
```

### Lua `LuaMod.GenerateSDK` (2024)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2024`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2028: lua.throw_error("Couldn't generate SDK because the pointer to 'Mod' was nullptr");
2032: return 0;
```

### Lua `LuaMod.GenerateLuaTypes` (2035)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2035`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2039: lua.throw_error("Couldn't generate lua types because the pointer to 'Mod' was nullptr");
2043: return 0;
```

### Lua `LuaMod.GenerateUHTCompatibleHeaders` (2046)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2046`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2049: return 0;
```

### Lua `LuaMod.DumpStaticMeshes` (2052)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2052`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2054: return 0;
```

### Lua `LuaMod.DumpAllActors` (2057)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2057`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2059: return 0;
```

### Lua `LuaMod.DumpUSMAP` (2062)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2062`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2064: return 0;
```

### Lua `LuaMod.StaticConstructObject` (2068)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2068`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2071: Overloads:
2072: #1: StaticConstructObject(
2073: UClass Class,
2074: UObject Outer,
2075: FName Name, #Optional
2076: EObjectFlags Flags, #Optional
2077: EInternalObjectFlags InternalSetFlags, #Optional
2078: bool CopyTransientsFromClassDefaults, #Optional
2079: bool AssumeTemplateIsArchetype, #Optional
2080: UObject Template, #Optional
2081: FObjectInstancingGraph InstanceGraph, #Optional
2082: UPackage ExternalPackage, #Optional
2088: if (!lua.is_userdata())
2089: {
2090: lua.throw_error(error_overload_not_found);
2092: Unreal::UClass* param_class = lua.get_userdata<LuaType::UClass>().get_remote_cpp_object();
2094: if (!lua.is_userdata())
2095: {
2096: lua.throw_error(error_overload_not_found);
2098: Unreal::UObject* param_outer = lua.get_userdata<LuaType::UObject>().get_remote_cpp_object();
2101: if (lua.is_userdata())
2102: {
2103: param_name = lua.get_userdata<LuaType::FName>().get_local_cpp_object();
2105: else if (lua.is_integer())
2106: {
2107: param_name = Unreal::FName(lua.get_integer());
2115: if (lua.is_integer())
2116: {
2117: param_set_flags = static_cast<Unreal::EObjectFlags>(lua.get_integer());
2121: if (lua.is_integer())
2122: {
2123: param_internal_set_flags = static_cast<Unreal::EInternalObjectFlags>(lua.get_integer());
2128: if (lua.is_bool())
2129: {
2130: param_copy_transients_from_class_defaults = lua.get_bool();
2134: if (lua.is_bool())
2135: {
2136: param_assume_template_is_archetype = lua.get_bool();
2140: if (lua.is_userdata())
2141: {
2142: param_template = lua.get_userdata<LuaType::UObject>().get_remote_cpp_object();
2147: if (lua.is_integer())
2148: {
2149: param_instance_graph = reinterpret_cast<void*>(static_cast<uintptr_t>(lua.get_integer()));
2154: if (lua.is_integer())
2155: {
2156: param_external_package = reinterpret_cast<void*>(static_cast<uintptr_t>(lua.get_integer()));
2160: // if (lua.is_integer()) { param_subobject_overrides = reinterpret_cast<void*>(static_cast<uintptr_t>(lua.get_integer())); }
2173: LuaType::UObject::construct(lua, created_object);
2175: return 1;
```

### Lua `LuaMod.RegisterCustomProperty` (2178)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2178`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2181: Overloads:
2182: #1: RegisterCustomProperty(table PropertyInfo))"};
2184: if (!lua.is_table())
2185: {
2186: lua.throw_error(error_overload_not_found);
2200: return false;
2204: return false;
2206: // if (!static_pointer) { return false; }
2208: return true;
2238: return true;
2242: return true;
2246: return true;
2250: return true;
2252: // if (element_size < 0) { return true; }
2256: return true;
2259: return false;
2265: auto lua_table = lua.get_table();
2306: lua.throw_error(std::format(
2307: "Parameter #1 for function 'RegisterCustomProperty'. The table value for key '{}' is outside the range of a 32-bit integer",
2308: error_field_names));
2311: return static_cast<int32_t>(integer);
2344: lua.throw_error("Parameter #1 for function 'RegisterCustomProperty'. The table entry 'ArrayProperty' is missing.");
2360: lua.throw_error("Parameter #1 for function 'RegisterCustomProperty'. The table is missing required fields.");
2366: lua.throw_error("Tried to 'RegisterCustomProperty' but 'BelongsToClass' could not be found");
2375: lua.throw_error(std::format("Was unable to find property '{}' in class '{}' for use for relative Offset_Internal",
2376: oi_property_name,
2377: to_string(property_info.belongs_to_class)));
2385: lua.throw_error(std::format("The size for property '{}' was unknown. Custom sizes are unsupported but will likely be supported in the future.",
2386: property_info.type.name));
2391: lua.throw_error(
2392: std::format("The size for inner property '{}' was unknown. Custom sizes are unsupported but will likely be supported in the future.",
2393: property_info.array_inner.name));
2398: Unreal::CustomArrayProperty::construct(property_info.offset_internal,
2399: belongs_to_class,
2400: static_cast<Unreal::UClass*>(property_info.type.ffieldclass_pointer),
2401: static_cast<Unreal::FProperty*>(property_info.array_inner.ffieldclass_pointer),
2402: property_info.is_array_property ? property_info.array_inner.size : property_info.type.size
2403:
2404: ));
2432: return 0;
```

### Lua `LuaMod.ForEachUObject` (2435)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2435`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2438: lua_pushvalue(lua.get_lua_state(), 1);
2444: lua.set_integer(chunk_index);
2447: lua.set_integer(object_index);
2449: lua.call_function(3, 1);
2451: return LoopAction::Continue;
2453: return 0;
```

### Lua `LuaMod.NotifyOnNewObject` (2456)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2456`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2459: Overloads:
2460: #1: NotifyOnNewObject(string UClassName, LuaFunction Callback))"};
2462: if (!lua.is_string())
2463: {
2464: lua.throw_error(error_overload_not_found);
2467: auto class_name = ensure_str(lua.get_string());
2469: if (!lua.is_function())
2470: {
2471: lua.throw_error(error_overload_not_found);
2478: lua_pushvalue(lua.get_lua_state(), 1);
2480: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2486: lua.throw_error(std::format("Param #1 for NotifyOnNewObject cannot contain spaces; Param value: '{}'", to_utf8_string(class_name)));
2492: lua.throw_error(std::format("Param #1 for NotifyOnNewObject must contain at least two parts; Param value: '{}'", to_utf8_string(class_name)));
2511: return 0;
```

### Lua `LuaMod.RegisterCustomEvent` (2514)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2514`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2518: Overloads:
2519: #1: RegisterCustomEvent(string EventName, LuaFunction Callback))"};
2521: if (!lua.is_string())
2522: {
2523: lua.throw_error(error_overload_not_found);
2526: auto event_name = ensure_str(lua.get_string());
2528: if (!lua.is_function())
2529: {
2530: lua.throw_error(error_overload_not_found);
2536: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2551: return 0;
```

### Lua `LuaMod.UnregisterCustomEvent` (2554)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2554`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2558: Overloads:
2559: #1: UnregisterCustomEvent(string EventName))"};
2561: if (!lua.is_string())
2562: {
2563: lua.throw_error(error_overload_not_found);
2565: auto custom_event_name = ensure_str(lua.get_string());
2569: return 0;
```

### Lua `LuaMod.RegisterLoadMapPreHook` (2572)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2572`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2575: Overloads:
2576: #1: RegisterLoadMapPreHook(LuaFunction Callback))"};
2578: if (!lua.is_function())
2579: {
2580: lua.throw_error(error_overload_not_found);
2586: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2596: return 0;
```

### Lua `LuaMod.RegisterLoadMapPostHook` (2599)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2599`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2602: Overloads:
2603: #1: RegisterLoadMapPostHook(LuaFunction Callback))"};
2605: if (!lua.is_function())
2606: {
2607: lua.throw_error(error_overload_not_found);
2613: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2623: return 0;
```

### Lua `LuaMod.RegisterInitGameStatePreHook` (2626)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2626`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2629: Overloads:
2630: #1: RegisterInitGameStatePreHook(LuaFunction Callback))"};
2632: if (!lua.is_function())
2633: {
2634: lua.throw_error(error_overload_not_found);
2640: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2651: return 0;
```

### Lua `LuaMod.RegisterInitGameStatePostHook` (2654)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2654`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2657: Overloads:
2658: #1: RegisterInitGameStatePostHook(LuaFunction Callback))"};
2660: if (!lua.is_function())
2661: {
2662: lua.throw_error(error_overload_not_found);
2668: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2679: return 0;
```

### Lua `LuaMod.RegisterBeginPlayPreHook` (2682)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2682`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2685: Overloads:
2686: #1: RegisterBeginPlayPreHook(LuaFunction Callback))"};
2688: if (!lua.is_function())
2689: {
2690: lua.throw_error(error_overload_not_found);
2696: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2707: return 0;
```

### Lua `LuaMod.RegisterBeginPlayPostHook` (2710)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2710`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2713: Overloads:
2714: #1: RegisterBeginPlayPostHook(LuaFunction Callback))"};
2716: if (!lua.is_function())
2717: {
2718: lua.throw_error(error_overload_not_found);
2724: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2735: return 0;
```

### Lua `LuaMod.RegisterEndPlayPreHook` (2738)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2738`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2741: Overloads:
2742: #1: RegisterEndPlayPreHook(LuaFunction Callback))"};
2744: if (!lua.is_function())
2745: {
2746: lua.throw_error(error_overload_not_found);
2752: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2763: return 0;
```

### Lua `LuaMod.RegisterEndPlayPostHook` (2766)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2766`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2769: Overloads:
2770: #1: RegisterEndPlayPostHook(LuaFunction Callback))"};
2772: if (!lua.is_function())
2773: {
2774: lua.throw_error(error_overload_not_found);
2780: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
2791: return 0;
```

### Lua `LuaMod.IterateGameDirectories` (2794)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:2794`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
2797: Overloads:
2798: #1: IterateGameDirectories())"};
2806: lua.set_nil();
2807: return 1;
2812: auto directories_table = lua.prepare_new_table();
2843: auto next_directory_table = lua.prepare_new_table();
2860: auto meta_table = lua.prepare_new_table();
2862: lua_pushcfunction(lua.get_lua_state(), [](lua_State* lua_state) -> int {
2863: return TRY([&] {
2866: if (!lua.is_string(2))
2867: {
2868: return 0;
2870: name = lua.get_string(2);
2875: lua_pushliteral(lua_state, "__name");
2879: if (!lua.is_string())
2880: {
2883: return 1;
2888: lua_pushliteral(lua_state, "__absolute_path");
2892: if (!lua.is_string())
2893: {
2896: return 1;
2901: lua_pushliteral(lua_state, "__absolute_path");
2905: if (!lua.is_string())
2906: {
2910: const auto path_str = lua.get_string();
2968: auto files_table = lua.prepare_new_table();
2983: auto file_table = lua.prepare_new_table();
3013: return 1;
3017: lua.set_nil();
3018: return 1;
3023: lua_setfield(lua.get_lua_state(), -2, "__index");
3032: lua_setmetatable(lua.get_lua_state(), -2);
3047: lua.set_nil();
3048: return 1;
3051: return 1;
```

### Lua `LuaMod.CreateLogicModsDirectory` (3054)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3054`。
- 错误类别：Lua error / protected-call failure; C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3057: Overloads:
3058: #1: CreateLogicModsDirectory())"};
3065: lua.throw_error("CreateLogicModsDirectory: Could not locate the \"Content\" directory because the directory structure is unknown (not "
3066: "<RootGamePath>/Game/Content)\n");
3076: lua.set_bool(true);
3077: return 1;
3105: lua.set_bool(true);
3106: return 1;
3109: lua.throw_error("CreateLogicModsDirectory: Unable to create \"LogicMods\" directory. Try creating manually.\n");
3114: lua.set_bool(true);
3115: return 1;
3120: lua.throw_error(e.what());
3121: return 0;
```

### Lua `LuaMod.ExecuteAsync` (3125)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3125`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3128: Overloads:
3129: #1: ExecuteAsync(LuaFunction Callback))"};
3133: if (!lua.is_function())
3134: {
3143: return 0;
```

### Lua `LuaMod.ExecuteWithDelay` (3146)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3146`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3149: Overloads:
3150: #1: ExecuteWithDelay(integer DelayInMilliseconds, LuaFunction Callback))"};
3152: if (!lua.is_integer())
3153: {
3156: int64_t delay = lua.get_integer();
3158: if (!lua.is_function())
3159: {
3175: return 0;
```

### Lua `LuaMod.LoopAsync` (3178)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3178`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3181: Overloads:
3182: #1: LoopAsync(integer DelayInMilliseconds, LuaFunction Callback))"};
3184: if (!lua.is_integer())
3185: {
3188: int64_t delay = lua.get_integer();
3190: if (!lua.is_function())
3191: {
3207: return 0;
```

### Lua `LuaMod.RegisterProcessConsoleExecPreHook` (3210)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3210`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3213: Overloads:
3214: #1: RegisterProcessConsoleExecPreHook(LuaFunction Callback))"};
3216: if (!lua.is_function())
3217: {
3225: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3228: return 0;
```

### Lua `LuaMod.RegisterProcessConsoleExecPostHook` (3231)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3231`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3234: Overloads:
3235: #1: RegisterProcessConsoleExecPostHook(LuaFunction Callback))"};
3237: if (!lua.is_function())
3238: {
3246: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3249: return 0;
```

### Lua `LuaMod.RegisterCallFunctionByNameWithArgumentsPreHook` (3252)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3252`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3255: Overloads:
3256: #1: RegisterCallFunctionByNameWithArgumentsPreHook(LuaFunction Callback))"};
3258: if (!lua.is_function())
3259: {
3267: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3270: return 0;
```

### Lua `LuaMod.RegisterCallFunctionByNameWithArgumentsPostHook` (3273)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3273`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3276: Overloads:
3277: #1: RegisterCallFunctionByNameWithArgumentsPostHook(LuaFunction Callback))"};
3279: if (!lua.is_function())
3280: {
3288: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3291: return 0;
```

### Lua `LuaMod.RegisterULocalPlayerExecPreHook` (3294)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3294`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3297: Overloads:
3298: #1: RegisterULocalPlayerExecPreHook(LuaFunction Callback))"};
3300: if (!lua.is_function())
3301: {
3309: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3312: return 0;
```

### Lua `LuaMod.RegisterULocalPlayerExecPostHook` (3315)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3315`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3318: Overloads:
3319: #1: RegisterULocalPlayerExecPostHook(LuaFunction Callback))"};
3321: if (!lua.is_function())
3322: {
3330: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3333: return 0;
```

### Lua `LuaMod.RegisterConsoleCommandGlobalHandler` (3336)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3336`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3339: Overloads:
3340: #1: RegisterConsoleCommandGlobalHandler(string CommandName, LuaFunction Callback))"};
3342: if (!lua.is_string())
3343: {
3346: auto command_name = ensure_str(lua.get_string());
3348: if (!lua.is_function())
3349: {
3365: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3368: return 0;
```

### Lua `LuaMod.RegisterConsoleCommandHandler` (3371)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3371`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3374: Overloads:
3375: #1: RegisterConsoleCommandHandler(string CommandName, LuaFunction Callback))"};
3377: if (!lua.is_string())
3378: {
3381: auto command_name = ensure_str(lua.get_string());
3383: if (!lua.is_function())
3384: {
3400: lua_xmove(lua.get_lua_state(), callback->lua->get_lua_state(), 1);
3403: return 0;
```

### Lua `LuaMod.LoadAsset` (3406)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3406`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3409: Overloads:
3410: #1: LoadAsset(string AssetPathAndName))"};
3417: if (!lua.is_string())
3418: {
3421: auto asset_path_and_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Add);
3449: lua.set_bool(was_asset_found);
3450: lua.set_bool(did_asset_load);
3451: return 3;
```

### Lua `LuaMod.FindObject` (3454)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3454`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3457: Overloads:
3458: #1: FindObject(UClass InClass, UObject|UClass InOuter, string Name, bool ExactClass)
3459: #2: FindObject(string|FName|nil ClassName, string|FName|nil ObjectShortName, EObjectFlags RequiredFlags, EObjectFlags BannedFlags)
3460: #3: FindObject(UClass|nil Class, string|FName|nil ObjectShortName, EObjectFlags RequiredFlags, EObjectFlags BannedFlags))"};
3462: if (!lua.is_string() && !lua.is_userdata() && !lua.is_nil())
3463: {
3470: if (lua.is_string())
3471: {
3472: object_class_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Add);
3474: else if (lua.is_userdata())
3475: {
3478: auto& userdata = lua.get_userdata<LuaType::UE4SSBaseObject>(1, true);
3481: in_class = lua.get_userdata<LuaType::UClass>().get_remote_cpp_object();
3487: object_class_name = lua.get_userdata<LuaType::FName>().get_local_cpp_object();
3494: else if (lua.is_nil())
3495: {
3504: if (!lua.is_string() && !lua.is_userdata() && !lua.is_integer() && !lua.is_nil())
3505: {
3513: if (lua.is_string())
3514: {
3515: object_short_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Add);
3518: else if (lua.is_userdata())
3519: {
3522: auto& userdata = lua.get_userdata<LuaType::UE4SSBaseObject>(1, true);
3529: in_outer = lua.get_userdata<LuaType::UClass>().get_remote_cpp_object();
3533: in_outer = lua.get_userdata<LuaType::UObject>().get_remote_cpp_object();
3539: object_short_name = lua.get_userdata<LuaType::FName>().get_local_cpp_object();
3547: else if (lua.is_integer())
3548: {
3549: if (lua.get_integer() == -1)
3550: {
3559: else if (lua.is_nil())
3560: {
3573: if (lua.is_string())
3574: {
3575: in_name = lua.get_string();
3578: else if (lua.is_integer())
3579: {
3580: required_flags = static_cast<int32_t>(lua.get_integer());
3582: else if (lua.is_nil())
3583: {
3590: if (lua.is_bool())
3591: {
3592: exact_class = lua.get_bool();
3595: else if (lua.is_integer())
3596: {
3597: banned_flags = static_cast<int32_t>(lua.get_integer());
3599: else if (lua.is_nil())
3600: {
3616: return 1;
```

### Lua `LuaMod.FindObjects` (3619)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3619`。
- 错误类别：C++ exception path (see source)。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3622: Overloads:
3623: #1: FindObjects(integer NumObjectsToFind, string|FName|nil ClassName, string|FName|nil ObjectShortName, EObjectFlags RequiredFlags, EObjectFlags BannedFlags, bool bExactClass)
3624: #2: FindObjects(integer NumObjectsToFind, UClass|nil Class, string|FName|nil ObjectShortName, EObjectFlags RequiredFlags, EObjectFlags BannedFlags, bool bExactClass))"};
3627: if (lua.is_integer())
3628: {
3633: num_objects_to_find = static_cast<int32_t>(lua.get_integer());
3635: else if (lua.is_nil())
3636: {
3644: if (!lua.is_string() && !lua.is_userdata() && !lua.is_nil())
3645: {
3651: if (lua.is_string())
3652: {
3653: object_class_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Add);
3655: else if (lua.is_userdata())
3656: {
3659: auto& userdata = lua.get_userdata<LuaType::UE4SSBaseObject>(1, true);
3662: object_class_name = lua.get_userdata<LuaType::UClass>().get_remote_cpp_object()->GetNamePrivate();
3666: object_class_name = lua.get_userdata<LuaType::FName>().get_local_cpp_object();
3673: else if (lua.is_nil())
3674: {
3683: if (!lua.is_string() && !lua.is_userdata() && !lua.is_nil())
3684: {
3689: if (lua.is_string())
3690: {
3691: object_short_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Add);
3693: else if (lua.is_userdata())
3694: {
3697: auto& userdata = lua.get_userdata<LuaType::UE4SSBaseObject>(1, true);
3700: object_short_name = lua.get_userdata<LuaType::FName>().get_local_cpp_object();
3707: else if (lua.is_nil())
3708: {
3725: if (lua.is_integer())
3726: {
3727: required_flags = static_cast<int32_t>(lua.get_integer());
3729: else if (lua.is_nil())
3730: {
3735: if (lua.is_integer())
3736: {
3737: banned_flags = static_cast<int32_t>(lua.get_integer());
3739: else if (lua.is_nil())
3740: {
3745: if (lua.is_integer())
3746: {
3747: exact_class = lua.get_integer();
3749: else if (lua.is_bool())
3750: {
3751: exact_class = lua.get_bool();
3753: else if (lua.is_nil())
3754: {
3767: auto table = lua.prepare_new_table(static_cast<int32_t>(objects_found.size()));
3775: return 1;
```

### Lua `LuaMod.GetCurrentThreadId` (3778)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3778`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3781: Overloads:
3782: #1: GetCurrentThreadId())"};
3784: LuaType::ThreadId::construct(lua, std::this_thread::get_id());
3786: return 1;
```

### Lua `LuaMod.GetMainModThreadId` (3789)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3789`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3792: Overloads:
3793: #1: GetMainModThreadId())"};
3796: LuaType::ThreadId::construct(lua, mod->get_main_thread_id());
3798: return 1;
```

### Lua `LuaMod.GetAsyncThreadId` (3801)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3801`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3804: Overloads:
3805: #1: GetAsyncThreadId())"};
3808: LuaType::ThreadId::construct(lua, mod->get_async_thread_id());
3810: return 1;
```

### Lua `LuaMod.GetGameThreadId` (3813)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3813`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3816: Overloads:
3817: #1: GetGameThreadId())"};
3819: LuaType::ThreadId::construct(lua, Unreal::GetGameThreadId());
3821: return 1;
```

### Lua `LuaMod.IsInMainModThread` (3824)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3824`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3827: Overloads:
3828: #1: IsInMainModThread())"};
3831: lua.set_bool(std::this_thread::get_id() == mod->get_main_thread_id());
3833: return 1;
```

### Lua `LuaMod.IsInAsyncThread` (3836)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3836`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3839: Overloads:
3840: #1: IsInAsyncThread())"};
3843: lua.set_bool(std::this_thread::get_id() == mod->get_async_thread_id());
3845: return 1;
```

### Lua `LuaMod.IsInGameThread` (3848)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:3848`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
3851: Overloads:
3852: #1: IsInGameThread())"};
3854: lua.set_bool(std::this_thread::get_id() == Unreal::GetGameThreadId());
3856: return 1;
```

### Lua `LuaMod.RegisterHook` (4050)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4050`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4055: Overloads:
4056: #1: RegisterHook(string UFunction_Name, LuaFunction Callback, LuaFunction PostCallback))"};
4058: if (!lua.is_string())
4059: {
4060: lua.throw_error(error_overload_not_found);
4063: auto function_name_no_prefix = get_function_name_without_prefix(ensure_str(lua.get_string()));
4065: if (!lua.is_function())
4066: {
4067: lua.throw_error(error_overload_not_found);
4074: lua_pushvalue(lua.get_lua_state(), 1); // operates on LuaMadeSimple::Lua::m_lua_state
4075: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
4082: if (lua.is_function())
4083: {
4086: lua_pushvalue(lua.get_lua_state(), 1); // operates on LuaMadeSimple::Lua::m_lua_state
4087: lua_xmove(lua.get_lua_state(), hook_lua->get_lua_state(), 1);
4095: lua.throw_error(std::format(
4096: "Tried to register a hook with Lua function 'RegisterHook' but no UFunction with the specified name was found.\nFunction Name: {}",
4097: to_string(function_name_no_prefix)));
4144: lua.throw_error(error_message);
4147: lua.set_integer(pre_id);
4148: lua.set_integer(post_id);
4150: return 2;
```

### Lua `LuaMod.ExecuteInGameThread` (4175)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4175`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4178: Overloads:
4179: #1: ExecuteInGameThread(LuaFunction callback)
4180: #2: ExecuteInGameThread(LuaFunction callback, EGameThreadMethod method)
4181: method: EGameThreadMethod.EngineTick or EGameThreadMethod.ProcessEvent)"};
4183: lua_State* L = lua.get_lua_state();
4189: // Overload #2: callback, method
4190: method = static_cast<GameThreadExecutionMethod>(lua_tointeger(L, 2));
4194: lua.throw_error(error_overload_not_found);
4204: lua.throw_error("ExecuteInGameThread: EngineTick method requested but EngineTick hook is not available (AOB scan failed)");
4212: lua.throw_error("ExecuteInGameThread: ProcessEvent method requested but ProcessEvent hook is not available (AOB scan failed)");
4219: lua_pushvalue(L, callback_idx);
4236: return 0;
```

### Lua `LuaMod.ExecuteInGameThreadWithDelay` (4241)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4241`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4244: Overloads:
4245: #1: ExecuteInGameThreadWithDelay(integer delayMs, LuaFunction callback) -> integer handle
4246: #2: ExecuteInGameThreadWithDelay(integer handle, integer delayMs, LuaFunction callback) -> nil (only creates if handle doesn't exist))"};
4248: lua_State* L = lua.get_lua_state();
4257: lua.throw_error(error_overload_not_found);
4272: lua.throw_error("ExecuteInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4285: lua.throw_error("ExecuteInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4293: // Overload #2: ExecuteInGameThreadWithDelay(handle, delayMs, callback)
4294: // Like UE's Delay - only creates if handle doesn't already exist
4295: auto handle = lua_tointeger(L, 1);
4296: auto delay_ms = lua_tointeger(L, 2);
4306: return 0;
4314: lua_pushvalue(L, 3);
4332: return 0;
4336: // Overload #1: ExecuteInGameThreadWithDelay(delayMs, callback) -> handle
4337: auto delay_ms = lua_tointeger(L, 1);
4340: lua_pushvalue(L, 2);
4358: lua.set_integer(action.handle);
4359: return 1;
```

### Lua `LuaMod.RetriggerableExecuteInGameThreadWithDelay` (4365)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4365`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4368: Overloads:
4369: #1: RetriggerableExecuteInGameThreadWithDelay(integer handle, integer delayMs, LuaFunction callback))"};
4371: lua_State* L = lua.get_lua_state();
4374: lua.throw_error(error_overload_not_found);
4389: lua.throw_error("RetriggerableExecuteInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4402: lua.throw_error("RetriggerableExecuteInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4408: auto handle = lua_tointeger(L, 1);
4409: auto delay_ms = lua_tointeger(L, 2);
4422: lua.set_integer(handle);
4423: return 1;
4431: lua_pushvalue(L, 3);
4450: return 0;
```

### Lua `LuaMod.ExecuteInGameThreadAfterFrames` (4455)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4455`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4458: Overloads:
4459: #1: ExecuteInGameThreadAfterFrames(integer frames, LuaFunction callback) -> integer handle)"};
4461: lua_State* L = lua.get_lua_state();
4462: if (!lua.is_integer() || !lua_isfunction(L, 2))
4463: {
4464: lua.throw_error(error_overload_not_found);
4470: lua.throw_error("ExecuteInGameThreadAfterFrames: EngineTick hook is not available (AOB scan failed). Frame-based delays require EngineTick.");
4473: auto frames = lua.get_integer();
4477: lua_pushvalue(L, 1);
4495: lua.set_integer(action.handle);
4496: return 1;
```

### Lua `LuaMod.LoopInGameThreadWithDelay` (4501)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4501`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4504: Overloads:
4505: #1: LoopInGameThreadWithDelay(integer delayMs, LuaFunction callback) -> integer handle)"};
4507: lua_State* L = lua.get_lua_state();
4508: if (!lua.is_integer() || !lua_isfunction(L, 2))
4509: {
4510: lua.throw_error(error_overload_not_found);
4525: lua.throw_error("LoopInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4538: lua.throw_error("LoopInGameThreadWithDelay: Neither EngineTick nor ProcessEvent hooks are available (AOB scans failed)");
4544: auto delay_ms = lua.get_integer();
4547: lua_pushvalue(L, 1);
4566: lua.set_integer(action.handle);
4567: return 1;
```

### Lua `LuaMod.LoopInGameThreadAfterFrames` (4572)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4572`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4575: Overloads:
4576: #1: LoopInGameThreadAfterFrames(integer frames, LuaFunction callback) -> integer handle)"};
4578: lua_State* L = lua.get_lua_state();
4579: if (!lua.is_integer() || !lua_isfunction(L, 2))
4580: {
4581: lua.throw_error(error_overload_not_found);
4587: lua.throw_error("LoopInGameThreadAfterFrames: EngineTick hook is not available (AOB scan failed). Frame-based delays require EngineTick.");
4590: auto frames = lua.get_integer();
4595: lua_pushvalue(L, 1);
4614: lua.set_integer(action.handle);
4615: return 1;
```

### Lua `LuaMod.ResetDelayedActionTimer` (4619)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4619`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4622: Overloads:
4623: #1: ResetDelayedActionTimer(integer handle) -> boolean success)"};
4625: if (!lua.is_integer())
4626: {
4627: lua.throw_error(error_overload_not_found);
4630: auto handle = lua.get_integer();
4658: lua.set_bool(found);
4659: return 1;
```

### Lua `LuaMod.SetDelayedActionTimer` (4663)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4663`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4666: Overloads:
4667: #1: SetDelayedActionTimer(integer handle, integer newDelay) -> boolean success)"};
4669: lua_State* L = lua.get_lua_state();
4670: if (!lua.is_integer() || !lua_isinteger(L, 2))
4671: {
4672: lua.throw_error(error_overload_not_found);
4675: auto handle = lua.get_integer();
4676: auto new_delay = lua_tointeger(L, 2);
4706: lua.set_bool(found);
4707: return 1;
```

### Lua `LuaMod.PauseDelayedAction` (4711)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4711`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4714: Overloads:
4715: #1: PauseDelayedAction(integer handle) -> boolean success)"};
4717: if (!lua.is_integer())
4718: {
4719: lua.throw_error(error_overload_not_found);
4722: auto handle = lua.get_integer();
4751: lua.set_bool(found);
4752: return 1;
```

### Lua `LuaMod.UnpauseDelayedAction` (4756)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4756`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4759: Overloads:
4760: #1: UnpauseDelayedAction(integer handle) -> boolean success)"};
4762: if (!lua.is_integer())
4763: {
4764: lua.throw_error(error_overload_not_found);
4767: auto handle = lua.get_integer();
4788: lua.set_bool(found);
4789: return 1;
```

### Lua `LuaMod.CancelDelayedAction` (4793)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4793`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4796: Overloads:
4797: #1: CancelDelayedAction(integer handle) -> boolean success)"};
4799: if (!lua.is_integer())
4800: {
4801: lua.throw_error(error_overload_not_found);
4804: auto handle = lua.get_integer();
4823: lua.set_bool(found);
4824: return 1;
```

### Lua `LuaMod.IsValidDelayedActionHandle` (4828)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4828`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4831: Overloads:
4832: #1: IsValidDelayedActionHandle(integer handle) -> boolean valid)"};
4834: if (!lua.is_integer())
4835: {
4836: lua.throw_error(error_overload_not_found);
4839: auto handle = lua.get_integer();
4854: lua.set_bool(valid);
4855: return 1;
```

### Lua `LuaMod.IsDelayedActionActive` (4859)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4859`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4862: Overloads:
4863: #1: IsDelayedActionActive(integer handle) -> boolean active)"};
4865: if (!lua.is_integer())
4866: {
4867: lua.throw_error(error_overload_not_found);
4870: auto handle = lua.get_integer();
4885: lua.set_bool(active);
4886: return 1;
```

### Lua `LuaMod.IsDelayedActionPaused` (4890)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4890`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4893: Overloads:
4894: #1: IsDelayedActionPaused(integer handle) -> boolean paused)"};
4896: if (!lua.is_integer())
4897: {
4898: lua.throw_error(error_overload_not_found);
4901: auto handle = lua.get_integer();
4916: lua.set_bool(paused);
4917: return 1;
```

### Lua `LuaMod.GetDelayedActionTimeRemaining` (4921)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4921`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4924: Overloads:
4925: #1: GetDelayedActionTimeRemaining(integer handle) -> integer remainingMs (or -1 if not found))"};
4927: if (!lua.is_integer())
4928: {
4929: lua.throw_error(error_overload_not_found);
4932: auto handle = lua.get_integer();
4943: // Frame-based: return frames remaining
4944: remaining = action.frames_remaining;
4948: // Paused: return stored remaining time
4949: remaining = action.time_remaining_ms;
4969: lua.set_integer(remaining);
4970: return 1;
```

### Lua `LuaMod.GetDelayedActionTimeElapsed` (4974)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:4974`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
4977: Overloads:
4978: #1: GetDelayedActionTimeElapsed(integer handle) -> integer elapsedMs (or -1 if not found))"};
4980: if (!lua.is_integer())
4981: {
4982: lua.throw_error(error_overload_not_found);
4985: auto handle = lua.get_integer();
4996: // Frame-based: return frames elapsed
4997: elapsed = action.delay_frames - action.frames_remaining;
5023: lua.set_integer(elapsed);
5024: return 1;
```

### Lua `LuaMod.GetDelayedActionRate` (5028)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5028`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5031: Overloads:
5032: #1: GetDelayedActionRate(integer handle) -> integer rateMs (or -1 if not found))"};
5034: if (!lua.is_integer())
5035: {
5036: lua.throw_error(error_overload_not_found);
5039: auto handle = lua.get_integer();
5050: // Frame-based: return frames
5051: rate = action.delay_frames;
5055: // Time-based: return ms
5056: rate = action.delay_ms;
5063: lua.set_integer(rate);
5064: return 1;
```

### Lua `LuaMod.ClearAllDelayedActions` (5068)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5068`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5071: Overloads:
5072: #1: ClearAllDelayedActions() -> integer count)"};
5092: lua.set_integer(count);
5093: return 1;
```

### Lua `LuaMod.MakeActionHandle` (5097)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5097`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5100: Overloads:
5101: #1: MakeActionHandle() -> integer Handle)"};
5103: lua.set_integer(m_next_delayed_action_handle++);
5105: return 1;
```

### Lua `LuaMod.RestartCurrentMod` (5108)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5108`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5112: lua.throw_error("RestartCurrentMod: Could not get mod reference");
5117: return 0;
```

### Lua `LuaMod.UninstallCurrentMod` (5120)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5120`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5124: lua.throw_error("UninstallCurrentMod: Could not get mod reference");
5129: return 0;
```

### Lua `LuaMod.RestartMod` (5133)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5133`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5136: Overloads:
5137: #1: RestartMod(string mod_name))"};
5139: if (!lua.is_string())
5140: {
5141: lua.throw_error(error_overload_not_found);
5144: LuaCompat::queue_reinstall_mod_by_name(lua.get_string());
5146: return 0;
```

### Lua `LuaMod.UninstallMod` (5150)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5150`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5153: Overloads:
5154: #1: UninstallMod(string mod_name))"};
5156: if (!lua.is_string())
5157: {
5158: lua.throw_error(error_overload_not_found);
5161: LuaCompat::queue_uninstall_mod_by_name(lua.get_string());
5163: return 0;
```

### Lua `LuaMod.GetVersion` (5214)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5214`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5215: lua.set_integer(UE4SS_LIB_VERSION_MAJOR);
5216: lua.set_integer(UE4SS_LIB_VERSION_MINOR);
5217: lua.set_integer(UE4SS_LIB_VERSION_HOTFIX);
5218: return 3;
```

### Lua `LuaMod.GetMajor` (5227)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5227`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5228: lua.set_integer(Unreal::Version::Major);
5229: return 1;
```

### Lua `LuaMod.GetMinor` (5232)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5232`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5233: lua.set_integer(Unreal::Version::Minor);
5234: return 1;
```

### Lua `LuaMod.IsEqual` (5237)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5237`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5240: Overloads:
5241: #1: IsEqual(number MajorVersion, number MinorVersion))"};
5245: return 1;
```

### Lua `LuaMod.IsAtLeast` (5248)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5248`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5251: Overloads:
5252: #1: IsAtLeast(number MajorVersion, number MinorVersion))"};
5256: return 1;
```

### Lua `LuaMod.IsAtMost` (5259)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5259`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5262: Overloads:
5263: #1: IsAtMost(number MajorVersion, number MinorVersion))"};
5267: return 1;
```

### Lua `LuaMod.IsBelow` (5270)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5270`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5273: Overloads:
5274: #1: IsBelow(number MajorVersion, number MinorVersion))"};
5278: return 1;
```

### Lua `LuaMod.IsAbove` (5281)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5281`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5284: Overloads:
5285: #1: IsAbove(number MajorVersion, number MinorVersion))"};
5289: return 1;
```

### Lua `LuaMod.FString` (5312)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5312`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5314: if (lua.get_stack_size() < 1 || !lua.is_string())
5315: {
5316: lua.throw_error("FString constructor requires a string argument");
5318: std::string_view str = lua.get_string();
5320: LuaType::FString::construct(lua, &fstring);
5321: return 1;
```

### Lua `LuaMod.FUtf8String` (5327)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5327`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5329: if (lua.get_stack_size() < 1 || !lua.is_string())
5330: {
5331: lua.throw_error("FUtf8String constructor requires a string argument");
5333: std::string_view str = lua.get_string();
5335: LuaType::FUtf8String::construct(lua, &utf8string);
5336: return 1;
```

### Lua `LuaMod.FAnsiString` (5342)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5342`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5344: if (lua.get_stack_size() < 1 || !lua.is_string())
5345: {
5346: lua.throw_error("FAnsiString constructor requires a string argument");
5348: std::string_view str = lua.get_string();
5350: LuaType::FAnsiString::construct(lua, &ansistring);
5351: return 1;
```

### Lua `LuaMod.IsShortPackageName` (5359)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5359`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5362: Overloads:
5363: #1: IsShortPackageName(string PossiblyLongName))"};
5365: if (!lua.is_string())
5366: {
5367: lua.throw_error(error_overload_not_found);
5370: RC::StringType PossiblyLongName = ensure_str(lua.get_string());
5371: lua.set_bool(Unreal::FPackageName::IsShortPackageName(PossiblyLongName));
5373: return 1;
```

### Lua `LuaMod.IsValidLongPackageName` (5376)

- 来源：`crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp:5376`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
5379: Overloads:
5380: #1: IsValidLongPackageName(string InLongPackageName))"};
5382: if (!lua.is_string())
5383: {
5384: lua.throw_error(error_overload_not_found);
5387: RC::StringType InLongPackageName = ensure_str(lua.get_string());
5388: lua.set_bool(Unreal::FPackageName::IsValidLongPackageName(InLongPackageName));
5390: return 1;
```

### Lua `LuaUObject.GetAddress` (171)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:171`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
172: const auto& lua_object = lua.get_userdata<SelfType>();
173: lua.set_integer(reinterpret_cast<uintptr_t>(lua_object.get_remote_cpp_object()));
174: return 1;
```

### Lua `LuaUObject.IsValid` (177)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:177`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
178: const auto& lua_object = lua.get_userdata<SelfType>();
181: lua.set_bool(true);
185: lua.set_bool(false);
187: return 1;
```

### Lua `LuaUObject.type` (194)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:194`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
195: lua.set_string("RemoteObjectBase");
196: return 1;
```

### Lua `LuaUObject.GetFullName` (511)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:511`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
515: const auto& lua_object = lua.get_userdata<SelfType>();
519: // Set the return value to the ansi version of the full name
520: lua.set_string(to_string(lua_object.get_remote_cpp_object()->GetFullName()).c_str());
524: // We have a nullptr, lets return 'nil' for easy object verification in Lua
525: lua.set_nil();
528: return 1;
```

### Lua `LuaUObject.GetFName` (531)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:531`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
533: return 1;
```

### Lua `LuaUObject.GetClass` (536)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:536`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
538: return 1;
```

### Lua `LuaUObject.GetOuter` (541)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:541`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
542: const auto& lua_object = lua.get_userdata<SelfType>();
544: UObject::construct(lua, lua_object.get_remote_cpp_object()->GetOuterPrivate());
546: return 1;
```

### Lua `LuaUObject.IsAnyClass` (549)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:549`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
550: const auto& lua_object = lua.get_userdata<SelfType>();
552: lua.set_bool(lua_object.get_remote_cpp_object()->template IsA<Unreal::UClass>());
554: return 1;
```

### Lua `LuaUObject.Reflection` (557)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:557`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
558: const auto& lua_object = lua.get_userdata<SelfType>();
560: auto reflection_table = lua.prepare_new_table();
563: SelfType::construct(lua, lua_object.get_remote_cpp_object());
571: if (!lua.is_string(2))
572: {
573: lua.throw_error("Function 'GetProperty' requires a string as the first parameter");
575: auto property_name = ensure_str(lua.get_string(2));
577: auto reflection_table = lua.get_table();
583: return 1;
594: return 1;
596: return 1;
```

### Lua `LuaUObject.GetProperty` (566)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:566`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
571: if (!lua.is_string(2))
572: {
573: lua.throw_error("Function 'GetProperty' requires a string as the first parameter");
575: auto property_name = ensure_str(lua.get_string(2));
577: auto reflection_table = lua.get_table();
583: return 1;
594: return 1;
```

### Lua `LuaUObject.GetPropertyValue` (599)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:599`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
600: prepare_to_handle(Operation::Get, lua);
601: return 1;
```

### Lua `LuaUObject.SetPropertyValue` (604)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:604`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
605: prepare_to_handle(Operation::Set, lua);
606: return 1;
```

### Lua `LuaUObject.IsClass` (609)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:609`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
614: const auto& lua_object = lua.get_userdata<SelfType>();
615: lua.set_bool(lua_object.get_remote_cpp_object()->template IsA<Unreal::UClass>());
616: return 1;
```

### Lua `LuaUObject.GetWorld` (619)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:619`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
620: const auto& lua_object = lua.get_userdata<SelfType>();
622: return 1;
```

### Lua `LuaUObject.CallFunction` (625)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:625`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
626: return call_ufunction_from_lua(lua);
```

### Lua `LuaUObject.IsA` (629)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:629`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
630: return is_a_implementation(lua);
```

### Lua `LuaUObject.HasAllFlags` (633)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:633`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
636: Overloads:
637: #1: HasAllFlags(EObjectFlags ObjectFlags))"};
639: const auto& lua_object = lua.get_userdata<SelfType>();
641: if (!lua.is_integer())
642: {
643: lua.throw_error(error_overload_not_found);
646: Unreal::EObjectFlags object_flags = static_cast<Unreal::EObjectFlags>(lua.get_integer());
647: lua.set_bool(lua_object.get_remote_cpp_object()->HasAllFlags(object_flags));
648: return 1;
```

### Lua `LuaUObject.HasAnyFlags` (651)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:651`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
654: Overloads:
655: #1: HasAnyFlags(EObjectFlags ObjectFlags))"};
657: const auto& lua_object = lua.get_userdata<SelfType>();
659: if (!lua.is_integer())
660: {
661: lua.throw_error(error_overload_not_found);
664: Unreal::EObjectFlags object_flags = static_cast<Unreal::EObjectFlags>(lua.get_integer());
665: lua.set_bool(lua_object.get_remote_cpp_object()->HasAnyFlags(object_flags));
666: return 1;
```

### Lua `LuaUObject.HasAnyInternalFlags` (669)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:669`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
672: Overloads:
673: #1: HasAnyInternalFlags(EInternalObjectFlags InternalObjectFlags))"};
675: const auto& lua_object = lua.get_userdata<SelfType>();
677: if (!lua.is_integer())
678: {
679: lua.throw_error(error_overload_not_found);
682: Unreal::EInternalObjectFlags object_internal_flags = static_cast<Unreal::EInternalObjectFlags>(lua.get_integer());
683: lua.set_bool(lua_object.get_remote_cpp_object()->HasAnyInternalFlags(object_internal_flags));
684: return 1;
```

### Lua `LuaUObject.ProcessConsoleExec` (687)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:687`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
690: Overloads:
691: #1: ProcessConsoleExec(string Cmd, nil Reserved, UObject Executor))"};
693: const auto& lua_object = lua.get_userdata<SelfType>();
695: if (!lua.is_string())
696: {
697: lua.throw_error(error_overload_not_found);
699: auto cmd = ensure_str(lua.get_string());
701: if (lua.get_stack_size() < 2)
702: {
703: lua.throw_error(error_overload_not_found);
707: if (!lua.is_userdata())
708: {
709: lua.throw_error(error_overload_not_found);
711: auto executor = lua.get_userdata<LuaType::UObject>();
717: lua.set_bool(return_value);
718: return 1;
```

### Lua `LuaUObject.IsValid` (721)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:721`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
722: const auto& lua_object = lua.get_userdata<SelfType>();
726: lua.set_bool(true);
730: lua.set_bool(false);
732: return 1;
```

### Lua `LuaUObject.type` (739)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:739`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
740: lua.set_string("UObject");
741: return 1;
```

### Lua `LuaUObject.Get` (841)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:841`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
842: prepare_to_handle(Operation::Get, lua);
843: return 1;
```

### Lua `LuaUObject.get` (846)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:846`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
847: prepare_to_handle(Operation::Get, lua);
848: return 1;
```

### Lua `LuaUObject.Set` (851)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:851`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
852: prepare_to_handle(Operation::Set, lua);
853: return 0;
```

### Lua `LuaUObject.set` (856)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:856`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
857: prepare_to_handle(Operation::Set, lua);
858: return 0;
```

### Lua `LuaUObject.type` (862)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:862`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
863: lua.set_string("LocalUnrealParam");
864: return 1;
```

### Lua `LuaUnrealString.ToString` (68)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:68`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
69: auto& lua_object = lua.get_userdata<TLuaStringBase>();
71: return 1;
```

### Lua `LuaUnrealString.Empty` (74)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:74`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
75: auto& lua_object = lua.get_userdata<TLuaStringBase>();
77: return 0;
```

### Lua `LuaUnrealString.Clear` (80)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:80`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
81: auto& lua_object = lua.get_userdata<TLuaStringBase>();
83: return 0;
```

### Lua `LuaUnrealString.Len` (86)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:86`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
87: auto& lua_object = lua.get_userdata<TLuaStringBase>();
88: lua.set_integer(lua_object.get_local_cpp_object().Len());
89: return 1;
```

### Lua `LuaUnrealString.IsEmpty` (92)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:92`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
93: auto& lua_object = lua.get_userdata<TLuaStringBase>();
94: lua.set_bool(lua_object.get_local_cpp_object().IsEmpty());
95: return 1;
```

### Lua `LuaUnrealString.Append` (98)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:98`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
99: auto& lua_object = lua.get_userdata<TLuaStringBase>(1, true); // preserve_stack = true
100:
101: if (lua.is_string(2))
102: {
103: std::string_view str_view = lua.get_string(2);
106: else if (lua.is_userdata(2))
107: {
108: auto& other = lua.get_userdata<TLuaStringBase>(2, true); // preserve_stack = true
109: append_from_object(lua_object.get_local_cpp_object(), other.get_local_cpp_object());
113: lua.throw_error("Append requires a string argument");
116: return 0;
```

### Lua `LuaUnrealString.Find` (119)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:119`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
120: auto& lua_object = lua.get_userdata<TLuaStringBase>(1, true); // preserve_stack = true
121:
122: if (!lua.is_string(2))
123: {
124: lua.throw_error("Find requires a string argument");
127: std::string_view search = lua.get_string(2);
132: lua.set_nil();
136: lua.set_integer(result + 1); // Convert to 1-based Lua indexing
137: }
139: return 1;
```

### Lua `LuaUnrealString.StartsWith` (142)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:142`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
143: auto& lua_object = lua.get_userdata<TLuaStringBase>(1, true); // preserve_stack = true
144:
145: if (!lua.is_string(2))
146: {
147: lua.throw_error("StartsWith requires a string argument");
150: std::string_view prefix = lua.get_string(2);
152: lua.set_bool(result);
154: return 1;
```

### Lua `LuaUnrealString.EndsWith` (157)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:157`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
158: auto& lua_object = lua.get_userdata<TLuaStringBase>(1, true); // preserve_stack = true
159:
160: if (!lua.is_string(2))
161: {
162: lua.throw_error("EndsWith requires a string argument");
165: std::string_view suffix = lua.get_string(2);
167: lua.set_bool(result);
169: return 1;
```

### Lua `LuaUnrealString.ToUpper` (172)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:172`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
173: auto& lua_object = lua.get_userdata<TLuaStringBase>();
175: construct(lua, &upper);
176: return 1;
```

### Lua `LuaUnrealString.ToLower` (179)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:179`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
180: auto& lua_object = lua.get_userdata<TLuaStringBase>();
182: construct(lua, &lower);
183: return 1;
```

### Lua `LuaUnrealString.type` (188)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUnrealString.hpp:188`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
189: lua.set_string(StringNameType::ToString());
190: return 1;
```

### Lua `LuaFName.operator:Call` (48)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:48`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
52: Overloads:
53: #1: FName(string Name, EFindName = FNAME_Add)
54: #2: FName(integer ComparisonIndex, EFindName = FNAME_Add))"};
56: if (lua.is_userdata())
57: {
65: if (lua.is_string())
66: {
67: name_string = ensure_str(lua.get_string());
69: else if (lua.is_integer())
70: {
71: name_comparison_index = lua.get_integer();
75: lua.throw_error(error_overload_not_found);
78: if (lua.is_integer())
79: {
80: find_type = static_cast<Unreal::EFindName>(lua.get_integer());
87: lua.throw_error("FName constructor cannot take an integer smaller than uint32.");
91: lua.throw_error("FName constructor cannot take an integer larger than uint32.");
93: LuaType::FName::construct(lua, Unreal::FName(static_cast<uint32_t>(name_comparison_index), find_type));
97: LuaType::FName::construct(lua, Unreal::FName(name_string, find_type));
100: return 1;
```

### Lua `LuaFName.operator:Equal` (103)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp:103`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
104: if (!lua.is_userdata(1) || !lua.is_userdata(2))
105: {
106: lua.throw_error("FName __eq metamethod called but there was not two userdata to compare");
109: auto name_a = lua.get_userdata<LuaType::FName>();
110: auto name_b = lua.get_userdata<LuaType::FName>();
112: return name_a.get_local_cpp_object() == name_b.get_local_cpp_object();
```

### Lua `LuaFText.operator:Call` (47)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp:47`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
51: Overloads:
52: #1: FText(string Text)
53: )"};
55: if (lua.is_userdata())
56: {
62: if (lua.is_string())
63: {
64: text_string = ensure_str(lua.get_string());
68: lua.throw_error(error_overload_not_found);
71: LuaType::FText::construct(lua, Unreal::FText(text_string));
73: return 1;
```

### Lua `LuaFText.operator:Equal` (76)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp:76`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
77: if (!lua.is_userdata(1) || !lua.is_userdata(2))
78: {
79: lua.throw_error("FText __eq metamethod called but there was not two userdata to compare");
82: auto text_a = lua.get_userdata<LuaType::FText>();
83: auto text_b = lua.get_userdata<LuaType::FText>();
85: return text_a.get_local_cpp_object().ToString() == text_b.get_local_cpp_object().ToString();
```

### Lua `LuaFURL.operator:Call` (44)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaFURL.cpp:44`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
47: return 1;
```

### Lua `LuaTArray.operator:Index` (59)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:59`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
60: prepare_to_handle(LuaMadeSimple::Type::Operation::Get, lua);
61: return 1;
```

### Lua `LuaTArray.operator:NewIndex` (64)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:64`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
65: prepare_to_handle(LuaMadeSimple::Type::Operation::Set, lua);
66: return 1;
```

### Lua `LuaTArray.operator:Length` (69)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:69`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
70: auto& lua_object = lua.get_userdata<TArray>();
71: lua.set_integer(lua_object.get_remote_cpp_object()->Num());
72: return 1;
```

### Lua `LuaTMap.operator:Length` (64)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:64`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
66: auto lua_object = lua.get_userdata<TMap>();
67: lua.set_integer(lua_object.get_remote_cpp_object()->Num());
68: return 1;
```

### Lua `LuaTSet.operator:Length` (58)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:58`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
60: auto lua_object = lua.get_userdata<TSet>();
61: lua.set_integer(lua_object.get_remote_cpp_object()->Num());
62: return 1;
```

### Lua `LuaThreadId.operator:Equal` (49)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp:49`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
50: if (!lua.is_userdata(1) || !lua.is_userdata(2))
51: {
52: lua.throw_error("ThreadId __eq metamethod called but there was not two userdata to compare");
55: auto a = lua.get_userdata<LuaType::ThreadId>();
56: auto b = lua.get_userdata<LuaType::ThreadId>();
58: return a.get_local_cpp_object() == b.get_local_cpp_object();
```

### Lua `LuaUDataTable.operator:Length` (50)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:50`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
52: auto& lua_object = lua.get_userdata<UDataTable>();
54: lua.set_integer(data_table->GetRowMap().Num());
55: return 1;
```

### Lua `LuaUFunction.operator:Call` (70)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp:70`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
71: return call_ufunction_from_lua(lua);
```

### Lua `LuaUScriptStruct.operator:Index` (58)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:58`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
59: prepare_to_handle(LuaMadeSimple::Type::Operation::Get, lua);
60: return 1;
```

### Lua `LuaUScriptStruct.operator:NewIndex` (63)

- 来源：`crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:63`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
64: prepare_to_handle(LuaMadeSimple::Type::Operation::Set, lua);
65: return 0;
```

### Lua `LuaUObject.operator:Index` (483)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:483`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
484: prepare_to_handle(Operation::Get, lua);
485: return 1;
```

### Lua `LuaUObject.operator:NewIndex` (488)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:488`。
- 错误类别：No explicit error in binding; VM conversions/engine/helper failures remain possible。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
489: prepare_to_handle(Operation::Set, lua);
490: return 0;
```

### Lua `LuaUObject.operator:Call` (493)

- 来源：`crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp:493`。
- 错误类别：Lua error / protected-call failure。
- 参数、overload、返回栈及错误分支（原始语句证据）：

```cpp
494: const auto& lua_object = lua.get_userdata<SelfType>();
498: lua.throw_error("Tried calling a member function but the UObject instance is nullptr\n");
500: return 0;
```

## 非函数全局、构造器和共享值转换

Lua `Key`/`ModifierKey`、FindName 等常量及全局表按以下冻结注册定义；JS `UE4SS` 对象不是第58个函数。数值常量不是参数校验范围的保证。Lua 字符串/名字/对象构造器通过 global/table/operator 三条路径暴露，均在上述条目与下面发布点覆盖。

### `crates/ue4ssl-javascript/native/cpp/JSMod.cpp` 数据发布点

```cpp
1310: JS_SetCanBlock(m_runtime, false);
1311:
1312: Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] QuickJS runtime created\n"));
1313: return true;
1314: }
1315:
1316: auto JSMod::init_context() -> bool
1317: {
1318: m_main_ctx = JS_NewContext(m_runtime);
1319: if (!m_main_ctx)
1320: {
1321: Output::send<LogLevel::Error>(STR("[UE4SSL.JavaScript] Failed to create QuickJS context\n"));
1322: return false;
1323: }
1324:
1325: js_std_add_helpers(m_main_ctx, 0, nullptr);
1326: setup_global_functions(m_main_ctx);
1327: setup_classes(m_main_ctx);
1328:
1329: m_fetch_worker_stop.store(false);
1330: m_fetch_worker = std::thread(fetch_worker_run, this);
1331: m_download_worker_stop.store(false);
1332: m_download_worker = std::thread(download_worker_run, this);
1333: Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] QuickJS context created, fetch/download workers started\n"));
1334: return true;
1335: }
1336:
1337: auto JSMod::setup_module_loader() -> void
1338: {
1339: JS_SetModuleLoaderFunc(m_runtime, js_module_normalize, js_module_loader, this);
1340: Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Module loader configured\n"));
1341: }
1342:
1343: auto JSMod::setup_global_functions(JSContext* ctx) -> void
1344: {
1345: JSValue global = JS_GetGlobalObject(ctx);
1346:
1347: size_t function_index{};
1348: const size_t api_count = ue4ssl_js_global_api_count();
1349: for (size_t api_index = 0; api_index < api_count; ++api_index)
1350: {
1351: if (!ue4ssl_js_global_api_is_function(api_index))
1352: {
1353: continue;
1354: }
1355:
1356: const char* name = ue4ssl_js_global_api_name(api_index);
1357: const int arity = ue4ssl_js_global_api_arity(api_index);
1358: if (!name)
1359: {
1360: Output::send<LogLevel::Warning>(
1361: STR("[UE4SSL.JavaScript] Skipping global API entry {} with null name\n"), api_index);
1362: continue;
1363: }
1364: if (function_index >= GlobalFunctionImplementations.size())
1365: {
1366: Output::send<LogLevel::Error>(
1367: STR("[UE4SSL.JavaScript] Rust global API manifest has more functions than the C++ implementation table\n"));
1368: break;
1369: }
1370:
1371: JS_SetPropertyStr(
1372: ctx,
1373: global,
1374: name,
1375: JS_NewCFunction(ctx, GlobalFunctionImplementations[function_index], name, arity));
1376: ++function_index;
1377: }
1378:
1379: if (function_index != GlobalFunctionImplementations.size())
1380: {
1381: Output::send<LogLevel::Warning>(
1382: STR("[UE4SSL.JavaScript] Registered {} global functions, expected {}\n"),
1383: function_index,
1384: GlobalFunctionImplementations.size());
1385: }
1386:
1387: JSValue ue4ss = JS_NewObject(ctx);
1388: JS_SetPropertyStr(ctx, ue4ss, "version", JS_NewString(ctx, "1.0.0"));
1389: JS_SetPropertyStr(ctx, ue4ss, "configuration", JS_NewString(ctx, UE4SS_CONFIGURATION));
1390: const char* ue4ss_name = api_count > 0 ? ue4ssl_js_global_api_name(api_count - 1) : nullptr;
1391: JS_SetPropertyStr(ctx, global, ue4ss_name ? ue4ss_name : "UE4SS", ue4ss);
1392: JS_SetPropertyStr(
1393: ctx,
1394: global,
1395: "TextDecoder",
1396: JS_NewCFunction2(ctx, js_text_decoder_constructor, "TextDecoder", 1, JS_CFUNC_constructor_or_func, 0));
1397:
1398: JS_FreeValue(ctx, global);
1399:
1400: Output::send<LogLevel::Normal>(STR("[UE4SSL.JavaScript] Global functions registered\n"));
1401: }
1402:
1403: auto JSMod::setup_classes(JSContext* ctx) -> void
1404: {
1405: JSUObject::init_class(ctx);
```

### `crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp` 数据发布点

```cpp
388: lua_table.make_global(variable_name);
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaAActor.cpp` 数据发布点

```cpp
75: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaFName.cpp` 数据发布点

```cpp
156: // table.make_global("FNameUserdata");
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaFOutputDevice.cpp` 数据发布点

```cpp
82: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaFSoftObjectPath.cpp` 数据发布点

```cpp
75: // table.make_global(metatable_name);// , is_final == LuaMadeSimple::Type::IsFinal::No);
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaFText.cpp` 数据发布点

```cpp
110: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaFWeakObjectPtr.cpp` 数据发布点

```cpp
73: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaModRef.cpp` 数据发布点

```cpp
228: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp` 数据发布点

```cpp
199: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp` 数据发布点

```cpp
183: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSoftObjectPtr.cpp` 数据发布点

```cpp
82: // table.make_global(metatable_name);// , is_final == LuaMadeSimple::Type::IsFinal::No);
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaThreadId.cpp` 数据发布点

```cpp
84: // table.make_global("ThreadIdUserdata");
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUClass.cpp` 数据发布点

```cpp
90: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUEnum.cpp` 数据发布点

```cpp
357: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUFunction.cpp` 数据发布点

```cpp
104: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUInterface.cpp` 数据发布点

```cpp
68: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp` 数据发布点

```cpp
1704: table.make_global(prop_name);
2480: // table.make_global("RemoteUnrealParam");
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp` 数据发布点

```cpp
157: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUStruct.cpp` 数据发布点

```cpp
138: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUWorld.cpp` 数据发布点

```cpp
154: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXArrayProperty.cpp` 数据发布点

```cpp
72: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXBoolProperty.cpp` 数据发布点

```cpp
90: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXEnumProperty.cpp` 数据发布点

```cpp
72: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXFieldClass.cpp` 数据发布点

```cpp
66: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXInterfaceProperty.cpp` 数据发布点

```cpp
72: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXObjectProperty.cpp` 数据发布点

```cpp
72: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXProperty.cpp` 数据发布点

```cpp
273: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/LuaType/LuaXStructProperty.cpp` 数据发布点

```cpp
75: // table.make_global(ClassName::ToString());
```

### `crates/ue4ssl-lua/native/cpp/Mod/LuaMod.cpp` 数据发布点

```cpp
565: key_table.add_pair("LEFT_MOUSE_BUTTON", static_cast<uint32_t>(Input::Key::LEFT_MOUSE_BUTTON));
566: key_table.add_pair("RIGHT_MOUSE_BUTTON", static_cast<uint32_t>(Input::Key::RIGHT_MOUSE_BUTTON));
567: key_table.add_pair("CANCEL", static_cast<uint32_t>(Input::Key::CANCEL));
568: key_table.add_pair("MIDDLE_MOUSE_BUTTON", static_cast<uint32_t>(Input::Key::MIDDLE_MOUSE_BUTTON));
569: key_table.add_pair("XBUTTON_ONE", static_cast<uint32_t>(Input::Key::XBUTTON_ONE));
570: key_table.add_pair("XBUTTON_TWO", static_cast<uint32_t>(Input::Key::XBUTTON_TWO));
571: key_table.add_pair("BACKSPACE", static_cast<uint32_t>(Input::Key::BACKSPACE));
572: key_table.add_pair("TAB", static_cast<uint32_t>(Input::Key::TAB));
573: key_table.add_pair("CLEAR", static_cast<uint32_t>(Input::Key::CLEAR));
574: key_table.add_pair("RETURN", static_cast<uint32_t>(Input::Key::RETURN));
575: key_table.add_pair("PAUSE", static_cast<uint32_t>(Input::Key::PAUSE));
576: key_table.add_pair("CAPS_LOCK", static_cast<uint32_t>(Input::Key::CAPS_LOCK));
577: key_table.add_pair("IME_KANA", static_cast<uint32_t>(Input::Key::IME_KANA));
578: key_table.add_pair("IME_HANGUEL", static_cast<uint32_t>(Input::Key::IME_HANGUEL));
579: key_table.add_pair("IME_HANGUL", static_cast<uint32_t>(Input::Key::IME_HANGUL));
580: key_table.add_pair("IME_ON", static_cast<uint32_t>(Input::Key::IME_ON));
581: key_table.add_pair("IME_JUNJA", static_cast<uint32_t>(Input::Key::IME_JUNJA));
582: key_table.add_pair("IME_FINAL", static_cast<uint32_t>(Input::Key::IME_FINAL));
583: key_table.add_pair("IME_HANJA", static_cast<uint32_t>(Input::Key::IME_HANJA));
584: key_table.add_pair("IME_KANJI", static_cast<uint32_t>(Input::Key::IME_KANJI));
585: key_table.add_pair("IME_OFF", static_cast<uint32_t>(Input::Key::IME_OFF));
586: key_table.add_pair("ESCAPE", static_cast<uint32_t>(Input::Key::ESCAPE));
587: key_table.add_pair("IME_CONVERT", static_cast<uint32_t>(Input::Key::IME_CONVERT));
588: key_table.add_pair("IME_NONCONVERT", static_cast<uint32_t>(Input::Key::IME_NONCONVERT));
589: key_table.add_pair("IME_ACCEPT", static_cast<uint32_t>(Input::Key::IME_ACCEPT));
590: key_table.add_pair("IME_MODECHANGE", static_cast<uint32_t>(Input::Key::IME_MODECHANGE));
591: key_table.add_pair("SPACE", static_cast<uint32_t>(Input::Key::SPACE));
592: key_table.add_pair("PAGE_UP", static_cast<uint32_t>(Input::Key::PAGE_UP));
593: key_table.add_pair("PAGE_DOWN", static_cast<uint32_t>(Input::Key::PAGE_DOWN));
594: key_table.add_pair("END", static_cast<uint32_t>(Input::Key::END));
595: key_table.add_pair("HOME", static_cast<uint32_t>(Input::Key::HOME));
596: key_table.add_pair("LEFT_ARROW", static_cast<uint32_t>(Input::Key::LEFT_ARROW));
597: key_table.add_pair("UP_ARROW", static_cast<uint32_t>(Input::Key::UP_ARROW));
598: key_table.add_pair("RIGHT_ARROW", static_cast<uint32_t>(Input::Key::RIGHT_ARROW));
599: key_table.add_pair("DOWN_ARROW", static_cast<uint32_t>(Input::Key::DOWN_ARROW));
600: key_table.add_pair("SELECT", static_cast<uint32_t>(Input::Key::SELECT));
601: key_table.add_pair("PRINT", static_cast<uint32_t>(Input::Key::PRINT));
602: key_table.add_pair("EXECUTE", static_cast<uint32_t>(Input::Key::EXECUTE));
603: key_table.add_pair("PRINT_SCREEN", static_cast<uint32_t>(Input::Key::PRINT_SCREEN));
604: key_table.add_pair("INS", static_cast<uint32_t>(Input::Key::INS));
605: key_table.add_pair("DEL", static_cast<uint32_t>(Input::Key::DEL));
606: key_table.add_pair("HELP", static_cast<uint32_t>(Input::Key::HELP));
607: key_table.add_pair("ZERO", static_cast<uint32_t>(Input::Key::ZERO));
608: key_table.add_pair("ONE", static_cast<uint32_t>(Input::Key::ONE));
609: key_table.add_pair("TWO", static_cast<uint32_t>(Input::Key::TWO));
610: key_table.add_pair("THREE", static_cast<uint32_t>(Input::Key::THREE));
611: key_table.add_pair("FOUR", static_cast<uint32_t>(Input::Key::FOUR));
612: key_table.add_pair("FIVE", static_cast<uint32_t>(Input::Key::FIVE));
613: key_table.add_pair("SIX", static_cast<uint32_t>(Input::Key::SIX));
614: key_table.add_pair("SEVEN", static_cast<uint32_t>(Input::Key::SEVEN));
615: key_table.add_pair("EIGHT", static_cast<uint32_t>(Input::Key::EIGHT));
616: key_table.add_pair("NINE", static_cast<uint32_t>(Input::Key::NINE));
617: key_table.add_pair("A", static_cast<uint32_t>(Input::Key::A));
618: key_table.add_pair("B", static_cast<uint32_t>(Input::Key::B));
619: key_table.add_pair("C", static_cast<uint32_t>(Input::Key::C));
620: key_table.add_pair("D", static_cast<uint32_t>(Input::Key::D));
621: key_table.add_pair("E", static_cast<uint32_t>(Input::Key::E));
622: key_table.add_pair("F", static_cast<uint32_t>(Input::Key::F));
623: key_table.add_pair("G", static_cast<uint32_t>(Input::Key::G));
624: key_table.add_pair("H", static_cast<uint32_t>(Input::Key::H));
625: key_table.add_pair("I", static_cast<uint32_t>(Input::Key::I));
626: key_table.add_pair("J", static_cast<uint32_t>(Input::Key::J));
627: key_table.add_pair("K", static_cast<uint32_t>(Input::Key::K));
628: key_table.add_pair("L", static_cast<uint32_t>(Input::Key::L));
629: key_table.add_pair("M", static_cast<uint32_t>(Input::Key::M));
630: key_table.add_pair("N", static_cast<uint32_t>(Input::Key::N));
631: key_table.add_pair("O", static_cast<uint32_t>(Input::Key::O));
632: key_table.add_pair("P", static_cast<uint32_t>(Input::Key::P));
633: key_table.add_pair("Q", static_cast<uint32_t>(Input::Key::Q));
634: key_table.add_pair("R", static_cast<uint32_t>(Input::Key::R));
635: key_table.add_pair("S", static_cast<uint32_t>(Input::Key::S));
636: key_table.add_pair("T", static_cast<uint32_t>(Input::Key::T));
637: key_table.add_pair("U", static_cast<uint32_t>(Input::Key::U));
638: key_table.add_pair("V", static_cast<uint32_t>(Input::Key::V));
639: key_table.add_pair("W", static_cast<uint32_t>(Input::Key::W));
640: key_table.add_pair("X", static_cast<uint32_t>(Input::Key::X));
641: key_table.add_pair("Y", static_cast<uint32_t>(Input::Key::Y));
642: key_table.add_pair("Z", static_cast<uint32_t>(Input::Key::Z));
643: key_table.add_pair("LEFT_WIN", static_cast<uint32_t>(Input::Key::LEFT_WIN));
644: key_table.add_pair("RIGHT_WIN", static_cast<uint32_t>(Input::Key::RIGHT_WIN));
645: key_table.add_pair("APPS", static_cast<uint32_t>(Input::Key::APPS));
646: key_table.add_pair("SLEEP", static_cast<uint32_t>(Input::Key::SLEEP));
647: key_table.add_pair("NUM_ZERO", static_cast<uint32_t>(Input::Key::NUM_ZERO));
648: key_table.add_pair("NUM_ONE", static_cast<uint32_t>(Input::Key::NUM_ONE));
649: key_table.add_pair("NUM_TWO", static_cast<uint32_t>(Input::Key::NUM_TWO));
650: key_table.add_pair("NUM_THREE", static_cast<uint32_t>(Input::Key::NUM_THREE));
651: key_table.add_pair("NUM_FOUR", static_cast<uint32_t>(Input::Key::NUM_FOUR));
652: key_table.add_pair("NUM_FIVE", static_cast<uint32_t>(Input::Key::NUM_FIVE));
653: key_table.add_pair("NUM_SIX", static_cast<uint32_t>(Input::Key::NUM_SIX));
654: key_table.add_pair("NUM_SEVEN", static_cast<uint32_t>(Input::Key::NUM_SEVEN));
655: key_table.add_pair("NUM_EIGHT", static_cast<uint32_t>(Input::Key::NUM_EIGHT));
656: key_table.add_pair("NUM_NINE", static_cast<uint32_t>(Input::Key::NUM_NINE));
657: key_table.add_pair("MULTIPLY", static_cast<uint32_t>(Input::Key::MULTIPLY));
658: key_table.add_pair("ADD", static_cast<uint32_t>(Input::Key::ADD));
659: key_table.add_pair("SEPARATOR", static_cast<uint32_t>(Input::Key::SEPARATOR));
660: key_table.add_pair("SUBTRACT", static_cast<uint32_t>(Input::Key::SUBTRACT));
661: key_table.add_pair("DECIMAL", static_cast<uint32_t>(Input::Key::DECIMAL));
662: key_table.add_pair("DIVIDE", static_cast<uint32_t>(Input::Key::DIVIDE));
663: key_table.add_pair("F1", static_cast<uint32_t>(Input::Key::F1));
664: key_table.add_pair("F2", static_cast<uint32_t>(Input::Key::F2));
665: key_table.add_pair("F3", static_cast<uint32_t>(Input::Key::F3));
666: key_table.add_pair("F4", static_cast<uint32_t>(Input::Key::F4));
667: key_table.add_pair("F5", static_cast<uint32_t>(Input::Key::F5));
668: key_table.add_pair("F6", static_cast<uint32_t>(Input::Key::F6));
669: key_table.add_pair("F7", static_cast<uint32_t>(Input::Key::F7));
670: key_table.add_pair("F8", static_cast<uint32_t>(Input::Key::F8));
671: key_table.add_pair("F9", static_cast<uint32_t>(Input::Key::F9));
672: key_table.add_pair("F10", static_cast<uint32_t>(Input::Key::F10));
673: key_table.add_pair("F11", static_cast<uint32_t>(Input::Key::F11));
674: key_table.add_pair("F12", static_cast<uint32_t>(Input::Key::F12));
675: key_table.add_pair("F13", static_cast<uint32_t>(Input::Key::F13));
676: key_table.add_pair("F14", static_cast<uint32_t>(Input::Key::F14));
677: key_table.add_pair("F15", static_cast<uint32_t>(Input::Key::F15));
678: key_table.add_pair("F16", static_cast<uint32_t>(Input::Key::F16));
679: key_table.add_pair("F17", static_cast<uint32_t>(Input::Key::F17));
680: key_table.add_pair("F18", static_cast<uint32_t>(Input::Key::F18));
681: key_table.add_pair("F19", static_cast<uint32_t>(Input::Key::F19));
682: key_table.add_pair("F20", static_cast<uint32_t>(Input::Key::F20));
683: key_table.add_pair("F21", static_cast<uint32_t>(Input::Key::F21));
684: key_table.add_pair("F22", static_cast<uint32_t>(Input::Key::F22));
685: key_table.add_pair("F23", static_cast<uint32_t>(Input::Key::F23));
686: key_table.add_pair("F24", static_cast<uint32_t>(Input::Key::F24));
687: key_table.add_pair("NUM_LOCK", static_cast<uint32_t>(Input::Key::NUM_LOCK));
688: key_table.add_pair("SCROLL_LOCK", static_cast<uint32_t>(Input::Key::SCROLL_LOCK));
689: key_table.add_pair("BROWSER_BACK", static_cast<uint32_t>(Input::Key::BROWSER_BACK));
690: key_table.add_pair("BROWSER_FORWARD", static_cast<uint32_t>(Input::Key::BROWSER_FORWARD));
691: key_table.add_pair("BROWSER_REFRESH", static_cast<uint32_t>(Input::Key::BROWSER_REFRESH));
692: key_table.add_pair("BROWSER_STOP", static_cast<uint32_t>(Input::Key::BROWSER_STOP));
693: key_table.add_pair("BROWSER_SEARCH", static_cast<uint32_t>(Input::Key::BROWSER_SEARCH));
694: key_table.add_pair("BROWSER_FAVORITES", static_cast<uint32_t>(Input::Key::BROWSER_FAVORITES));
695: key_table.add_pair("BROWSER_HOME", static_cast<uint32_t>(Input::Key::BROWSER_HOME));
696: key_table.add_pair("VOLUME_MUTE", static_cast<uint32_t>(Input::Key::VOLUME_MUTE));
697: key_table.add_pair("VOLUME_DOWN", static_cast<uint32_t>(Input::Key::VOLUME_DOWN));
698: key_table.add_pair("VOLUME_UP", static_cast<uint32_t>(Input::Key::VOLUME_UP));
699: key_table.add_pair("MEDIA_NEXT_TRACK", static_cast<uint32_t>(Input::Key::MEDIA_NEXT_TRACK));
700: key_table.add_pair("MEDIA_PREV_TRACK", static_cast<uint32_t>(Input::Key::MEDIA_PREV_TRACK));
701: key_table.add_pair("MEDIA_STOP", static_cast<uint32_t>(Input::Key::MEDIA_STOP));
702: key_table.add_pair("MEDIA_PLAY_PAUSE", static_cast<uint32_t>(Input::Key::MEDIA_PLAY_PAUSE));
703: key_table.add_pair("LAUNCH_MAIL", static_cast<uint32_t>(Input::Key::LAUNCH_MAIL));
704: key_table.add_pair("LAUNCH_MEDIA_SELECT", static_cast<uint32_t>(Input::Key::LAUNCH_MEDIA_SELECT));
705: key_table.add_pair("LAUNCH_APP1", static_cast<uint32_t>(Input::Key::LAUNCH_APP1));
706: key_table.add_pair("LAUNCH_APP2", static_cast<uint32_t>(Input::Key::LAUNCH_APP2));
707: key_table.add_pair("OEM_ONE", static_cast<uint32_t>(Input::Key::OEM_ONE));
708: key_table.add_pair("OEM_PLUS", static_cast<uint32_t>(Input::Key::OEM_PLUS));
709: key_table.add_pair("OEM_COMMA", static_cast<uint32_t>(Input::Key::OEM_COMMA));
710: key_table.add_pair("OEM_MINUS", static_cast<uint32_t>(Input::Key::OEM_MINUS));
711: key_table.add_pair("OEM_PERIOD", static_cast<uint32_t>(Input::Key::OEM_PERIOD));
712: key_table.add_pair("OEM_TWO", static_cast<uint32_t>(Input::Key::OEM_TWO));
713: key_table.add_pair("OEM_THREE", static_cast<uint32_t>(Input::Key::OEM_THREE));
714: key_table.add_pair("OEM_FOUR", static_cast<uint32_t>(Input::Key::OEM_FOUR));
715: key_table.add_pair("OEM_FIVE", static_cast<uint32_t>(Input::Key::OEM_FIVE));
716: key_table.add_pair("OEM_SIX", static_cast<uint32_t>(Input::Key::OEM_SIX));
717: key_table.add_pair("OEM_SEVEN", static_cast<uint32_t>(Input::Key::OEM_SEVEN));
718: key_table.add_pair("OEM_EIGHT", static_cast<uint32_t>(Input::Key::OEM_EIGHT));
719: key_table.add_pair("OEM_102", static_cast<uint32_t>(Input::Key::OEM_102));
720: key_table.add_pair("IME_PROCESS", static_cast<uint32_t>(Input::Key::IME_PROCESS));
721: key_table.add_pair("PACKET", static_cast<uint32_t>(Input::Key::PACKET));
722: key_table.add_pair("ATTN", static_cast<uint32_t>(Input::Key::ATTN));
723: key_table.add_pair("CRSEL", static_cast<uint32_t>(Input::Key::CRSEL));
724: key_table.add_pair("EXSEL", static_cast<uint32_t>(Input::Key::EXSEL));
725: key_table.add_pair("EREOF", static_cast<uint32_t>(Input::Key::EREOF));
726: key_table.add_pair("PLAY", static_cast<uint32_t>(Input::Key::PLAY));
727: key_table.add_pair("ZOOM", static_cast<uint32_t>(Input::Key::ZOOM));
728: key_table.add_pair("PA1", static_cast<uint32_t>(Input::Key::PA1));
729: key_table.add_pair("OEM_CLEAR", static_cast<uint32_t>(Input::Key::OEM_CLEAR));
730: key_table.make_global("Key");
733: modifier_key_table.add_pair("SHIFT", 0x10);
734: modifier_key_table.add_pair("CONTROL", 0x11);
735: modifier_key_table.add_pair("ALT", 0x12);
736: /*modifier_key_table.add_pair("LEFT_SHIFT", 0xA0);
737: modifier_key_table.add_pair("RIGHT_SHIFT", 0xA1);
738: modifier_key_table.add_pair("LEFT_CONTROL", 0xA2);
739: modifier_key_table.add_pair("RIGHT_CONTROL", 0xA3);
740: modifier_key_table.add_pair("LEFT_ALT", 0xA4);
741: modifier_key_table.add_pair("RIGHT_ALT", 0xA5);*/
742: modifier_key_table.make_global("ModifierKey");
748: object_flags_table.add_pair("RF_NoFlags", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_NoFlags));
749: object_flags_table.add_pair("RF_Public", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_Public));
750: object_flags_table.add_pair("RF_Standalone", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_Standalone));
751: object_flags_table.add_pair("RF_MarkAsNative", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_MarkAsNative));
752: object_flags_table.add_pair("RF_Transactional", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_Transactional));
755: object_flags_table.add_pair("RF_ArchetypeObject", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_ArchetypeObject));
756: object_flags_table.add_pair("RF_Transient", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_Transient));
757: object_flags_table.add_pair("RF_MarkAsRootSet", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_MarkAsRootSet));
758: object_flags_table.add_pair("RF_TagGarbageTemp", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_TagGarbageTemp));
761: object_flags_table.add_pair("RF_NeedLoad", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_NeedLoad));
762: object_flags_table.add_pair("RF_KeepForCooker", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_KeepForCooker));
763: object_flags_table.add_pair("RF_NeedPostLoad", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_NeedPostLoad));
768: object_flags_table.add_pair("RF_BeginDestroyed", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_BeginDestroyed));
769: object_flags_table.add_pair("RF_FinishDestroyed", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_FinishDestroyed));
770: object_flags_table.add_pair("RF_BeingRegenerated", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_BeingRegenerated));
771: object_flags_table.add_pair("RF_DefaultSubObject", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_DefaultSubObject));
772: object_flags_table.add_pair("RF_WasLoaded", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_WasLoaded));
775: object_flags_table.add_pair("RF_LoadCompleted", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_LoadCompleted));
780: object_flags_table.add_pair("RF_StrongRefOnFrame", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_StrongRefOnFrame));
783: object_flags_table.add_pair("RF_Dynamic", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_Dynamic));
784: object_flags_table.add_pair("RF_WillBeLoaded", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_WillBeLoaded));
787: object_flags_table.add_pair("RF_AllFlags", static_cast<std::underlying_type_t<Unreal::EObjectFlags>>(Unreal::EObjectFlags::RF_AllFlags));
788: object_flags_table.make_global("EObjectFlags");
795: object_internal_flags_table.add_pair("Native", static_cast<std::underlying_type_t<Unreal::EInternalObjectFlags>>(Unreal::EInternalObjectFlags::Native));
796: object_internal_flags_table.add_pair("Async", static_cast<std::underlying_type_t<Unreal::EInternalObjectFlags>>(Unreal::EInternalObjectFlags::Async));
803: object_internal_flags_table.add_pair("RootSet", static_cast<std::underlying_type_t<Unreal::EInternalObjectFlags>>(Unreal::EInternalObjectFlags::RootSet));
807: object_internal_flags_table.add_pair("AllFlags", static_cast<std::underlying_type_t<Unreal::EInternalObjectFlags>>(Unreal::EInternalObjectFlags::AllFlags));
808: object_internal_flags_table.make_global("EInternalObjectFlags");
814: efindname_table.add_pair("FNAME_Find", static_cast<std::underlying_type_t<Unreal::EFindName>>(Unreal::EFindName::FNAME_Find));
815: efindname_table.add_pair("FNAME_Add", static_cast<std::underlying_type_t<Unreal::EFindName>>(Unreal::EFindName::FNAME_Add));
818: efindname_table.make_global("EFindName");
849: property_type_table.add_pair("Size", static_cast<int64_t>(sizeof(typename PropertyType::TCppType)));
856: property_type_table.add_pair("Size", 0);
862: property_type_table.add_pair("FFieldClassPointer", static_cast<int64_t>(PropertyType::StaticClass().HashObject()));
864: property_type_table.add_pair("StaticPointer", 0);
1075: property_types_table.make_global("PropertyTypes");
5220: mod_class.make_global("UE4SS");
5291: unreal_version_class.make_global("UnrealVersion");
5393: package_name.make_global("FPackageName");
```

### `crates/ue4ssl-lua/native/include/LuaType/LuaUObject.hpp` 数据发布点

```cpp
201: // table.make_global("RemoteObjectBase");
746: // table.make_global("UObject");
869: // table.make_global("LocalUnrealParam");
```

### 共享参数/返回转换（被多个 callable 委托调用）

- Lua Map `Find(key)`、`Contains(key)`、`Remove(key)`、`Add(key,value)`、`Empty()`；Set `Add(value)`、`Contains(value)`、`Remove(value)`、`Empty()`：self 保存远端容器与 FProperty；值类型由 key/value/element property pusher 决定，不是统一 Lua number/string。稀疏物理槽不能当连续逻辑索引。ForEach 回调借用元素值，迭代期间的结构修改没有安全迭代承诺。
- Lua DataTable `FindRow`/`AddRow`/`RemoveRow` 等委托 `LuaUDataTable.cpp` 中 prepare/operation helper；row 的实际 UScriptStruct 决定参数/返回 table 字段，FindRow 不存在与类型错误不能归为同一返回。
- Lua UObject `__call`/UFunction：实际参数数由 `GetNumParms` 和 return/out flags 决定，转换由 `LuaUObject.cpp:113–395` 及 property pusher 完成。缺 calling context/失效对象/参数 buffer offset 不合法进入 Lua error；in/out/return 使用实际游戏 UFunction 的反射签名。保存远端参数到 callback 返回后不延长引擎 buffer 寿命。
- JS Get/SetProperty、Get/SetPropertyPath、CallFunction/Ex、Delegate 和 UObject wrapper 使用 `JSPropertyAccess.cpp`、`JSPropertyUtils.cpp` 的 property conversion。scalar/string/struct/array/set/map 的返回 schema 从 FProperty 推导；读不到对象与转换错误以调用函数的 null/throw 分支为准，不补另一套脚本类型系统。
- JS Response `text()`/`json()`、body.getReader().read() 的 Promise state 来自 fetch result/stream queue；read 返回 `{value,done}`；TextDecoder decode 输入类型校验见对应条目。HTTP 非2xx不等于网络错误，JSON parse error 不等于 fetch 本身失败。异步 download result字段见 `JSAudio.cpp:539–627`；普通失败仍resolve，circuit-breaker分支可reject。
- Lua `print`/LoadExport 的 helper 定义见 LuaLibrary.cpp；LoadExport 涉及外部 C 导出函数/DefaultDataStruct，不允许把 C++引用ABI视为稳定 Rust ABI。外部 execute_lua_in_mod 没有输出容量参数，结果/错误分类须按该文件导出状态枚举处理。

下面直接列出被调用的共享操作 helper 的参数/结果/错误语句，防止仅从一行注册 wrapper 推断返回值。

#### `crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp:36` / `global_print`

```cpp
49: int32_t stack_size = lua.get_stack_size();
55: const char* raw_string = luaL_tolstring(lua.get_lua_state(), i, nullptr);
110: return 0;
```

#### `crates/ue4ssl-lua/native/cpp/LuaLibrary.cpp:136` / `load_export`

```cpp
138: if (lua.get_stack_size() != 1 || !lua.is_string())
139: {
141: lua.set_nil();
142: return 1;
145: const auto symbol_name = std::string{lua.get_string()};
147: lua.set_integer(std::bit_cast<intptr_t>(Unreal::UnrealInitializer::LoadExport(symbol_name)));
148: return 1;
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTArray.cpp:232` / `prepare_to_handle`

```cpp
232: auto TArray::prepare_to_handle(const LuaMadeSimple::Type::Operation operation, const LuaMadeSimple::Lua& lua) -> void
233: {
234: auto& lua_object = lua.get_userdata<TArray>();
235: int64_t array_index64 = lua.get_integer() - 1; // Subtracting 1 here to account for that fact that Lua tables are 1-indexed
236: if (array_index64 < 0 || array_index64 > std::numeric_limits<int32_t>::max())
237: {
238: lua.throw_error("TArray index out of range.");
251: handle_unreal_property_value(operation, lua, lua_object.get_remote_cpp_object(), array_index, lua_object);
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTMap.cpp:187` / `prepare_to_handle`

```cpp
187: auto TMap::prepare_to_handle(const MapOperation operation, const LuaMadeSimple::Lua& lua) -> void
188: {
189: TMap& lua_object = lua.get_userdata<TMap>();
192: info.validate_pushers(lua);
214: return info.key->GetValueTypeHash(src);
217: return info.key->Identical(a, b);
221: lua.throw_error("Map key not found.");
277: return info.key->GetValueTypeHash(src);
280: return info.key->Identical(a, b);
313: return info.key->GetValueTypeHash(src);
316: return info.key->Identical(a, b);
319: lua.set_bool(index != Unreal::INDEX_NONE);
337: return info.key->GetValueTypeHash(src);
340: return info.key->Identical(a, b);
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaTSet.cpp:119` / `prepare_to_handle`

```cpp
119: auto TSet::prepare_to_handle(const SetOperation operation, const LuaMadeSimple::Lua& lua) -> void
120: {
121: TSet& lua_object = lua.get_userdata<TSet>();
124: info.validate_pushers(lua);
168: return info.element->GetValueTypeHash(src);
171: return info.element->Identical(a, b);
198: lua.set_bool(index != Unreal::INDEX_NONE);
240: lua_pushvalue(lua.get_lua_state(), 1);
253: // Call function passing element, expecting 1 return value
254: lua.call_function(1, 1);
257: if (lua.is_bool(2) && lua.get_bool(2))
258: {
263: // There's a 'nil' on the stack because we told Lua that we expect a return value.
264: // Lua will put 'nil' on the stack if the Lua function doesn't explicitly return anything.
265: // We discard the 'nil' here, otherwise the Lua stack is corrupted on the next iteration of the 'ForEach' loop.
266: // We explicitly specify index 2 because we duplicated the function earlier and that's located at index 1.
267: lua.discard_value(2);
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUDataTable.cpp:233` / `prepare_to_handle`

```cpp
233: auto UDataTable::prepare_to_handle(const DataTableOperation operation, const LuaMadeSimple::Lua& lua) -> void
234: {
235: UDataTable& lua_object = lua.get_userdata<UDataTable>();
240: lua.throw_error("DataTable is null");
248: info.validate_row_struct(lua);
251: Unreal::FName row_name(ensure_str(lua.get_string(1)).c_str(), Unreal::FNAME_Add);
256: lua.set_nil();
257: return;
268: UScriptStruct::construct(lua, row_wrapper);
272: info.validate_row_struct(lua);
275: if (!lua.is_string(1))
276: {
277: lua.throw_error("AddRow expects a string as the first parameter");
279: Unreal::FName row_name(ensure_str(lua.get_string(1)).c_str(), Unreal::FNAME_Add);
283: bool is_struct = lua.is_userdata(1);
284: bool is_table = lua.is_table(1);
288: lua.throw_error("AddRow expects a table or UScriptStruct as the second parameter");
296: auto& struct_wrapper = lua.get_userdata<UScriptStruct>();
323: lua_pushvalue(lua.get_lua_state(), 1); // Table is at position 1 now
324: StaticState::m_property_value_pushers[comparison_index](pusher_params);
345: Unreal::FName row_name(ensure_str(lua.get_string(1)).c_str(), Unreal::FNAME_Add);
356: auto lua_table = lua.prepare_new_table();
360: lua.set_string(to_string(row_names[i].ToString()));
367: info.validate_row_struct(lua);
371: auto lua_table = lua.prepare_new_table();
379: auto row_table = lua.prepare_new_table();
383: lua.set_string(to_string(Pair.Key.ToString()));
396: UScriptStruct::construct(lua, row_wrapper);
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:2483` / `prepare_to_handle`

```cpp
2483: auto RemoteUnrealParam::prepare_to_handle(const Operation operation, const LuaMadeSimple::Lua& lua) -> void
2484: {
2485: auto& lua_object = lua.get_userdata<LuaType::RemoteUnrealParam>();
2503: lua.throw_error(std::format(
2504: "[RemoteUnrealParam::prepare_to_handle] Tried accessing unreal property without a registered handler. Property type '{}' not supported.",
2505: property_type_name));
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUObject.cpp:113` / `call_ufunction_from_lua`

```cpp
115: if (!lua.is_userdata())
116: {
117: lua.throw_error("[UFunction::setup_metamethods -> __call] Attempted to call a UFunction without UFunction userdata attached");
120: auto& lua_object = lua.get_userdata<LuaType::UFunction>();
136: if (lua.is_userdata())
137: {
138: auto& lua_object2 = lua.get_userdata<LuaType::UE4SSBaseObject>(1, true);
141: func = lua.get_userdata<LuaType::UFunction>(1).get_remote_cpp_object();
145: calling_context = lua.get_userdata<LuaType::UObject>(1).get_remote_cpp_object();
155: lua.throw_error("[UFunction::call_ufunction_from_lua] Tried calling function without both UFunction and calling context");
160: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] Tried calling '{}' on a stale or invalid UObject",
161: to_string(func->GetFullName())));
169: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] UFunction '{}' has invalid params size {}",
170: to_string(func->GetFullName()),
171: params_size));
186: uint8_t num_supplied_params = Helper::Integer::to<uint8_t>(lua.get_stack_size());
188: // When return_value_offset is 0xFFFF it means that there is no return value so num_ufunc_params is accurate
189: // Otherwise you must subtract 1 to account for the return value (stored in the same struct and counts as a param)
190: // The ternary makes sure that we never have a negative number of params
191: bool has_return_value = return_value_offset != 0xFFFF;
197: lua.throw_error(std::format("[UFunction::setup_metamethods -> __call] UFunction expected {} parameters, received {}",
198: num_expected_params,
199: num_supplied_params));
227: // If yes, then this parameter should be treated as the return value
228: // If no, then treat this as just another parameter
229: if (offset_internal == return_value_offset)
230: {
244: if (!lua.is_table())
245: {
246: lua.throw_error(
247: "Tried storing reference to a Lua table for an 'Out' parameter when calling a UFunction but no table was on the stack");
251: lua_pushvalue(lua.get_lua_state(), 1);
268: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] Parameter '{}' is outside params buffer for '{}'",
269: to_string(param_next->GetName()),
270: to_string(func->GetFullName())));
287: lua.throw_error(
288: std::format("Tried calling UFunction without a registered handler for parameter. Parameter '{}' of type '{}' not supported.",
289: parameter_name,
290: parameter_type_name));
298: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] ProcessEvent failed for '{}'", to_string(func->GetFullName())));
313: auto lua_table = lua.get_table();
328: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] Out parameter '{}' is outside params buffer for '{}'",
329: to_string(param->GetName()),
330: to_string(func->GetFullName())));
348: lua.throw_error(std::format("Tried calling UFunction without a registered handler 'Out' param. Type '{}' not supported.", param_type_name));
358: // If there's a return value, then forward it to the Lua script
359: if (return_value_property)
360: {
367: lua.throw_error(std::format("[UFunction::call_ufunction_from_lua] Return value is outside params buffer for '{}'",
368: to_string(func->GetFullName())));
380: return 1;
385: lua.throw_error(std::format("Tried calling UFunction without a registered handler for return value. Return value of type '{}' not supported.",
386: return_value_type_name));
389: return 0;
393: return 0;
```

#### `crates/ue4ssl-lua/native/cpp/LuaType/LuaUScriptStruct.cpp:199` / `prepare_to_handle`

```cpp
199: auto UScriptStruct::prepare_to_handle(const LuaMadeSimple::Type::Operation operation, const LuaMadeSimple::Lua& lua) -> void
200: {
201: auto& lua_object = lua.get_userdata<UScriptStruct>();
203: Unreal::FName property_name = Unreal::FName(ensure_str(lua.get_string()), Unreal::FNAME_Find);
208: // No property was found so lets return nil and let the Lua script handle this failure
209: lua.set_nil();
210: return;
213: handle_unreal_property_value(operation, lua, lua_object.get_local_cpp_object(), property_name);
```

## 外部 DLL 脚本 API（非脚本全局）

下列6个Lua导出和1个JS导出另计，不混入上面的VM callable数量。输入 `const char*` 为调用期借用、NUL结尾；除明确长度参数外不接受内嵌NUL作为完整文本。Lua声明含C++引用与union，`extern "C"`只约束符号名，不构成可供Rust自行复制的稳定结构ABI。

| API / 参数 | 返回、错误与副作用 | 来源 |
|---|---|---|
| `get_lua_state_by_mod_name(mod_name)` | Mod不存在→nullptr；否则外借main Lua state，不做installed/started筛选。不能由caller lua_close；重载即失效。没有统一异常捕获保证。 | LuaLibrary.cpp:192–200 |
| `execute_lua_in_mod(mod_name,script,output_buffer)` | Mod缺失/未installed/未started：错误字节memcpy至caller buffer并返回该指针；load/pcall失败进入Lua throw_error，catch std::runtime_error分支同样复制并返回buffer；成功→nullptr。没有capacity参数，也未显式追加NUL；不能声称任何大小buffer安全。 | LuaLibrary.cpp:202–234 |
| `set_script_variable_int32(mod_name,variable_name,new_value:int32,ReturnValue&)` | 未初始化→UE4SS_NOT_INITIALIZED；Mod未安装/启动→MOD_IS_NULLPTR；global值nil→VARIABLE_NOT_FOUND；否则写integer并SUCCESS；捕获runtime_error→UNKNOWN_ERROR。不能创建原本nil的global。 | LuaLibrary.cpp:236–287 |
| `set_script_variable_default_data(mod_name,variable_name,DefaultDataStruct&,ReturnValue&)` | 输入4个tagged union字段，tag为ConstCharPtr/Float；每字段先按union的as_string非零检查，再按tag写data1…data4；创建新global table。未初始化/Mod缺失/成功/runtime_error分类分别8/3/1/7。不能将null-bit-pattern的float字段解释为必定发布。 | LuaLibrary.cpp:289–397；LuaLibrary.hpp:64–78 |
| `call_script_function(mod_name,function_name,ReturnValue&,ScriptFuncReturnValue&)` | 调无参数Lua global，取1返回。缺初始化8、Mod3、function6；boolean false使状态4；integer>=1写true，其他integer false但可SUCCESS；nil/其他类型不覆盖调用方预置return_value。仅当入参status仍0才补SUCCESS；调用方须初始化两个out结构，不得重用旧状态后期待自动清零；runtime_error→7。 | LuaLibrary.cpp:399–480 |
| `is_ue4ss_initialized()` | 返回UnrealInitializer的bIsInitialized，不等于脚本Mod已加载或event loop已启动。 | LuaLibrary.cpp:482–485 |
| `eval_js_code(code,code_len,filename)` | code按明确字节长度借用，filename为NUL结尾；仅event-loop线程，从其他Mod on_update调用；引擎不可用、执行失败、捕获异常→false；成功→true。 | JS dllmain.cpp:223–244，JSMod.cpp:1490–1526 |

Lua外部状态枚举（LuaLibrary.hpp:41–52）：0 NO_ERROR_TO_EXPORT、1 SUCCESS、2 VARIABLE_NOT_FOUND、3 MOD_IS_NULLPTR、4 SCRIPT_FUNCTION_RETURNED_FALSE、5 UNABLE_TO_CALL_SCRIPT_FUNCTION、6 SCRIPT_FUNCTION_NOT_FOUND、7 UNKNOWN_ERROR、8 UE4SS_NOT_INITIALIZED。枚举有值不代表每个导出实际设置它；不能将buffer错误返回替换为这些状态码而不版本化。
