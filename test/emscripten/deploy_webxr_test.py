#!/usr/bin/env python3
"""
SDL WebXR Test Deployment Script

This script helps build and deploy WebXR test applications for browser testing.
WebXR requires HTTPS, so we provide options for local testing with self-signed certs.

Usage:
    python deploy_webxr_test.py build    # Build with Emscripten
    python deploy_webxr_test.py serve    # Start HTTPS server
    python deploy_webxr_test.py all      # Build and serve

Requirements:
    - Emscripten SDK (emcc in PATH)
    - Python 3.7+
    - openssl (for generating self-signed certs)

For testing:
    1. Install WebXR API Emulator browser extension
    2. Run this script with 'all' argument
    3. Open https://localhost:8443/webxr_test.html
    4. Click to enter VR mode
"""

import argparse
import http.server
import os
import shutil
import ssl
import subprocess
import sys
from pathlib import Path

# Configuration
SCRIPT_DIR = Path(__file__).parent
SDL_ROOT = SCRIPT_DIR.parent.parent
BUILD_DIR = SCRIPT_DIR / "webxr_build"
CERT_DIR = BUILD_DIR / "certs"
HTTPS_PORT = 8443

def run_command(cmd, cwd=None, check=True):
    """Run a command and return the result."""
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed with code {result.returncode}")
    return result

def check_emscripten():
    """Check if Emscripten is available."""
    try:
        result = run_command(["emcc", "--version"], check=False)
        if result.returncode == 0:
            print("✓ Emscripten found")
            return True
    except FileNotFoundError:
        pass
    print("✗ Emscripten not found. Please install and activate emsdk.")
    return False

def generate_self_signed_cert():
    """Generate self-signed certificate for HTTPS testing."""
    CERT_DIR.mkdir(parents=True, exist_ok=True)
    
    key_file = CERT_DIR / "localhost.key"
    cert_file = CERT_DIR / "localhost.crt"
    
    if key_file.exists() and cert_file.exists():
        print("✓ Using existing self-signed certificate")
        return str(cert_file), str(key_file)
    
    print("Generating self-signed certificate...")
    
    # Generate private key
    run_command([
        "openssl", "genrsa",
        "-out", str(key_file),
        "2048"
    ])
    
    # Generate self-signed certificate
    run_command([
        "openssl", "req",
        "-new", "-x509",
        "-key", str(key_file),
        "-out", str(cert_file),
        "-days", "365",
        "-subj", "/CN=localhost"
    ])
    
    print(f"✓ Certificate generated: {cert_file}")
    return str(cert_file), str(key_file)

def build_webxr_test():
    """Build the WebXR test with Emscripten."""
    if not check_emscripten():
        return False
    
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    
    source_file = SCRIPT_DIR / "webxr_test.c"
    if not source_file.exists():
        print(f"✗ Source file not found: {source_file}")
        return False
    
    output_file = BUILD_DIR / "webxr_test.html"
    
    # Emscripten compile command
    # Note: This assumes SDL3 is available via -sUSE_SDL=3 (may need adjustment)
    cmd = [
        "emcc",
        str(source_file),
        "-o", str(output_file),
        "-O2",
        "-s", "USE_SDL=3",
        "-s", "FULL_ES3=1",
        "-s", "ASYNCIFY=1",  # Required for emscripten_set_main_loop
        "-s", "ALLOW_MEMORY_GROWTH=1",
        # Include WebXR JS library
        f"--js-library={SDL_ROOT}/src/gpu/xr/library_sdl_webxr.js",
        # Add include paths
        f"-I{SDL_ROOT}/include",
        # Debugging
        "-s", "ASSERTIONS=1",
        "-g",  # Include debug info
    ]
    
    try:
        run_command(cmd)
        print(f"✓ Build successful: {output_file}")
        return True
    except Exception as e:
        print(f"✗ Build failed: {e}")
        
        # Alternative: Try simpler build without custom library
        print("\nTrying simplified build (WebXR may not work)...")
        cmd_simple = [
            "emcc",
            str(source_file),
            "-o", str(output_file),
            "-O2",
            "-s", "USE_SDL=3",
            "-s", "FULL_ES3=1",
            "-s", "ASYNCIFY=1",
            f"-I{SDL_ROOT}/include",
        ]
        try:
            run_command(cmd_simple)
            print(f"✓ Simplified build successful (WebXR stubs only)")
            return True
        except:
            return False

def create_index_html():
    """Create an index.html with WebXR-friendly setup."""
    index_content = """<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>SDL WebXR Test</title>
    <style>
        body {
            background: #1a1a2e;
            color: #eee;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0;
            padding: 20px;
            min-height: 100vh;
        }
        h1 { color: #e94560; }
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        .status {
            background: #16213e;
            padding: 15px;
            border-radius: 8px;
            margin: 20px 0;
        }
        .status.ok { border-left: 4px solid #0f0; }
        .status.warn { border-left: 4px solid #ff0; }
        .status.error { border-left: 4px solid #f00; }
        a {
            color: #e94560;
            text-decoration: none;
        }
        a:hover { text-decoration: underline; }
        iframe {
            width: 100%;
            height: 600px;
            border: 2px solid #16213e;
            border-radius: 8px;
        }
        .btn {
            background: #e94560;
            color: white;
            padding: 12px 24px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 16px;
            margin: 10px 5px 10px 0;
        }
        .btn:hover { background: #c73e54; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🥽 SDL WebXR Test</h1>
        
        <div id="status-webxr" class="status">
            Checking WebXR support...
        </div>
        
        <p>
            <button class="btn" onclick="openTest()">Launch Test App</button>
            <button class="btn" onclick="checkXR()">Refresh XR Status</button>
        </p>
        
        <h2>Instructions</h2>
        <ol>
            <li>Install the <a href="https://chromewebstore.google.com/detail/webxr-api-emulator/mjddjgeghkdijejnciaefnkjmkafnmgk" target="_blank">WebXR API Emulator</a> browser extension (if no VR headset)</li>
            <li>Click "Launch Test App" above</li>
            <li>Click anywhere in the app OR press SPACE to enter VR mode</li>
            <li>If using emulator: Open DevTools (F12) → WebXR tab to configure device</li>
        </ol>
        
        <h2>Test Application</h2>
        <iframe id="test-frame" src="about:blank"></iframe>
        
        <h2>Debug Log</h2>
        <pre id="log" style="background: #16213e; padding: 15px; border-radius: 8px; overflow-x: auto;"></pre>
    </div>
    
    <script>
        function log(msg) {
            const el = document.getElementById('log');
            el.textContent += new Date().toISOString().split('T')[1].slice(0,8) + ' ' + msg + '\\n';
            el.scrollTop = el.scrollHeight;
        }
        
        function checkXR() {
            const statusEl = document.getElementById('status-webxr');
            
            if (!navigator.xr) {
                statusEl.className = 'status error';
                statusEl.innerHTML = '❌ <strong>WebXR not available</strong><br>Install the WebXR API Emulator extension.';
                log('WebXR API not found');
                return;
            }
            
            navigator.xr.isSessionSupported('immersive-vr').then(supported => {
                if (supported) {
                    statusEl.className = 'status ok';
                    statusEl.innerHTML = '✅ <strong>WebXR available!</strong> Immersive VR is supported.';
                    log('WebXR: immersive-vr supported');
                } else {
                    statusEl.className = 'status warn';
                    statusEl.innerHTML = '⚠️ <strong>WebXR available</strong> but immersive-vr not supported.<br>Check emulator settings or connect VR headset.';
                    log('WebXR: immersive-vr NOT supported');
                }
            }).catch(err => {
                statusEl.className = 'status error';
                statusEl.innerHTML = '❌ <strong>WebXR error:</strong> ' + err.message;
                log('WebXR error: ' + err.message);
            });
        }
        
        function openTest() {
            document.getElementById('test-frame').src = 'webxr_test.html';
            log('Loaded webxr_test.html');
        }
        
        // Check on load
        checkXR();
        log('Page loaded');
    </script>
</body>
</html>
"""
    
    index_file = BUILD_DIR / "index.html"
    index_file.write_text(index_content)
    print(f"✓ Created {index_file}")

def serve_https():
    """Start HTTPS server for WebXR testing."""
    try:
        cert_file, key_file = generate_self_signed_cert()
    except Exception as e:
        print(f"✗ Certificate generation failed: {e}")
        print("\nFallback: Using HTTP (WebXR may not work)")
        serve_http()
        return
    
    os.chdir(BUILD_DIR)
    
    # Create SSL context
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert_file, key_file)
    
    handler = http.server.SimpleHTTPRequestHandler
    
    print(f"\n{'='*60}")
    print(f"HTTPS Server running at: https://localhost:{HTTPS_PORT}/")
    print(f"{'='*60}")
    print("\nNOTE: You'll see a browser warning about the self-signed cert.")
    print("Click 'Advanced' → 'Proceed to localhost' to continue.\n")
    print("Press Ctrl+C to stop the server.\n")
    
    with http.server.HTTPServer(('', HTTPS_PORT), handler) as httpd:
        httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")

def serve_http():
    """Fallback HTTP server (WebXR won't work but useful for testing)."""
    os.chdir(BUILD_DIR)
    
    handler = http.server.SimpleHTTPRequestHandler
    port = 8080
    
    print(f"\n{'='*60}")
    print(f"HTTP Server running at: http://localhost:{port}/")
    print(f"{'='*60}")
    print("\nWARNING: WebXR requires HTTPS! Using HTTP as fallback.\n")
    print("Press Ctrl+C to stop the server.\n")
    
    with http.server.HTTPServer(('', port), handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")

def main():
    parser = argparse.ArgumentParser(
        description="Build and deploy SDL WebXR test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        'command',
        choices=['build', 'serve', 'all'],
        help='Command to run'
    )
    args = parser.parse_args()
    
    if args.command in ('build', 'all'):
        print("\n=== Building WebXR Test ===\n")
        if not build_webxr_test():
            if args.command == 'all':
                print("\nBuild failed, cannot serve.")
                return 1
        create_index_html()
    
    if args.command in ('serve', 'all'):
        print("\n=== Starting HTTPS Server ===\n")
        if not BUILD_DIR.exists():
            print(f"Build directory not found: {BUILD_DIR}")
            print("Run with 'build' first, or use 'all' to build and serve.")
            return 1
        serve_https()
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
