#include "Game.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstdint>

// Save-file format (binary, little-endian). Bump kSaveVersion on any layout change so old
// files are rejected. A save is also rejected if its body count differs from the current
// build's (e.g. after editing the solar system), so stale saves degrade to "no save".
namespace {
    const uint32_t kSaveMagic   = 0x4F524254; // "ORBT"
    const uint32_t kSaveVersion = 2; // v2 added progression (science, fuel upgrades, unlocks, visits)

    template <typename T>
    void writePod(std::ostream& os, const T& v) { os.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
    template <typename T>
    bool readPod(std::istream& is, T& v) { return (bool)is.read(reinterpret_cast<char*>(&v), sizeof(T)); }
}

Game::Game() : zoom_(0.02f), throttle_(0.0f), joystick_({0, 0}) {
    // Spread starting phases with the golden angle so no two bodies begin aligned and orbital
    // periods don't share simple integer ratios — keeps the system decorrelated and stable over
    // long runs (instead of every body starting collinear on the +x axis). The counter advances
    // once per body created, so planets fan out around the Sun and each planet's moons fan out
    // around it, each ~137.5 degrees from the previous one.
    constexpr float kGoldenAngle = 2.39996323f; // ~137.5 degrees, in radians
    int phaseIndex = 0;

    // Helper: a planet in a circular heliocentric orbit at `dist` from the Sun.
    auto makePlanet = [&](float dist, float mass, float radius, float color[4]) {
        float theta = kGoldenAngle * phaseIndex++;
        float vel = std::sqrt(bodies_[0].mu / dist); // index 0 is always the Sun
        Vec2 pos = {dist * std::cos(theta), dist * std::sin(theta)};
        Vec2 v   = {-vel * std::sin(theta), vel * std::cos(theta)};
        bodies_.push_back({pos, v, mass, radius, G * mass, {color[0], color[1], color[2], color[3]}, 0});
    };
    // Helper: a moon in a circular orbit at `dist` from its parent planet.
    auto makeMoon = [&](int parent, float dist, float mass, float radius, float color[4]) {
        float theta = kGoldenAngle * phaseIndex++;
        const CelestialBody& p = bodies_[parent];
        float vel = std::sqrt(p.mu / dist);
        Vec2 rel  = {dist * std::cos(theta), dist * std::sin(theta)};
        Vec2 vrel = {-vel * std::sin(theta), vel * std::cos(theta)};
        bodies_.push_back({p.pos + rel, {p.vel.x + vrel.x, p.vel.y + vrel.y}, mass, radius, G * mass, {color[0], color[1], color[2], color[3]}, parent});
    };

    // Index 0: Sun (pinned at origin, no parent)
    bodies_.push_back({{0, 0}, {0, 0}, 1000000.0f, 1000.0f, G * 1000000.0f, {1.0f, 0.8f, 0.0f, 1.0f}, -1});

    // Inner planets. Distances are roughly proportional to real semi-major axes,
    // scaled so Earth sits at 40000 units (Mercury 0.39 AU, Venus 0.72, Mars 1.52).
    float mercuryColor[4] = {0.6f, 0.55f, 0.5f, 1.0f}; // gray-brown
    float venusColor[4]   = {0.9f, 0.8f, 0.5f, 1.0f};  // pale yellow
    float earthColor[4]   = {0.2f, 0.6f, 1.0f, 1.0f};  // blue
    float marsColor[4]    = {0.8f, 0.35f, 0.2f, 1.0f}; // reddish
    makePlanet(77000.0f,  600.0f,   90.0f,  mercuryColor); // index 1: Mercury
    makePlanet(145000.0f, 8100.0f,  190.0f, venusColor);   // index 2: Venus

    float earthDist = 200000.0f;
    float earthVel = std::sqrt(bodies_[0].mu / earthDist);
    makePlanet(earthDist, 10000.0f, 200.0f, earthColor);  // index 3: Earth
    const int EARTH = 3;

    // Moon - 1200 units is well within Earth's Hill sphere (~3000 units)
    float moonColor[4] = {0.7f, 0.7f, 0.7f, 1.0f};
    makeMoon(EARTH, 1200.0f, 100.0f, 50.0f, moonColor);   // index 4: Moon

    makePlanet(305000.0f, 1100.0f, 110.0f, marsColor);     // index 5: Mars
    const int MARS = 5;

    // Mars moons. Distances are exaggerated for visibility (Phobos inside Deimos);
    // both stay well within Mars's Hill sphere (~2100 units at this orbit).
    float phobosColor[4] = {0.6f, 0.55f, 0.5f, 1.0f};
    float deimosColor[4] = {0.65f, 0.6f, 0.55f, 1.0f};
    makeMoon(MARS, 600.0f,  1.0f, 14.0f, phobosColor);    // index 6: Phobos
    makeMoon(MARS, 1300.0f, 0.5f, 10.0f, deimosColor);    // index 7: Deimos

    // Outer planets. Heliocentric distances stay proportional to the real semi-major
    // axes (Earth = 200000 units): Jupiter 5.20 AU, Saturn 9.58, Uranus 19.18, Neptune 30.07.
    // Masses are kept well below the Sun's so the system stays stable; their large Hill
    // spheres (a consequence of the wide orbits) leave plenty of room for moons. Moon
    // distances preserve the real ordering/ratios but are scaled for visibility, like the
    // inner moons.
    float jupiterColor[4] = {0.80f, 0.70f, 0.50f, 1.0f}; // banded tan
    float saturnColor[4]  = {0.85f, 0.78f, 0.55f, 1.0f}; // pale gold
    float uranusColor[4]  = {0.60f, 0.85f, 0.90f, 1.0f}; // pale cyan
    float neptuneColor[4] = {0.25f, 0.40f, 0.85f, 1.0f}; // deep blue

    int JUPITER = (int)bodies_.size();
    makePlanet(1040000.0f, 40000.0f, 600.0f, jupiterColor);
    float ioColor[4]       = {0.90f, 0.85f, 0.40f, 1.0f}; // sulfur yellow
    float europaColor[4]   = {0.85f, 0.85f, 0.80f, 1.0f}; // icy white
    float ganymedeColor[4] = {0.60f, 0.55f, 0.50f, 1.0f}; // tan-gray
    float callistoColor[4] = {0.45f, 0.42f, 0.40f, 1.0f}; // dark gray
    makeMoon(JUPITER, 1800.0f, 30.0f, 40.0f, ioColor);
    makeMoon(JUPITER, 2850.0f, 25.0f, 35.0f, europaColor);
    makeMoon(JUPITER, 4550.0f, 50.0f, 58.0f, ganymedeColor);
    makeMoon(JUPITER, 7800.0f, 45.0f, 52.0f, callistoColor);

    int SATURN = (int)bodies_.size();
    makePlanet(1916000.0f, 28000.0f, 520.0f, saturnColor);
    float enceladusColor[4] = {0.90f, 0.90f, 0.95f, 1.0f}; // bright ice
    float rheaColor[4]      = {0.70f, 0.70f, 0.70f, 1.0f}; // gray
    float titanColor[4]     = {0.85f, 0.60f, 0.30f, 1.0f}; // orange haze
    float iapetusColor[4]   = {0.50f, 0.45f, 0.40f, 1.0f}; // two-tone brown
    makeMoon(SATURN, 950.0f,   3.0f, 14.0f, enceladusColor);
    makeMoon(SATURN, 2050.0f, 15.0f, 30.0f, rheaColor);
    makeMoon(SATURN, 4800.0f, 60.0f, 55.0f, titanColor);
    makeMoon(SATURN, 12000.0f, 12.0f, 28.0f, iapetusColor);

    int URANUS = (int)bodies_.size();
    makePlanet(3836000.0f, 15000.0f, 360.0f, uranusColor);
    float mirandaColor[4] = {0.60f, 0.60f, 0.65f, 1.0f};
    float arielColor[4]   = {0.70f, 0.70f, 0.72f, 1.0f};
    float titaniaColor[4] = {0.65f, 0.60f, 0.60f, 1.0f};
    float oberonColor[4]  = {0.55f, 0.50f, 0.50f, 1.0f};
    makeMoon(URANUS, 2000.0f,  4.0f, 14.0f, mirandaColor);
    makeMoon(URANUS, 2950.0f, 10.0f, 26.0f, arielColor);
    makeMoon(URANUS, 6700.0f, 16.0f, 32.0f, titaniaColor);
    makeMoon(URANUS, 9000.0f, 15.0f, 31.0f, oberonColor);

    int NEPTUNE = (int)bodies_.size();
    makePlanet(6014000.0f, 17000.0f, 350.0f, neptuneColor);
    float proteusColor[4] = {0.45f, 0.45f, 0.50f, 1.0f}; // dark
    float tritonColor[4]  = {0.80f, 0.75f, 0.78f, 1.0f}; // pinkish ice
    makeMoon(NEPTUNE, 2000.0f,  6.0f, 16.0f, proteusColor);
    makeMoon(NEPTUNE, 6000.0f, 40.0f, 45.0f, tritonColor);

    // Science eligibility: every body awards points for landing except the Sun, Earth, and the
    // four gas giants (their moons still count). visited_/returnAwarded_ track per-body progress.
    scienceEligible_.assign(bodies_.size(), true);
    scienceEligible_[0] = false;     // Sun
    scienceEligible_[EARTH] = false; // home base
    scienceEligible_[JUPITER] = scienceEligible_[SATURN] = false;
    scienceEligible_[URANUS]  = scienceEligible_[NEPTUNE] = false;
    visited_.assign(bodies_.size(), false);
    returnAwarded_.assign(bodies_.size(), false);

    // Ship - placed landed on Earth, ready to launch. The triangle is the default until the
    // player unlocks another in the Change Ship screen.
    startWithShip(ShipType::Triangle);
}

void Game::startWithShip(ShipType type) {
    // Earth is index 3 (see constructor). Rest the ship on its surface, sharing Earth's
    // orbital velocity so it stays put, with the nose pointing radially outward (+x here,
    // away from Earth) so full throttle lifts it straight off the ground.
    const CelestialBody& earth = bodies_[kEarthIndex];
    Vec2 outward = {1.0f, 0.0f};
    ship_.pos = earth.pos + outward * earth.radius;
    ship_.vel = earth.vel;
    ship_.angle = 0.0f;
    ship_.throttle = 0.0f;
    throttle_ = 0.0f;
    joystick_ = {0, 0};
    thrustLimit_ = 1.0f; // start at full thrust so the ship can lift off Earth
    ship_.mass = 1.0f;
    // All ships get the same thrust. Earth's surface gravity is mu/r^2 = 250; 350 gives a
    // thrust-to-weight ratio of ~1.4, enough for every variant to take off.
    ship_.thrust = 350.0f;
    ship_.color[0] = 1.0f; ship_.color[1] = 1.0f; ship_.color[2] = 1.0f; ship_.color[3] = 1.0f;

    // Ship type is purely cosmetic; fuel capacity is the shared, upgradeable player stat.
    ship_.type = type;
    ship_.maxFuel = ship_.fuel = getFuelCapacity();
}

void Game::update(float dt) {
    dt = std::min(dt, 0.05f);

    if (joystick_.lengthSq() > 0.001f) {
        ship_.angle = std::atan2(joystick_.y, joystick_.x);
    }

    // Recomputed below by the landing detection; true if the ship ends this frame parked
    // on Earth (its home base).
    landedOnEarth_ = false;

    // Using more sub-steps for planetary stability. Up to 50x, timewarp keeps the per-substep
    // duration constant (accuracy unchanged) and just runs more substeps. Above that the substep
    // count is capped (so 100x/500x cost no more than 50x) by taking larger steps instead — a
    // deliberate accuracy-for-performance trade to keep high warp from lagging. Either way the
    // frame still simulates timeWarp_ * dt seconds.
    int baseSteps = 20;
    int totalSteps = std::min(baseSteps * timeWarp_, baseSteps * 50);
    float subDt = totalSteps > 0 ? (float)timeWarp_ * dt / totalSteps : 0.0f;

    // Thrust is disabled while warping (timeWarp_ > 1); the ship coasts on rails.
    float effThrottle = (timeWarp_ > 1) ? 0.0f : throttle_;

    for (int s = 0; s < totalSteps; ++s) {
        Vec2 totalAcc = {0, 0};
        for (size_t bi = 0; bi < bodies_.size(); ++bi) {
            CelestialBody& body = bodies_[bi];
            Vec2 r = body.pos - ship_.pos;
            float distSq = r.lengthSq();
            float dist = std::sqrt(distSq);

            if (distSq > 1.0f) {
                totalAcc += r.normalized() * (body.mu / distSq);
            }

            if (dist < body.radius) {
                Vec2 normal = (ship_.pos - body.pos).normalized();
                ship_.pos = body.pos + normal * body.radius;
                Vec2 relativeVel = ship_.vel - body.vel;
                if (relativeVel.length() < 20.0f) {
                    ship_.vel = body.vel;
                    if ((int)bi == kEarthIndex) {
                        // Earth is the home base: parking refuels, flags it as landed (which
                        // surfaces the in-game buttons), and pays out the return bonus for every
                        // eligible world visited since the last Earth landing.
                        landedOnEarth_ = true;
                        ship_.fuel = ship_.maxFuel;
                        for (size_t j = 0; j < bodies_.size(); ++j) {
                            if (scienceEligible_[j] && visited_[j] && !returnAwarded_[j]) {
                                returnAwarded_[j] = true;
                                science_ += 1;
                            }
                        }
                    } else if (scienceEligible_[bi] && !visited_[bi]) {
                        // First landing on an eligible world: award a science point.
                        visited_[bi] = true;
                        science_ += 1;
                    }
                } else {
                    ship_.vel = body.vel + (relativeVel - 2.0f * normal * relativeVel.dot(normal)) * 0.3f;
                }
            }
        }

        // Fuel: the ship can only thrust while it has fuel, and burning it draws the tank down
        // proportionally to throttle.
        float stepThrottle = effThrottle;
        if (ship_.fuel <= 0.0f) {
            ship_.fuel = 0.0f;
            stepThrottle = 0.0f;
        } else {
            // Fuel burn scales with the thrust actually produced, so limiting thrust for
            // precision burns also sips fuel rather than wasting it.
            ship_.fuel = std::max(0.0f, ship_.fuel - stepThrottle * thrustLimit_ * subDt);
        }

        Vec2 thrustDir = {std::cos(ship_.angle), std::sin(ship_.angle)};
        totalAcc += thrustDir * (ship_.thrust * thrustLimit_ * stepThrottle / ship_.mass);

        ship_.vel += totalAcc * subDt;
        ship_.pos += ship_.vel * subDt;

        // Planet N-body interaction
        std::vector<Vec2> bodyAccs(bodies_.size(), {0, 0});
        for (size_t i = 0; i < bodies_.size(); ++i) {
            for (size_t j = 0; j < bodies_.size(); ++j) {
                if (i == j) continue;
                Vec2 r = bodies_[j].pos - bodies_[i].pos;
                float dSq = r.lengthSq();
                if (dSq > 1.0f) {
                    bodyAccs[i] += r.normalized() * (bodies_[j].mu / dSq);
                }
            }
        }
        for (size_t i = 0; i < bodies_.size(); ++i) {
            bodies_[i].vel += bodyAccs[i] * subDt;
            // Sun is fixed at origin to prevent whole-system drift for better gameplay
            if (i > 0) bodies_[i].pos += bodies_[i].vel * subDt;
        }
    }
}

bool Game::buyFuelUpgrade() {
    if (science_ < 1) return false;
    science_ -= 1;
    fuelUpgrades_ += 1;
    // Purchasing is only offered on Earth, so reflect the new capacity immediately and top up.
    ship_.maxFuel = getFuelCapacity();
    ship_.fuel = ship_.maxFuel;
    return true;
}

bool Game::unlockShip(ShipType t) {
    if (shipUnlocked_[(int)t]) return false;
    int cost = shipUnlockCost(t);
    if (science_ < cost) return false;
    science_ -= cost;
    shipUnlocked_[(int)t] = true;
    return true;
}

bool Game::saveTo(const std::string& path) const {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;

    writePod(os, kSaveMagic);
    writePod(os, kSaveVersion);
    writePod(os, (uint32_t)bodies_.size());
    for (const auto& b : bodies_) {
        writePod(os, b.pos.x); writePod(os, b.pos.y);
        writePod(os, b.vel.x); writePod(os, b.vel.y);
    }
    writePod(os, (int32_t)ship_.type);
    writePod(os, ship_.pos.x); writePod(os, ship_.pos.y);
    writePod(os, ship_.vel.x); writePod(os, ship_.vel.y);
    writePod(os, ship_.angle);
    writePod(os, ship_.fuel);
    writePod(os, zoom_);
    writePod(os, thrustLimit_);
    writePod(os, (int32_t)cameraMode_);

    // Progression (v2). Per-body visited/returnAwarded flags are written as bytes; their count
    // matches bodyCount above, validated on load.
    writePod(os, (int32_t)science_);
    writePod(os, (int32_t)fuelUpgrades_);
    for (int i = 0; i < 3; ++i) writePod(os, (uint8_t)(shipUnlocked_[i] ? 1 : 0));
    for (size_t i = 0; i < bodies_.size(); ++i) writePod(os, (uint8_t)(visited_[i] ? 1 : 0));
    for (size_t i = 0; i < bodies_.size(); ++i) writePod(os, (uint8_t)(returnAwarded_[i] ? 1 : 0));
    return (bool)os;
}

bool Game::hasValidSave(const std::string& path) const {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    uint32_t magic, version, bodyCount;
    if (!readPod(is, magic) || !readPod(is, version) || !readPod(is, bodyCount)) return false;
    return magic == kSaveMagic && version == kSaveVersion && bodyCount == bodies_.size();
}

bool Game::loadFrom(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    uint32_t magic, version, bodyCount;
    if (!readPod(is, magic) || !readPod(is, version) || !readPod(is, bodyCount)) return false;
    if (magic != kSaveMagic || version != kSaveVersion || bodyCount != bodies_.size()) return false;

    // Read everything into locals first, so a truncated/corrupt file can't leave the live
    // game half-overwritten — we only commit once all reads succeed.
    std::vector<Vec2> pos(bodyCount), vel(bodyCount);
    for (uint32_t i = 0; i < bodyCount; ++i) {
        if (!readPod(is, pos[i].x) || !readPod(is, pos[i].y) ||
            !readPod(is, vel[i].x) || !readPod(is, vel[i].y)) return false;
    }
    int32_t shipType, camMode;
    float sx, sy, svx, svy, sangle, sfuel, szoom, sthrust;
    if (!readPod(is, shipType) ||
        !readPod(is, sx) || !readPod(is, sy) || !readPod(is, svx) || !readPod(is, svy) ||
        !readPod(is, sangle) || !readPod(is, sfuel) ||
        !readPod(is, szoom) || !readPod(is, sthrust) || !readPod(is, camMode)) return false;
    if (shipType < 0 || shipType > (int32_t)ShipType::Falcon) return false;

    // Progression (v2).
    int32_t sScience, sFuelUpg;
    uint8_t sUnlock[3];
    std::vector<uint8_t> sVisited(bodyCount), sReturned(bodyCount);
    if (!readPod(is, sScience) || !readPod(is, sFuelUpg)) return false;
    for (int i = 0; i < 3; ++i) if (!readPod(is, sUnlock[i])) return false;
    for (uint32_t i = 0; i < bodyCount; ++i) if (!readPod(is, sVisited[i])) return false;
    for (uint32_t i = 0; i < bodyCount; ++i) if (!readPod(is, sReturned[i])) return false;

    // Commit. Progression first, so getFuelCapacity() (used by startWithShip) sees the upgrades.
    science_ = sScience;
    fuelUpgrades_ = sFuelUpg;
    for (int i = 0; i < 3; ++i) shipUnlocked_[i] = (sUnlock[i] != 0);
    shipUnlocked_[0] = true; // Triangle is always available
    for (uint32_t i = 0; i < bodyCount; ++i) {
        visited_[i] = (sVisited[i] != 0);
        returnAwarded_[i] = (sReturned[i] != 0);
    }

    // startWithShip rebuilds all type-derived ship fields (and resets the ship onto Earth),
    // then we overwrite the saved dynamic state on top of it.
    for (uint32_t i = 0; i < bodyCount; ++i) {
        bodies_[i].pos = pos[i];
        bodies_[i].vel = vel[i];
    }
    startWithShip((ShipType)shipType);
    ship_.pos = {sx, sy};
    ship_.vel = {svx, svy};
    ship_.angle = sangle;
    ship_.fuel = std::min(sfuel, ship_.maxFuel);
    zoom_ = szoom;
    thrustLimit_ = sthrust;
    cameraMode_ = (camMode == (int32_t)CameraMode::Body) ? CameraMode::Body : CameraMode::Ship;
    // Safety: never resume mid-burn or fast-forwarding.
    throttle_ = 0.0f;
    ship_.throttle = 0.0f;
    timeWarp_ = 1;
    return true;
}
