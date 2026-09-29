# JSScriptMod - JavaScript Scripting for UE4SS

JSScriptMod provides JavaScript scripting support for UE4SS using the QuickJS engine, similar to the built-in Lua scripting support.

## Features

- ES2023 JavaScript support via QuickJS engine
- UE4 object access through familiar API
- Module system with ES6 `import`/`export`
- Similar API to Lua scripting (FindFirstInstanceOfClass, FindAllInstancesOfClass, etc.)
- Memory scanning (SigScan) and binary patching (PatchByte/ReadByte)

## Installation

1. Build the JSScriptMod project as part of UE4SS
2. The `JSScriptMod.dll` will be created in the output directory
3. Create your JavaScript scripts in `Mods/JSScripts/js/`

## Directory Structure

```
Mods/
└── JSScripts/
    └── js/
        ├── main.js          # Entry point script (required)
        └── modules/         # Optional module directory
            └── utils.js
```

## API Reference

### Global Functions

#### `print(...args)`
Print values to the UE4SS console.

```javascript
print("Hello", "World", 123);
```

#### `FindFirstInstanceOfClass(className)`
Find the first live instance of a given class. `FindFirstOf` is a deprecated legacy alias.

```javascript
const engine = FindFirstInstanceOfClass("GameEngine");
if (engine) {
    print(engine.GetFullName());
}
```

#### `FindAllInstancesOfClass(className)`
Find all live instances of a given class. `FindAllOf` is a deprecated legacy alias.

```javascript
const players = FindAllInstancesOfClass("PlayerController");
for (const player of players) {
    print(player.GetName());
}
```

#### `StaticFindObject(path)`
Find an object by its full path.

```javascript
const obj = StaticFindObject("/Script/Engine.Default__GameEngine");
```

#### `RegisterHook(functionPath, preCallback, postCallback)`
Register a hook for a UFunction by its path. This is similar to C#'s `Hooking.HookUFunction`.

**Parameters:**
- `functionPath` - Full path to the UFunction (e.g., "/Game/MyMod.MyClass_C:MyFunction")
- `preCallback` - Function called before the original function (or `null` to skip)
- `postCallback` - (optional) Function called after the original function (or `null` to skip)

**Callback Parameters:**
- `thisObject` - The UObject that the function is being called on
- `params` - Array of raw pointers to function parameters (as BigInt)
- `returnValue` - Raw pointer to return value location (as BigInt)

**Returns:** Array `[preHookId, postHookId]`

```javascript
// Example: Hook a Blueprint function
var hookIds = RegisterHook(
    "/Game/ModIntegration/MI_SpawnMods.MI_SpawnMods_C:OnInitCave",
    function(thisObj, params, ret) {
        print("OnInitCave Pre-hook: function is about to execute");
    },
    function(thisObj, params, ret) {
        print("OnInitCave Post-hook: function finished executing");
    }
);

print("Hook registered with IDs:", hookIds[0], hookIds[1]);
```

#### `HookUFunction(ufunctionObject, preCallback, postCallback)`
Register a hook for a UFunction object directly. Use this when you already have a UFunction reference.

**Parameters:**
- `ufunctionObject` - A UFunction object (obtained via `StaticFindObject`)
- `preCallback` - Function called before the original function (or `null` to skip)
- `postCallback` - (optional) Function called after the original function (or `null` to skip)

**Returns:** Array `[preHookId, postHookId]`

```javascript
// Find the UFunction first, then hook it
var func = StaticFindObject("/Game/MyMod.MyClass_C:MyFunction");
if (func) {
    var hookIds = HookUFunction(func,
        function(thisObj, params, ret) {
            print("Pre-hook fired!");
        },
        function(thisObj, params, ret) {
            print("Post-hook fired!");
        }
    );
}
```

#### `UnregisterHook(preId, postId)`
Unregister a previously registered hook.

**Parameters:**
- `preId` - The pre-hook ID returned from RegisterHook/HookUFunction
- `postId` - The post-hook ID returned from RegisterHook/HookUFunction

**Returns:** Boolean indicating success

```javascript
// Later, unregister the hook
var success = UnregisterHook(hookIds[0], hookIds[1]);
print("Hook unregistered:", success);
```

#### `NotifyOnNewObject(className, callback)`
Get notified when a new object of the specified class is created.

```javascript
NotifyOnNewObject("PlayerController", (newObject) => {
    print("New PlayerController created:", newObject.GetName());
});
```

### Memory Scanning / Patching

#### `SigScan(signature)`
Scan the main executable for a byte pattern (AOB signature). Wildcards use `??`.

**Parameters:**
- `signature` - Space-separated hex byte pattern (e.g., `"48 8b c4 ?? 89"`)

**Returns:** BigInt address if found, `null` if not found

```javascript
var addr = SigScan("48 8b c4 48 89 48 08 55 57");
if (addr) {
    print("Found at 0x" + addr.toString(16));
}
```

#### `PatchByte(address, value)`
Write a single byte to a memory address. Automatically handles memory protection (VirtualProtect).

**Parameters:**
- `address` - BigInt address (from `SigScan`)
- `value` - Byte value to write (0-255)

**Returns:** `true` on success

```javascript
var addr = SigScan("48 8b c4 48 89 48 08");
if (addr) {
    PatchByte(addr, 0xC3);       // Patch first byte to RET
    PatchByte(addr + 1n, 0x90);  // Patch second byte to NOP
}
```

#### `ReadByte(address)`
Read a single byte from a memory address.

**Parameters:**
- `address` - BigInt address

**Returns:** Byte value (0-255)

```javascript
var addr = SigScan("48 8b c4");
if (addr) {
    var original = ReadByte(addr);
    print("Original byte: 0x" + original.toString(16));
}
```

### UObject Methods

When you get a UObject from `FindFirstInstanceOfClass`, `FindAllInstancesOfClass`, etc., you can use these methods:

- `GetFullName()` - Get the full name of the object
- `GetName()` - Get the short name of the object
- `GetClass()` - Get the UClass of the object
- `IsA(className)` - Check if object is instance of a class
- `GetAddress()` - Get the memory address (for debugging)
- `IsValid()` - Check if the object is still valid

### Global Objects

#### `UE4SS`
```javascript
print(UE4SS.version);  // "1.0.0"
```

## Module System

JSScriptMod supports ES6 modules. Create modules in your scripts directory:

**utils.js**
```javascript
export function helper() {
    return "Hello from helper";
}

export const VERSION = "1.0.0";
```

**main.js**
```javascript
import { helper, VERSION } from './utils.js';

print(helper());
print("Version:", VERSION);
```

## Building

The `ue4ssl-javascript` Cargo package lives under `scripts/` and builds the JavaScript plugin against the core SDK. Use the repository's Rust and MSVC toolchain requirements, then build from the repository root:

```powershell
cargo ue4ssl-build
```

This builds the core before linking the script engines. For cross-compilation, add `--target x86_64-pc-windows-msvc`.

The QuickJS source is provided by `scripts/ue4ssl-javascript-support/vendor/quickjs/`.

## Limitations

- Not all UE4 types are bound yet
- Property access on UObjects is limited
- Hook callback parameters are passed as raw pointers (BigInt) - type-safe param access coming soon

## Stability Validation

- Stress and release-gate checklist: keep this README's validation notes in sync with `scripts/ue4ssl-javascript/native/cpp`.

## License

QuickJS is licensed under MIT. See `scripts/ue4ssl-javascript-support/vendor/quickjs/LICENSE` for details.
