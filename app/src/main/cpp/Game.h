#ifndef ORBITAL_VELOCITY_GAME_H
#define ORBITAL_VELOCITY_GAME_H

#include <vector>
#include "MathUtils.h"

struct CelestialBody {
    Vec2 pos;
    Vec2 vel;
    float mass;
    float radius;
    float mu; // G * mass
    float color[4];
    int parent; // index of the body this orbits; -1 for the Sun
};

enum class CameraMode { Ship, Body };

// Selectable spacecraft. They differ only in appearance and fuel capacity; all share the
// same thrust (enough to lift off Earth's surface). See Game::startWithShip.
enum class ShipType { Triangle, Rocket, Falcon };

struct Ship {
    Vec2 pos;
    Vec2 vel;
    float angle; // in radians
    float throttle; // 0 to 1
    float thrust;
    float mass;
    float color[4];
    ShipType type;
    float fuel;          // remaining fuel, in seconds of full-throttle burn
    float maxFuel;       // capacity (for the gauge); meaningless when infiniteFuel
    bool infiniteFuel;   // true for the Millennium Falcon
};

class Game {
public:
    Game();
    void update(float dt);

    // (Re)start gameplay with the chosen ship, landed on Earth's surface and ready to launch.
    void startWithShip(ShipType type);

    const std::vector<CelestialBody>& getBodies() const { return bodies_; }
    const Ship& getShip() const { return ship_; }

    // Fuel display helpers: fraction is 0..1 of capacity (always 1 for infinite-fuel ships).
    float getFuelFraction() const { return ship_.infiniteFuel ? 1.0f : (ship_.maxFuel > 0 ? ship_.fuel / ship_.maxFuel : 0.0f); }
    bool shipHasInfiniteFuel() const { return ship_.infiniteFuel; }

    // Controls
    void setJoystick(Vec2 joystick) { joystick_ = joystick; }
    void setThrottle(float throttle) { throttle_ = throttle; ship_.throttle = throttle; }
    void setZoom(float zoom) { zoom_ = zoom; }
    void setTimeWarp(int w) { timeWarp_ = w; }
    // Caps the fraction of full thrust the engine produces, for fine maneuvering. At 1.0 the
    // ship has full thrust (enough to take off from Earth); lower values give gentler control.
    void setThrustLimit(float limit) { thrustLimit_ = limit; }

    float getZoom() const { return zoom_; }
    int getTimeWarp() const { return timeWarp_; }
    float getThrustLimit() const { return thrustLimit_; }

    CameraMode getCameraMode() const { return cameraMode_; }
    void toggleCameraMode() {
        cameraMode_ = (cameraMode_ == CameraMode::Ship) ? CameraMode::Body : CameraMode::Ship;
    }

private:
    std::vector<CelestialBody> bodies_;
    Ship ship_;

    Vec2 joystick_;
    float throttle_;
    float zoom_;
    int timeWarp_ = 1; // simulation speed multiplier (1x, 2x, 5x, 10x, 50x, 100x, 500x)
    float thrustLimit_ = 1.0f; // 0..1 cap on engine thrust (1 = full, set by the upper-left slider)
    CameraMode cameraMode_ = CameraMode::Ship;

    const float G = 1000.0f; // Scaled gravitational constant
};

#endif // ORBITAL_VELOCITY_GAME_H
