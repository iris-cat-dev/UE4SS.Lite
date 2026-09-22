use parking_lot::{Condvar, Mutex, MutexGuard};
use std::cell::Cell;
use std::collections::{HashSet, VecDeque};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, LazyLock};
use std::time::{Duration, Instant};

use super::{cpp_mod::CppModHandle, mods, wide};
use ue4ssl_abi::{CppModRuntimeStatus, ProgramFlags, RuntimeConfig, RuntimeEvent, SliceU16};

#[derive(Clone, Copy, PartialEq, Eq, Default)]
enum Phase {
    #[default]
    Stopped,
    Starting,
    Running,
    Reloading,
    Stopping,
}

struct ModView {
    id: u64,
    name: Vec<u16>,
    adapter: usize,
    status: CppModRuntimeStatus,
}

struct Event(RuntimeEvent);
// The ABI transfers the context to the actor; no other thread may use it after submission.
unsafe impl Send for Event {}
impl Drop for Event {
    fn drop(&mut self) {
        if let Some(release) = self.0.release.take() {
            unsafe { release(self.0.context) };
        }
    }
}

#[derive(Default)]
struct Completion {
    done: Mutex<bool>,
    wake: Condvar,
}
struct CompletionSignal(Arc<Completion>);
impl Drop for CompletionSignal {
    fn drop(&mut self) {
        *self.0.done.lock() = true;
        self.0.wake.notify_all();
    }
}

enum Request {
    Event(Event),
    DllLoad(u64, Vec<u16>),
    Action(u64, u32, bool),
    Create(u64, Vec<u16>, Vec<u16>),
    Complete(CompletionSignal),
}

#[derive(Default)]
struct State {
    phase: Phase,
    accepting: bool,
    started: bool,
    paused: bool,
    reload: bool,
    queue: VecDeque<Request>,
    mods: Vec<ModView>,
    retired_owners: HashSet<usize>,
}
struct Shared {
    state: Mutex<State>,
    wake: Condvar,
}
static SHARED: LazyLock<Shared> = LazyLock::new(|| Shared {
    state: Mutex::new(State::default()),
    wake: Condvar::new(),
});
fn shared() -> &'static Shared {
    &SHARED
}
fn lock() -> MutexGuard<'static, State> {
    shared().state.lock()
}
static STOP: AtomicBool = AtomicBool::new(false);
static NEXT_ID: AtomicU64 = AtomicU64::new(1);
thread_local! {
    static IS_ACTOR: Cell<bool> = const { Cell::new(false) };
    static OWNER: Cell<usize> = const { Cell::new(0) };
}
struct OwnerScope {
    previous: usize,
    input_previous: usize,
}
impl OwnerScope {
    fn new(owner: usize) -> Self {
        let previous = OWNER.with(|slot| slot.replace(owner));
        let input_previous = unsafe { ue4ssl_native_input_owner_enter(owner) };
        Self {
            previous,
            input_previous,
        }
    }
}
impl Drop for OwnerScope {
    fn drop(&mut self) {
        unsafe { ue4ssl_native_input_owner_leave(self.input_previous) };
        OWNER.with(|slot| slot.set(self.previous));
    }
}

pub fn get_program_flags() -> ProgramFlags {
    let state = lock();
    ProgramFlags {
        is_program_started: u8::from(state.started),
        processing_events: u8::from(state.phase == Phase::Running && !STOP.load(Ordering::Acquire)),
        pause_events_processing: u8::from(state.paused || state.phase == Phase::Reloading),
    }
}
// v1 setters now update the one authoritative state; stopping is always an actor request.
pub fn set_program_started(value: bool) {
    lock().started = value;
}
pub fn set_processing_state(processing: bool, paused: bool) {
    lock().paused = paused;
    if !processing {
        ue4ssl_runtime_request_shutdown();
    }
    shared().wake.notify_all();
}

fn submit(request: Request) -> bool {
    let mut state = lock();
    if !state.accepting || STOP.load(Ordering::Acquire) {
        drop(state);
        drop(request);
        return false;
    }
    state.queue.push_back(request);
    drop(state);
    shared().wake.notify_one();
    true
}

fn submit_legacy(request: Request) -> bool {
    // Callback threads must not wait for an actor that may be waiting for their
    // source to quiesce. Reentrant mutation, especially unload, is always deferred.
    if IS_ACTOR.with(Cell::get) || unsafe { ue4ssl_native_input_current_owner() } != 0 {
        return submit(request);
    }
    let completion = Arc::new(Completion::default());
    let mut state = lock();
    if !state.accepting || STOP.load(Ordering::Acquire) {
        drop(state);
        drop(request);
        return false;
    }
    state.queue.push_back(request);
    state
        .queue
        .push_back(Request::Complete(CompletionSignal(completion.clone())));
    drop(state);
    shared().wake.notify_one();
    let mut done = completion.done.lock();
    while !*done {
        completion.wake.wait(&mut done);
    }
    true
}

pub fn legacy_action(handle: *mut CppModHandle, action: u32, value: u8) {
    submit_legacy(Request::Action(handle as usize as u64, action, value != 0));
}
pub fn legacy_dll_load(handle: *mut CppModHandle, name: SliceU16) {
    submit_legacy(Request::DllLoad(handle as usize as u64, copy_slice(name)));
}

#[no_mangle]
pub extern "C" fn ue4ssl_runtime_current_owner() -> usize {
    let owner = OWNER.with(Cell::get);
    if owner != 0 {
        owner
    } else {
        unsafe { ue4ssl_native_input_current_owner() }
    }
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_request_shutdown() {
    // No allocation, mutex acquisition, unload or join: safe for the DLL detach notification.
    STOP.store(true, Ordering::Release);
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_shutdown() -> u8 {
    ue4ssl_runtime_request_shutdown();
    if IS_ACTOR.with(Cell::get) || unsafe { ue4ssl_native_input_current_owner() } != 0 {
        return 0;
    }
    let mut state = lock();
    while state.phase != Phase::Stopped {
        shared().wake.wait(&mut state);
    }
    1
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_reinstall() -> u8 {
    let mut state = lock();
    if !matches!(state.phase, Phase::Starting | Phase::Running)
        || state.reload
        || STOP.load(Ordering::Acquire)
    {
        return 0;
    }
    // Close ingress immediately; the actor commits reload only after the active callback returns.
    state.accepting = false;
    state.reload = true;
    drop(state);
    shared().wake.notify_one();
    1
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_queue_event(event: RuntimeEvent) -> u8 {
    let event = Event(event);
    let mut state = lock();
    if event.0.callback.is_none()
        || !state.accepting
        || STOP.load(Ordering::Acquire)
        || state.retired_owners.contains(&event.0.owner)
    {
        drop(state);
        drop(event);
        return 0;
    }
    state.queue.push_back(Request::Event(event));
    drop(state);
    shared().wake.notify_one();
    1
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_queue_empty() -> u8 {
    u8::from(lock().queue.is_empty())
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_cancel_owner(owner: usize) {
    let cancelled = {
        let mut state = lock();
        if !state.retired_owners.insert(owner) {
            return;
        }
        let mut cancelled = Vec::new();
        let queued = state.queue.len();
        for _ in 0..queued {
            let request = state
                .queue
                .pop_front()
                .expect("queue length remains stable while cancelling");
            if matches!(&request, Request::Event(event) if event.0.owner == owner) {
                cancelled.push(request);
            } else {
                state.queue.push_back(request);
            }
        }
        cancelled
    };
    drop(cancelled);
}
fn copy_slice(value: SliceU16) -> Vec<u16> {
    if value.data.is_null() || value.len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(value.data, value.len).to_vec() }
    }
}
fn slice(value: &[u16]) -> SliceU16 {
    SliceU16 {
        data: value.as_ptr(),
        len: value.len(),
    }
}
fn should_guard_updates(name: &[u16]) -> bool {
    let text = String::from_utf16_lossy(name);
    !text.starts_with("UE4SSL.") && !text.starts_with("UE4SS.")
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_dll_load(name: SliceU16) {
    ue4ssl_runtime_mod_dll_load(0, name);
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_mod_dll_load(id: u64, name: SliceU16) {
    if name.data.is_null() || name.len == 0 {
        return;
    }
    submit(Request::DllLoad(id, copy_slice(name)));
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_find_mod(
    name: SliceU16,
    installed: u8,
    started: u8,
) -> *mut c_void {
    let name = copy_slice(name);
    let state = lock();
    if !state.accepting {
        return std::ptr::null_mut();
    }
    state
        .mods
        .iter()
        .find(|view| {
            view.name == name
                && (installed == 0 || view.status.installed != 0)
                && (started == 0 || view.status.started != 0)
        })
        .map_or(std::ptr::null_mut(), |view| view.adapter as *mut c_void)
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_mod_status(id: u64) -> CppModRuntimeStatus {
    lock()
        .mods
        .iter()
        .find(|view| view.id == id)
        .map_or_else(CppModRuntimeStatus::default, |view| view.status)
}
#[no_mangle]
pub extern "C" fn ue4ssl_runtime_mod_action(id: u64, action: u32, value: u8) {
    submit(Request::Action(id, action, value != 0));
}
pub fn legacy_create(path: SliceU16, dll: SliceU16) -> *mut CppModHandle {
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    if submit_legacy(Request::Create(id, copy_slice(path), copy_slice(dll))) {
        id as usize as *mut CppModHandle
    } else {
        std::ptr::null_mut()
    }
}

struct LoadedMod {
    id: u64,
    name: Vec<u16>,
    handle: Box<CppModHandle>,
    adapter: *mut c_void,
    guarded: bool,
}
struct Runtime {
    config: RuntimeConfig,
    working: PathBuf,
    directory: PathBuf,
    mods: Vec<LoadedMod>,
    unreal_ready: bool,
    program_ready: bool,
}
impl Runtime {
    fn publish(&self, index: usize) {
        let loaded = &self.mods[index];
        if let Some(view) = lock().mods.iter_mut().find(|view| view.id == loaded.id) {
            view.status = loaded.handle.status();
        }
    }
    fn interrupted(&self) -> bool {
        STOP.load(Ordering::Acquire) || lock().reload
    }
    fn log(&self, level: u32, message: &str) {
        let wide: Vec<u16> = message.encode_utf16().collect();
        unsafe { ue4ssl_native_runtime_log(level, slice(&wide)) };
    }
    fn load(&mut self, id: u64, name: Vec<u16>, path: Vec<u16>, dll: Vec<u16>, install: bool) {
        let _scope = OwnerScope::new(id as usize);
        let mut handle = CppModHandle::load(slice(&path), slice(&dll));
        let status = handle.status();
        if status.installable == 0 {
            self.log(
                3,
                &format!(
                    "Could not load mod '{}': failure={} Win32={}\n",
                    String::from_utf16_lossy(&name),
                    status.failure_code,
                    status.last_error
                ),
            );
            self.unregister(id as usize);
        }
        handle.set_installed(install && status.installable != 0);
        let adapter =
            unsafe { ue4ssl_native_runtime_create_mod_view(id, slice(&name), slice(&path)) };
        if adapter.is_null() {
            self.unregister(id as usize);
            return;
        }
        let guarded = should_guard_updates(&name);
        {
            let mut state = lock();
            state.retired_owners.remove(&(id as usize));
            state.mods.push(ModView {
                id,
                name: name.clone(),
                adapter: adapter as usize,
                status: handle.status(),
            });
        }
        self.mods.push(LoadedMod {
            id,
            name,
            handle,
            adapter,
            guarded,
        });
    }
    fn discover_and_start(&mut self) -> bool {
        if !self.directory.is_dir() {
            self.log(
                4,
                &format!(
                    "Mods directory does not exist: {}\n",
                    self.directory.display()
                ),
            );
            return false;
        }
        let mut names = HashSet::new();
        // Discovery supplies JS -> Lua -> ordinary mods, keeping the compatibility order.
        for spec in mods::discover_mods(&self.working, &self.directory) {
            if self.interrupted() {
                break;
            }
            let name = wide::os_str_to_utf16(&spec.mod_name);
            if !names.insert(name.clone()) {
                self.log(
                    3,
                    &format!(
                        "Duplicate mod name '{}' skipped\n",
                        spec.mod_name.to_string_lossy()
                    ),
                );
                continue;
            }
            let path = wide::os_str_to_utf16(spec.mod_path.as_os_str());
            let dll = spec
                .dll_name
                .map(|dll| wide::os_str_to_utf16(&dll))
                .unwrap_or_default();
            self.load(
                NEXT_ID.fetch_add(1, Ordering::Relaxed),
                name,
                path,
                dll,
                true,
            );
        }
        for index in 0..self.mods.len() {
            if self.interrupted() {
                break;
            }
            self.call(index, 4);
        }
        true
    }
    fn call(&mut self, index: usize, action: u32) {
        let loaded = &mut self.mods[index];
        if action != 4 && loaded.handle.status().started == 0 {
            return;
        }
        if action == 4 && loaded.handle.status().started == 0 {
            lock().retired_owners.remove(&(loaded.id as usize));
        }
        let _scope = OwnerScope::new(loaded.id as usize);
        let start = Instant::now();
        match action {
            4 => loaded.handle.start(),
            6 => loaded.handle.fire_unreal_init(),
            7 => loaded.handle.fire_ui_init(),
            8 => loaded.handle.fire_program_start(),
            9 if loaded.handle.status().updates_disabled == 0 => loaded.handle.fire_update(),
            _ => return,
        }
        let elapsed = start.elapsed().as_millis();
        if action == 9
            && self.config.enable_slow_update_guard != 0
            && loaded.guarded
            && elapsed >= u128::from(self.config.slow_update_threshold_ms)
        {
            loaded.handle.set_updates_disabled(true);
            let message = format!(
                "Disabled further on_update calls for mod '{}' after {}ms (threshold={}ms)\n",
                String::from_utf16_lossy(&loaded.name),
                elapsed,
                self.config.slow_update_threshold_ms
            );
            self.log(3, &message);
        }
        self.publish(index);
        if action == 4 && self.mods[index].handle.status().started == 0 {
            self.log(
                3,
                &format!(
                    "Mod '{}' returned no instance; other mods continue\n",
                    String::from_utf16_lossy(&self.mods[index].name)
                ),
            );
            self.unregister(self.mods[index].id as usize);
        }
    }
    fn broadcast(&mut self, action: u32) {
        for index in 0..self.mods.len() {
            if self.interrupted() {
                break;
            }
            self.call(index, action);
        }
    }
    fn unregister(&self, owner: usize) {
        if owner == 0 {
            return;
        }
        if let Some(unregister) = self.config.unregister_owner {
            unsafe { unregister(self.config.context, owner) };
        }
        ue4ssl_runtime_cancel_owner(owner);
    }
    fn remove(&mut self, index: usize) {
        let id = self.mods[index].id;
        self.unregister(id as usize);
        self.unregister(self.mods[index].handle.owner());
        lock().mods.retain(|view| view.id != id);
        let mut loaded = self.mods.remove(index);
        let _scope = OwnerScope::new(id as usize);
        // All sources are detached and actor callbacks have returned before uninstall/FreeLibrary.
        loaded.handle.uninstall();
        unsafe { ue4ssl_native_runtime_destroy_mod_view(loaded.adapter) };
    }
    fn clear(&mut self, stopping: bool) {
        lock().accepting = false;
        if stopping {
            unsafe { ue4ssl_stop_dll_notifications() };
        }
        if let Some(close) = self.config.close_sources {
            unsafe { close(self.config.context) };
        }
        for loaded in &self.mods {
            self.unregister(loaded.id as usize);
            self.unregister(loaded.handle.owner());
        }
        let cancelled = std::mem::take(&mut lock().queue);
        let mut completions = Vec::new();
        for request in cancelled {
            match request {
                Request::Complete(signal) => completions.push(signal),
                other => drop(other),
            }
        }
        while !self.mods.is_empty() {
            self.remove(self.mods.len() - 1);
        }
        drop(completions);
    }
    fn process(&mut self, request: Request) {
        // Completion signals also run when cancelled, so external v1 callers cannot hang.
        match request {
            Request::Complete(signal) => drop(signal),
            Request::Event(event) => {
                let _scope = OwnerScope::new(event.0.owner);
                if let Some(callback) = event.0.callback {
                    unsafe { callback(event.0.context) };
                }
                drop(event);
            }
            Request::DllLoad(id, name) => {
                for index in 0..self.mods.len() {
                    if self.interrupted() {
                        break;
                    }
                    let loaded = &mut self.mods[index];
                    if id != 0 && loaded.id != id {
                        continue;
                    }
                    let _scope = OwnerScope::new(loaded.id as usize);
                    loaded.handle.fire_dll_load(slice(&name));
                }
            }
            Request::Action(id, action, value) => {
                let Some(index) = self.mods.iter().position(|loaded| loaded.id == id) else {
                    return;
                };
                match action {
                    1 => self.mods[index].handle.set_installable(value),
                    2 => self.mods[index].handle.set_installed(value),
                    3 => self.mods[index].handle.set_updates_disabled(value),
                    5 => {
                        self.remove(index);
                        return;
                    }
                    10 => {
                        self.unregister(self.mods[index].id as usize);
                        self.unregister(self.mods[index].handle.owner());
                        let _scope = OwnerScope::new(self.mods[index].id as usize);
                        self.mods[index].handle.uninstall();
                    }
                    _ => self.call(index, action),
                }
                self.publish(index);
            }
            Request::Create(id, path, dll) => {
                let name = wide::slice_to_path_buf(slice(&path))
                    .file_name()
                    .map(wide::os_str_to_utf16)
                    .unwrap_or_default();
                self.load(id, name, path, dll, false);
            }
        }
    }
    fn run(&mut self) -> bool {
        if !self.discover_and_start() || STOP.load(Ordering::Acquire) {
            return false;
        }
        self.broadcast(7);
        if STOP.load(Ordering::Acquire) {
            return false;
        }
        if unsafe { self.config.prepare_engine.unwrap()(self.config.context) } == 0 {
            return false;
        }
        self.unreal_ready = true;
        self.broadcast(6);
        if STOP.load(Ordering::Acquire) {
            return false;
        }
        if unsafe { self.config.program_ready.unwrap()(self.config.context) } == 0 {
            return false;
        }
        self.program_ready = true;
        lock().started = true;
        self.broadcast(8);
        lock().phase = Phase::Running;
        while !STOP.load(Ordering::Acquire) {
            let reload = {
                let mut state = lock();
                std::mem::take(&mut state.reload)
            };
            if reload {
                lock().phase = Phase::Reloading;
                self.clear(false);
                crate::host::ue4ssl_host_reset_dll_dispatch_cache();
                if STOP.load(Ordering::Acquire) {
                    break;
                }
                {
                    let mut state = lock();
                    state.retired_owners.clear();
                    state.accepting = true;
                }
                if !self.discover_and_start() {
                    return false;
                }
                if self.unreal_ready {
                    self.broadcast(6);
                }
                if self.program_ready {
                    self.broadcast(8);
                }
                lock().phase = Phase::Running;
            }
            // Take one task at a time so callback cancellation cannot leave a detached batch.
            // Explicit legacy controls still complete while ordinary event processing is paused.
            for _ in 0..5 {
                if self.interrupted() {
                    break;
                }
                let request = {
                    let mut state = lock();
                    if state.paused {
                        let next = state
                            .queue
                            .iter()
                            .position(|request| !matches!(request, Request::Event(_)));
                        next.and_then(|index| state.queue.remove(index))
                    } else {
                        state.queue.pop_front()
                    }
                };
                let Some(request) = request else {
                    break;
                };
                self.process(request);
            }
            if !lock().paused {
                if !self.interrupted() {
                    if unsafe { self.config.poll_input.unwrap()(self.config.context) } == 0 {
                        break;
                    }
                }
                if !self.interrupted() {
                    self.broadcast(9);
                }
            }
            let mut state = lock();
            shared().wake.wait_for(&mut state, Duration::from_millis(5));
        }
        true
    }
}

#[no_mangle]
pub unsafe extern "C" fn ue4ssl_runtime_start(config: *const RuntimeConfig) -> u8 {
    let Some(config) = config.as_ref().copied() else {
        return 0;
    };
    if config.prepare_engine.is_none()
        || config.program_ready.is_none()
        || config.poll_input.is_none()
        || config.close_sources.is_none()
        || config.unregister_owner.is_none()
    {
        return 0;
    }
    {
        let mut state = lock();
        if state.phase != Phase::Stopped || STOP.load(Ordering::Acquire) {
            return 0;
        }
        state.phase = Phase::Starting;
        state.accepting = true;
        state.started = false;
        state.paused = false;
        state.reload = false;
        state.retired_owners.clear();
    }
    IS_ACTOR.with(|slot| slot.set(true));
    let mut runtime = Runtime {
        config,
        working: wide::slice_to_path_buf(config.working_directory),
        directory: wide::slice_to_path_buf(config.mods_directory),
        mods: Vec::new(),
        unreal_ready: false,
        program_ready: false,
    };
    let result = catch_unwind(AssertUnwindSafe(|| runtime.run())).unwrap_or(false);
    {
        let mut state = lock();
        state.phase = Phase::Stopping;
        state.accepting = false;
        state.started = false;
    }
    runtime.clear(true);
    {
        let mut state = lock();
        state.phase = Phase::Stopped;
        state.paused = false;
        state.reload = false;
    }
    IS_ACTOR.with(|slot| slot.set(false));
    shared().wake.notify_all();
    u8::from(result)
}

#[cfg(not(test))]
extern "C" {
    fn ue4ssl_native_input_owner_enter(owner: usize) -> usize;
    fn ue4ssl_native_input_owner_leave(previous: usize);
    fn ue4ssl_native_input_current_owner() -> usize;
    fn ue4ssl_stop_dll_notifications();
    fn ue4ssl_native_runtime_create_mod_view(
        id: u64,
        name: SliceU16,
        path: SliceU16,
    ) -> *mut c_void;
    fn ue4ssl_native_runtime_destroy_mod_view(view: *mut c_void);
    fn ue4ssl_native_runtime_log(level: u32, message: SliceU16);
}

#[cfg(test)]
unsafe fn ue4ssl_native_input_owner_enter(_owner: usize) -> usize {
    0
}
#[cfg(test)]
unsafe fn ue4ssl_native_input_owner_leave(_previous: usize) {}
#[cfg(test)]
unsafe fn ue4ssl_native_input_current_owner() -> usize {
    0
}
#[cfg(test)]
unsafe fn ue4ssl_stop_dll_notifications() {}
#[cfg(test)]
unsafe fn ue4ssl_native_runtime_create_mod_view(
    _id: u64,
    _name: SliceU16,
    _path: SliceU16,
) -> *mut c_void {
    std::ptr::null_mut()
}
#[cfg(test)]
unsafe fn ue4ssl_native_runtime_destroy_mod_view(_view: *mut c_void) {}
#[cfg(test)]
unsafe fn ue4ssl_native_runtime_log(_level: u32, _message: SliceU16) {}

pub fn force_link_exports() {
    let _ = ue4ssl_runtime_start as *const () as usize;
    let _ = ue4ssl_runtime_shutdown as *const () as usize;
    let _ = ue4ssl_runtime_request_shutdown as *const () as usize;
    let _ = ue4ssl_runtime_reinstall as *const () as usize;
    let _ = ue4ssl_runtime_queue_event as *const () as usize;
    let _ = ue4ssl_runtime_queue_empty as *const () as usize;
    let _ = ue4ssl_runtime_current_owner as *const () as usize;
    let _ = ue4ssl_runtime_cancel_owner as *const () as usize;
    let _ = ue4ssl_runtime_dll_load as *const () as usize;
    let _ = ue4ssl_runtime_find_mod as *const () as usize;
    let _ = ue4ssl_runtime_mod_status as *const () as usize;
    let _ = ue4ssl_runtime_mod_action as *const () as usize;
    let _ = ue4ssl_runtime_mod_dll_load as *const () as usize;
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::AtomicUsize;

    static TEST_LOCK: Mutex<()> = Mutex::new(());

    #[derive(Default)]
    struct Trace {
        steps: Mutex<Vec<&'static str>>,
        reload_accepted: AtomicUsize,
        rejected: AtomicUsize,
        self_shutdown: AtomicUsize,
    }
    impl Trace {
        fn record(&self, step: &'static str) {
            self.steps.lock().push(step);
        }
    }
    unsafe fn trace(context: *mut c_void) -> &'static Trace {
        &*context.cast::<Trace>()
    }
    unsafe extern "C" fn prepare(context: *mut c_void) -> u8 {
        trace(context).record("prepare");
        1
    }
    unsafe extern "C" fn prepare_failure(context: *mut c_void) -> u8 {
        trace(context).record("prepare-failed");
        0
    }
    unsafe extern "C" fn release(context: *mut c_void) {
        trace(context).record("release");
    }
    unsafe extern "C" fn cancelled_callback(context: *mut c_void) {
        trace(context).record("must-not-run");
    }
    unsafe extern "C" fn reload(context: *mut c_void) {
        let trace = trace(context);
        trace.record("reload");
        trace
            .reload_accepted
            .store(ue4ssl_runtime_reinstall() as usize, Ordering::Relaxed);
        trace.rejected.store(
            ue4ssl_runtime_queue_event(RuntimeEvent {
                owner: 7,
                callback: Some(cancelled_callback),
                context,
                release: Some(release),
            }) as usize,
            Ordering::Relaxed,
        );
    }
    unsafe extern "C" fn ready(context: *mut c_void) -> u8 {
        trace(context).record("ready");
        ue4ssl_runtime_queue_event(RuntimeEvent {
            owner: 7,
            callback: Some(reload),
            context,
            release: Some(release),
        });
        ue4ssl_runtime_queue_event(RuntimeEvent {
            owner: 7,
            callback: Some(cancelled_callback),
            context,
            release: Some(release),
        });
        1
    }
    unsafe extern "C" fn poll(context: *mut c_void) -> u8 {
        let trace = trace(context);
        trace.record("poll");
        trace
            .self_shutdown
            .store(ue4ssl_runtime_shutdown() as usize, Ordering::Relaxed);
        1
    }
    unsafe extern "C" fn close(context: *mut c_void) {
        trace(context).record("detach");
    }
    unsafe extern "C" fn unregister(_context: *mut c_void, _owner: usize) {}

    unsafe extern "C" fn count_update(context: *mut c_void) {
        (*context.cast::<AtomicUsize>()).fetch_add(1, Ordering::Relaxed);
    }

    #[test]
    fn slow_update_guard_stops_ordinary_mod_but_not_builtin_runtime_updates() {
        let _serial = TEST_LOCK.lock();
        STOP.store(false, Ordering::Release);
        let ordinary = AtomicUsize::new(0);
        let builtin = AtomicUsize::new(0);
        let mut runtime = Runtime {
            config: RuntimeConfig {
                enable_slow_update_guard: 1,
                slow_update_threshold_ms: 0,
                ..RuntimeConfig::default()
            },
            working: PathBuf::new(),
            directory: PathBuf::new(),
            mods: Vec::new(),
            unreal_ready: false,
            program_ready: false,
        };
        for (id, name, counter) in [
            (91, "Example", &ordinary),
            (92, "UE4SSL.JavaScript", &builtin),
        ] {
            let name: Vec<u16> = name.encode_utf16().collect();
            let handle = CppModHandle::test_instance(
                (counter as *const AtomicUsize).cast_mut().cast(),
                count_update,
            );
            lock().mods.push(ModView {
                id,
                name: name.clone(),
                adapter: 0,
                status: handle.status(),
            });
            let guarded = should_guard_updates(&name);
            runtime.mods.push(LoadedMod {
                id,
                name,
                handle,
                adapter: std::ptr::null_mut(),
                guarded,
            });
        }
        runtime.broadcast(9);
        runtime.broadcast(9);
        assert_eq!(ordinary.load(Ordering::Relaxed), 1);
        assert_eq!(builtin.load(Ordering::Relaxed), 2);
        assert_eq!(ue4ssl_runtime_mod_status(91).updates_disabled, 1);
        assert_eq!(ue4ssl_runtime_mod_status(92).updates_disabled, 0);
        runtime.clear(true);
    }

    #[test]
    fn reload_reentry_cancels_owned_tasks_before_shutdown_and_initialization_failure_cleans_up() {
        let _serial = TEST_LOCK.lock();
        STOP.store(false, Ordering::Release);
        let directory = std::env::temp_dir().join(format!("ue4ssl-runtime-{}", std::process::id()));
        std::fs::create_dir_all(&directory).unwrap();
        let path = wide::os_str_to_utf16(directory.as_os_str());
        let mut trace = Trace::default();
        let mut config = RuntimeConfig {
            working_directory: slice(&path),
            mods_directory: slice(&path),
            context: (&mut trace as *mut Trace).cast(),
            prepare_engine: Some(prepare),
            program_ready: Some(ready),
            poll_input: Some(poll),
            close_sources: Some(close),
            unregister_owner: Some(unregister),
            ..RuntimeConfig::default()
        };
        assert_eq!(unsafe { ue4ssl_runtime_start(&config) }, 1);
        assert_eq!(trace.reload_accepted.load(Ordering::Relaxed), 1);
        assert_eq!(trace.rejected.load(Ordering::Relaxed), 0);
        assert_eq!(trace.self_shutdown.load(Ordering::Relaxed), 0);
        assert_eq!(
            *trace.steps.lock(),
            [
                "prepare", "ready", "reload", "release", "release", "detach", "release", "poll",
                "detach"
            ]
        );
        assert_eq!(ue4ssl_runtime_shutdown(), 1);
        assert_eq!(get_program_flags().is_program_started, 0);
        assert_eq!(ue4ssl_runtime_queue_empty(), 1);

        STOP.store(false, Ordering::Release);
        trace.steps.lock().clear();
        config.prepare_engine = Some(prepare_failure);
        assert_eq!(unsafe { ue4ssl_runtime_start(&config) }, 0);
        assert_eq!(*trace.steps.lock(), ["prepare-failed", "detach"]);
        assert_eq!(get_program_flags().processing_events, 0);
        ue4ssl_runtime_request_shutdown();
        assert_eq!(unsafe { ue4ssl_runtime_start(&config) }, 0);
        assert_eq!(*trace.steps.lock(), ["prepare-failed", "detach"]);
        std::fs::remove_dir(&directory).unwrap();
        STOP.store(false, Ordering::Release);
    }
}
