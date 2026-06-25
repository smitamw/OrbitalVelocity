# OrbitalVelocity Project Information

## Overview
OrbitalVelocity is a 2D space exploration game built for Android using C++ (GLES3) and Kotlin. It features Newtonian mechanics, real-time orbital trajectory prediction using conic sections, and a simple polygonal art style. Gameplay is a light progression loop: Earth is a home base, fuel is limited, and landing on other worlds earns **science points** spent on fuel-capacity upgrades and (cosmetic) ship unlocks. Game state is persisted so the player can close the app and resume.

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
- **Physics**: N-body gravitational simulation. The ship is attracted by every body, and the bodies attract each other; the Sun is pinned to the origin to prevent whole-system drift. `dt` is clamped to 0.05s. Collision response pushes the ship to a body's surface and resolves contact by **stopping only the inward (surface-penetrating) component** of the ship's relative velocity, preserving tangential/outward motion so the engine can always build up launch velocity (zeroing the *whole* velocity here previously pinned the ship to the surface during a gravity-turn or marginal-thrust launch — the contact clamp wiped each sub-step's thrust). A **fixed-deceleration surface friction** (`kSurfaceFriction`, capped at the current slide speed) bleeds off tangential sliding so a grounded ship coasts to a stop instead of skating around the planet; a fast hit instead bounces with restitution. A soft (low relative-velocity) contact counts as a landing.
- **Time warp**: Selectable speeds `{1, 2, 5, 10, 50, 100, 500}×`. The base is 20 sub-steps per frame; total sub-steps = `min(20 × warp, 1000)`. So up to 50× the per-sub-step duration stays constant (`dt/20`, accuracy unchanged) and only the count grows; 100× and 500× are capped at 1000 sub-steps and instead take larger steps — a deliberate accuracy-for-performance trade to avoid lag at high warp. Either way a frame simulates `warp × dt` seconds. Thrust is disabled while warping (`>1×`); the ship coasts on rails.
- **Fuel**: A single shared capacity (`Game::getFuelCapacity()` = 3.0 + 0.1 per purchased upgrade) used by every ship variant. Burn is proportional to throttle × thrust-limit, so capping thrust also sips fuel. Landing on Earth refuels to full.
- **Rendering**: OpenGL ES 3.0, orthographic projection, polygonal shapes (triangle fans / line strips). The camera center is determined each frame by the active camera mode (see below); world is drawn with a global zoomed transform, bodies/ship with per-object model transforms.
    - **Body shadows**: Each body (except the Sun, the light source) is drawn with a translucent gray "night side" — a semicircle covering the half facing away from the Sun, oriented by the Sun→body direction and drawn on top of the disc.
    - **Position markers**: A translucent disc in each body's own color, drawn at a fixed minimum on-screen size when the body's real disc would be smaller (i.e. when zoomed far out), so planets and moons stay findable. Skipped once the actual disc is large enough. They fade on the same idle timer as the HUD (reusing `uiFade`), so any touch wakes them.
- **Camera Modes**: Two modes selectable via a UI button. `CameraMode` enum lives in `Game.h`; `Game` stores the current mode and exposes `getCameraMode()` / `toggleCameraMode()`.
    - **Ship** (default): camera centered on the ship (`camCenter = ship.pos`).
    - **Body**: camera centered on the dominant gravitational body — the one with the highest `mu / r²` relative to the ship, which is the same body the patched-conic trajectory is drawn around.
    - The dominant body is computed once per frame at the top of `Renderer::render()` and reused for both the camera target (in Body mode) and the ship-orbit conic draw.
- **UI**: Custom OpenGL-rendered controls (rendered in a separate aspect-corrected UI pass). The whole HUD auto-fades after a few seconds of inactivity and any touch wakes it; the first touch after a full fade only reveals the HUD (it doesn't actuate a control).
    - **Floating joystick**: Combined steering + throttle. It spawns wherever a single finger touches down; drag *direction* sets heading and drag *distance* (past a deadzone, up to a max reach) sets throttle. Only shown while a finger is down.
    - **Thrust limiter**: Upper-left horizontal slider that caps engine thrust (0..1) for fine maneuvering; at full it can lift off Earth.
    - **Pinch-to-Zoom**: Two-finger pinch (replaced the old zoom slider). Its range (`MIN_ZOOM` and `MAX_ZOOM_FACTOR` in `Renderer.cpp`) reaches from a close ship view out far enough to frame Neptune's orbit, so the entire solar system is viewable.
    - **Camera Mode Button**: Top-right; small translucent square with a polygonal camera icon. Tap toggles between Ship and Body modes. Current mode name shown as a text label above the button.
    - **Time-warp buttons**: Bottom-left grid laid out in **two rows** (so all seven fit in portrait); the active speed is highlighted.
    - **Fuel gauge**: Vertical bar on the right edge; turns red when low.
    - **Science readout**: `SCI n` in the lower-right.
    - **Change Ship / Upgrades buttons**: Stacked under the thrust limiter, shown **only while landed on Earth** (driven by `Game::isOnEarth()`); open the ship-selection and fuel-upgrade screens.
    - **Labels**: A minimal vector font (`drawText`) renders control labels. Implemented glyphs: all `A`–`Z`, `0`–`9`, and `.`, `+`, `-`.
- **Celestial Bodies**: The Sun plus all eight planets — Mercury, Venus, Earth (Moon), Mars (Phobos, Deimos), Jupiter (Io, Europa, Ganymede, Callisto), Saturn (Enceladus, Rhea, Titan, Iapetus), Uranus (Miranda, Ariel, Titania, Oberon), and Neptune (Proteus, Triton) — 26 bodies total. All orbit radii (planets around the Sun, moons around their planets) are scaled roughly proportional to the real semi-major axes, with Earth fixed at 200000 units (Mercury ~77000 … Mars ~305000 … Neptune ~6014000). Moon distances preserve real ordering/ratios but are exaggerated for visibility while staying within each planet's Hill sphere. Planet masses are kept well below the Sun's to preserve N-body stability. **Starting phases**: rather than every body beginning collinear on the +x axis (which produced correlated perturbations and long-term instability), each body's initial orbital phase is offset from the previous one by the golden angle (~137.5°). A single counter in the `Game` constructor advances once per body created, so planets fan out around the Sun and each planet's moons fan out around it; the `makePlanet`/`makeMoon` helpers rotate both position and velocity by this phase so each body still starts on a circular orbit. The ship starts landed on Earth's surface, ready to launch. (The zoom-out range — `MIN_ZOOM` in `Renderer.cpp` — was widened so the distant outer planets are viewable.)
- **Body hierarchy**: Each `CelestialBody` carries a `parent` index (the body it orbits; `-1` for the Sun). The renderer uses this to draw each body's orbit ellipse around its primary, replacing the previous hardcoded `[Sun, Earth, Moon]` index assumptions, so bodies can be added/reordered freely.
- **Orbit Prediction**: Analytic conics (ellipse/hyperbola) computed from the orbital state vector relative to the dominant gravitational body (highest `mu / r²`).

## Gameplay & Progression
- **Home base (Earth)**: Earth (body index 3) is the home base. Landing there refuels the ship and surfaces the in-game **Change Ship** and **Upgrades** buttons. `Game::isOnEarth()` is recomputed each `update()` from the landing detection.
- **Ship variants**: `Triangle`, `Rocket`, `Falcon` (`ShipType` in `Game.h`). They are **purely cosmetic** — identical thrust and fuel; only `drawShipShape` differs (each with its own exhaust plume). Triangle is the free default; Rocket and Falcon are unlocked with science points (costs 4 and 6 via `Game::shipUnlockCost`).
- **Science points** (`Game::getScience()`): the progression currency. Earned by landing: **+1** the first time you land on an eligible body, and **+1** more when you next land back on Earth after that visit (classic visit-and-return). Eligible bodies are every body **except** the Sun, Earth, and the four gas-giant planets (Jupiter/Saturn/Uranus/Neptune) — their moons *do* count. Per-body progress is tracked by `visited_` / `returnAwarded_`; eligibility by `scienceEligible_` (built in the `Game` constructor, not persisted).
- **Spending**: `Game::buyFuelUpgrade()` spends 1 point for +0.1 fuel capacity; `Game::unlockShip()` spends the unlock cost. Both reject unaffordable purchases.

## Screens & Menu Flow
`Renderer::Screen` = `{ Start, Customize, Upgrades, Playing }`. `render()` dispatches the per-screen draw; `handleInput()` routes `Playing` → `handleGameInput`, every other screen → `handleMenuInput`.
- **Start (title)**: Shows **PLAY** when no save exists; **CONTINUE** + **NEW GAME** when one does (NEW GAME wipes the save and starts fresh).
- **Customize / "Change Ship"**: Reached from the in-game button on Earth. Cycle the (cosmetic) ships; **SELECT** an unlocked ship to launch with it, or **UNLOCK (cost)** a locked one. Shows the science balance and lock status.
- **Upgrades**: Reached from the in-game button on Earth. Shows science + fuel capacity and a **BUY FUEL +0.1** button.

## Persistence
- Game state is saved to a small **binary file** (`savegame.dat` in the app's `internalDataPath`). Saved on `APP_CMD_PAUSE` (see `main.cpp`) and as a `Renderer` destructor backstop; only written once a game is in progress.
- Persisted: every body's pos/vel, the ship's type/pos/vel/heading/fuel, view state (zoom, camera mode, thrust limit), and progression (science, fuel upgrades, ship unlocks, per-body visit/return flags). Derived fields (body mass/radius/etc., and the ship fields rebuilt by `startWithShip`) are not stored. Throttle resets to 0 and time-warp to 1× on load.
- The file has a `magic` + `version` + `bodyCount` header; a mismatch (e.g. after changing the solar-system layout, or bumping `kSaveVersion`) invalidates the save, which the title screen treats as "no save". Implemented by `Game::saveTo` / `hasValidSave` / `loadFrom`.

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
