pub struct BuiltinModContract {
    pub mod_name: &'static str,
    pub dll_name: &'static str,
}

pub const BUILTIN_MOD_LOAD_ORDER: [BuiltinModContract; 3] = [
    BuiltinModContract {
        mod_name: "UE4SSL.JavaScript",
        dll_name: "UE4SSL.JavaScript.dll",
    },
    BuiltinModContract {
        mod_name: "UE4SSL.Lua",
        dll_name: "UE4SSL.Lua.dll",
    },
    BuiltinModContract {
        mod_name: "UE4SSL.DRG",
        dll_name: "UE4SSL.DRG.dll",
    },
];
