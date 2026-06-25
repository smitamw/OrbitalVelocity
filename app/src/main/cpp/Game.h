#ifndef ORBITAL_VELOCITY_GAME_H
#define ORBITAL_VELOCITY_GAME_H

#include <vector>
#include <string>
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

// Selectable spacecraft. Purely cosmetic — all variants share the same thrust and fuel
// capacity; they differ only in appearance. Rocket/Falcon are unlocked with science points.
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
    float maxFuel;       // capacity (for the gauge) = Game::getFuelCapacity()
};

class Game {
public:
    Game();
    void update(float dt);

    // (Re)start gameplay with the chosen ship, landed on Earth's surface and ready to launch.
    void startWithShip(ShipType type);

    // Persistence. The simulation (every body's pos/vel and the ship's type/pos/vel/heading/
    // fuel, plus zoom/camera/thrust-limit) is written to a small binary file so the player can
    // close the app and resume. Derived fields (body mass/radius/etc. and ship fields set by
    // startWithShip) are rebuilt rather than stored. See Game.cpp for the format.
    bool saveTo(const std::string& path) const;
    // Cheap header-only check: true iff the file is a save matching this build's body count.
    bool hasValidSave(const std::string& path) const;
    // Applies a save to this game; on any mismatch/IO error returns false and leaves the game
    // in its freshly-constructed state so the caller can fall back to a new game.
    bool loadFrom(const std::string& path);

    const std::vector<CelestialBody>& getBodies() const { return bodies_; }
    const Ship& getShip() const { return ship_; }

    // Fuel display helper: fraction is 0..1 of capacity.
    float getFuelFraction() const { return ship_.maxFuel > 0 ? ship_.fuel / ship_.maxFuel : 0.0f; }

    // Progression: science points are earned by landing on worlds (see update()) and spent on
    // fuel-capacity upgrades and ship unlocks. Fuel capacity grows 0.1 per purchased upgrade
    // from a base of 3.0 units; all ships share it.
    static constexpr float kBaseFuel = 3.0f;
    float getFuelCapacity() const { return kBaseFuel + 0.1f * fuelUpgrades_; }
    int getScience() const { return science_; }
    bool isShipUnlocked(ShipType t) const { return shipUnlocked_[(int)t]; }
    int shipUnlockCost(ShipType t) const {
        switch (t) { case ShipType::Rocket: return 4; case ShipType::Falcon: return 6; default: return 0; }
    }
    // Spend science. Each returns true on success, false if unaffordable / already owned.
    bool buyFuelUpgrade();
    bool unlockShip(ShipType t);

    // True while the ship is resting on Earth's surface (its home base). Set during update()
    // by the landing detection; drives the in-game "Change ship" button. Earth refuels the
    // ship automatically while landed.
    bool isOnEarth() const { return landedOnEarth_; }

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
    static constexpr int kEarthIndex = 3; // Earth's index in bodies_ (see Game() / startWithShip)

    std::vector<CelestialBody> bodies_;
    Ship ship_;
    bool landedOnEarth_ = false; // recomputed each update(); true while parked on Earth

    // Progression state. science_/fuelUpgrades_/shipUnlocked_ and the per-body visited_/
    // returnAwarded_ flags are persisted in the save; scienceEligible_ is rebuilt each
    // construction (which bodies grant science: all except Sun, Earth, and the gas giants).
    int science_ = 0;
    int fuelUpgrades_ = 0;
    bool shipUnlocked_[3] = {true, false, false}; // Triangle always; Rocket/Falcon unlockable
    std::vector<bool> visited_;        // landed on this body at least once
    std::vector<bool> returnAwarded_;  // the Earth-return bonus for this body was granted
    std::vector<bool> scienceEligible_; // body awards science (not Sun/Earth/gas giant)

    Vec2 joystick_;
    float throttle_;
    float zoom_;
    int timeWarp_ = 1; // simulation speed multiplier (1x, 2x, 5x, 10x, 50x, 100x, 500x)
    float thrustLimit_ = 1.0f; // 0..1 cap on engine thrust (1 = full, set by the upper-left slider)
    CameraMode cameraMode_ = CameraMode::Ship;

    const float G = 1000.0f; // Scaled gravitational constant
};

#endif // ORBITAL_VELOCITY_GAME_H
