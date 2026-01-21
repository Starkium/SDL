/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/**
 * SDL GPU WebXR Support
 *
 * WebXR is fundamentally different from OpenXR:
 * - JavaScript Promise-based API vs C function calls
 * - Tight integration with WebGL/WebGPU contexts
 * - Browser security model (requires user gesture to start session)
 * - Frame loop driven by requestAnimationFrame, not polling
 *
 * Architecture (following Godot's proven pattern):
 * 1. JS side: library_sdl_webxr.js - WebXR session management, layer creation
 * 2. C side: This file - Thin wrapper exposing SDL-style API
 * 3. Bridge: Emscripten library linkage + extern declarations
 *
 * The key trick is monkey-patching requestAnimationFrame:
 * - When XR session is active, use XRSession.requestAnimationFrame
 * - This gives us XRFrame with pose data each frame
 * - SDL's main loop continues working normally
 */

#include "SDL_internal.h"

#ifdef SDL_PLATFORM_EMSCRIPTEN

#include <SDL3/SDL_webxr.h>
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

/*
 * External declarations for JS library functions
 * These are implemented in library_sdl_webxr.js
 */
extern int sdl_webxr_is_supported(void);
extern void sdl_webxr_is_session_supported(int mode, void (*callback)(int, int));
extern void sdl_webxr_initialize(
    int mode,
    int refSpaceType,
    void (*onStarted)(const char *),
    void (*onEnded)(void),
    void (*onFailed)(const char *),
    void (*onFrame)(void)
);
extern void sdl_webxr_uninitialize(void);
extern int sdl_webxr_get_session_state(void);
extern int sdl_webxr_get_view_count(void);
extern int sdl_webxr_get_render_target_size(int *outSize);
extern int sdl_webxr_get_transform_for_view(int viewIndex, float *outMatrix);
extern int sdl_webxr_get_projection_for_view(int viewIndex, float *outMatrix);
extern int sdl_webxr_get_viewport_for_view(int viewIndex, int *outViewport);
extern unsigned int sdl_webxr_get_framebuffer(void);
extern unsigned int sdl_webxr_get_color_texture_for_view(int viewIndex);
extern unsigned int sdl_webxr_get_depth_texture_for_view(int viewIndex);

/*
 * Internal session structure
 */
struct SDL_WebXRSession {
    SDL_WebXRSessionMode mode;
    SDL_WebXRReferenceSpaceType refSpaceType;
    SDL_WebXRSessionState state;
    char refSpaceTypeString[32];
    bool initialized;
};

/* Global session (WebXR only supports one session at a time) */
static SDL_WebXRSession *g_session = NULL;

/*
 * Callbacks from JavaScript
 * These are called by the JS library via function pointers
 */

EMSCRIPTEN_KEEPALIVE
static void webxr_on_session_started(const char *refSpaceType)
{
    if (g_session) {
        if (refSpaceType) {
            SDL_strlcpy(g_session->refSpaceTypeString, refSpaceType, 
                       sizeof(g_session->refSpaceTypeString));
        }
        g_session->state = SDL_WEBXR_STATE_RUNNING;
        g_session->initialized = true;
    }
}

EMSCRIPTEN_KEEPALIVE
static void webxr_on_session_ended(void)
{
    if (g_session) {
        g_session->state = SDL_WEBXR_STATE_ENDED;
        g_session->initialized = false;
    }
}

EMSCRIPTEN_KEEPALIVE
static void webxr_on_session_failed(const char *message)
{
    if (g_session) {
        g_session->state = SDL_WEBXR_STATE_IDLE;
        g_session->initialized = false;
        if (message) {
            SDL_SetError("WebXR session failed: %s", message);
        }
    }
}

EMSCRIPTEN_KEEPALIVE
static void webxr_on_frame(void)
{
    /* Frame callback - currently unused, frame data accessed via getters */
}

/*
 * Public API Implementation
 */

bool SDL_WebXR_IsAvailable(void)
{
    return sdl_webxr_is_supported() != 0;
}

/* Async session mode check - for now we provide a sync approximation */
static int g_mode_supported[3] = { -1, -1, -1 }; /* -1 = unknown, 0 = no, 1 = yes */

static void mode_supported_callback(int mode, int supported)
{
    if (mode >= 0 && mode < 3) {
        g_mode_supported[mode] = supported;
    }
}

bool SDL_WebXR_IsSessionModeSupported(SDL_WebXRSessionMode mode)
{
    if (!SDL_WebXR_IsAvailable()) {
        return false;
    }
    
    /* Check cached result */
    if (mode >= 0 && mode < 3 && g_mode_supported[mode] >= 0) {
        return g_mode_supported[mode] != 0;
    }
    
    /* Trigger async check (result will be available next call) */
    sdl_webxr_is_session_supported(mode, mode_supported_callback);
    
    /* Optimistically return true for immersive-vr (most common) */
    if (mode == SDL_WEBXR_SESSION_IMMERSIVE_VR) {
        return true;
    }
    
    return false;
}

SDL_WebXRSession *SDL_WebXR_RequestSession(
    SDL_WebXRSessionMode mode,
    SDL_WebXRReferenceSpaceType refSpaceType)
{
    if (!SDL_WebXR_IsAvailable()) {
        SDL_SetError("WebXR is not available");
        return NULL;
    }
    
    if (g_session) {
        SDL_SetError("WebXR session already active");
        return NULL;
    }
    
    g_session = SDL_calloc(1, sizeof(SDL_WebXRSession));
    if (!g_session) {
        return NULL;
    }
    
    g_session->mode = mode;
    g_session->refSpaceType = refSpaceType;
    g_session->state = SDL_WEBXR_STATE_REQUESTING;
    g_session->initialized = false;
    
    /* Initialize via JavaScript library */
    sdl_webxr_initialize(
        mode,
        refSpaceType,
        webxr_on_session_started,
        webxr_on_session_ended,
        webxr_on_session_failed,
        webxr_on_frame
    );
    
    return g_session;
}

SDL_WebXRSessionState SDL_WebXR_GetSessionState(SDL_WebXRSession *session)
{
    if (!session) {
        return SDL_WEBXR_STATE_IDLE;
    }
    
    /* Sync state from JS side */
    session->state = (SDL_WebXRSessionState)sdl_webxr_get_session_state();
    return session->state;
}

void SDL_WebXR_EndSession(SDL_WebXRSession *session)
{
    if (!session) {
        return;
    }
    
    sdl_webxr_uninitialize();
    
    if (g_session == session) {
        g_session = NULL;
    }
    
    SDL_free(session);
}

bool SDL_WebXR_BindGPUDevice(
    SDL_WebXRSession *session,
    SDL_GPUDevice *device)
{
    if (!session || !device) {
        SDL_SetError("Invalid session or device");
        return false;
    }
    
    /* The JS library handles WebGL binding automatically during initialization.
     * For SDL GPU, we need to ensure the device is using WebGL backend.
     * This is currently a no-op since the binding happens in JS. */
    
    /* TODO: Validate that device is using WebGL/GLES backend */
    
    return true;
}

bool SDL_WebXR_BeginFrame(
    SDL_WebXRSession *session,
    SDL_WebXRFrame *frame)
{
    if (!session || !frame) {
        return false;
    }
    
    /* Check if we're in a valid state for rendering */
    SDL_WebXRSessionState state = SDL_WebXR_GetSessionState(session);
    if (state != SDL_WEBXR_STATE_RUNNING && 
        state != SDL_WEBXR_STATE_VISIBLE &&
        state != SDL_WEBXR_STATE_VISIBLE_BLURRED) {
        return false;
    }
    
    /* Get view count */
    frame->viewCount = sdl_webxr_get_view_count();
    if (frame->viewCount <= 0) {
        return false;
    }
    if (frame->viewCount > 2) {
        frame->viewCount = 2; /* Clamp to stereo max */
    }
    
    /* Get data for each view */
    for (int i = 0; i < frame->viewCount; i++) {
        SDL_WebXRView *view = &frame->views[i];
        
        /* Get projection matrix */
        if (!sdl_webxr_get_projection_for_view(i, view->projectionMatrix)) {
            SDL_memset(view->projectionMatrix, 0, sizeof(view->projectionMatrix));
        }
        
        /* Get view/eye matrix */
        if (!sdl_webxr_get_transform_for_view(i, view->viewMatrix)) {
            SDL_memset(view->viewMatrix, 0, sizeof(view->viewMatrix));
        }
        
        /* Get viewport */
        int viewport[4] = { 0, 0, 0, 0 };
        if (sdl_webxr_get_viewport_for_view(i, viewport)) {
            view->viewportX = viewport[0];
            view->viewportY = viewport[1];
            view->viewportWidth = viewport[2];
            view->viewportHeight = viewport[3];
        } else {
            view->viewportX = 0;
            view->viewportY = 0;
            view->viewportWidth = 0;
            view->viewportHeight = 0;
        }
    }
    
    /* Get predicted display time (not directly available, use 0) */
    frame->predictedDisplayTime = 0.0;
    
    return true;
}

void SDL_WebXR_EndFrame(SDL_WebXRSession *session)
{
    if (!session) {
        return;
    }
    
    /* The frame is automatically submitted when the XR requestAnimationFrame
     * callback returns. No explicit end-frame call needed for WebXR. */
}

/*
 * Additional helper functions for advanced usage
 */

int SDL_WebXR_GetViewCount(SDL_WebXRSession *session)
{
    if (!session) {
        return 0;
    }
    return sdl_webxr_get_view_count();
}

bool SDL_WebXR_GetRenderTargetSize(SDL_WebXRSession *session, int *width, int *height)
{
    if (!session || !width || !height) {
        return false;
    }
    
    int size[2] = { 0, 0 };
    if (!sdl_webxr_get_render_target_size(size)) {
        return false;
    }
    
    *width = size[0];
    *height = size[1];
    return true;
}

bool SDL_WebXR_GetHeadTransform(SDL_WebXRSession *session, float *matrix)
{
    if (!session || !matrix) {
        return false;
    }
    
    return sdl_webxr_get_transform_for_view(-1, matrix) != 0;
}

unsigned int SDL_WebXR_GetFramebuffer(SDL_WebXRSession *session)
{
    if (!session) {
        return 0;
    }
    return sdl_webxr_get_framebuffer();
}

unsigned int SDL_WebXR_GetColorTextureForView(SDL_WebXRSession *session, int viewIndex)
{
    if (!session) {
        return 0;
    }
    return sdl_webxr_get_color_texture_for_view(viewIndex);
}

unsigned int SDL_WebXR_GetDepthTextureForView(SDL_WebXRSession *session, int viewIndex)
{
    if (!session) {
        return 0;
    }
    return sdl_webxr_get_depth_texture_for_view(viewIndex);
}

#else /* !SDL_PLATFORM_EMSCRIPTEN */

#include <SDL3/SDL_webxr.h>

/*
 * Stub implementations for non-Emscripten builds
 */

bool SDL_WebXR_IsAvailable(void)
{
    return false;
}

bool SDL_WebXR_IsSessionModeSupported(SDL_WebXRSessionMode mode)
{
    (void)mode;
    return false;
}

SDL_WebXRSession *SDL_WebXR_RequestSession(
    SDL_WebXRSessionMode mode,
    SDL_WebXRReferenceSpaceType refSpaceType)
{
    (void)mode;
    (void)refSpaceType;
    SDL_SetError("WebXR is only available on Emscripten");
    return NULL;
}

SDL_WebXRSessionState SDL_WebXR_GetSessionState(SDL_WebXRSession *session)
{
    (void)session;
    return SDL_WEBXR_STATE_IDLE;
}

void SDL_WebXR_EndSession(SDL_WebXRSession *session)
{
    (void)session;
}

bool SDL_WebXR_BindGPUDevice(
    SDL_WebXRSession *session,
    SDL_GPUDevice *device)
{
    (void)session;
    (void)device;
    return false;
}

bool SDL_WebXR_BeginFrame(
    SDL_WebXRSession *session,
    SDL_WebXRFrame *frame)
{
    (void)session;
    (void)frame;
    return false;
}

void SDL_WebXR_EndFrame(SDL_WebXRSession *session)
{
    (void)session;
}

int SDL_WebXR_GetViewCount(SDL_WebXRSession *session)
{
    (void)session;
    return 0;
}

bool SDL_WebXR_GetRenderTargetSize(SDL_WebXRSession *session, int *width, int *height)
{
    (void)session;
    (void)width;
    (void)height;
    return false;
}

bool SDL_WebXR_GetHeadTransform(SDL_WebXRSession *session, float *matrix)
{
    (void)session;
    (void)matrix;
    return false;
}

unsigned int SDL_WebXR_GetFramebuffer(SDL_WebXRSession *session)
{
    (void)session;
    return 0;
}

unsigned int SDL_WebXR_GetColorTextureForView(SDL_WebXRSession *session, int viewIndex)
{
    (void)session;
    (void)viewIndex;
    return 0;
}

unsigned int SDL_WebXR_GetDepthTextureForView(SDL_WebXRSession *session, int viewIndex)
{
    (void)session;
    (void)viewIndex;
    return 0;
}

#endif /* SDL_PLATFORM_EMSCRIPTEN */
