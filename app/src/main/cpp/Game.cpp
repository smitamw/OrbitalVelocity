#include "Game.h"
#include <cmath>
#include <algorithm>

Game::Game() : zoom_(0.01f), throttle_(0.0f), joystick_({0, 0}) {
    // Sun
    bodies_.push_back({{0, 0}, {0, 0}, 1000000.0f, 1000.0f, G * 1000000.0f, {1.0f, 0.8f, 0.0f, 1.0f}});

    // Earth
    float earthDist = 20000.0f;
    float earthVel = std::sqrt(bodies_[0].mu / earthDist);
    bodies_.push_back({{earthDist, 0}, {0, earthVel}, 10000.0f, 200.0f, G * 10000.0f, {0.2f, 0.6f, 1.0f, 1.0f}});

    // Moon - 1200 units is well within Earth's Hill sphere (~3000 units)
    float moonDist = 1200.0f;
    float moonVel = std::sqrt(bodies_[1].mu / moonDist);
    bodies_.push_back({{earthDist + moonDist, 0}, {0, earthVel + moonVel}, 100.0f, 50.0f, G * 100.0f, {0.7f, 0.7f, 0.7f, 1.0f}});

    // Ship - start orbiting Earth stably
    float shipDist = 350.0f;
    float shipRelVel = std::sqrt(bodies_[1].mu / shipDist);
    ship_.pos = {earthDist + shipDist, 0};
    ship_.vel = {0, earthVel + shipRelVel};
    ship_.angle = 0.0f;
    ship_.throttle = 0.0f;
    ship_.thrust = 120.0f;
    ship_.mass = 1.0f;
    ship_.color[0] = 1.0f; ship_.color[1] = 1.0f; ship_.color[2] = 1.0f; ship_.color[3] = 1.0f;
}

void Game::update(float dt) {
    dt = std::min(dt, 0.05f);

    if (joystick_.lengthSq() > 0.001f) {
        ship_.angle = std::atan2(joystick_.y, joystick_.x);
    }

    // Using more sub-steps for planetary stability
    int steps = 20;
    float subDt = dt / steps;

    for (int s = 0; s < steps; ++s) {
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

        Vec2 thrustDir = {std::cos(ship_.angle), std::sin(ship_.angle)};
        totalAcc += thrustDir * (ship_.thrust * throttle_ / ship_.mass);

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
