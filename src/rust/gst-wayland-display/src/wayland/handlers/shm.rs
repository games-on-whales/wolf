use smithay::{
    delegate_shm,
    wayland::shm::{ShmHandler, ShmState},
};

use crate::comp::State;

impl ShmHandler for State {
    fn shm_state(&self) -> &ShmState {
        &self.shm_state
    }
}

delegate_shm!(State);
