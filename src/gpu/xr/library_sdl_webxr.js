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
 * SDL WebXR JavaScript Library
 * 
 * This library gets linked into Emscripten builds to provide WebXR support.
 * Modeled after Godot's library_godot_webxr.js
 * 
 * Key insight: WebXR requires monkey-patching requestAnimationFrame
 * to use XRSession.requestAnimationFrame when in XR mode.
 */

const SDLWebXR = {
    $SDLWebXR__deps: ['$MainLoop', '$GL'],
    $SDLWebXR: {
        // State
        session: null,
        gl: null,
        glBinding: null,
        baseLayer: null,
        projectionLayer: null,
        referenceSpace: null,
        frame: null,
        pose: null,
        viewCount: 1,
        
        // Session info
        sessionMode: null,
        sessionState: 0, // SDL_WEBXR_STATE_IDLE
        
        // Callbacks (function pointers from C)
        onSessionStarted: null,
        onSessionEnded: null,
        onSessionFailed: null,
        onFrame: null,
        
        // Original requestAnimationFrame
        origRequestAnimationFrame: null,
        
        // Session state enum (must match SDL_WebXRSessionState)
        STATE_IDLE: 0,
        STATE_REQUESTING: 1,
        STATE_READY: 2,
        STATE_RUNNING: 3,
        STATE_VISIBLE: 4,
        STATE_VISIBLE_BLURRED: 5,
        STATE_ENDED: 6,
        
        // Mode enum (must match SDL_WebXRSessionMode)
        MODE_INLINE: 0,
        MODE_IMMERSIVE_VR: 1,
        MODE_IMMERSIVE_AR: 2,
        
        // Reference space enum (must match SDL_WebXRReferenceSpaceType)
        REFSPACE_VIEWER: 0,
        REFSPACE_LOCAL: 1,
        REFSPACE_LOCAL_FLOOR: 2,
        REFSPACE_BOUNDED_FLOOR: 3,
        REFSPACE_UNBOUNDED: 4,
        
        // Convert enum to string
        getModeString: function(mode) {
            const modeMap = ['inline', 'immersive-vr', 'immersive-ar'];
            return modeMap[mode] || 'inline';
        },
        
        getRefSpaceString: function(refSpace) {
            const refSpaceMap = ['viewer', 'local', 'local-floor', 'bounded-floor', 'unbounded'];
            return refSpaceMap[refSpace] || 'local';
        },
        
        // Monkey-patch requestAnimationFrame like Godot does
        requestAnimationFrame: function(callback) {
            if (SDLWebXR.session && SDLWebXR.referenceSpace) {
                // Use XR's requestAnimationFrame
                SDLWebXR.session.requestAnimationFrame(function(time, frame) {
                    SDLWebXR.frame = frame;
                    SDLWebXR.pose = frame.getViewerPose(SDLWebXR.referenceSpace);
                    if (SDLWebXR.pose) {
                        SDLWebXR.viewCount = SDLWebXR.pose.views.length;
                    }
                    callback(time);
                    SDLWebXR.frame = null;
                    SDLWebXR.pose = null;
                });
            } else if (SDLWebXR.origRequestAnimationFrame) {
                // Fallback to original
                SDLWebXR.origRequestAnimationFrame(callback);
            }
        },
        
        // Install/uninstall monkey patch
        monkeyPatchRequestAnimationFrame: function(enable) {
            if (SDLWebXR.origRequestAnimationFrame === null) {
                SDLWebXR.origRequestAnimationFrame = MainLoop.requestAnimationFrame;
            }
            MainLoop.requestAnimationFrame = enable 
                ? SDLWebXR.requestAnimationFrame 
                : SDLWebXR.origRequestAnimationFrame;
        },
        
        // Pause and resume main loop to trigger monkey-patch
        pauseResumeMainLoop: function() {
            MainLoop.pause();
            MainLoop.resume();
        },
        
        // Get or create WebGL binding for XR layers
        getGLBinding: function() {
            if (!SDLWebXR.glBinding && SDLWebXR.session && SDLWebXR.gl) {
                try {
                    SDLWebXR.glBinding = new XRWebGLBinding(SDLWebXR.session, SDLWebXR.gl);
                } catch (e) {
                    console.warn('XRWebGLBinding not available, falling back to XRWebGLLayer');
                    return null;
                }
            }
            return SDLWebXR.glBinding;
        },
        
        // Create XR layer (projection layer if available, otherwise base layer)
        createLayer: function() {
            if (!SDLWebXR.session || !SDLWebXR.gl) {
                return null;
            }
            
            const binding = SDLWebXR.getGLBinding();
            
            // Try projection layer first (better performance)
            if (binding && binding.createProjectionLayer) {
                try {
                    SDLWebXR.projectionLayer = binding.createProjectionLayer({
                        textureType: 'texture',
                        colorFormat: SDLWebXR.gl.RGBA8,
                        depthFormat: SDLWebXR.gl.DEPTH_COMPONENT24,
                    });
                    SDLWebXR.session.updateRenderState({
                        layers: [SDLWebXR.projectionLayer]
                    });
                    console.log('SDL WebXR: Using projection layer');
                    return SDLWebXR.projectionLayer;
                } catch (e) {
                    console.warn('SDL WebXR: Projection layer failed, trying base layer:', e);
                }
            }
            
            // Fallback to XRWebGLLayer
            try {
                SDLWebXR.baseLayer = new XRWebGLLayer(SDLWebXR.session, SDLWebXR.gl);
                SDLWebXR.session.updateRenderState({
                    baseLayer: SDLWebXR.baseLayer
                });
                console.log('SDL WebXR: Using base layer (XRWebGLLayer)');
                return SDLWebXR.baseLayer;
            } catch (e) {
                console.error('SDL WebXR: Failed to create any XR layer:', e);
                return null;
            }
        },
    },
    
    /*
     * C-callable functions
     */
    
    // Check if WebXR is supported
    sdl_webxr_is_supported__sig: 'i',
    sdl_webxr_is_supported: function() {
        return navigator.xr ? 1 : 0;
    },
    
    // Check if a session mode is supported (async - returns immediately, result cached)
    sdl_webxr_is_session_supported__sig: 'iii',
    sdl_webxr_is_session_supported: function(mode, callback) {
        if (!navigator.xr) {
            {{{ makeDynCall('vii', 'callback') }}}(mode, 0);
            return;
        }
        
        const modeStr = SDLWebXR.getModeString(mode);
        navigator.xr.isSessionSupported(modeStr).then(function(supported) {
            {{{ makeDynCall('vii', 'callback') }}}(mode, supported ? 1 : 0);
        }).catch(function(err) {
            console.error('SDL WebXR: isSessionSupported failed:', err);
            {{{ makeDynCall('vii', 'callback') }}}(mode, 0);
        });
    },
    
    // Initialize WebXR (request session)
    sdl_webxr_initialize__sig: 'viiiiiii',
    sdl_webxr_initialize: function(
        mode, 
        refSpaceType,
        onStarted, 
        onEnded, 
        onFailed,
        onFrame
    ) {
        // Enable monkey-patching
        SDLWebXR.monkeyPatchRequestAnimationFrame(true);
        
        const modeStr = SDLWebXR.getModeString(mode);
        const refSpaceStr = SDLWebXR.getRefSpaceString(refSpaceType);
        
        // Store callbacks
        SDLWebXR.onSessionStarted = onStarted;
        SDLWebXR.onSessionEnded = onEnded;
        SDLWebXR.onSessionFailed = onFailed;
        SDLWebXR.onFrame = onFrame;
        SDLWebXR.sessionMode = mode;
        SDLWebXR.sessionState = SDLWebXR.STATE_REQUESTING;
        
        // Get WebGL context from Emscripten
        const contextHandle = Module.ctx;
        if (!contextHandle) {
            console.error('SDL WebXR: No WebGL context available');
            SDLWebXR.sessionState = SDLWebXR.STATE_IDLE;
            if (onFailed) {
                const msg = stringToNewUTF8('No WebGL context');
                {{{ makeDynCall('vi', 'onFailed') }}}(msg);
                _free(msg);
            }
            return;
        }
        SDLWebXR.gl = contextHandle;
        
        // Make WebGL context XR-compatible
        const makeXRCompatible = SDLWebXR.gl.makeXRCompatible 
            ? SDLWebXR.gl.makeXRCompatible() 
            : Promise.resolve();
        
        makeXRCompatible.then(function() {
            // Request XR session
            return navigator.xr.requestSession(modeStr, {
                requiredFeatures: [refSpaceStr],
            });
        }).then(function(session) {
            SDLWebXR.session = session;
            SDLWebXR.sessionState = SDLWebXR.STATE_READY;
            console.log('SDL WebXR: Session created:', modeStr);
            
            // Set up event listeners
            session.addEventListener('end', function() {
                SDLWebXR.sessionState = SDLWebXR.STATE_ENDED;
                SDLWebXR.monkeyPatchRequestAnimationFrame(false);
                SDLWebXR.pauseResumeMainLoop();
                
                if (SDLWebXR.onSessionEnded) {
                    {{{ makeDynCall('v', 'SDLWebXR.onSessionEnded') }}}();
                }
                
                // Cleanup
                SDLWebXR.session = null;
                SDLWebXR.glBinding = null;
                SDLWebXR.baseLayer = null;
                SDLWebXR.projectionLayer = null;
                SDLWebXR.referenceSpace = null;
                SDLWebXR.frame = null;
                SDLWebXR.pose = null;
            });
            
            session.addEventListener('visibilitychange', function() {
                if (session.visibilityState === 'visible') {
                    SDLWebXR.sessionState = SDLWebXR.STATE_VISIBLE;
                } else if (session.visibilityState === 'visible-blurred') {
                    SDLWebXR.sessionState = SDLWebXR.STATE_VISIBLE_BLURRED;
                } else if (session.visibilityState === 'hidden') {
                    // Keep RUNNING state for hidden
                }
            });
            
            // Create XR layer
            const layer = SDLWebXR.createLayer();
            if (!layer) {
                throw new Error('Failed to create XR layer');
            }
            
            // Request reference space
            return session.requestReferenceSpace(refSpaceStr);
        }).then(function(refSpace) {
            SDLWebXR.referenceSpace = refSpace;
            SDLWebXR.sessionState = SDLWebXR.STATE_RUNNING;
            console.log('SDL WebXR: Reference space acquired:', refSpaceStr);
            
            // Pause/resume to activate monkey-patch
            SDLWebXR.pauseResumeMainLoop();
            
            // Call started callback
            if (SDLWebXR.onSessionStarted) {
                const refSpaceTypeStr = stringToNewUTF8(refSpaceStr);
                {{{ makeDynCall('vi', 'SDLWebXR.onSessionStarted') }}}(refSpaceTypeStr);
                _free(refSpaceTypeStr);
            }
        }).catch(function(err) {
            console.error('SDL WebXR: Session request failed:', err);
            SDLWebXR.sessionState = SDLWebXR.STATE_IDLE;
            SDLWebXR.monkeyPatchRequestAnimationFrame(false);
            
            if (SDLWebXR.onSessionFailed) {
                const msg = stringToNewUTF8(err.message || 'Unknown error');
                {{{ makeDynCall('vi', 'SDLWebXR.onSessionFailed') }}}(msg);
                _free(msg);
            }
        });
    },
    
    // Uninitialize WebXR
    sdl_webxr_uninitialize__sig: 'v',
    sdl_webxr_uninitialize: function() {
        if (SDLWebXR.session) {
            SDLWebXR.session.end().catch(function() {
                // Ignore errors if session already ended
            });
        }
        
        SDLWebXR.monkeyPatchRequestAnimationFrame(false);
        SDLWebXR.pauseResumeMainLoop();
        
        SDLWebXR.session = null;
        SDLWebXR.glBinding = null;
        SDLWebXR.baseLayer = null;
        SDLWebXR.projectionLayer = null;
        SDLWebXR.referenceSpace = null;
        SDLWebXR.sessionState = SDLWebXR.STATE_IDLE;
    },
    
    // Get session state
    sdl_webxr_get_session_state__sig: 'i',
    sdl_webxr_get_session_state: function() {
        return SDLWebXR.sessionState;
    },
    
    // Get view count for current frame
    sdl_webxr_get_view_count__sig: 'i',
    sdl_webxr_get_view_count: function() {
        if (!SDLWebXR.pose) {
            return 1;
        }
        return SDLWebXR.pose.views.length;
    },
    
    // Get render target size
    sdl_webxr_get_render_target_size__sig: 'ii',
    sdl_webxr_get_render_target_size: function(outSize) {
        if (!SDLWebXR.baseLayer && !SDLWebXR.projectionLayer) {
            return 0;
        }
        
        let width = 0, height = 0;
        
        if (SDLWebXR.baseLayer) {
            width = SDLWebXR.baseLayer.framebufferWidth;
            height = SDLWebXR.baseLayer.framebufferHeight;
        } else if (SDLWebXR.projectionLayer && SDLWebXR.pose && SDLWebXR.pose.views.length > 0) {
            // For projection layer, get size from first view
            const view = SDLWebXR.pose.views[0];
            const binding = SDLWebXR.getGLBinding();
            if (binding) {
                const subImage = binding.getViewSubImage(SDLWebXR.projectionLayer, view);
                if (subImage && subImage.viewport) {
                    width = subImage.viewport.width;
                    height = subImage.viewport.height;
                }
            }
        }
        
        if (width === 0 || height === 0) {
            return 0;
        }
        
        {{{ makeSetValue('outSize', 0, 'width', 'i32') }}};
        {{{ makeSetValue('outSize', 4, 'height', 'i32') }}};
        return 1;
    },
    
    // Get transform for a view (-1 for head/viewer)
    sdl_webxr_get_transform_for_view__sig: 'iii',
    sdl_webxr_get_transform_for_view: function(viewIndex, outMatrix) {
        if (!SDLWebXR.pose) {
            return 0;
        }
        
        let matrix;
        if (viewIndex === -1) {
            // Head/viewer transform
            matrix = SDLWebXR.pose.transform.matrix;
        } else if (viewIndex >= 0 && viewIndex < SDLWebXR.pose.views.length) {
            matrix = SDLWebXR.pose.views[viewIndex].transform.matrix;
        } else {
            return 0;
        }
        
        // Copy 4x4 matrix (16 floats)
        for (let i = 0; i < 16; i++) {
            {{{ makeSetValue('outMatrix', 'i * 4', 'matrix[i]', 'float') }}};
        }
        return 1;
    },
    
    // Get projection matrix for a view
    sdl_webxr_get_projection_for_view__sig: 'iii',
    sdl_webxr_get_projection_for_view: function(viewIndex, outMatrix) {
        if (!SDLWebXR.pose || viewIndex < 0 || viewIndex >= SDLWebXR.pose.views.length) {
            return 0;
        }
        
        const matrix = SDLWebXR.pose.views[viewIndex].projectionMatrix;
        
        // Copy 4x4 matrix (16 floats)
        for (let i = 0; i < 16; i++) {
            {{{ makeSetValue('outMatrix', 'i * 4', 'matrix[i]', 'float') }}};
        }
        return 1;
    },
    
    // Get viewport for a view
    sdl_webxr_get_viewport_for_view__sig: 'iii',
    sdl_webxr_get_viewport_for_view: function(viewIndex, outViewport) {
        if (!SDLWebXR.pose || viewIndex < 0 || viewIndex >= SDLWebXR.pose.views.length) {
            return 0;
        }
        
        const view = SDLWebXR.pose.views[viewIndex];
        let viewport;
        
        if (SDLWebXR.baseLayer) {
            viewport = SDLWebXR.baseLayer.getViewport(view);
        } else if (SDLWebXR.projectionLayer) {
            const binding = SDLWebXR.getGLBinding();
            if (binding) {
                const subImage = binding.getViewSubImage(SDLWebXR.projectionLayer, view);
                viewport = subImage ? subImage.viewport : null;
            }
        }
        
        if (!viewport) {
            return 0;
        }
        
        {{{ makeSetValue('outViewport', 0, 'viewport.x', 'i32') }}};
        {{{ makeSetValue('outViewport', 4, 'viewport.y', 'i32') }}};
        {{{ makeSetValue('outViewport', 8, 'viewport.width', 'i32') }}};
        {{{ makeSetValue('outViewport', 12, 'viewport.height', 'i32') }}};
        return 1;
    },
    
    // Get framebuffer for XR rendering
    sdl_webxr_get_framebuffer__sig: 'i',
    sdl_webxr_get_framebuffer: function() {
        if (SDLWebXR.baseLayer) {
            return SDLWebXR.baseLayer.framebuffer;
        }
        // For projection layers, framebuffer is per-view (handled differently)
        return null;
    },
    
    // Get color texture for a view (projection layer only)
    sdl_webxr_get_color_texture_for_view__sig: 'ii',
    sdl_webxr_get_color_texture_for_view: function(viewIndex) {
        if (!SDLWebXR.projectionLayer || !SDLWebXR.pose || 
            viewIndex < 0 || viewIndex >= SDLWebXR.pose.views.length) {
            return 0;
        }
        
        const binding = SDLWebXR.getGLBinding();
        if (!binding) {
            return 0;
        }
        
        const view = SDLWebXR.pose.views[viewIndex];
        const subImage = binding.getViewSubImage(SDLWebXR.projectionLayer, view);
        
        return subImage ? subImage.colorTexture : 0;
    },
    
    // Get depth texture for a view (projection layer only)
    sdl_webxr_get_depth_texture_for_view__sig: 'ii',
    sdl_webxr_get_depth_texture_for_view: function(viewIndex) {
        if (!SDLWebXR.projectionLayer || !SDLWebXR.pose || 
            viewIndex < 0 || viewIndex >= SDLWebXR.pose.views.length) {
            return 0;
        }
        
        const binding = SDLWebXR.getGLBinding();
        if (!binding) {
            return 0;
        }
        
        const view = SDLWebXR.pose.views[viewIndex];
        const subImage = binding.getViewSubImage(SDLWebXR.projectionLayer, view);
        
        return subImage ? subImage.depthStencilTexture : 0;
    },
};

// Register the library
autoAddDeps(SDLWebXR, '$SDLWebXR');
mergeInto(LibraryManager.library, SDLWebXR);
