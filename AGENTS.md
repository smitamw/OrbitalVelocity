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
- **Rendering**: OpenGL ES 3.0, orthographic projection, polygonal shapes (triangle fans / line strips). The camera is centered on the ship; world is drawn with a global zoomed transform, bodies/ship with per-object model transforms.
- **UI**: Custom OpenGL-rendered controls (rendered in a separate aspect-corrected UI pass):
    - **Joystick**: Lower-left for ship orientation.
    - **Throttle Slider**: Upper-left for engine power.
    - **Zoom Slider**: Right side for logarithmic camera scale.
    - **Labels**: A minimal vector font (`drawText`) renders the "THR", "JOY", and "ZOOM" labels; it only implements the glyphs used by those labels.
- **Celestial Bodies**: Sun, Earth, Moon. The ship starts in a stable circular orbit around Earth.
- **Orbit Prediction**: Analytic conics (ellipse/hyperbola) computed from the orbital state vector relative to the dominant gravitational body (highest `mu / r²`).

## Build & Tooling
- **Android Gradle Plugin**: 9.2.1 · **Gradle**: 9.4.1 · **Kotlin**: 2.2.10
- **SDK**: `compileSdk`/`targetSdk` 36, `minSdk` 30
- **NDK / CMake**: native build via CMake 3.22.1
- **Java/Kotlin compatibility**: Java 11

## Dependencies
- `androidx.games:games-activity:4.0.0`
- `androidx.appcompat:appcompat:1.6.1`
- `androidx.core:core-ktx:1.10.1`
- `com.google.android.material:material:1.10.0`
- Kotlin Android plugin `2.2.10` (pulls in the matching `kotlin-stdlib`)
- OpenGL ES 3.0
- Android NDK
- Test: `junit:4.13.2`, `androidx.test.ext:junit:1.1.5`, `androidx.test.espresso:espresso-core:3.5.1`
