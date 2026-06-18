#include "Game.h"
#include <cmath>
#include <algorithm>

Game::Game() : zoom_(0.02f), throttle_(0.0f), joystick_({0, 0}) {
    // Helper: a planet in a circular heliocentric orbit at `dist` from the Sun.
    auto makePlanet = [&](float dist, float mass, float radius, float color[4]) {
        float vel = std::sqrt(bodies_[0].mu / dist); // index 0 is always the Sun
        bodies_.push_back({{dist, 0}, {0, vel}, mass, radius, G * mass, {color[0], color[1], color[2], color[3]}, 0});
    };
    // Helper: a moon in a circular orbit at `dist` from its parent planet.
    auto makeMoon = [&](int parent, float dist, float mass, float radius, float color[4]) {
        const CelestialBody& p = bodies_[parent];
        float vel = std::sqrt(p.mu / dist);
        bodies_.push_back({{p.pos.x + dist, p.pos.y}, {p.vel.x, p.vel.y + vel}, mass, radius, G * mass, {color[0], color[1], color[2], color[3]}, parent});
    };

    // Index 0: Sun (pinned at origin, no parent)
    bodies_.push_back({{0, 0}, {0, 0}, 1000000.0f, 1000.0f, G * 1000000.0f, {1.0f, 0.8f, 0.0f, 1.0f}, -1});

    // Inner planets. Distances are roughly proportional to real semi-major axes,
    // scaled so Earth sits at 20000 units (Mercury 0.39 AU, Venus 0.72, Mars 1.52).
    float mercuryColor[4] = {0.6f, 0.55f, 0.5f, 1.0f}; // gray-brown
    float venusColor[4]   = {0.9f, 0.8f, 0.5f, 1.0f};  // pale yellow
    float earthColor[4]   = {0.2f, 0.6f, 1.0f, 1.0f};  // blue
    float marsColor[4]    = {0.8f, 0.35f, 0.2f, 1.0f}; // reddish
    makePlanet(7700.0f,  600.0f,   90.0f,  mercuryColor); // index 1: Mercury
    makePlanet(14500.0f, 8100.0f,  190.0f, venusColor);   // index 2: Venus

    float earthDist = 20000.0f;
    float earthVel = std::sqrt(bodies_[0].mu / earthDist);
    makePlanet(earthDist, 10000.0f, 200.0f, earthColor);  // index 3: Earth
    const int EARTH = 3;

    // Moon - 1200 units is well within Earth's Hill sphere (~3000 units)
    float moonColor[4] = {0.7f, 0.7f, 0.7f, 1.0f};
    makeMoon(EARTH, 1200.0f, 100.0f, 50.0f, moonColor);   // index 4: Moon

    makePlanet(30500.0f, 1100.0f, 110.0f, marsColor);     // index 5: Mars
    const int MARS = 5;

    // Mars moons. Distances are exaggerated for visibility (Phobos inside Deimos);
    // both stay well within Mars's Hill sphere (~2100 units at this orbit).
    float phobosColor[4] = {0.6f, 0.55f, 0.5f, 1.0f};
    float deimosColor[4] = {0.65f, 0.6f, 0.55f, 1.0f};
    makeMoon(MARS, 600.0f,  1.0f, 14.0f, phobosColor);    // index 6: Phobos
    makeMoon(MARS, 1300.0f, 0.5f, 10.0f, deimosColor);    // index 7: Deimos

    // Outer planets. Heliocentric distances stay proportional to the real semi-major
    // axes (Earth = 20000 units): Jupiter 5.20 AU, Saturn 9.58, Uranus 19.18, Neptune 30.07.
    // Masses are kept well below the Sun's so the system stays stable; their large Hill
    // spheres (a consequence of the wide orbits) leave plenty of room for moons. Moon
    // distances preserve the real ordering/ratios but are scaled for visibility, like the
    // inner moons.
    float jupiterColor[4] = {0.80f, 0.70f, 0.50f, 1.0f}; // banded tan
    float saturnColor[4]  = {0.85f, 0.78f, 0.55f, 1.0f}; // pale gold
    float uranusColor[4]  = {0.60f, 0.85f, 0.90f, 1.0f}; // pale cyan
    float neptuneColor[4] = {0.25f, 0.40f, 0.85f, 1.0f}; // deep blue

    int JUPITER = (int)bodies_.size();
    makePlanet(104000.0f, 40000.0f, 600.0f, jupiterColor);
    float ioColor[4]       = {0.90f, 0.85f, 0.40f, 1.0f}; // sulfur yellow
    float europaColor[4]   = {0.85f, 0.85f, 0.80f, 1.0f}; // icy white
    float ganymedeColor[4] = {0.60f, 0.55f, 0.50f, 1.0f}; // tan-gray
    float callistoColor[4] = {0.45f, 0.42f, 0.40f, 1.0f}; // dark gray
    makeMoon(JUPITER, 1800.0f, 30.0f, 40.0f, ioColor);
    makeMoon(JUPITER, 2850.0f, 25.0f, 35.0f, europaColor);
    makeMoon(JUPITER, 4550.0f, 50.0f, 58.0f, ganymedeColor);
    makeMoon(JUPITER, 7800.0f, 45.0f, 52.0f, callistoColor);

    int SATURN = (int)bodies_.size();
    makePlanet(191600.0f, 28000.0f, 520.0f, saturnColor);
    float enceladusColor[4] = {0.90f, 0.90f, 0.95f, 1.0f}; // bright ice
    float rheaColor[4]      = {0.70f, 0.70f, 0.70f, 1.0f}; // gray
    float titanColor[4]     = {0.85f, 0.60f, 0.30f, 1.0f}; // orange haze
    float iapetusColor[4]   = {0.50f, 0.45f, 0.40f, 1.0f}; // two-tone brown
    makeMoon(SATURN, 950.0f,   3.0f, 14.0f, enceladusColor);
    makeMoon(SATURN, 2050.0f, 15.0f, 30.0f, rheaColor);
    makeMoon(SATURN, 4800.0f, 60.0f, 55.0f, titanColor);
    makeMoon(SATURN, 12000.0f, 12.0f, 28.0f, iapetusColor);

    int URANUS = (int)bodies_.size();
    makePlanet(383600.0f, 15000.0f, 360.0f, uranusColor);
    float mirandaColor[4] = {0.60f, 0.60f, 0.65f, 1.0f};
    float arielColor[4]   = {0.70f, 0.70f, 0.72f, 1.0f};
    float titaniaColor[4] = {0.65f, 0.60f, 0.60f, 1.0f};
    float oberonColor[4]  = {0.55f, 0.50f, 0.50f, 1.0f};
    makeMoon(URANUS, 2000.0f,  4.0f, 14.0f, mirandaColor);
    makeMoon(URANUS, 2950.0f, 10.0f, 26.0f, arielColor);
    makeMoon(URANUS, 6700.0f, 16.0f, 32.0f, titaniaColor);
    makeMoon(URANUS, 9000.0f, 15.0f, 31.0f, oberonColor);

    int NEPTUNE = (int)bodies_.size();
    makePlanet(601400.0f, 17000.0f, 350.0f, neptuneColor);
    float proteusColor[4] = {0.45f, 0.45f, 0.50f, 1.0f}; // dark
    float tritonColor[4]  = {0.80f, 0.75f, 0.78f, 1.0f}; // pinkish ice
    makeMoon(NEPTUNE, 2000.0f,  6.0f, 16.0f, proteusColor);
    makeMoon(NEPTUNE, 6000.0f, 40.0f, 45.0f, tritonColor);

    // Ship - placed landed on Earth, ready to launch. The triangle is the default until the
    // player picks another in the Customize screen.
    startWithShip(ShipType::Triangle);
}

void Game::startWithShip(ShipType type) {
    // Earth is index 3 (see constructor). Rest the ship on its surface, sharing Earth's
    // orbital velocity so it stays put, with the nose pointing radially outward (+x here,
    // away from Earth) so full throttle lifts it straight off the ground.
    const CelestialBody& earth = bodies_[3];
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

    ship_.type = type;
    switch (type) {
        case ShipType::Triangle: ship_.infiniteFuel = false; ship_.maxFuel = ship_.fuel = 6.0f; break;  // low
        case ShipType::Rocket:   ship_.infiniteFuel = false; ship_.maxFuel = ship_.fuel = 15.0f; break;  // medium
        case ShipType::Falcon:   ship_.infiniteFuel = true;  ship_.maxFuel = ship_.fuel = 1.0f;  break;  // infinite
    }
}

void Game::update(float dt) {
    dt = std::min(dt, 0.05f);

    if (joystick_.lengthSq() > 0.001f) {
        ship_.angle = std::atan2(joystick_.y, joystick_.x);
    }

    // Using more sub-steps for planetary stability. Timewarp keeps the per-substep
    // duration constant (so integration accuracy is unchanged) and instead runs more
    // substeps per frame, simulating timeWarp_ * dt seconds each frame.
    int baseSteps = 20;
    float subDt = dt / baseSteps;
    int totalSteps = baseSteps * timeWarp_;

    // Thrust is disabled while warping (timeWarp_ > 1); the ship coasts on rails.
    float effThrottle = (timeWarp_ > 1) ? 0.0f : throttle_;

    for (int s = 0; s < totalSteps; ++s) {
        Vec2 totalAcc = {0, 0};
        for (auto& body : bodies_) {
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
                } else {
                    ship_.vel = body.vel + (relativeVel - 2.0f * normal * relativeVel.dot(normal)) * 0.3f;
                }
            }
        }

        // Fuel: a finite-fuel ship can only thrust while it has fuel, and burning it draws
        // the tank down proportionally to throttle. The Falcon (infiniteFuel) never depletes.
        float stepThrottle = effThrottle;
        if (!ship_.infiniteFuel) {
            if (ship_.fuel <= 0.0f) {
                ship_.fuel = 0.0f;
                stepThrottle = 0.0f;
            } else {
                // Fuel burn scales with the thrust actually produced, so limiting thrust for
                // precision burns also sips fuel rather than wasting it.
                ship_.fuel = std::max(0.0f, ship_.fuel - stepThrottle * thrustLimit_ * subDt);
            }
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
