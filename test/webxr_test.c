/*
 * SDL WebXR Test Example
 * 
 * A minimal WebXR test that renders spinning cubes in VR.
 * Can be built with Emscripten and tested in a WebXR-capable browser.
 *
 * Build (Emscripten):
 *   emcc webxr_test.c -o webxr_test.html -s USE_SDL=3 -s FULL_ES3=1
 *
 * Test:
 *   1. Serve via HTTPS (required for WebXR)
 *   2. Open in Chrome/Edge with WebXR emulator OR connected VR headset
 *   3. Click "Enter VR" button
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef SDL_PLATFORM_EMSCRIPTEN
#include <SDL3/SDL_webxr.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <math.h>

/* Application state */
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static bool running = true;
static float animTime = 0.0f;

#ifdef SDL_PLATFORM_EMSCRIPTEN
static SDL_WebXRSession *xrSession = NULL;
static bool xrActive = false;
#endif

/* Simple matrix math */
typedef struct { float m[16]; } Mat4;

static Mat4 Mat4_Identity(void) {
    return (Mat4){{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
}

static Mat4 Mat4_Perspective(float fovY, float aspect, float nearZ, float farZ) {
    float tanHalfFov = tanf(fovY * 0.5f);
    Mat4 m = {{0}};
    m.m[0] = 1.0f / (aspect * tanHalfFov);
    m.m[5] = 1.0f / tanHalfFov;
    m.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m.m[11] = -1.0f;
    m.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

static Mat4 Mat4_RotationY(float rad) {
    float c = cosf(rad), s = sinf(rad);
    return (Mat4){{ c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1 }};
}

/* Draw a simple colored cube using SDL_Renderer (for demonstration) */
static void DrawCube(float x, float y, float z, float size, Uint8 r, Uint8 g, Uint8 b) {
    /* This is a VERY simplified cube drawing - just draws a colored square */
    /* In a real VR app, you'd use SDL_GPU with proper 3D rendering */
    float screenX = 400 + x * 100;
    float screenY = 300 - y * 100;
    float screenSize = size * 50 * (2.0f / (2.0f - z));
    
    SDL_FRect rect = {
        screenX - screenSize/2,
        screenY - screenSize/2,
        screenSize,
        screenSize
    };
    
    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &rect);
}

/* Render the scene */
static void RenderScene(float viewX, float viewY, float viewZ) {
    /* Clear background */
    SDL_SetRenderDrawColor(renderer, 32, 32, 64, 255);
    SDL_RenderClear(renderer);
    
    /* Draw spinning cubes */
    float angle = animTime;
    
    /* Center cube (red) */
    float cx = sinf(angle) * 0.3f;
    float cy = cosf(angle * 0.7f) * 0.2f;
    DrawCube(cx + viewX, cy + viewY, -2.0f + viewZ, 0.5f, 255, 100, 100);
    
    /* Orbiting cubes */
    for (int i = 0; i < 4; i++) {
        float a = angle + i * (3.14159f * 0.5f);
        float ox = sinf(a) * 1.2f;
        float oz = cosf(a) * 1.2f - 2.0f;
        float oy = sinf(a * 2.0f) * 0.3f;
        
        Uint8 colors[4][3] = {{100,255,100}, {100,100,255}, {255,255,100}, {255,100,255}};
        DrawCube(ox + viewX, oy + viewY, oz + viewZ, 0.3f, colors[i][0], colors[i][1], colors[i][2]);
    }
}

#ifdef SDL_PLATFORM_EMSCRIPTEN
/* Try to start WebXR session - must be called from user gesture */
static void TryStartXR(void) {
    if (xrSession) {
        SDL_Log("XR session already active");
        return;
    }
    
    if (!SDL_WebXR_IsAvailable()) {
        SDL_Log("WebXR not available in this browser");
        return;
    }
    
    SDL_Log("Requesting WebXR session...");
    xrSession = SDL_WebXR_RequestSession(
        SDL_WEBXR_SESSION_IMMERSIVE_VR,
        SDL_WEBXR_REFSPACE_LOCAL_FLOOR
    );
    
    if (xrSession) {
        SDL_Log("WebXR session requested, waiting for browser approval...");
    } else {
        SDL_Log("Failed to request WebXR session: %s", SDL_GetError());
    }
}

/* Main loop for Emscripten */
static void MainLoop(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        }
        else if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.scancode == SDL_SCANCODE_SPACE || 
                event.key.scancode == SDL_SCANCODE_RETURN) {
                TryStartXR();
            }
            else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                if (xrSession) {
                    SDL_WebXR_EndSession(xrSession);
                    xrSession = NULL;
                    xrActive = false;
                }
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            /* User gesture - can start XR */
            TryStartXR();
        }
    }
    
    /* Update animation */
    animTime += 0.016f;
    
    /* Check XR session state */
    if (xrSession) {
        SDL_WebXRSessionState state = SDL_WebXR_GetSessionState(xrSession);
        
        if (state == SDL_WEBXR_STATE_RUNNING || state == SDL_WEBXR_STATE_VISIBLE) {
            xrActive = true;
            
            /* Get XR frame data */
            SDL_WebXRFrame frame;
            if (SDL_WebXR_BeginFrame(xrSession, &frame)) {
                /* In a real app, you'd render to the XR framebuffer here */
                /* For now, just log that we're getting frames */
                static int frameCount = 0;
                if (++frameCount % 60 == 0) {
                    SDL_Log("XR Frame: %d views, display time: %.3f", 
                            frame.viewCount, frame.predictedDisplayTime);
                }
                
                /* Render each view (eye) */
                for (int i = 0; i < frame.viewCount; i++) {
                    SDL_WebXRView *view = &frame.views[i];
                    
                    /* In a real app, you'd:
                     * 1. Bind the XR framebuffer
                     * 2. Set viewport to view->viewportX/Y/Width/Height
                     * 3. Use view->projectionMatrix and view->viewMatrix
                     * 4. Render 3D scene
                     */
                    
                    /* For this demo, we just show we're getting view data */
                    if (frameCount % 300 == 0) {
                        SDL_Log("View %d: viewport=(%d,%d %dx%d)", 
                                i, view->viewportX, view->viewportY,
                                view->viewportWidth, view->viewportHeight);
                    }
                }
                
                SDL_WebXR_EndFrame(xrSession);
            }
        }
        else if (state == SDL_WEBXR_STATE_ENDED) {
            SDL_Log("XR session ended");
            SDL_WebXR_EndSession(xrSession);
            xrSession = NULL;
            xrActive = false;
        }
    }
    
    /* Render to window (non-XR preview or 2D fallback) */
    if (!xrActive) {
        RenderScene(0, 0, 0);
        
        /* Draw instructions */
        /* Note: SDL_Renderer doesn't have text by default */
        /* In a real app you'd use SDL_ttf or similar */
    }
    
    SDL_RenderPresent(renderer);
    
    if (!running) {
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    
    window = SDL_CreateWindow("SDL WebXR Test", 800, 600, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    
    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }
    
#ifdef SDL_PLATFORM_EMSCRIPTEN
    SDL_Log("=== SDL WebXR Test ===");
    SDL_Log("Click or press SPACE/ENTER to start VR");
    SDL_Log("Press ESC to exit VR");
    
    if (SDL_WebXR_IsAvailable()) {
        SDL_Log("WebXR is available!");
        if (SDL_WebXR_IsSessionModeSupported(SDL_WEBXR_SESSION_IMMERSIVE_VR)) {
            SDL_Log("Immersive VR is supported");
        }
    } else {
        SDL_Log("WebXR not available - install WebXR emulator extension");
    }
    
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    /* Non-Emscripten: just render preview */
    SDL_Log("WebXR only available on Emscripten builds");
    SDL_Log("This build shows a 2D preview only");
    
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        
        animTime += 0.016f;
        RenderScene(0, 0, 0);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
#endif
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
