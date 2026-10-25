# JavaScript 系统、网络与文件 API

[返回 JavaScript API](README.md) · [对象与属性](objects.md) · [钩子与事件](hooks.md) · [API 总览](../README.md)

本文以当前 QuickJS 绑定实现为准，不等同浏览器 API。入口为已启用 JavaScript 引擎时每个 Mod 的 `js/main.js`：引擎扫描 Mod 根目录的**直接子目录**，执行其中的 `js/main.js` ES 模块；所有脚本共用同一 QuickJS 上下文。`getModsDirectory()` 返回实际 Mod 根目录，不假定进程工作目录等于此目录。项目的 59 个全局函数名称/函数 `length` 来自 [Rust `GLOBAL_API`](../../../scripts/ue4ssl-javascript/src/lib.rs)，对应实现由 [JSMod.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp) 按顺序注册；`length` 不代表必填参数个数。**另有 QuickJS 辅助全局**，见下文专节；`UE4SS` 对象和 `TextDecoder` 也单独设置，不要把上述函数数目当作全部可调用 API 的数目。以下签名用 TypeScript 仅描述接口；可执行示例均为 JavaScript。

## 日志、元信息与按键

```typescript
function print(...args: unknown[]): void;
const UE4SS: { version: string; configuration: string };
function RegisterKeyBind(key: string, callback: () => void,
    modifiers?: { ctrl?: boolean; shift?: boolean; alt?: boolean }): boolean;
function __withExecBudget<T>(budgetMs: number, callback: () => T): T;
```

| 接口 | 输入（入） | 返回（出）、错误与副作用 |
|---|---|---|
| `print` | 任意数量的值；逐个转换为字符串，空格拼接 | `undefined`；向 UE4SS 普通级别日志写一行，前缀 `[UE4SSL.JavaScript]`。不是浏览器 `console.log`；值的字符串转换可能调用其自定义方法。 |
| `UE4SS.version` | 无 | 当前注册时固定字符串 `"1.0.0"`，不是动态查询的游戏/引擎/构建版本。 |
| `UE4SS.configuration` | 无 | 构建宏 `UE4SS_CONFIGURATION` 的字符串；未定义宏时为 `"Unknown"`。 |
| `RegisterKeyBind` | `key`：键名；`callback`：无参函数；`modifiers`：可选对象，`ctrl`、`shift`、`alt` **只有布尔值**才生效，缺省均为 `false` | 成功为 `true`；底层注册异常抛异常，无 JS 注销句柄。参数不足、未知键名或非函数回调抛 `TypeError`。注册由引擎持有，停止 JS 引擎时移除。 |
| `__withExecBudget` | `budgetMs`：可转换为数字的毫秒数；`callback`：无参函数 | 同步返回回调原值（回调抛错则错误继续传出；受保护调用失败抛内部错误）。正数设置本次同步调用的 QuickJS 中断截止时间；`<= 0` 暂时关闭中断；调用后恢复进入前的预算，包括嵌套调用。**不会**给返回的 Promise 后续 continuation 设置同一预算。缺参数或无效回调抛 `TypeError`。 |

`key` 支持单个英文字母（大小写均可）、数字 `0`–`9`、`F1`–`F12`、`ESCAPE`/`ESC`、`SPACE`、`ENTER`/`RETURN`、`TAB`、`BACKSPACE`、`DELETE`/`DEL`、`INSERT`/`INS`、`HOME`、`END`、`PAGEUP`、`PAGEDOWN`、`UP`、`DOWN`、`LEFT`、`RIGHT`、`NUMLOCK`、`CAPSLOCK`、`SCROLLLOCK`、`PAUSE`、`PRINTSCREEN` 和 `NUM0`–`NUM9`；字母及数字键以外的命名键不区分大小写。输入线程只将按键事件入队，JS 回调在随后的 JS tick 执行，不是在输入线程或保证为游戏线程的上下文执行；积压的事件可延迟，执行中相同绑定的重入会跳过。本接口无单独解绑方法；注册时避免反复重复绑定。见 [JSGlobalFunctions.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSGlobalFunctions.cpp)、[JSHook.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSHook.cpp)。

```javascript
print("JS API", UE4SS.version, UE4SS.configuration);
const installed = RegisterKeyBind("F6", () => {
    __withExecBudget(20, () => print("Ctrl+Shift+F6"));
}, { ctrl: true, shift: true });
print("按键绑定:", installed);
```

## QuickJS 辅助全局（不属于 59 个项目函数）

`JSMod::init_context()` 先调用 `js_std_add_helpers(ctx, 0, nullptr)`，再调用项目的 `setup_global_functions()`。因此 [vendored `quickjs-libc.c`](../../../scripts/ue4ssl-javascript-support/vendor/quickjs/quickjs-libc.c) 额外留下 `console` 与 `scriptArgs`；辅助函数最初设置的全局 `print` **随后被项目同名函数覆盖**，以本页上面的 UE4SS 日志语义为准。

```typescript
const console: { log(...args: unknown[]): void };
const scriptArgs: string[];
```

| 全局 | 实际行为 |
|---|---|
| `console.log` | QuickJS 自带 `js_print`；将所有入参转为字符串，用空格连接并换行，返回 `undefined`。对象字符串转换失败时尝试对象的字符串表示，仍失败则输出 `<exception>`。写到进程 **stdout**（Windows 控制台用 UTF-8 `WriteConsoleA`，否则 `fwrite`/`fflush`），**不等于**项目 `print` 的 `[UE4SSL.JavaScript]` 日志通道；没有由此助手注册的 `console.warn`、`console.error` 等浏览器方法。 |
| `scriptArgs` | `js_std_add_helpers` 在 `argc >= 0` 时设置的数组；这里调用时 `argc=0`、`argv=nullptr`，所以初始为 `[]`，不是 Mod/游戏启动参数。它是普通 JS 数组，可以被脚本更改；不应视为动态配置来源。 |

```javascript
console.log("stdout 消息", scriptArgs.length);
print("UE4SS 日志消息");
```

**没有**由这个 helper 注入的 `__loadScript` 全局：在当前 vendor 实现及 JS 宿主绑定中找不到该名称。`quickjs-libc.c` 内部确有 `js_loadScript`，但它注册在 QuickJS 的 `std` 模块函数表中，不是 `js_std_add_helpers` 注册的全局；当前 JS 宿主未调用 `js_init_module_std`，其模块加载器只查找磁盘 `.js` 文件。脚本入口与模块导入走宿主的 ES 模块加载器，而不是 `__loadScript`/全局 `loadScript`。见 [JSMod.cpp 的初始化/注册顺序](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)、[JSPropertyUtils.cpp 的模块加载器](../../../scripts/ue4ssl-javascript/native/cpp/JSPropertyUtils.cpp) 与 [quickjs-libc.c 的 helper 和 `std` 函数表](../../../scripts/ue4ssl-javascript-support/vendor/quickjs/quickjs-libc.c)。

## 定时器与 Promise 调度

```typescript
function setTimeout(callback: () => void, delayMs: number): number;
function setInterval(callback: () => void, intervalMs: number): number;
function clearTimeout(id?: number): void;
function clearInterval(id?: number): void;
```

- 两种注册都要求函数和可转换为数字的毫秒数，少参/非函数/数字转换失败抛 `TypeError`；计时器子系统熔断时抛内部错误。`setTimeout` 负数归零；`setInterval` 小于 10 毫秒归为 10 毫秒。返回该运行时内自增的 32 位 ID，不是 Promise；回调没有实参，返回值不使用。间隔回调以**处理当前 tick 的时刻**重新设置下次到期时间，不补发错过的轮次。异常会记录为定时器子系统失败；熔断会取消全部计时器。
- `clearTimeout` 与 `clearInterval` 完全相同：提供 ID 时转换为 `int32` 并标记取消；不提供、转换失败或 ID 不存在时返回 `undefined`，不报告是否取消成功。停止引擎时清理所有计时器。不要期待额外传给 `setTimeout` 的参数被转发给回调。
- `tick` 顺序为按键/钩子等事件 → **到期定时器** → fetch/download 结果 → QuickJS 的 Promise 待执行任务。回调里 `Promise.resolve().then(...)`、`fetch(...).then(...)` 等不会在注册语句中同步执行；异步 I/O 结果必须等 JS tick 搬运，Promise continuation 随后按每次 tick 最多 **256 个任务/6 毫秒**的预算处理，积压可延至后续 tick。定时器也不是墙钟时间保证，不应 busy-wait 等待异步结果；同步阻塞会阻止当前 tick 推进。JS tick 有中断预算，不能把它当成游戏线程。实现：[JSTimer.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSTimer.cpp)、[JSMod.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSMod.cpp)、[JSMod.hpp](../../../scripts/ue4ssl-javascript/native/include/JSMod.hpp)。

```javascript
const timeoutId = setTimeout(() => print("一次性到期"), 100);
const intervalId = setInterval(() => print("周期 tick"), 1000);
setTimeout(() => clearInterval(intervalId), 5000);
// 不再需要一次性任务时可调用 clearTimeout(timeoutId)。
```

## HTTP：`fetch` / `fetchSync`

```typescript
type FetchOptions = {
    method?: string;
    headers?: Record<string, string>;
    body?: string;
};
type FetchResponse = {
    ok: boolean;
    status: number;
    statusText: string;
    body: string | FetchBody;
    text(): Promise<string>;
    json(): Promise<unknown>;
};
type FetchBody = { getReader(): FetchReader };
type FetchReader = { read(): Promise<{ done: boolean; value: Uint8Array | undefined }> };
function fetch(url: string, options?: FetchOptions): Promise<FetchResponse>;
function fetchSync(url: string, options?: FetchOptions): { ok: boolean; status: number; body: string };
```

| 入参 | 实际处理 |
|---|---|
| `url` | 必填；QuickJS 转字符串交给 WinHTTP。URL 编码/解析/连接失败：`fetch` 拒绝 Promise（`Error.message`）；`fetchSync` 同步抛内部错误。实现未做 URL/协议预校验。 |
| `options.method` | 默认 `GET`；转换为字符串后大写。仅 `POST`、`PUT`、`PATCH` 会发送非空 `body`，其他方法即使提供 `body` 也不会发送。 |
| `options.headers` | 对象自身可枚举的**字符串键**按 `键: 值\r\n` 拼为 WinHTTP 请求头；值用字符串转换。不是浏览器 `Headers` 对象/完整请求选项。 |
| `options.body` | 非 `null`/`undefined` 时转换为字符串，不支持原生请求流或二进制上传选项。未识别的选项没有实现。 |

`fetch` 在独立工作线程执行请求，在后续 JS tick resolve/reject；`fetchSync` 在调用线程阻塞直至 WinHTTP 完成，**不**会在阻塞期间帮你推进 Promise 任务。两者使用连接/发送/接收超时 5/10/10/30 秒（WinHTTP 设置）；HTTPS 请求显式忽略未知 CA、证书主机名不匹配和证书日期错误，**不要把它当作具有正常 TLS 证书校验的安全传输**。`fetch` 和 `fetchSync` 使用 WinHTTP URL path，但未拼接 URL 的 query/fragment；如需带查询参数，不应假设请求仍保持原 URL。HTTP 非 2xx **不是网络异常**：返回对象的 `ok=false`，`status` 为 HTTP 状态码；读取正文遇到错误的非流分支可能返回已有正文，没有专门报错字段。

非流 `fetch` 的 `body` 为字符串；`statusText` **仅** `200 → "OK"`、`404 → "Not Found"`、`>=500 → "Server Error"`，其他为空字符串；`text()` 立即以当前 `body` 创建已兑现 Promise；`json()` 在调用 `json()` 时同步执行 `JSON.parse(body)`，非法 JSON 会**同步抛出**，解析成功后返回已兑现 Promise。`fetchSync` 的对象仅有 `ok`、`status`、`body`，没有 `statusText`、`text()`、`json()` 或流。实现用以 NUL 结尾的字符串构建非流响应，含 `\0` 的二进制正文可能被截断；不能把这里的 `body` 当作浏览器的原始字节流。

**有限流支持：** `fetch` 仅在序列化后的请求头中同时出现 `Accept:` 和 `text/event-stream`（大小写不敏感）时启用流模式，**不是**由响应 `Content-Type` 自动决定。响应头到达时 Promise 即兑现，`body` 为仅带 `getReader()` 的对象；`read()` 返回 Promise，数据时 `{done:false,value:Uint8Array}`，结束时 `{done:true,value:undefined}`，读错误时拒绝 `Error`。排队的块可能被合并，不保留 SSE 行或消息边界；需要自行解码/拼帧。多个 reader 没有浏览器式独占锁，亦无 `cancel()`/`releaseLock()`。流响应的 `text()`、`json()` **只读取调用当下已累计的字节快照**，不会等待流结束；`json()` 对不完整 JSON 可同步抛错。流及待处理回调由 JS 运行时持有，停止时清理，不要保留跨运行时的 reader。熔断会阻止新 `fetch`/`fetchSync`，并可能使已排队的异步结果不再交付。实现：[JSFetch.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSFetch.cpp)。

```javascript
fetch("https://example.com/", { headers: { Accept: "text/plain" } })
    .then(async response => {
        if (!response.ok) { print("HTTP", response.status); return; }
        print((await response.text()).slice(0, 80));
    })
    .catch(error => print("网络失败:", error.message));

const syncResponse = fetchSync("https://example.com/");
if (syncResponse.ok) print(syncResponse.body.slice(0, 80));
```

```javascript
fetch("https://example.com/events", { headers: { Accept: "text/event-stream" } })
    .then(async response => {
        if (!response.ok) { print("HTTP", response.status); return; }
        const reader = response.body.getReader();
        const decoder = new TextDecoder("utf-8");
        for (;;) {
            const item = await reader.read();
            if (item.done) break;
            print(decoder.decode(item.value)); // 块可能切断多字节字符或一条事件。
        }
    })
    .catch(error => print("流错误:", error.message));
```

## `TextDecoder` 的实际子集

```typescript
function TextDecoder(encoding?: string, options?: { fatal?: boolean; ignoreBOM?: boolean }): {
    encoding: "utf-8";
    fatal: false;
    ignoreBOM: false;
    decode(input?: ArrayBuffer | ArrayBufferView | unknown): string;
};
```

构造函数允许 `new TextDecoder()` 或直接 `TextDecoder()`；**忽略所有传入参数**（包括 `encoding`、`fatal`、`ignoreBOM`）。属性始终初始化为上述值，不表示选项生效。`decode()` 无入参或 `null`/`undefined` 返回空字符串；TypedArray/DataView 从其底层 buffer 的视图偏移与字节长度取字节，ArrayBuffer 取全体字节，其他值尝试 JS 字符串转换。以 `JS_NewStringLen` 创建 JS 文本，预期字节为 UTF-8；没有真正的编码切换、流式解码状态、`{stream:true}`、`fatal` 拒绝模式或 BOM 处理。按网络块逐个 `decode` 不能确保跨块的 UTF-8 字符合并；不能将此子集当浏览器 `TextDecoder`。实现：[JSFetch.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSFetch.cpp)。

```javascript
const decoder = new TextDecoder();
print(decoder.encoding, decoder.decode(new Uint8Array([72, 105]))); // utf-8 Hi
```

## 下载与声音

```typescript
type DownloadOptions = { timeoutMs?: number; headers?: Record<string, string> };
type DownloadResult = {
    ok: boolean; status: number; bytesWritten: number; path: string; error: string;
};
function downloadFile(url: string, path: string, options?: DownloadOptions): Promise<DownloadResult>;
function downloadFileSync(url: string, path: string, options?: DownloadOptions): DownloadResult;
function playSoundFile(path: string, options?: {
    async?: boolean; loop?: boolean; noStop?: boolean;
}): boolean;
function stopSound(): boolean;
```

| 接口 | 输入（入） | 输出/错误、副作用 |
|---|---|---|
| `downloadFile` / `downloadFileSync` | 必填非空 `url`、`path`（可转成非空 UTF-8 字符串）；`timeoutMs` 转为正 `int32` 才覆盖默认 **30000 毫秒**；`headers` 是对象的自有字符串键到字符串值 | 仅执行 **GET**。异步版本由下载线程执行并在后续 JS tick 兑现；同步版阻塞调用线程。普通网络、HTTP、文件失败不抛错/不拒绝，均返回 `{ok:false,status,bytesWritten,path,error}`；缺少 URL/路径抛 `TypeError`。异步版本在 Fetch 子系统熔断时禁止调用，已排队结果处理时熔断则可拒绝 `Error`；同步版无该熔断入口检查。 |
| `playSoundFile` | 必填非空路径；`async` 默认 `true`、`loop` 默认 `false`、`noStop` 默认 `false`，选项非 `null` 时按 JS 真值解释；`loop:true` 强制 `async:true` | Win32 `PlaySoundW` 用文件名播放；返回系统调用是否成功的布尔值，不返回播放句柄。`async:false` 同步阻塞；`noStop:true` 指示播放中不要打断现有声音。缺失/空路径或非法 UTF-8 抛 `TypeError`。 |
| `stopSound` | 无 | `PlaySoundW(nullptr, nullptr, 0)` 停止当前声音，返回系统调用成功与否；无单独声音实例 ID。 |

下载结果 `status` 是 HTTP 状态（请求未获状态码时为 0），`bytesWritten` 是写入临时文件的字节数（**失败时也可能大于零**），`path` 是原输入路径字符串，`error` 成功时为空、失败时给出具体错误/`HTTP n`。路径的父目录自动创建；先写入同目录的 `path.download`，仅在 2xx 且正文**非空**时替换目标文件，失败尽量清理临时文件。最终替换流程先移除旧目标再重命名/复制，失败时**不能保证旧目标仍存在**；不要把它当作无损原子写。HTTPS 同样忽略 CA、主机名及日期证书错误；异步下载结果依赖 JS tick，`downloadFileSync` 不会驱动待执行任务。实现：[JSAudio.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSAudio.cpp)。

```javascript
const target = getModsDirectory() + "/MyMod/assets/notice.wav";
downloadFile("https://example.com/notice.wav", target, { timeoutMs: 15000 })
    .then(result => {
        if (!result.ok) { print("下载失败:", result.status, result.error); return; }
        print("写入字节:", result.bytesWritten);
        if (playSoundFile(result.path, { async: true, noStop: true })) {
            setTimeout(() => stopSound(), 2000);
        }
    })
    .catch(error => print("下载回调失败:", error.message));
```

## 本地文件与目录

```typescript
function readFile(path: string): string | null;
function writeFile(path: string, content: string): boolean;
function getModsDirectory(): string;
function getGameDirectory(): string;
```

- `readFile` 必须提供路径；路径经 JS 字符串转换后交给 C++ 文件系统，以二进制方式读取全文件，再以 QuickJS 字符串返回；文件不存在、无法打开或文件系统异常返回 `null`，参数缺失/转换失败抛 `TypeError`。**不是** ArrayBuffer API；任意二进制内容不能保证无损往返 JS 字符串。`writeFile` 必须提供路径及内容，两者转换为字符串，内容用 UTF-8 字节长度写入，创建不存在的父目录，以二进制截断模式覆盖目标；打开或文件系统异常返回 `false`，成功完成写入调用后返回 `true`（实现未逐项检查写入/关闭的最终错误）。参数缺失/转换失败抛 `TypeError`。相对路径按进程当前工作目录解析，不自动相对于 Mod 目录，也没有隔离沙箱。
- `getModsDirectory()` 无入参，返回引擎解析的 Mod 根目录 UTF-8 字符串；`getGameDirectory()` 无入参，从**游戏可执行文件目录上移两级**所得路径返回字符串；都将 `\` 替换为 `/`。取路径异常抛内部错误，`getModsDirectory()` 还要求运行中的 JSMod。路径结果可用于显式拼接文件路径，不应假设等于当前工作目录。实现：[JSFileIO.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSFileIO.cpp)。

```javascript
const configPath = getModsDirectory() + "/MyMod/config.json";
if (writeFile(configPath, JSON.stringify({ enabled: true }))) {
    const text = readFile(configPath);
    if (text !== null) print("配置:", JSON.parse(text).enabled);
}
print("游戏根目录:", getGameDirectory());
```

## 签名扫描与单字节内存访问

```typescript
function SigScan(signature: string): bigint | null;
function PatchByte(address: bigint, value: number): boolean;
function ReadByte(address: bigint): number;
```

| 接口 | 输入（入） | 输出/限制 |
|---|---|---|
| `SigScan` | 签名字符串，格式由底层 SinglePassScanner 解析 | **仅扫描游戏主 EXE**；返回首个匹配地址 `bigint`，未找到为 `null`；缺参/无法转字符串抛 `TypeError`，扫描异常抛内部错误。调用是同步扫描，可能耗时；会记录签名与地址日志。 |
| `ReadByte` | 可转为 64 位 BigInt 的地址，推荐直接传 `SigScan` 的结果 | 返回地址处无符号单字节 `number`（0–255）；缺参/转换失败抛 `TypeError`，访问违规抛内部错误。不会校验地址是否来自本次扫描，内存地址可能随游戏版本/进程而变。 |
| `PatchByte` | 可转为 64 位 BigInt 的地址，以及转为 `int32` 后在 0–255 的数字 | 成功写一个字节返回 `true`，**不返回原值**；切换目标页为可写可执行权限，写入后尝试恢复权限，打印新旧字节。安全模式禁用时抛内部错误；缺参/转换失败抛 `TypeError`、范围外抛 `RangeError`、页保护失败或访问违规抛内部错误。未实现撤销、并发同步或确保修改安全的校验。 |

只对自己确认的代码/数据地址写入；错误字节会损坏进程。此类操作不保证在游戏线程执行；`bigint` 不要先转换成 JS `number`（高地址丢精度）。实现：[JSMemory.cpp](../../../scripts/ue4ssl-javascript/native/cpp/JSMemory.cpp)。

```javascript
const address = SigScan("48 89 5C 24 ?? 57 48 83 EC");
if (address !== null) {
    const before = ReadByte(address);
    print("首字节:", before);
    // 仅在已验证目标指令及游戏版本后，才可调用 PatchByte(address, 新字节)。
}
```
