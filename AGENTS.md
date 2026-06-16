# OrbitalVelocity Project Information

## Overview
OrbitalVelocity is a 2D space exploration game built for Android using C++ (GLES3) and Kotlin. It features Newtonian mechanics, real-time orbital trajectory prediction using conic sections, and a simple polygonal art style.

## Project Structure
- **Root Directory**: `C:/Users/Administrator/OrbitalVelocity`
- **Package / Namespace**: `com.quantaflare.orbitalvelocity`
- **Main Module**: `:app` (Android Application)
- **Kotlin Entry Point**: `app/src/main/java/com/quantaflare/orbitalvelocity/MainActivity.kt` — a `GameActivity` subclass that loads the native `orbitalvelocity` library and hides the system UI.
- **Native Code**: Located in `app/src/main/cpp`
    - `main.cpp`: Entry point for the Android Native Activity (`android_main`), event loop, and input filter.
    - `Game.cpp/h`: Core physics simulation and game logic (celestial bodies + ship).
    - `Renderer.cpp/h`: OpenGL ES 3.0 rendering engine, camera, UI controls, and input handling.
    - `Shader.cpp/h`: GLES shader management.
    - `MathUtils.h`: 2D vector (`Vec2`) math utilities.
    - `Utility.cpp/h`: GL helper functions (e.g. matrix builders).
    - `AndroidOut.cpp/h`: `aout` logcat output stream used for debug logging.
- **Native build**: `app/src/main/cpp/CMakeLists.txt` compiles `main.cpp`, `AndroidOut.cpp`, `Renderer.cpp`, `Shader.cpp`, `Game.cpp`, and `Utility.cpp` into the shared library `orbitalvelocity`.

## Technical Details
- **Physics**: N-body gravitational simulation. The ship is attracted by every body, and the bodies attract each other; the Sun is pinned to the origin to prevent whole-system drift. Each frame uses 20 fixed sub-steps for stability, and `dt` is clamped to 0.05s. Simple collision response pushes the ship to a body's surface and damps/reflects relative velocity.
- **Rendering**: OpenGL ES 3.0, orthographic projection, polygonal shapes (triangle fans / line strips). The camera center is determined each frame by the active camera mode (see below); world is drawn with a global zoomed transform, bodies/ship with per-object model transforms.
- **Camera Modes**: Two modes selectable via a UI button. `CameraMode` enum lives in `Game.h`; `Game` stores the current mode and exposes `getCameraMode()` / `toggleCameraMode()`.
    - **Ship** (default): camera centered on the ship (`camCenter = ship.pos`).
    - **Body**: camera centered on the dominant gravitational body — the one with the highest `mu / r²` relative to the ship, which is the same body the patched-conic trajectory is drawn around.
    - The dominant body is computed once per frame at the top of `Renderer::render()` and reused for both the camera target (in Body mode) and the ship-orbit conic draw.
- **UI**: Custom OpenGL-rendered controls (rendered in a separate aspect-corrected UI pass):
    - **Joystick**: Lower-left for ship orientation.
    - **Throttle Slider**: Upper-left for engine power.
    - **Zoom Slider**: Right side for logarithmic camera scale.
    - **Camera Mode Button**: Top-center; small translucent square with a polygonal camera icon. Tap toggles between Ship and Body modes. Current mode name shown as a text label above the button.
    - **Labels**: A minimal vector font (`drawText`) renders control labels. Implemented glyphs: T, H, R, J, O, Y, Z, M, S, I, P, B, D.
- **Celestial Bodies**: The Sun plus all eight planets — Mercury, Venus, Earth (Moon), Mars (Phobos, Deimos), Jupiter (Io, Europa, Ganymede, Callisto), Saturn (Enceladus, Rhea, Titan, Iapetus), Uranus (Miranda, Ariel, Titania, Oberon), and Neptune (Proteus, Triton) — 26 bodies total. All orbit radii (planets around the Sun, moons around their planets) are scaled roughly proportional to the real semi-major axes, with Earth fixed at 20000 units (Mercury ~7700 … Mars ~30500 … Neptune ~601400). Moon distances preserve real ordering/ratios but are exaggerated for visibility while staying within each planet's Hill sphere. Planet masses are kept well below the Sun's to preserve N-body stability. The ship starts in a stable circular orbit around Earth. (The zoom-out range — `MIN_ZOOM` in `Renderer.cpp` — was widened so the distant outer planets are viewable.)
- **Body hierarchy**: Each `CelestialBody` carries a `parent` index (the body it orbits; `-1` for the Sun). The renderer uses this to draw each body's orbit ellipse around its primary, replacing the previous hardcoded `[Sun, Earth, Moon]` index assumptions, so bodies can be added/reordered freely.
- **Orbit Prediction**: Analytic conics (ellipse/hyperbola) computed from the orbital state vector relative to the dominant gravitational body (highest `mu / r²`).

## Build & Tooling
- **Android Gradle Plugin**: 9.2.1 · **Gradle**: 9.4.1 · **Kotlin**: 2.2.10
- **SDK**: `compileSdk`/`targetSdk` 36, `minSdk` 30
- **NDK / CMake**: native build via CMake 3.22.1
- **Java/Kotlin compatibility**: Java 11
- **Note**: The Kotlin Android plugin (`alias(libs.plugins.kotlin.android)`) is commented out in `app/build.gradle.kts` due to Android Studio compatibility issues.

## Dependencies
- `androidx.games:games-activity:4.0.0`
- `androidx.appcompat:appcompat:1.6.1`
- `androidx.core:core-ktx:1.10.1`
- `com.google.android.material:material:1.10.0`
- Kotlin Android plugin `2.2.10` (pulls in the matching `kotlin-stdlib`)
- OpenGL ES 3.0
- Android NDK
- Test: `junit:4.13.2`, `androidx.test.ext:junit:1.1.5`, `androidx.test.espresso:espresso-core:3.5.1`
