use std::sync::{Mutex, OnceLock};

#[derive(Clone, Copy, Default)]
pub struct ProgramFlags {
    pub is_program_started: bool,
    pub processing_events: bool,
    pub pause_events_processing: bool,
}

fn program_state() -> &'static Mutex<ProgramFlags> {
    static PROGRAM_STATE: OnceLock<Mutex<ProgramFlags>> = OnceLock::new();
    PROGRAM_STATE.get_or_init(|| Mutex::new(ProgramFlags::default()))
}

pub fn get_program_flags() -> ProgramFlags {
    match program_state().lock() {
        Ok(state) => *state,
        Err(poisoned) => *poisoned.into_inner(),
    }
}

pub fn set_program_started(is_program_started: bool) {
    match program_state().lock() {
        Ok(mut state) => {
            state.is_program_started = is_program_started;
        }
        Err(poisoned) => {
            let mut state = poisoned.into_inner();
            state.is_program_started = is_program_started;
        }
    }
}

pub fn set_processing_state(processing_events: bool, pause_events_processing: bool) {
    match program_state().lock() {
        Ok(mut state) => {
            state.processing_events = processing_events;
            state.pause_events_processing = pause_events_processing;
        }
        Err(poisoned) => {
            let mut state = poisoned.into_inner();
            state.processing_events = processing_events;
            state.pause_events_processing = pause_events_processing;
        }
    }
}
