fn package_marker() {
    ue4ssl_lua_support::ue4ssl_lua_support_anchor();
}

#[used]
static FORCE_LINK_LUA_PACKAGE: fn() = package_marker;
