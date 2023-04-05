use std::{
    ops,
    sync::Mutex,
};

use smithay::{reexports::{
    wayland_protocols::wp::pointer_constraints::zv1::server::{
        zwp_confined_pointer_v1::{self, ZwpConfinedPointerV1},
        zwp_locked_pointer_v1::{self, ZwpLockedPointerV1},
        zwp_pointer_constraints_v1::{self, Lifetime, ZwpPointerConstraintsV1},
    },
    wayland_server::{
        self,
        backend::GlobalId,
        protocol::{wl_surface::WlSurface},
        Client, DataInit, Dispatch, DisplayHandle, GlobalDispatch, New, Resource, WEnum,
    },
}};

use smithay::{
    input::SeatHandler,
    utils::{Logical, Point},
    wayland::compositor::{self, RegionAttributes},
};

// **NOTE**: This is not a proper implementation, as it exploids the fact, that this plugin never has more than one Seat

const VERSION: u32 = 1;

#[derive(Clone)]
pub struct ConfinedPointer {
    handle: zwp_confined_pointer_v1::ZwpConfinedPointerV1,
    active: bool,
    pub region: Option<RegionAttributes>,
    pending_region: Option<RegionAttributes>,
    lifetime: WEnum<Lifetime>,
}

#[derive(Clone)]
pub struct LockedPointer {
    handle: zwp_locked_pointer_v1::ZwpLockedPointerV1,
    active: bool,
    pub region: Option<RegionAttributes>,
    pending_region: Option<RegionAttributes>,
    lifetime: WEnum<Lifetime>,
    cursor_position_hint: Option<Point<f64, Logical>>,
    pending_cursor_position_hint: Option<Point<f64, Logical>>,
}

#[derive(Clone)]
pub enum PointerConstraint {
    Confined(ConfinedPointer),
    Locked(LockedPointer),
}

pub struct PointerConstraintRef<'a> {
    entry: &'a mut Option<PointerConstraint>,
}

impl<'a> ops::Deref for PointerConstraintRef<'a> {
    type Target = PointerConstraint;

    fn deref(&self) -> &Self::Target {
        self.entry.as_ref().unwrap()
    }
}

impl<'a> ops::DerefMut for PointerConstraintRef<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.entry.as_mut().unwrap()
    }
}

impl<'a> PointerConstraintRef<'a> {
    /// Send `locked`/`unlocked`
    pub fn activate(&mut self) {
        match self.entry.as_mut().unwrap() {
            PointerConstraint::Confined(confined) => confined.activate(),
            PointerConstraint::Locked(locked) => locked.activate(),
        }
    }

    /// Send `unlocked`/`unconfined`
    ///
    /// For oneshot constraints, will destroy the constraint.
    pub fn deactivate(self) {
        match self.entry.as_mut().unwrap() {
            PointerConstraint::Confined(confined) => if confined.active {
                confined.active = false;
                confined.handle.unconfined()
            },
            PointerConstraint::Locked(locked) => if locked.active {
                locked.active = false;
                locked.handle.unlocked()
            },
        }

        if self.lifetime() == WEnum::Value(Lifetime::Oneshot) {
            self.entry.take();
        }
    }

    fn lifetime(&self) -> WEnum<Lifetime> {
        match self.entry.as_ref().unwrap() {
            PointerConstraint::Confined(confined) => confined.lifetime,
            PointerConstraint::Locked(locked) => locked.lifetime,
        }
    }
}

impl LockedPointer {
    pub fn activate(&mut self) {
        if !self.active {
            self.active = true;
            self.handle.locked()
        }
    }

    pub fn is_active(&self) -> bool {
        self.active
    }
}

impl ConfinedPointer {
    pub fn activate(&mut self) {
        if !self.active {
            self.active = true;
            self.handle.confined()
        }
    }

    pub fn is_active(&self) -> bool {
        self.active
    }
}

impl PointerConstraint {
    fn commit(&mut self) {
        match self {
            Self::Confined(confined) => {
                confined.region = confined.pending_region.clone();
            }
            Self::Locked(locked) => {
                locked.region = locked.pending_region.clone();
                locked.cursor_position_hint = locked.pending_cursor_position_hint;
            }
        }
    }
}

#[derive(Debug)]
pub struct PointerConstraintsState {
    global: GlobalId,
}

impl PointerConstraintsState {
    pub fn new<D>(display: &DisplayHandle) -> Self
    where
        D: GlobalDispatch<ZwpPointerConstraintsV1, ()>,
        D: Dispatch<ZwpPointerConstraintsV1, ()>,
        D: Dispatch<ZwpConfinedPointerV1, PointerConstraintUserData>,
        D: Dispatch<ZwpLockedPointerV1, PointerConstraintUserData>,
        D: SeatHandler,
        D: 'static,
    {
        let global = display.create_global::<D, ZwpPointerConstraintsV1, _>(VERSION, ());

        Self { global }
    }

    pub fn global(&self) -> GlobalId {
        self.global.clone()
    }
}

pub struct PointerConstraintUserData {
    surface: WlSurface,
}

struct PointerConstraintData {
    constraint: Option<PointerConstraint>,
}

// TODO Public method to get current constraints for surface/seat
/// Get constraint for surface and pointer, if any
pub fn with_pointer_constraint<T, F: FnOnce(Option<PointerConstraintRef<'_>>) -> T>(
    surface: &WlSurface,
    f: F,
) -> T {
    with_constraint_data(surface, |data| {
        let constraint = data.map(|data| PointerConstraintRef { entry: &mut data.constraint });
        f(constraint)
    })
}

fn commit_hook(_dh: &DisplayHandle, surface: &WlSurface) {
    with_constraint_data(surface, |data| {
        let data = data.unwrap();
        if let Some(constraint) = data.constraint.as_mut() {
            match constraint {
                PointerConstraint::Confined(confined) => {
                    confined.region = confined.pending_region.clone();
                }
                PointerConstraint::Locked(locked) => {
                    locked.region = locked.pending_region.clone();
                    locked.cursor_position_hint = locked.pending_cursor_position_hint;
                }
            }
        }
    })
}

/// Get `PointerConstraintData` associated with a surface, if any.
fn with_constraint_data<T, F: FnOnce(Option<&mut PointerConstraintData>) -> T>(
    surface: &WlSurface,
    f: F,
) -> T {
    compositor::with_states(surface, |states| {
        let data = states.data_map.get::<Mutex<PointerConstraintData>>();
        if let Some(data) = data {
            f(Some(&mut data.lock().unwrap()))
        } else {
            f(None)
        }
    })
}

/// Add constraint for surface, or raise protocol error if one exists
fn add_constraint(
    pointer_constraints: &ZwpPointerConstraintsV1,
    surface: &WlSurface,
    constraint: PointerConstraint,
) {
    let mut added = false;
    compositor::with_states(surface, |states| {
        added = states.data_map.insert_if_missing_threadsafe(|| {
            Mutex::new(PointerConstraintData {
                constraint: None,
            })
        });
        let data = states
            .data_map
            .get::<Mutex<PointerConstraintData>>()
            .unwrap();
        let mut data = data.lock().unwrap();

        if data.constraint.is_some() {
            pointer_constraints.post_error(
                zwp_pointer_constraints_v1::Error::AlreadyConstrained,
                "pointer constrait already exists for surface and seat",
            );
        } else {
            data.constraint = Some(constraint);
        }
    });

    if added {
        compositor::add_pre_commit_hook(surface, commit_hook);
    }
}

fn remove_constraint(surface: &WlSurface) {
    with_constraint_data(surface, |data| {
        if let Some(data) = data {
            data.constraint.take();
        }
    });
}

impl<D> Dispatch<ZwpPointerConstraintsV1, (), D> for PointerConstraintsState
where
    D: Dispatch<ZwpPointerConstraintsV1, ()>,
    D: Dispatch<ZwpConfinedPointerV1, PointerConstraintUserData>,
    D: Dispatch<ZwpLockedPointerV1, PointerConstraintUserData>,
    D: SeatHandler,
    D: 'static,
{
    fn request(
        _state: &mut D,
        _client: &wayland_server::Client,
        pointer_constraints: &ZwpPointerConstraintsV1,
        request: zwp_pointer_constraints_v1::Request,
        _data: &(),
        _dh: &DisplayHandle,
        data_init: &mut wayland_server::DataInit<'_, D>,
    ) {
        match request {
            zwp_pointer_constraints_v1::Request::LockPointer {
                id,
                surface,
                pointer: _,
                region,
                lifetime,
            } => {
                let region = region.as_ref().map(compositor::get_region_attributes);
                let handle = data_init.init(
                    id,
                    PointerConstraintUserData {
                        surface: surface.clone(),
                    },
                );
                add_constraint(
                    pointer_constraints,
                    &surface,
                    PointerConstraint::Locked(LockedPointer {
                        handle,
                        active: false,
                        region: region.clone(),
                        pending_region: region.clone(),
                        lifetime,
                        cursor_position_hint: None,
                        pending_cursor_position_hint: None,
                    }),
                );
            }
            zwp_pointer_constraints_v1::Request::ConfinePointer {
                id,
                surface,
                pointer: _,
                region,
                lifetime,
            } => {
                let region = region.as_ref().map(compositor::get_region_attributes);
                let handle = data_init.init(
                    id,
                    PointerConstraintUserData {
                        surface: surface.clone(),
                    },
                );
                add_constraint(
                    pointer_constraints,
                    &surface,
                    PointerConstraint::Confined(ConfinedPointer {
                        handle,
                        active: false,
                        region: region.clone(),
                        pending_region: region,
                        lifetime,
                    }),
                );
            }
            zwp_pointer_constraints_v1::Request::Destroy => {}
            _ => unreachable!(),
        }
    }
}

impl<D> GlobalDispatch<ZwpPointerConstraintsV1, (), D> for PointerConstraintsState
where
    D: GlobalDispatch<ZwpPointerConstraintsV1, ()>
        + Dispatch<ZwpPointerConstraintsV1, ()>
        + SeatHandler
        + 'static,
{
    fn bind(
        _state: &mut D,
        _dh: &DisplayHandle,
        _client: &Client,
        resource: New<ZwpPointerConstraintsV1>,
        _global_data: &(),
        data_init: &mut DataInit<'_, D>,
    ) {
        data_init.init(resource, ());
    }
}

impl<D> Dispatch<ZwpConfinedPointerV1, PointerConstraintUserData, D> for PointerConstraintsState
where
    D: Dispatch<ZwpConfinedPointerV1, PointerConstraintUserData>,
    D: SeatHandler,
    D: 'static,
{
    fn request(
        _state: &mut D,
        _client: &wayland_server::Client,
        _confined_pointer: &ZwpConfinedPointerV1,
        request: zwp_confined_pointer_v1::Request,
        data: &PointerConstraintUserData,
        _dh: &DisplayHandle,
        _data_init: &mut wayland_server::DataInit<'_, D>,
    ) {
        match request {
            zwp_confined_pointer_v1::Request::SetRegion { region } => {
                with_pointer_constraint(&data.surface, |constraint| {
                    if let Some(PointerConstraint::Confined(confined)) =
                        constraint.map(|x| x.entry.as_mut().unwrap())
                    {
                        confined.pending_region =
                            region.as_ref().map(compositor::get_region_attributes);
                    }
                });
            }
            zwp_confined_pointer_v1::Request::Destroy => {
                remove_constraint(&data.surface);
            }
            _ => unreachable!(),
        }
    }
}

impl<D> Dispatch<ZwpLockedPointerV1, PointerConstraintUserData, D> for PointerConstraintsState
where
    D: Dispatch<ZwpLockedPointerV1, PointerConstraintUserData>,
    D: SeatHandler,
    D: 'static,
{
    fn request(
        _state: &mut D,
        _client: &wayland_server::Client,
        _locked_pointer: &ZwpLockedPointerV1,
        request: zwp_locked_pointer_v1::Request,
        data: &PointerConstraintUserData,
        _dh: &DisplayHandle,
        _data_init: &mut wayland_server::DataInit<'_, D>,
    ) {
        match request {
            zwp_locked_pointer_v1::Request::SetCursorPositionHint {
                surface_x,
                surface_y,
            } => {
                with_pointer_constraint(&data.surface, |constraint| {
                    if let Some(PointerConstraint::Locked(locked)) =
                        constraint.map(|x| x.entry.as_mut().unwrap())
                    {
                        locked.pending_cursor_position_hint = Some((surface_x, surface_y).into());
                    }
                });
            }
            zwp_locked_pointer_v1::Request::SetRegion { region } => {
                with_pointer_constraint(&data.surface, |constraint| {
                    if let Some(PointerConstraint::Locked(locked)) =
                        constraint.map(|x| x.entry.as_mut().unwrap())
                    {
                        locked.pending_region =
                            region.as_ref().map(compositor::get_region_attributes);
                    }
                });
            }
            zwp_locked_pointer_v1::Request::Destroy => {
                remove_constraint(&data.surface);
            }
            _ => unreachable!(),
        }
    }
}

macro_rules! delegate_pointer_constraints {
    ($(@<$( $lt:tt $( : $clt:tt $(+ $dlt:tt )* )? ),+>)? $ty: ty) => {
        smithay::reexports::wayland_server::delegate_global_dispatch!($(@< $( $lt $( : $clt $(+ $dlt )* )? ),+ >)? $ty: [
            smithay::reexports::wayland_protocols::wp::pointer_constraints::zv1::server::zwp_pointer_constraints_v1::ZwpPointerConstraintsV1: ()
        ] => $crate::wayland::protocols::pointer_constraints::PointerConstraintsState);
        smithay::reexports::wayland_server::delegate_dispatch!($(@< $( $lt $( : $clt $(+ $dlt )* )? ),+ >)? $ty: [
            smithay::reexports::wayland_protocols::wp::pointer_constraints::zv1::server::zwp_pointer_constraints_v1::ZwpPointerConstraintsV1: ()
        ] => $crate::wayland::protocols::pointer_constraints::PointerConstraintsState);
        smithay::reexports::wayland_server::delegate_dispatch!($(@< $( $lt $( : $clt $(+ $dlt )* )? ),+ >)? $ty: [
            smithay::reexports::wayland_protocols::wp::pointer_constraints::zv1::server::zwp_confined_pointer_v1::ZwpConfinedPointerV1: $crate::wayland::protocols::pointer_constraints::PointerConstraintUserData
        ] => $crate::wayland::protocols::pointer_constraints::PointerConstraintsState);
        smithay::reexports::wayland_server::delegate_dispatch!($(@< $( $lt $( : $clt $(+ $dlt )* )? ),+ >)? $ty: [
            smithay::reexports::wayland_protocols::wp::pointer_constraints::zv1::server::zwp_locked_pointer_v1::ZwpLockedPointerV1: $crate::wayland::protocols::pointer_constraints::PointerConstraintUserData
        ] => $crate::wayland::protocols::pointer_constraints::PointerConstraintsState);
    };
}
pub(crate) use delegate_pointer_constraints;
