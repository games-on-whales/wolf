# Wolf Session Management Architecture

## CRITICAL FINDINGS: True Session Sharing Not Implemented

### **What Actually Works:**
- ✅ **Auto-persistent sessions**: Background containers auto-start and stay running
- ✅ **Single client replacement**: Newest client can connect to existing background session
- ✅ **Container persistence**: Apps like Zed/Hyprland survive between client connections
- ✅ **Stop protection**: Background sessions ignore stop requests

### **What Does NOT Work (Session Sharing Illusion):**
- ❌ **Multiple simultaneous clients**: Each new client kicks out the previous one
- ❌ **Dynamic resolution changes**: Video pipeline has resolution hardcoded at startup
- ❌ **True multi-client routing**: No per-client RTSP/encryption management
- ❌ **Client preference isolation**: New client overwrites session metadata

## Architectural Reality

### **Wolf Video Pipeline Constraints:**
1. **Resolution hardcoded at startup**: GStreamer pipelines baked with specific width/height
2. **Cannot restart video pipeline**: Would kill compositor and lose app state
3. **Compositor tied to resolution**: Wayland display mode set at container creation
4. **App state non-transferable**: Zed projects, Hyprland workspaces lost if compositor restarts

### **Current "Session Sharing" = Session Hijacking:**
```cpp
// When new client connects to existing session:
updated_session.display_mode.width = ss.video_width;    // Breaks previous client
updated_session.ip = ss.client_ip;                      // Steals RTSP routing
updated_session.rtsp_fake_ip = ss.rtsp_fake_ip;         // Overwrites networking
```

**Result**: Previous client disconnected, new client gets old resolution (not what they requested)

## Simplified Configuration

Replaced 3 confusing toggles with 1 clear toggle:

```toml
[session_management]
auto_persistent_sessions = true  # Enable persistent background sessions with single-client replacement
```

## Behavior Changes

### `reuse_existing_sessions = true`
- Before creating new session, check if session already exists for same `app_id`
- If exists and container is running → return existing session ID
- If not exists → create new session (existing behavior)
- **NEW**: Resolution/refresh rate updates to match latest connecting client

### `clients_share_sessions = true`
- Multiple clients can connect to same session simultaneously
- All input events from all clients get sent to same Wayland session (like multiple keyboards/mice)
- Sessions persist when clients disconnect (perfect for AI agents!)
- **NEW**: Sessions continue running in background until explicitly stopped

### Background Session Behavior
- **Helix Background Client**: Uses dedicated `helix-background-launcher` client ID
- **Default Resolution**: 4K@120fps for background sessions (gets updated when real clients connect)
- **Auto-start**: Background sessions created immediately when Personal Dev environments are created
- **Persistent**: Sessions run continuously until explicitly stopped (not when clients disconnect)

## Moonlight Protocol Integration

### App Status Reporting
- **Running Apps**: Apps with active containers should show as "running" in Moonlight UI
- **UI Controls**: Running apps show "Resume" and "Stop" buttons instead of "Start"
- **Stopped Apps**: Apps without containers show "Start" button
- **Client Disconnect**: Does NOT stop the container/session (background persistence)
- **Explicit Stop**: Only stops container when user explicitly clicks "Stop" button

### Container State Mapping
- **Container Running**: Moonlight shows app as "Running" status
- **Container Stopped**: Moonlight shows app as "Stopped" status
- **Resume Behavior**: Connecting to running app resumes existing session
- **Start Behavior**: Connecting to stopped app starts new container

### Screenshot/Preview Feature
- **Live Thumbnails**: Helix UI shows live screenshots of running sessions
- **Update Frequency**: Screenshots refresh every 10 seconds automatically
- **Video Feed Sampling**: Wolf samples from existing video stream (no additional capture overhead)
- **API Endpoint**: `/api/v1/sessions/{session_id}/screenshot` returns JPEG thumbnail
- **Default Size**: 320x180 thumbnails for UI responsiveness

## Implementation Plan

### 1. Configuration Changes
- [x] Add config flags to `state/data-structures.hpp` Config struct
- [x] Add TOML parsing for new flags in `state/configTOML.cpp`
- [x] Enable flags in Helix Wolf config

### 2. Session Lookup Logic
- [x] Add `find_session_by_app_id()` helper function
- [x] Modify `endpoint_StreamSessionAdd()` to check existing sessions when `reuse_existing_sessions=true`
- [x] Add resolution updating for existing sessions

### 3. Session Persistence
- [ ] Modify session cleanup logic to respect `clients_share_sessions` flag
- [ ] Modify Moonlight protocol endpoints to report container status correctly
- [ ] Update app status API to reflect running/stopped state

### 4. Helix Integration
- [x] Create dedicated background client ID in Helix (`helix-background-launcher`)
- [x] Enable session reuse flags in Wolf config used by Helix
- [x] Set 4K@120fps defaults for background sessions

### 5. Build Fix
- [ ] Fix compilation errors in Wolf exceptions.h

## Current Issues
- Wolf build failing due to compilation errors in exceptions.h
- Need to implement Moonlight protocol status updates for running containers

## Backward Compatibility
- Default behavior unchanged (both flags default to false)
- Existing Wolf installations continue working as before
- Opt-in to new behavior via configuration

## Testing
- [ ] Fix Wolf build and test session reuse
- [ ] Test multiple clients connecting to same session
- [ ] Test session persistence after client disconnect
- [ ] Test Moonlight UI shows correct running/stopped status