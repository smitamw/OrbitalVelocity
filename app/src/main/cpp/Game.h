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
};

enum class CameraMode { Ship, Body };

struct Ship {
    Vec2 pos;
    Vec2 vel;
    float angle; // in radians
    float throttle; // 0 to 1
    float thrust;
    float mass;
    float color[4];
};

class Game {
public:
    Game();
    void update(float dt);

    const std::vector<CelestialBody>& getBodies() const { return bodies_; }
    const Ship& getShip() const { return ship_; }

    // Controls
    void setJoystick(Vec2 joystick) { joystick_ = joystick; }
    void setThrottle(float throttle) { throttle_ = throttle; ship_.throttle = throttle; }
    void setZoom(float zoom) { zoom_ = zoom; }

    float getZoom() const { return zoom_; }

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
    CameraMode cameraMode_ = CameraMode::Ship;

    const float G = 1000.0f; // Scaled gravitational constant
};

#endif // ORBITAL_VELOCITY_GAME_H
