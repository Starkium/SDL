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
 * # CategoryWebXR
 *
 * Functions for WebXR VR/AR support in SDL_gpu on Emscripten.
 *
 * WebXR is the browser standard for VR and AR experiences.
 * Unlike OpenXR (native), WebXR is a JavaScript API that requires
 * special handling via Emscripten's JS interop.
 *
 * ## Key Differences from OpenXR:
 * - Async/Promise-based vs synchronous C calls
 * - WebGL/WebGPU only (no Vulkan/D3D12)
 * - Browser security model (user gesture required)
 * - Frame loop driven by requestAnimationFrame
 *
 * ## Usage:
 * 1. Check SDL_WebXR_IsAvailable()
 * 2. Request session with SDL_WebXR_RequestSession()
 * 3. Poll state with SDL_WebXR_GetSessionState()
 * 4. When RUNNING, use SDL_WebXR_BeginFrame() / SDL_WebXR_EndFrame()
 */

#ifndef SDL_webxr_h_
#define SDL_webxr_h_

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_gpu.h>

#include <SDL3/SDL_begin_code.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * WebXR session modes.
 */
typedef enum SDL_WebXRSessionMode {
    SDL_WEBXR_SESSION_INLINE,       /**< Non-immersive, rendered in page */
    SDL_WEBXR_SESSION_IMMERSIVE_VR, /**< Full VR headset takeover */
    SDL_WEBXR_SESSION_IMMERSIVE_AR  /**< AR pass-through */
} SDL_WebXRSessionMode;

/**
 * WebXR reference space types.
 */
typedef enum SDL_WebXRReferenceSpaceType {
    SDL_WEBXR_REFSPACE_VIEWER,        /**< Head-locked, no tracking */
    SDL_WEBXR_REFSPACE_LOCAL,         /**< Seated, origin at initial head position */
    SDL_WEBXR_REFSPACE_LOCAL_FLOOR,   /**< Standing, origin at floor level */
    SDL_WEBXR_REFSPACE_BOUNDED_FLOOR, /**< Room-scale with boundaries */
    SDL_WEBXR_REFSPACE_UNBOUNDED      /**< Large-scale AR experiences */
} SDL_WebXRReferenceSpaceType;

/**
 * WebXR session states.
 */
typedef enum SDL_WebXRSessionState {
    SDL_WEBXR_STATE_IDLE,            /**< No session */
    SDL_WEBXR_STATE_REQUESTING,      /**< Session requested, waiting for browser */
    SDL_WEBXR_STATE_READY,           /**< Session created, setting up */
    SDL_WEBXR_STATE_RUNNING,         /**< Session running, can render */
    SDL_WEBXR_STATE_VISIBLE,         /**< Content visible to user */
    SDL_WEBXR_STATE_VISIBLE_BLURRED, /**< Visible but not focused */
    SDL_WEBXR_STATE_ENDED            /**< Session ended */
} SDL_WebXRSessionState;

/**
 * Opaque handle to a WebXR session.
 */
typedef struct SDL_WebXRSession SDL_WebXRSession;

/**
 * Per-eye view data for rendering.
 */
typedef struct SDL_WebXRView {
    float projectionMatrix[16]; /**< 4x4 projection matrix (column-major) */
    float viewMatrix[16];       /**< 4x4 view/eye matrix (column-major) */
    int viewportX;              /**< Viewport X offset */
    int viewportY;              /**< Viewport Y offset */
    int viewportWidth;          /**< Viewport width in pixels */
    int viewportHeight;         /**< Viewport height in pixels */
} SDL_WebXRView;

/**
 * Frame data provided each frame.
 */
typedef struct SDL_WebXRFrame {
    double predictedDisplayTime; /**< When frame will be displayed */
    int viewCount;               /**< Number of views (1 for mono, 2 for stereo) */
    SDL_WebXRView views[2];      /**< View data (up to 2 for stereo) */
} SDL_WebXRFrame;

/**
 * Check if WebXR is available in this browser.
 *
 * \returns true if WebXR is available, false otherwise.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_IsAvailable(void);

/**
 * Check if a specific session mode is supported.
 *
 * \param mode The session mode to check.
 * \returns true if the mode is supported, false otherwise.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_IsSessionModeSupported(SDL_WebXRSessionMode mode);

/**
 * Request a WebXR session.
 *
 * This is an async operation. The session will be in REQUESTING state
 * initially. Poll with SDL_WebXR_GetSessionState() to check when it's
 * ready.
 *
 * Note: For immersive sessions, this MUST be called from a user gesture
 * (click/tap event handler) due to browser security restrictions.
 *
 * \param mode The type of session to create.
 * \param refSpaceType The reference space type to use.
 * \returns A session handle, or NULL on error.
 *
 * \since This function is available since SDL 3.x.x.
 *
 * \sa SDL_WebXR_GetSessionState
 * \sa SDL_WebXR_EndSession
 */
extern SDL_DECLSPEC SDL_WebXRSession * SDLCALL SDL_WebXR_RequestSession(
    SDL_WebXRSessionMode mode,
    SDL_WebXRReferenceSpaceType refSpaceType);

/**
 * Get the current state of a WebXR session.
 *
 * \param session The session to query.
 * \returns The current session state.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC SDL_WebXRSessionState SDLCALL SDL_WebXR_GetSessionState(SDL_WebXRSession *session);

/**
 * End a WebXR session.
 *
 * \param session The session to end.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC void SDLCALL SDL_WebXR_EndSession(SDL_WebXRSession *session);

/**
 * Bind an SDL GPU device to the WebXR session.
 *
 * This creates the XRWebGLLayer and binds it to the session.
 * Must be called after session reaches READY state.
 *
 * \param session The WebXR session.
 * \param device The SDL GPU device (must be using WebGL backend).
 * \returns true on success, false on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_BindGPUDevice(
    SDL_WebXRSession *session,
    SDL_GPUDevice *device);

/**
 * Begin a WebXR frame.
 *
 * Call this at the start of your render loop when in XR mode.
 * Returns frame data including view matrices and viewports.
 *
 * \param session The WebXR session.
 * \param frame Pointer to receive frame data.
 * \returns true if a frame is available, false otherwise.
 *
 * \since This function is available since SDL 3.x.x.
 *
 * \sa SDL_WebXR_EndFrame
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_BeginFrame(
    SDL_WebXRSession *session,
    SDL_WebXRFrame *frame);

/**
 * End a WebXR frame and submit to compositor.
 *
 * \param session The WebXR session.
 *
 * \since This function is available since SDL 3.x.x.
 *
 * \sa SDL_WebXR_BeginFrame
 */
extern SDL_DECLSPEC void SDLCALL SDL_WebXR_EndFrame(SDL_WebXRSession *session);

/*
 * Additional helper functions for advanced WebGL interop
 */

/**
 * Get the number of views (eyes) for the current frame.
 *
 * \param session The WebXR session.
 * \returns The number of views (1 for mono, 2 for stereo), or 0 on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC int SDLCALL SDL_WebXR_GetViewCount(SDL_WebXRSession *session);

/**
 * Get the recommended render target size.
 *
 * \param session The WebXR session.
 * \param width Pointer to receive width in pixels.
 * \param height Pointer to receive height in pixels.
 * \returns true on success, false on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_GetRenderTargetSize(
    SDL_WebXRSession *session,
    int *width,
    int *height);

/**
 * Get the head/viewer transform matrix.
 *
 * \param session The WebXR session.
 * \param matrix Pointer to 16 floats to receive the 4x4 matrix (column-major).
 * \returns true on success, false on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC bool SDLCALL SDL_WebXR_GetHeadTransform(
    SDL_WebXRSession *session,
    float *matrix);

/**
 * Get the XRWebGLLayer framebuffer ID for direct WebGL rendering.
 *
 * This returns the WebGL framebuffer ID that should be bound before
 * rendering to the XR layer. Use this for direct WebGL interop.
 *
 * \param session The WebXR session.
 * \returns The WebGL framebuffer ID, or 0 on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC unsigned int SDLCALL SDL_WebXR_GetFramebuffer(SDL_WebXRSession *session);

/**
 * Get the color texture ID for a specific view.
 *
 * For XRProjectionLayer usage with XRWebGLBinding.
 *
 * \param session The WebXR session.
 * \param viewIndex The view index (0 for left eye, 1 for right eye).
 * \returns The WebGL texture ID, or 0 on error.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC unsigned int SDLCALL SDL_WebXR_GetColorTextureForView(
    SDL_WebXRSession *session,
    int viewIndex);

/**
 * Get the depth texture ID for a specific view.
 *
 * For XRProjectionLayer usage with XRWebGLBinding (if depth was requested).
 *
 * \param session The WebXR session.
 * \param viewIndex The view index (0 for left eye, 1 for right eye).
 * \returns The WebGL texture ID, or 0 if depth is not available.
 *
 * \since This function is available since SDL 3.x.x.
 */
extern SDL_DECLSPEC unsigned int SDLCALL SDL_WebXR_GetDepthTextureForView(
    SDL_WebXRSession *session,
    int viewIndex);

#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_webxr_h_ */
