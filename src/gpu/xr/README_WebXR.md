# SDL WebXR Implementation Status

## Overview

This directory contains WebXR support for SDL on Emscripten/Web platforms. WebXR is the browser-standard API for VR/AR experiences, complementing the native OpenXR support.

## Architecture

Following Godot's proven pattern:

1. **library_sdl_webxr.js** - JavaScript library linked via Emscripten
   - WebXR session management
   - XR layer creation (XRWebGLLayer / XRProjectionLayer)
   - Monkey-patches `requestAnimationFrame` for XR frame timing
   
2. **SDL_gpu_webxr.c** - C implementation
   - Thin wrapper calling into JS library via `extern` declarations
   - `EMSCRIPTEN_KEEPALIVE` callbacks for session events
   - Stub implementations for non-Emscripten builds

3. **SDL_webxr.h** - Public API header

## Current Status

### ✅ Implemented
- [x] WebXR session management (request, state tracking, end)
- [x] Reference space support (viewer, local, local-floor, bounded-floor, unbounded)
- [x] Session mode support (inline, immersive-vr, immersive-ar)
- [x] View/eye matrix retrieval
- [x] Projection matrix retrieval
- [x] Viewport retrieval per view
- [x] XRWebGLLayer support (fallback, widely compatible)
- [x] XRProjectionLayer support (better performance when available)
- [x] Monkey-patched requestAnimationFrame for XR frame timing
- [x] CMake integration for Emscripten builds

### ⚠️ Partially Implemented / Needs Testing
- [ ] `SDL_WebXR_BindGPUDevice()` - Currently no-op, needs validation
- [ ] Depth texture support (XRProjectionLayer only)
- [ ] Visibility state callbacks

### ❌ Not Yet Implemented
- [ ] **Input handling** (controllers, hands, gaze)
  - WebXR has XRInputSource, XRInputSourceArray
  - Controller tracking via XRInputSource.gamepad
  - Hand tracking via XRHand API
- [ ] **Haptics** (XRInputSource.gamepad.hapticActuators)
- [ ] **Hit testing** (XRHitTestSource for AR)
- [ ] **Anchors** (XRAnchor for AR)
- [ ] **WebGPU support** (XRGPUBinding) - See below
- [ ] **Depth sensing** (XRDepthInformation for AR)
- [ ] **Light estimation** (XRLightProbe for AR)

## WebGPU Support Status

**Current State: NOT SUPPORTED**

WebGPU + WebXR uses a different binding mechanism than WebGL:

```javascript
// WebGL approach (current)
const glBinding = new XRWebGLBinding(session, gl);

// WebGPU approach (not implemented)
const gpuBinding = new XRGPUBinding(session, gpuDevice);
const layer = gpuBinding.createProjectionLayer({ format: 'rgba8unorm' });
```

### Requirements for WebGPU Support:
1. SDL GPU needs a WebGPU backend (currently only D3D12/Metal/Vulkan)
2. XRGPUBinding API in library_sdl_webxr.js
3. Different texture format handling (GPUTextureFormat vs WebGL)

### WebGPU Resources:
- W3C WebXR WebGPU Binding: https://immersive-web.github.io/webxr-webgpu-binding/
- Chromium implementation tracking: https://chromestatus.com/feature/5722055207288832

## Comparison with OpenXR Implementation

| Feature | OpenXR | WebXR |
|---------|--------|-------|
| Session management | xrCreateSession/xrEndSession | Promise-based requestSession |
| Frame timing | xrWaitFrame/xrBeginFrame/xrEndFrame | XRSession.requestAnimationFrame |
| Swapchain | Manual via xrCreateSwapchain | Automatic via XRWebGLLayer |
| Graphics binding | XR_KHR_vulkan_enable2, etc. | XRWebGLBinding / XRGPUBinding |
| Input | XrActionSet, XrAction | XRInputSource, XRInputSourceArray |
| Reference spaces | xrCreateReferenceSpace | session.requestReferenceSpace |

## Testing

### Requirements
- Modern browser with WebXR support (Chrome, Edge, Firefox)
- HTTPS server (WebXR requires secure context)
- VR headset OR WebXR emulator extension

### WebXR Emulators
- [WebXR API Emulator (Chrome/Firefox extension)](https://github.com/nicholasareed/webxr-api-emulator)
- [Immersive Web Emulator](https://github.com/nicholasareed/immersive-web-emulator)

### Test Deploy Script
See `test/emscripten/deploy_webxr_test.py` (TODO: create this)

## Example Usage

```c
#include <SDL3/SDL.h>
#include <SDL3/SDL_webxr.h>

// Check availability
if (SDL_WebXR_IsAvailable()) {
    // Request VR session (must be called from user gesture!)
    SDL_WebXRSession *session = SDL_WebXR_RequestSession(
        SDL_WEBXR_SESSION_IMMERSIVE_VR,
        SDL_WEBXR_REFSPACE_LOCAL_FLOOR
    );
    
    // Main loop
    while (running) {
        SDL_WebXRSessionState state = SDL_WebXR_GetSessionState(session);
        
        if (state == SDL_WEBXR_STATE_RUNNING) {
            SDL_WebXRFrame frame;
            if (SDL_WebXR_BeginFrame(session, &frame)) {
                // Render each eye
                for (int i = 0; i < frame.viewCount; i++) {
                    // Use frame.views[i].projectionMatrix
                    // Use frame.views[i].viewMatrix
                    // Render to frame.views[i].viewport
                }
                SDL_WebXR_EndFrame(session);
            }
        }
    }
    
    SDL_WebXR_EndSession(session);
}
```

## Build Configuration

CMake automatically links the JS library for Emscripten builds:
```cmake
# In SDL's CMakeLists.txt (EMSCRIPTEN section):
target_link_options(SDL3-static INTERFACE "SHELL:--js-library ${SDL_WEBXR_JS_LIBRARY}")
```

## References

- [WebXR Device API](https://immersive-web.github.io/webxr/)
- [WebXR Samples](https://immersive-web.github.io/webxr-samples/)
- [Godot WebXR Implementation](https://github.com/nicholasareed/godot/tree/master/platform/web/js/libs)
- [Three.js WebXR](https://threejs.org/docs/#api/en/renderers/webxr/WebXRManager)
