use smithay::{
    delegate_data_device,
    wayland::data_device::{
        ClientDndGrabHandler, DataDeviceHandler, DataDeviceState, ServerDndGrabHandler,
    },
};

use crate::comp::State;

impl ServerDndGrabHandler for State {}
impl ClientDndGrabHandler for State {}
impl DataDeviceHandler for State {
    fn data_device_state(&self) -> &DataDeviceState {
        &self.data_device_state
    }
}

delegate_data_device!(State);
