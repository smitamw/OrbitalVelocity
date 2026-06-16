# OrbitalVelocity Project Information

## Overview
OrbitalVelocity is a 2D space exploration game built for Android using C++ (GLES3) and Kotlin. It features Newtonian mechanics, real-time orbital trajectory prediction using conic sections, and a simple polygonal art style.

## Project Structure
- **Root Directory**: `C:/Users/amol/AndroidStudioProjects/OrbitalVelocity`
- **Main Module**: `:app` (Android Application)
- **Native Code**: Located in `app/src/main/cpp`
    - `main.cpp`: Entry point for the Android Native Activity.
    - `Game.cpp/h`: Core physics simulation and game logic.
    - `Renderer.cpp/h`: OpenGL ES 3.0 rendering engine and UI management.
    - `Shader.cpp/h`: GLES shader management.
    - `MathUtils.h`: 2D vector and orbital mechanics utilities.
    - `Utility.cpp/h`: GL helper functions.

## Technical Details
- **Physics**: N-body gravitational simulation with sub-stepping for stability.
- **Rendering**: OpenGL ES 3.0, orthographic projection, polygonal shapes.
- **UI**: Custom OpenGL-rendered controls:
    - **Joystick**: Lower-left for ship orientation.
    - **Throttle Slider**: Upper-left for engine power.
    - **Zoom Slider**: Right side for logarithmic camera scale.
- **Celestial Bodies**: Sun, Earth, Moon.
- **Orbit Prediction**: Analytic conics based on dominant gravitational body.

## Dependencies
- `androidx.games:games-activity:4.0.0`
- `androidx.appcompat:appcompat:1.6.1`
- `androidx.core:core-ktx:1.10.1`
- `com.google.android.material:material:1.10.0`
- `org.jetbrains.kotlin:kotlin-stdlib:2.2.10`
- OpenGL ES 3.0
- Android NDK
