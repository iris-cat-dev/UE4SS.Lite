#[no_mangle]
pub extern "C" fn ue4ssl_native_support_anchor() {}

#[used]
static FORCE_LINK_NATIVE_SUPPORT: extern "C" fn() = ue4ssl_native_support_anchor;

#[no_mangle]
pub extern "C" fn ue4ssl_cpp_support_anchor() {}

#[used]
static FORCE_LINK_UE4SSL_CPP_SUPPORT: extern "C" fn() = ue4ssl_cpp_support_anchor;

pub use ue4ssl_abi::OwnedString as NativeOwnedString;

fn ffi_boundary<T: Default>(body: impl FnOnce() -> T) -> T {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(body)).unwrap_or_default()
}

mod file_io {
    use super::NativeOwnedString;
    use std::fs::{self, File, OpenOptions};
    use std::io::{self, Write};
    use std::path::{Path, PathBuf};
    use std::ptr;

    #[cfg(windows)]
    use std::ffi::OsString;
    #[cfg(windows)]
    use std::os::windows::ffi::OsStringExt;

    pub unsafe fn path_from_utf16_z(path: *const u16) -> Option<PathBuf> {
        if path.is_null() {
            return None;
        }

        let mut len = 0;
        while ptr::read(path.add(len)) != 0 {
            len += 1;
        }

        #[cfg(windows)]
        {
            Some(OsString::from_wide(std::slice::from_raw_parts(path, len)).into())
        }

        #[cfg(not(windows))]
        {
            String::from_utf16(std::slice::from_raw_parts(path, len))
                .ok()
                .map(PathBuf::from)
        }
    }

    pub fn owned_string_from_utf16(data: Vec<u16>) -> NativeOwnedString {
        let data = data.into_boxed_slice();
        let len = data.len();
        NativeOwnedString {
            data: Box::into_raw(data).cast::<u16>(),
            len,
        }
    }

    pub unsafe fn free_owned_string(string: NativeOwnedString) {
        if !string.data.is_null() {
            drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                string.data,
                string.len,
            )));
        }
    }

    pub unsafe fn read_to_string(path: *const u16) -> NativeOwnedString {
        let Some(path) = path_from_utf16_z(path) else {
            return NativeOwnedString::default();
        };
        let Ok(bytes) = fs::read(path) else {
            return NativeOwnedString::default();
        };

        let decoded = decode_text(&bytes);
        owned_string_from_utf16(decoded.encode_utf16().collect())
    }

    pub fn open_append(path: &Path, truncate: bool) -> io::Result<File> {
        if let Some(parent) = path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
        {
            fs::create_dir_all(parent)?;
        }
        OpenOptions::new()
            .create(true)
            .write(true)
            .append(!truncate)
            .truncate(truncate)
            .open(path)
    }

    pub fn write_utf16(file: &mut File, data: &[u16]) -> io::Result<()> {
        file.write_all(String::from_utf16_lossy(data).as_bytes())
    }

    pub unsafe fn prepare_append(path: *const u16, truncate_existing: u8) -> bool {
        path_from_utf16_z(path)
            .is_some_and(|path| open_append(&path, truncate_existing != 0).is_ok())
    }

    pub unsafe fn append_utf16(path: *const u16, data: *const u16, len: usize) -> bool {
        let Some(path) = path_from_utf16_z(path) else {
            return false;
        };
        if data.is_null() && len != 0 {
            return false;
        }
        let data = if len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(data, len)
        };
        open_append(&path, false)
            .and_then(|mut file| write_utf16(&mut file, data))
            .is_ok()
    }

    fn decode_text(bytes: &[u8]) -> String {
        if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
            return String::from_utf8_lossy(&bytes[3..]).into_owned();
        }

        if bytes.starts_with(&[0xFF, 0xFE]) {
            let units = bytes[2..]
                .chunks_exact(2)
                .map(|chunk| u16::from_le_bytes([chunk[0], chunk[1]]))
                .collect::<Vec<_>>();
            return String::from_utf16_lossy(&units);
        }

        if bytes.starts_with(&[0xFE, 0xFF]) {
            let units = bytes[2..]
                .chunks_exact(2)
                .map(|chunk| u16::from_be_bytes([chunk[0], chunk[1]]))
                .collect::<Vec<_>>();
            return String::from_utf16_lossy(&units);
        }

        String::from_utf8_lossy(bytes).into_owned()
    }
}

mod input {
    use std::cell::{Cell, RefCell};
    use std::collections::BTreeMap;
    use std::ffi::c_void;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::{Arc, Condvar, LazyLock, Mutex, MutexGuard};

    pub type Callback = unsafe extern "C" fn(*mut c_void);
    thread_local! {
        static OWNER: Cell<usize> = const { Cell::new(0) };
        static ACTIVE: RefCell<Vec<Arc<Event>>> = const { RefCell::new(Vec::new()) };
    }

    pub fn enter(owner: usize) -> usize {
        OWNER.with(|value| value.replace(owner))
    }
    pub fn leave(owner: usize) {
        OWNER.with(|value| value.set(owner));
    }
    pub fn current_owner() -> usize {
        OWNER.with(Cell::get)
    }
    fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
        mutex
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    struct Invocation {
        cancelled: bool,
        running: bool,
        finalizing: bool,
        data: Option<usize>,
    }

    struct Event {
        key: u8,
        modifiers: u8,
        owner: usize,
        kind: u8,
        callback: Callback,
        release: Option<Callback>,
        invocation: Mutex<Invocation>,
        idle: Condvar,
    }

    impl Event {
        fn cancel(&self) {
            lock(&self.invocation).cancelled = true;
        }

        fn drain(self: &Arc<Self>) {
            let is_self =
                ACTIVE.with(|active| active.borrow().iter().any(|entry| Arc::ptr_eq(entry, self)));
            let mut invocation = lock(&self.invocation);
            while (invocation.running || invocation.finalizing) && !is_self {
                invocation = self
                    .idle
                    .wait(invocation)
                    .unwrap_or_else(|error| error.into_inner());
            }
            let data = if invocation.running || invocation.finalizing {
                None
            } else {
                invocation.data.take()
            };
            invocation.finalizing |= data.is_some();
            drop(invocation);
            if let Some(data) = data {
                ACTIVE.with(|active| active.borrow_mut().push(self.clone()));
                let previous = enter(self.owner);
                if let Some(release) = self.release {
                    unsafe {
                        release(data as *mut c_void);
                    }
                }
                leave(previous);
                ACTIVE.with(|active| {
                    active.borrow_mut().pop();
                });
                lock(&self.invocation).finalizing = false;
                self.idle.notify_all();
            }
        }

        fn invoke(self: &Arc<Self>) {
            let data = {
                let mut invocation = lock(&self.invocation);
                if invocation.cancelled || invocation.running {
                    return;
                }
                invocation.running = true;
                invocation.data.expect("active input callback context")
            };
            ACTIVE.with(|active| active.borrow_mut().push(self.clone()));
            let previous = enter(self.owner);
            unsafe {
                (self.callback)(data as *mut c_void);
            }
            let mut invocation = lock(&self.invocation);
            let data = if invocation.cancelled {
                invocation.data.take()
            } else {
                None
            };
            if data.is_none() {
                invocation.running = false;
            }
            drop(invocation);
            if let Some(data) = data {
                if let Some(release) = self.release {
                    unsafe {
                        release(data as *mut c_void);
                    }
                }
                lock(&self.invocation).running = false;
            }
            leave(previous);
            ACTIVE.with(|active| {
                active.borrow_mut().pop();
            });
            self.idle.notify_all();
        }
    }

    impl Drop for Event {
        fn drop(&mut self) {
            let data = lock(&self.invocation).data.take();
            if let (Some(release), Some(data)) = (self.release, data) {
                unsafe {
                    release(data as *mut c_void);
                }
            }
        }
    }

    struct State {
        classes: Vec<Vec<u16>>,
        events: BTreeMap<u64, Arc<Event>>,
        keys_down: [bool; 256],
        allow_input: bool,
        closed: bool,
        next: u64,
    }

    pub struct Handler {
        state: Mutex<State>,
        processing: AtomicBool,
    }

    impl Handler {
        fn new() -> Self {
            Self {
                state: Mutex::new(State {
                    classes: Vec::new(),
                    events: BTreeMap::new(),
                    keys_down: [false; 256],
                    allow_input: true,
                    closed: false,
                    next: 1,
                }),
                processing: AtomicBool::new(false),
            }
        }

        pub unsafe fn add_window_class(&self, name: *const u16) {
            let Some(name) = read_utf16_z(name) else {
                return;
            };
            let mut state = lock(&self.state);
            if !state.closed && !state.classes.contains(&name) {
                state.classes.push(name);
            }
        }

        pub unsafe fn register(
            &self,
            key: u8,
            modifiers: *const u8,
            count: usize,
            owner: usize,
            kind: u8,
            callback: Option<Callback>,
            data: *mut c_void,
            release: Option<Callback>,
        ) -> u64 {
            let Some(callback) = callback else {
                return 0;
            };
            let Some(modifiers) = modifier_mask(modifiers, count) else {
                return 0;
            };
            let owner = if owner == 0 {
                OWNER.with(Cell::get)
            } else {
                owner
            };
            // A cancelled callback cannot resurrect its own registrations while unwinding its work.
            if ACTIVE.with(|active| {
                active
                    .borrow()
                    .iter()
                    .any(|event| event.owner == owner && lock(&event.invocation).cancelled)
            }) {
                return 0;
            }
            let mut state = lock(&self.state);
            if state.closed || state.next == u64::MAX {
                return 0;
            }
            let handle = state.next;
            state.next += 1;
            state.events.insert(
                handle,
                Arc::new(Event {
                    key,
                    modifiers,
                    owner,
                    kind,
                    callback,
                    release,
                    invocation: Mutex::new(Invocation {
                        cancelled: false,
                        running: false,
                        finalizing: false,
                        data: Some(data as usize),
                    }),
                    idle: Condvar::new(),
                }),
            );
            handle
        }

        fn remove(&self, predicate: impl Fn(u64, &Event) -> bool, close: bool) -> usize {
            let removed = {
                let mut state = lock(&self.state);
                state.closed |= close;
                let removed = state
                    .events
                    .iter()
                    .filter_map(|(handle, event)| predicate(*handle, event).then(|| event.clone()))
                    .collect::<Vec<_>>();
                // Invalidate every pending entry before waiting for any in-flight callback.
                for event in &removed {
                    event.cancel();
                }
                removed
            };
            for event in &removed {
                event.drain();
            }
            self.reap_cancelled();
            removed.len()
        }

        fn reap_cancelled(&self) {
            lock(&self.state).events.retain(|_, event| {
                let invocation = lock(&event.invocation);
                !invocation.cancelled
                    || invocation.running
                    || invocation.finalizing
                    || invocation.data.is_some()
            });
        }

        pub fn remove_handle(&self, handle: u64) -> bool {
            self.remove(|id, _| id == handle, false) != 0
        }
        pub fn remove_owner(&self, owner: usize) -> usize {
            self.remove(|_, event| event.owner == owner, false)
        }
        pub fn remove_kind(&self, kind: u8) -> usize {
            self.remove(|_, event| event.kind == kind, false)
        }
        pub fn close(&self) {
            self.remove(|_, _| true, true);
        }
        pub fn allow(&self) -> bool {
            lock(&self.state).allow_input
        }
        pub fn set_allow(&self, allow: bool) {
            lock(&self.state).allow_input = allow;
        }

        pub unsafe fn contains(&self, key: u8, modifiers: *const u8, count: usize) -> bool {
            let Some(modifiers) = modifier_mask(modifiers, count) else {
                return false;
            };
            lock(&self.state).events.values().any(|event| {
                event.key == key
                    && event.modifiers == modifiers
                    && !lock(&event.invocation).cancelled
            })
        }

        fn collect(&self, keys: &[bool; 256], focused: bool) -> Vec<Arc<Event>> {
            let mut state = lock(&self.state);
            let modifiers =
                u8::from(keys[0x10]) | (u8::from(keys[0x11]) << 1) | (u8::from(keys[0x12]) << 2);
            let pending = if focused && state.allow_input && !state.closed {
                state
                    .events
                    .values()
                    .filter(|event| {
                        keys[event.key as usize]
                            && !state.keys_down[event.key as usize]
                            && event.modifiers == modifiers
                    })
                    .cloned()
                    .collect()
            } else {
                Vec::new()
            };
            // Track held keys while unfocused/disabled, preventing a phantom press on focus regain.
            state.keys_down = *keys;
            pending
        }

        pub fn process(&self) {
            if self
                .processing
                .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
            {
                return;
            }
            struct Processing<'a>(&'a AtomicBool);
            impl Drop for Processing<'_> {
                fn drop(&mut self) {
                    self.0.store(false, Ordering::Release);
                }
            }
            let _processing = Processing(&self.processing);
            let (focused, keys) = {
                let state = lock(&self.state);
                let mut keys = [false; 256];
                let mut sampled = [false; 256];
                for key in [0x10, 0x11, 0x12]
                    .into_iter()
                    .chain(state.events.values().map(|event| event.key))
                {
                    let index = key as usize;
                    if !sampled[index] {
                        keys[index] = is_key_down(key as i32);
                        sampled[index] = true;
                    }
                }
                (
                    state
                        .classes
                        .iter()
                        .any(|name| foreground_class_matches(name.as_ptr())),
                    keys,
                )
            };
            for event in self.collect(&keys, focused) {
                if !self.allow()
                    || !lock(&self.state)
                        .classes
                        .iter()
                        .any(|name| foreground_class_matches(name.as_ptr()))
                {
                    break;
                }
                event.invoke();
            }
            self.reap_cancelled();
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::sync::atomic::AtomicUsize;

        struct Context {
            handler: usize,
            owner: usize,
            cancel: bool,
            calls: Arc<AtomicUsize>,
            releases: Arc<AtomicUsize>,
            releases_inside: Arc<AtomicUsize>,
        }
        unsafe extern "C" fn call(data: *mut c_void) {
            let context = &*data.cast::<Context>();
            context.calls.fetch_add(1, Ordering::SeqCst);
            if context.cancel {
                get(context.handler as *mut c_void)
                    .unwrap()
                    .remove_owner(context.owner);
                context
                    .releases_inside
                    .store(context.releases.load(Ordering::SeqCst), Ordering::SeqCst);
            }
        }
        unsafe extern "C" fn release(data: *mut c_void) {
            let context = Box::from_raw(data.cast::<Context>());
            context.releases.fetch_add(1, Ordering::SeqCst);
        }
        unsafe extern "C" fn count(data: *mut c_void) {
            (&*data.cast::<AtomicUsize>()).fetch_add(1, Ordering::SeqCst);
        }
        fn dispatch(handler: &Handler, keys: &[bool; 256], focused: bool) {
            for event in handler.collect(keys, focused) {
                event.invoke();
            }
        }

        #[test]
        fn self_unregister_cancels_pending_and_defers_own_context_release() {
            let id = new_handler();
            let handler = get(id).unwrap();
            let calls = Arc::new(AtomicUsize::new(0));
            let releases = Arc::new(AtomicUsize::new(0));
            let releases_inside = Arc::new(AtomicUsize::new(0));
            for cancel in [true, false] {
                let data = Box::into_raw(Box::new(Context {
                    handler: id as usize,
                    owner: 41,
                    cancel,
                    calls: calls.clone(),
                    releases: releases.clone(),
                    releases_inside: releases_inside.clone(),
                }));
                unsafe {
                    handler.register(
                        65,
                        std::ptr::null(),
                        0,
                        41,
                        1,
                        Some(call),
                        data.cast(),
                        Some(release),
                    );
                }
            }
            let mut keys = [false; 256];
            keys[65] = true;
            for event in handler.collect(&keys, true) {
                event.invoke();
            }
            assert_eq!(calls.load(Ordering::SeqCst), 1);
            assert_eq!(
                releases_inside.load(Ordering::SeqCst),
                1,
                "only the pending context can be freed inside the first callback"
            );
            assert_eq!(releases.load(Ordering::SeqCst), 2);
            assert!(!unsafe { handler.contains(65, std::ptr::null(), 0) });
            destroy(id);
            assert_eq!(releases.load(Ordering::SeqCst), 2);
        }

        #[test]
        fn chord_matching_duplicates_and_focus_edges_use_one_registry() {
            let handler = Handler::new();
            let modifiers = [0x11, 0x10, 0];
            let calls = AtomicUsize::new(0);
            let data = (&calls as *const AtomicUsize).cast_mut().cast();
            let first = unsafe {
                handler.register(65, modifiers.as_ptr(), 3, 1, 0, Some(count), data, None)
            };
            let second = unsafe {
                handler.register(65, modifiers.as_ptr(), 3, 2, 0, Some(count), data, None)
            };
            assert_ne!(first, second);
            assert!(unsafe { handler.contains(65, [0x10, 0x11].as_ptr(), 2) });
            assert!(!unsafe { handler.contains(65, [0x11].as_ptr(), 1) });
            let mut keys = [false; 256];
            keys[65] = true;
            keys[0x10] = true;
            keys[0x11] = true;
            dispatch(&handler, &keys, false);
            dispatch(&handler, &keys, true);
            assert_eq!(
                calls.load(Ordering::SeqCst),
                0,
                "focus regain does not fabricate a key edge"
            );
            keys[65] = false;
            dispatch(&handler, &keys, true);
            keys[65] = true;
            dispatch(&handler, &keys, true);
            assert_eq!(calls.load(Ordering::SeqCst), 2);
            dispatch(&handler, &keys, true);
            assert_eq!(calls.load(Ordering::SeqCst), 2, "held key does not repeat");
            assert!(handler.remove_handle(first));
            assert!(unsafe { handler.contains(65, modifiers.as_ptr(), 3) });
            handler.remove_owner(2);
            assert!(!unsafe { handler.contains(65, modifiers.as_ptr(), 3) });
            handler.close();
        }
    }

    static HANDLERS: LazyLock<Mutex<BTreeMap<usize, Arc<Handler>>>> =
        LazyLock::new(|| Mutex::new(BTreeMap::new()));
    static NEXT_HANDLER: AtomicUsize = AtomicUsize::new(1);
    fn handlers() -> &'static Mutex<BTreeMap<usize, Arc<Handler>>> {
        &HANDLERS
    }
    pub fn new_handler() -> *mut c_void {
        let Ok(id) =
            NEXT_HANDLER.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
        else {
            return std::ptr::null_mut();
        };
        lock(handlers()).insert(id, Arc::new(Handler::new()));
        id as *mut c_void
    }
    pub fn get(handler: *mut c_void) -> Option<Arc<Handler>> {
        lock(handlers()).get(&(handler as usize)).cloned()
    }
    pub fn destroy(handler: *mut c_void) {
        let handler = lock(handlers()).remove(&(handler as usize));
        if let Some(handler) = handler {
            handler.close();
        }
    }

    unsafe fn modifier_mask(modifiers: *const u8, count: usize) -> Option<u8> {
        if count > 3 || (count != 0 && modifiers.is_null()) {
            return None;
        }
        let mut mask = 0;
        for index in 0..count {
            mask |= match *modifiers.add(index) {
                0 => 0,
                0x10 => 1,
                0x11 => 2,
                0x12 => 4,
                _ => return None,
            };
        }
        Some(mask)
    }

    unsafe fn read_utf16_z(value: *const u16) -> Option<Vec<u16>> {
        if value.is_null() {
            return None;
        }
        let mut len = 0;
        while len < 260 && *value.add(len) != 0 {
            len += 1;
        }
        if len == 260 {
            return None;
        }
        let mut value = std::slice::from_raw_parts(value, len).to_vec();
        value.push(0);
        Some(value)
    }

    #[cfg(windows)]
    #[link(name = "user32")]
    extern "system" {
        fn GetAsyncKeyState(key: i32) -> i16;
        fn GetForegroundWindow() -> *mut c_void;
        fn GetClassNameW(window: *mut c_void, name: *mut u16, count: i32) -> i32;
    }
    pub fn is_key_down(key: i32) -> bool {
        #[cfg(windows)]
        {
            unsafe { GetAsyncKeyState(key) as u16 & 0x8000 != 0 }
        }
        #[cfg(not(windows))]
        {
            let _ = key;
            false
        }
    }
    pub fn foreground_class_matches(expected: *const u16) -> bool {
        #[cfg(windows)]
        {
            if expected.is_null() {
                return false;
            }
            let mut expected_len = 0;
            while expected_len < 260 && unsafe { *expected.add(expected_len) } != 0 {
                expected_len += 1;
            }
            if expected_len == 260 {
                return false;
            }
            let mut name = [0_u16; 260];
            let window = unsafe { GetForegroundWindow() };
            if window.is_null() {
                return false;
            }
            let len = unsafe { GetClassNameW(window, name.as_mut_ptr(), name.len() as i32) };
            len > 0
                && len as usize == expected_len
                && name[..len as usize]
                    == unsafe { std::slice::from_raw_parts(expected, expected_len) }[..]
        }
        #[cfg(not(windows))]
        {
            let _ = expected;
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_file_read_to_string(path: *const u16) -> NativeOwnedString {
    ffi_boundary(|| unsafe { file_io::read_to_string(path) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_file_free_string(string: NativeOwnedString) {
    ffi_boundary(|| unsafe {
        file_io::free_owned_string(string);
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_file_prepare_append(path: *const u16, truncate: u8) -> u8 {
    ffi_boundary(|| unsafe { u8::from(file_io::prepare_append(path, truncate)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_file_append_utf16(
    path: *const u16,
    data: *const u16,
    len: usize,
) -> u8 {
    ffi_boundary(|| unsafe { u8::from(file_io::append_utf16(path, data, len)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_is_key_down(key: i32) -> u8 {
    ffi_boundary(|| u8::from(input::is_key_down(key)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_foreground_class_matches(name: *const u16) -> u8 {
    ffi_boundary(|| u8::from(input::foreground_class_matches(name)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_new() -> *mut std::ffi::c_void {
    ffi_boundary(|| input::new_handler())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_destroy(handler: *mut std::ffi::c_void) {
    ffi_boundary(|| {
        input::destroy(handler);
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_add_window_class(
    handler: *mut std::ffi::c_void,
    name: *const u16,
) {
    ffi_boundary(|| {
        if let Some(handler) = input::get(handler) {
            unsafe {
                handler.add_window_class(name);
            }
        }
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_register_keydown_event_v2(
    handler: *mut std::ffi::c_void,
    key: u8,
    modifiers: *const u8,
    count: usize,
    owner: usize,
    kind: u8,
    callback: Option<input::Callback>,
    data: *mut std::ffi::c_void,
    release: Option<input::Callback>,
) -> u64 {
    ffi_boundary(|| {
        input::get(handler).map_or(0, |handler| unsafe {
            handler.register(key, modifiers, count, owner, kind, callback, data, release)
        })
    })
}
// v1 borrows callback data; every in-tree caller transfers ownership through v2.
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_register_keydown_event(
    handler: *mut std::ffi::c_void,
    key: u8,
    modifiers: *const u8,
    count: usize,
    callback: Option<input::Callback>,
    data: *mut std::ffi::c_void,
) {
    ffi_boundary(|| {
        ue4ssl_native_input_handler_register_keydown_event_v2(
            handler, key, modifiers, count, 0, 0, callback, data, None,
        );
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_unregister_event(
    handler: *mut std::ffi::c_void,
    handle: u64,
) -> u8 {
    ffi_boundary(|| {
        input::get(handler).map_or(0, |handler| u8::from(handler.remove_handle(handle)))
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_unregister_owner(
    handler: *mut std::ffi::c_void,
    owner: usize,
) -> usize {
    ffi_boundary(|| input::get(handler).map_or(0, |handler| handler.remove_owner(owner)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_unregister_kind(
    handler: *mut std::ffi::c_void,
    kind: u8,
) -> usize {
    ffi_boundary(|| input::get(handler).map_or(0, |handler| handler.remove_kind(kind)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_is_keydown_event_registered(
    handler: *mut std::ffi::c_void,
    key: u8,
    modifiers: *const u8,
    count: usize,
) -> u8 {
    ffi_boundary(|| {
        input::get(handler).map_or(0, |handler| unsafe {
            u8::from(handler.contains(key, modifiers, count))
        })
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_process_event(handler: *mut std::ffi::c_void) {
    ffi_boundary(|| {
        if let Some(handler) = input::get(handler) {
            handler.process();
        }
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_get_allow_input(
    handler: *mut std::ffi::c_void,
) -> u8 {
    ffi_boundary(|| input::get(handler).map_or(0, |handler| u8::from(handler.allow())))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_handler_set_allow_input(
    handler: *mut std::ffi::c_void,
    allow: u8,
) {
    ffi_boundary(|| {
        if let Some(handler) = input::get(handler) {
            handler.set_allow(allow != 0);
        }
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_owner_enter(owner: usize) -> usize {
    ffi_boundary(|| input::enter(owner))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_input_owner_leave(owner: usize) {
    ffi_boundary(|| {
        input::leave(owner);
    })
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_input_current_owner() -> usize {
    ffi_boundary(|| input::current_owner())
}

mod logging {
    use super::file_io;
    use std::cell::RefCell;
    use std::collections::BTreeMap;
    use std::ffi::c_void;
    use std::fs::File;
    use std::io::Write;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicI32, AtomicU64, Ordering};
    use std::sync::{Arc, Condvar, LazyLock, Mutex, MutexGuard};

    pub type Receive = unsafe extern "C" fn(*mut c_void, *const u16, usize, i32) -> u8;
    pub type Release = unsafe extern "C" fn(*mut c_void);
    pub type Find = unsafe extern "C" fn(*mut c_void, *mut c_void) -> *mut c_void;
    thread_local! { static ACTIVE: RefCell<Vec<usize>> = const { RefCell::new(Vec::new()) }; }
    fn lock<T>(value: &Mutex<T>) -> MutexGuard<'_, T> {
        value.lock().unwrap_or_else(|error| error.into_inner())
    }

    struct Lifetime {
        data: Option<usize>,
        running: usize,
        closed: bool,
        finalizing: bool,
    }
    struct Device {
        receive: Receive,
        release: Release,
        state: Mutex<Lifetime>,
        idle: Condvar,
    }
    impl Device {
        fn acquire(&self) -> Option<usize> {
            let mut state = lock(&self.state);
            if state.closed {
                return None;
            }
            state.running += 1;
            ACTIVE.with(|active| active.borrow_mut().push(self as *const Self as usize));
            state.data
        }
        fn release_context(&self, data: usize) {
            ACTIVE.with(|active| active.borrow_mut().push(self as *const Self as usize));
            unsafe {
                (self.release)(data as *mut c_void);
            }
            ACTIVE.with(|active| {
                active.borrow_mut().pop();
            });
            lock(&self.state).finalizing = false;
            self.idle.notify_all();
        }
        fn finish(&self) {
            ACTIVE.with(|active| {
                active.borrow_mut().pop();
            });
            let mut state = lock(&self.state);
            state.running -= 1;
            let data = if state.closed && state.running == 0 {
                state.data.take()
            } else {
                None
            };
            state.finalizing |= data.is_some();
            drop(state);
            if let Some(data) = data {
                self.release_context(data);
            }
            self.idle.notify_all();
        }
        fn cancel(&self) {
            lock(&self.state).closed = true;
        }
        fn drain(&self) {
            if ACTIVE.with(|active| active.borrow().contains(&(self as *const Self as usize))) {
                return;
            }
            let mut state = lock(&self.state);
            while state.running != 0 || state.finalizing {
                state = self
                    .idle
                    .wait(state)
                    .unwrap_or_else(|error| error.into_inner());
            }
            let data = state.data.take();
            state.finalizing = data.is_some();
            drop(state);
            if let Some(data) = data {
                self.release_context(data);
            }
        }
    }
    impl Drop for Device {
        fn drop(&mut self) {
            let data = lock(&self.state).data.take();
            if let Some(data) = data {
                unsafe {
                    (self.release)(data as *mut c_void);
                }
            }
        }
    }
    #[derive(Default)]
    struct Group {
        devices: Vec<Arc<Device>>,
        closed: bool,
    }
    static GROUPS: LazyLock<Mutex<BTreeMap<u64, Arc<Mutex<Group>>>>> =
        LazyLock::new(|| Mutex::new(BTreeMap::new()));
    static NEXT_GROUP: AtomicU64 = AtomicU64::new(1);
    static DEFAULT: LazyLock<u64> = LazyLock::new(group_new);
    static LEVEL: AtomicI32 = AtomicI32::new(1);
    fn groups() -> &'static Mutex<BTreeMap<u64, Arc<Mutex<Group>>>> {
        &GROUPS
    }
    fn group(id: u64) -> Option<Arc<Mutex<Group>>> {
        lock(groups()).get(&id).cloned()
    }
    fn next_id(next: &AtomicU64) -> Option<u64> {
        next.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .ok()
    }
    pub fn group_new() -> u64 {
        let Some(id) = next_id(&NEXT_GROUP) else {
            return 0;
        };
        lock(groups()).insert(id, Arc::new(Mutex::new(Group::default())));
        id
    }
    pub fn default_group() -> u64 {
        *DEFAULT
    }
    pub fn level() -> i32 {
        LEVEL.load(Ordering::Relaxed)
    }
    pub fn set_level(level: i32) {
        LEVEL.store(level, Ordering::Relaxed);
    }
    pub fn clear(id: u64, destroy: bool) -> bool {
        let group = if destroy {
            lock(groups()).remove(&id)
        } else {
            group(id)
        };
        let Some(group) = group else {
            return false;
        };
        let devices = {
            let mut group = lock(&group);
            group.closed |= destroy;
            let devices = group.devices.clone();
            for device in &devices {
                device.cancel();
            }
            devices
        };
        for device in &devices {
            device.drain();
        }
        reap(&group);
        true
    }
    fn reap(group: &Mutex<Group>) {
        lock(group).devices.retain(|device| {
            let state = lock(&device.state);
            !state.closed || state.running != 0 || state.finalizing || state.data.is_some()
        });
    }
    pub fn add(
        id: u64,
        data: *mut c_void,
        receive: Option<Receive>,
        release: Option<Release>,
    ) -> bool {
        let (Some(receive), Some(release), Some(group)) = (receive, release, group(id)) else {
            return false;
        };
        let mut group = lock(&group);
        if group.closed {
            return false;
        }
        group.devices.push(Arc::new(Device {
            receive,
            release,
            state: Mutex::new(Lifetime {
                data: Some(data as usize),
                running: 0,
                closed: false,
                finalizing: false,
            }),
            idle: Condvar::new(),
        }));
        true
    }
    pub unsafe fn send(id: u64, text: *const u16, len: usize, level: i32) -> bool {
        if len != 0 && text.is_null() {
            return false;
        }
        let Some(group) = group(id) else {
            return false;
        };
        let devices = lock(&group).devices.clone();
        let mut dispatched = false;
        let mut success = true;
        for device in devices {
            if let Some(data) = device.acquire() {
                dispatched = true;
                let result = (device.receive)(data as *mut c_void, text, len, level);
                device.finish();
                if result == 0 {
                    success = false;
                    break;
                }
            }
        }
        reap(&group);
        success && dispatched
    }
    pub unsafe fn find(id: u64, context: *mut c_void, find: Option<Find>) -> *mut c_void {
        let (Some(group), Some(find)) = (group(id), find) else {
            return std::ptr::null_mut();
        };
        let devices = lock(&group).devices.clone();
        for device in devices {
            if let Some(data) = device.acquire() {
                let result = find(data as *mut c_void, context);
                device.finish();
                if !result.is_null() {
                    return result;
                }
            }
        }
        std::ptr::null_mut()
    }

    enum Sink {
        File {
            path: Option<PathBuf>,
            truncate: bool,
            file: Option<File>,
        },
        Console {
            mode: Option<(usize, u32)>,
        },
    }
    static SINKS: LazyLock<Mutex<BTreeMap<u64, Sink>>> =
        LazyLock::new(|| Mutex::new(BTreeMap::new()));
    static NEXT_SINK: AtomicU64 = AtomicU64::new(1);
    fn sinks() -> &'static Mutex<BTreeMap<u64, Sink>> {
        &SINKS
    }
    pub fn file_new() -> u64 {
        let Some(id) = next_id(&NEXT_SINK) else {
            return 0;
        };
        lock(sinks()).insert(
            id,
            Sink::File {
                path: None,
                truncate: false,
                file: None,
            },
        );
        id
    }
    pub fn console_new() -> u64 {
        let Some(id) = next_id(&NEXT_SINK) else {
            return 0;
        };
        lock(sinks()).insert(id, Sink::Console { mode: None });
        id
    }
    pub unsafe fn set_path(id: u64, path: *const u16, truncate: bool) -> bool {
        let Some(path) = file_io::path_from_utf16_z(path) else {
            return false;
        };
        let mut sinks = lock(sinks());
        let Some(Sink::File {
            path: old_path,
            truncate: old_truncate,
            file,
        }) = sinks.get_mut(&id)
        else {
            return false;
        };
        if let Some(file) = file {
            if file.flush().is_err() {
                return false;
            }
        }
        *file = None;
        *old_path = Some(path);
        *old_truncate = truncate;
        true
    }
    pub unsafe fn file_write(id: u64, data: *const u16, len: usize) -> bool {
        if len != 0 && data.is_null() {
            return false;
        }
        let mut sinks = lock(sinks());
        let Some(Sink::File {
            path,
            truncate,
            file,
        }) = sinks.get_mut(&id)
        else {
            return false;
        };
        if file.is_none() {
            let Some(path) = path else {
                return false;
            };
            let Ok(opened) = file_io::open_append(path, *truncate) else {
                return false;
            };
            *file = Some(opened);
        }
        let data = if len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(data, len)
        };
        file_io::write_utf16(file.as_mut().expect("opened log file"), data).is_ok()
    }
    pub fn close(id: u64) -> bool {
        let Some(mut sink) = lock(sinks()).remove(&id) else {
            return false;
        };
        match &mut sink {
            Sink::File { file, .. } => file.as_mut().is_none_or(|file| file.flush().is_ok()),
            Sink::Console { mode } => restore_console(mode.take()),
        }
    }
    pub unsafe fn console_write(id: u64, data: *const u16, len: usize, level: i32) -> bool {
        if len != 0 && data.is_null() {
            return false;
        }
        let mut sinks = lock(sinks());
        let Some(Sink::Console { mode }) = sinks.get_mut(&id) else {
            return false;
        };
        let data = if len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(data, len)
        };
        write_console(data, level, mode)
    }
    fn color(level: i32) -> &'static str {
        match level {
            2 => "\x1b[1;36m",
            3 => "\x1b[1;33m",
            4 => "\x1b[1;31m",
            5 => "\x1b[1;32m",
            6 => "\x1b[1;94m",
            7 => "\x1b[1;35m",
            _ => "\x1b[0;0m",
        }
    }
    #[cfg(windows)]
    #[link(name = "kernel32")]
    extern "system" {
        fn GetStdHandle(kind: u32) -> *mut c_void;
        fn GetConsoleMode(handle: *mut c_void, mode: *mut u32) -> i32;
        fn SetConsoleMode(handle: *mut c_void, mode: u32) -> i32;
        fn WriteConsoleW(
            handle: *mut c_void,
            text: *const u16,
            len: u32,
            written: *mut u32,
            reserved: *mut c_void,
        ) -> i32;
    }
    #[cfg(windows)]
    static CONSOLE_MODES: LazyLock<Mutex<BTreeMap<usize, (u32, usize)>>> =
        LazyLock::new(|| Mutex::new(BTreeMap::new()));
    fn restore_console(mode: Option<(usize, u32)>) -> bool {
        #[cfg(windows)]
        {
            let Some((handle, _)) = mode else {
                return true;
            };
            let mut modes = lock(&CONSOLE_MODES);
            let Some((original, users)) = modes.get_mut(&handle) else {
                return true;
            };
            *users -= 1;
            if *users != 0 {
                return true;
            }
            let original = *original;
            modes.remove(&handle);
            unsafe { SetConsoleMode(handle as *mut c_void, original) != 0 }
        }
        #[cfg(not(windows))]
        {
            let _ = mode;
            true
        }
    }
    fn write_console(data: &[u16], level: i32, original: &mut Option<(usize, u32)>) -> bool {
        #[cfg(windows)]
        {
            let handle = unsafe { GetStdHandle((-11_i32) as u32) };
            if handle.is_null() || handle as isize == -1 {
                return false;
            }
            let mut mode = 0;
            if unsafe { GetConsoleMode(handle, &mut mode) } != 0 {
                if original.is_some_and(|(previous, _)| previous != handle as usize)
                    && !restore_console(original.take())
                {
                    return false;
                }
                if original.is_none() {
                    let mut modes = lock(&CONSOLE_MODES);
                    if unsafe { SetConsoleMode(handle, mode | 4) } == 0 {
                        return false;
                    }
                    let shared = modes.entry(handle as usize).or_insert((mode, 0));
                    shared.1 += 1;
                    *original = Some((handle as usize, shared.0));
                }
                fn write(handle: *mut c_void, mut data: &[u16]) -> bool {
                    while !data.is_empty() {
                        let count = data.len().min(32768) as u32;
                        let mut written = 0;
                        if unsafe {
                            WriteConsoleW(
                                handle,
                                data.as_ptr(),
                                count,
                                &mut written,
                                std::ptr::null_mut(),
                            )
                        } == 0
                            || written == 0
                        {
                            return false;
                        }
                        data = &data[written as usize..];
                    }
                    true
                }
                let mut prefix = [0_u16; 8];
                let color = color(level).as_bytes();
                for (target, byte) in prefix.iter_mut().zip(color) {
                    *target = *byte as u16;
                }
                return write(handle, &prefix[..color.len()])
                    && write(handle, data)
                    && write(handle, &[27, 91, 48, 109]);
            }
        }
        #[cfg(not(windows))]
        let _ = original;
        let mut stdout = std::io::stdout().lock();
        stdout
            .write_all(color(level).as_bytes())
            .and_then(|_| stdout.write_all(String::from_utf16_lossy(data).as_bytes()))
            .and_then(|_| stdout.write_all(b"\x1b[0m"))
            .and_then(|_| stdout.flush())
            .is_ok()
    }
}

#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_new() -> u64 {
    ffi_boundary(|| logging::group_new())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_default_group() -> u64 {
    ffi_boundary(|| logging::default_group())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_destroy(group: u64) {
    ffi_boundary(|| {
        logging::clear(group, true);
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_clear(group: u64) -> u8 {
    ffi_boundary(|| u8::from(logging::clear(group, false)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_add(
    group: u64,
    data: *mut std::ffi::c_void,
    receive: Option<logging::Receive>,
    release: Option<logging::Release>,
) -> u8 {
    ffi_boundary(|| u8::from(logging::add(group, data, receive, release)))
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_send(
    group: u64,
    data: *const u16,
    len: usize,
    level: i32,
) -> u8 {
    ffi_boundary(|| unsafe { u8::from(logging::send(group, data, len, level)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_group_find(
    group: u64,
    context: *mut std::ffi::c_void,
    find: Option<logging::Find>,
) -> *mut std::ffi::c_void {
    ffi_boundary(|| unsafe { logging::find(group, context, find) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_set_default_level(level: i32) {
    ffi_boundary(|| {
        logging::set_level(level);
    })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_get_default_level() -> i32 {
    ffi_boundary(|| logging::level())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_file_new() -> u64 {
    ffi_boundary(|| logging::file_new())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_console_new() -> u64 {
    ffi_boundary(|| logging::console_new())
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_file_set_path(sink: u64, path: *const u16, truncate: u8) -> u8 {
    ffi_boundary(|| unsafe { u8::from(logging::set_path(sink, path, truncate != 0)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_file_write(sink: u64, data: *const u16, len: usize) -> u8 {
    ffi_boundary(|| unsafe { u8::from(logging::file_write(sink, data, len)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_console_write(
    sink: u64,
    data: *const u16,
    len: usize,
    level: i32,
) -> u8 {
    ffi_boundary(|| unsafe { u8::from(logging::console_write(sink, data, len, level)) })
}
#[no_mangle]
pub extern "C" fn ue4ssl_native_log_sink_close(sink: u64) -> u8 {
    ffi_boundary(|| u8::from(logging::close(sink)))
}

#[cfg(test)]
mod logging_tests {
    use super::*;
    use std::ffi::c_void;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};

    struct Device {
        group: u64,
        close_on_receive: bool,
        received: Arc<Mutex<Vec<(String, i32)>>>,
        released: Arc<AtomicUsize>,
    }
    unsafe extern "C" fn receive(
        data: *mut c_void,
        text: *const u16,
        len: usize,
        level: i32,
    ) -> u8 {
        let device = &*data.cast::<Device>();
        let text = String::from_utf16_lossy(std::slice::from_raw_parts(text, len));
        device.received.lock().unwrap().push((text, level));
        if device.close_on_receive {
            ue4ssl_native_log_group_clear(device.group);
        }
        1
    }
    unsafe extern "C" fn release(data: *mut c_void) {
        let device = Box::from_raw(data.cast::<Device>());
        device.released.fetch_add(1, Ordering::SeqCst);
    }
    #[test]
    fn closing_from_device_callback_cancels_pending_output_without_double_release() {
        let group = ue4ssl_native_log_group_new();
        let received = Arc::new(Mutex::new(Vec::new()));
        let released = Arc::new(AtomicUsize::new(0));
        for close_on_receive in [true, false] {
            let data = Box::into_raw(Box::new(Device {
                group,
                close_on_receive,
                received: received.clone(),
                released: released.clone(),
            }));
            assert_eq!(
                ue4ssl_native_log_group_add(group, data.cast(), Some(receive), Some(release)),
                1
            );
        }
        let text = "hello".encode_utf16().collect::<Vec<_>>();
        assert_eq!(
            ue4ssl_native_log_group_send(group, text.as_ptr(), text.len(), 4),
            1
        );
        assert_eq!(*received.lock().unwrap(), vec![("hello".into(), 4)]);
        assert_eq!(released.load(Ordering::SeqCst), 2);
        assert_eq!(
            ue4ssl_native_log_group_send(group, text.as_ptr(), text.len(), 4),
            0
        );
        ue4ssl_native_log_group_destroy(group);
        assert_eq!(released.load(Ordering::SeqCst), 2);
    }

    #[test]
    fn file_devices_truncate_once_append_utf8_and_reject_closed_handles() {
        static NEXT: AtomicUsize = AtomicUsize::new(0);
        let directory = std::env::temp_dir().join(format!(
            "ue4ssl-support-log-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::SeqCst)
        ));
        std::fs::create_dir_all(&directory).unwrap();
        let path = directory.join("output.log");
        std::fs::write(&path, "old").unwrap();
        let path_units = path
            .to_string_lossy()
            .encode_utf16()
            .chain(Some(0))
            .collect::<Vec<_>>();
        let first = ue4ssl_native_log_file_new();
        assert_eq!(
            ue4ssl_native_log_file_set_path(first, path_units.as_ptr(), 1),
            1
        );
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "old");
        let message = "héllo\n".encode_utf16().collect::<Vec<_>>();
        for _ in 0..2 {
            assert_eq!(
                ue4ssl_native_log_file_write(first, message.as_ptr(), message.len()),
                1
            );
        }
        assert_eq!(ue4ssl_native_log_sink_close(first), 1);
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "héllo\nhéllo\n");
        assert_eq!(
            ue4ssl_native_log_file_write(first, message.as_ptr(), message.len()),
            0
        );
        let second = ue4ssl_native_log_file_new();
        assert_eq!(
            ue4ssl_native_log_file_set_path(second, path_units.as_ptr(), 0),
            1
        );
        assert_eq!(
            ue4ssl_native_log_file_write(second, message.as_ptr(), message.len()),
            1
        );
        assert_eq!(ue4ssl_native_log_sink_close(second), 1);
        assert_eq!(
            std::fs::read_to_string(&path).unwrap(),
            "héllo\nhéllo\nhéllo\n"
        );
        let invalid = ue4ssl_native_log_file_new();
        let directory_units = directory
            .to_string_lossy()
            .encode_utf16()
            .chain(Some(0))
            .collect::<Vec<_>>();
        assert_eq!(
            ue4ssl_native_log_file_set_path(invalid, directory_units.as_ptr(), 0),
            1
        );
        assert_eq!(
            ue4ssl_native_log_file_write(invalid, message.as_ptr(), message.len()),
            0
        );
        assert_eq!(ue4ssl_native_log_sink_close(invalid), 1);
        std::fs::remove_dir_all(directory).unwrap();
    }
}
