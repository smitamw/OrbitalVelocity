#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <vector>
#include <time.h>
#include <algorithm>
#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// UI Layout Constants
static const float JOYSTICK_CENTER_X_OFFSET = 0.5f;
static const float JOYSTICK_CENTER_Y = -0.6f;
static const float JOYSTICK_RADIUS = 0.12f;

static const float THROTTLE_X_MIN_OFFSET = 0.1f;
static const float THROTTLE_X_MAX_OFFSET = 0.25f;
static const float THROTTLE_Y_MIN = 0.5f;
static const float THROTTLE_Y_MAX = 0.9f;
static const float THROTTLE_SENSE_MARGIN = 0.25f;

static const float ZOOM_X_MIN_OFFSET = 0.1f;
static const float ZOOM_X_MAX_OFFSET = 0.25f;
static const float ZOOM_Y_MIN = -0.8f;
static const float ZOOM_Y_MAX = 0.8f;

static const float MIN_ZOOM = 0.00001f;
static const float MAX_ZOOM_FACTOR = 1000000.0f;

static const float CAM_BTN_CENTER_Y = 0.82f; // top-center
static const float CAM_BTN_HALF = 0.08f;     // half side length (small square)

static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)vertex";

static const char *fragment = R"fragment(#version 300 es
precision mediump float;
uniform vec4 uColor;
out vec4 outColor;
void main() {
    outColor = uColor;
}
)fragment";

Renderer::~Renderer() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }
}

void Renderer::initRenderer() {
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24, EGL_NONE
    };

    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);

    auto config = supportedConfigs[0];
    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    eglMakeCurrent(display, surface, surface, context);

    display_ = display;
    surface_ = surface;
    context_ = context;
    width_ = -1;
    height_ = -1;

    shader_ = std::unique_ptr<Shader>(Shader::loadShader(vertex, fragment));
    shader_->activate();

    glClearColor(0.0f, 0.0f, 0.05f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::updateRenderArea() {
    EGLint width, height;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);
    }
}

void Renderer::drawPolygon(const std::vector<Vec2>& points, float color[4]) {
    shader_->setColor(color);
    std::vector<float> vertices;
    for (const auto& p : points) {
        vertices.push_back(p.x);
        vertices.push_back(p.y);
        vertices.push_back(0.0f);
    }
    glVertexAttribPointer(shader_->getPositionLocation(), 3, GL_FLOAT, GL_FALSE, 0, vertices.data());
    glEnableVertexAttribArray(shader_->getPositionLocation());
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)points.size());
}

void Renderer::drawCircle(Vec2 center, float radius, int segments, float color[4]) {
    std::vector<Vec2> points;
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        points.push_back({center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius});
    }
    drawPolygon(points, color);
}

void Renderer::drawOrbit(Vec2 pos, Vec2 vel, const CelestialBody& primary, float mu, float color[4]) {
    Vec2 r = pos - primary.pos;
    Vec2 v = vel - primary.vel;

    float r_mag = r.length();
    float v_mag = v.length();
    if (r_mag < 1.0f) return;

    float h = r.x * v.y - r.y * v.x;
    float energy = v_mag * v_mag / 2.0f - mu / r_mag;

    Vec2 e_vec = { (v.y * h) / mu - r.x / r_mag, (-v.x * h) / mu - r.y / r_mag };

    float e = e_vec.length();
    float a = -mu / (2.0f * energy);

    float periapsis_angle = std::atan2(e_vec.y, e_vec.x);

    std::vector<float> orbitVertices;
    shader_->setColor(color);

    int numPoints = 200;
    if (e < 1.0f) { // Ellipse
        for (int i = 0; i <= numPoints; ++i) {
            float trueAnomaly = 2.0f * M_PI * i / numPoints;
            float dist = (a * (1.0f - e * e)) / (1.0f + e * std::cos(trueAnomaly));
            float angle = trueAnomaly + periapsis_angle;
            Vec2 p = primary.pos + Vec2{std::cos(angle) * dist, std::sin(angle) * dist};
            orbitVertices.push_back(p.x);
            orbitVertices.push_back(p.y);
            orbitVertices.push_back(0.0f);
        }
    } else { // Hyperbola
        float limit = std::acos(-1.0f / e) - 0.1f;
        for (int i = 0; i <= numPoints; ++i) {
            float trueAnomaly = -limit + 2.0f * limit * i / numPoints;
            float dist = (a * (1.0f - e * e)) / (1.0f + e * std::cos(trueAnomaly));
            float angle = trueAnomaly + periapsis_angle;
            Vec2 p = primary.pos + Vec2{std::cos(angle) * dist, std::sin(angle) * dist};
            orbitVertices.push_back(p.x);
            orbitVertices.push_back(p.y);
            orbitVertices.push_back(0.0f);
        }
    }

    glVertexAttribPointer(shader_->getPositionLocation(), 3, GL_FLOAT, GL_FALSE, 0, orbitVertices.data());
    glEnableVertexAttribArray(shader_->getPositionLocation());
    glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)orbitVertices.size() / 3);
}

// Minimalistic but clean 5x7 vector font
void Renderer::drawText(const std::string& text, Vec2 pos, float size, float color[4]) {
    float x = pos.x;
    float y = pos.y;
    float charW = size * 0.7f;
    float charH = size;
    float gap = size * 0.2f;

    auto drawLine = [&](float x1, float y1, float x2, float y2) {
        std::vector<float> v = { x + x1*charW, y + y1*charH, 0.0f, x + x2*charW, y + y2*charH, 0.0f };
        shader_->setColor(color);
        glVertexAttribPointer(shader_->getPositionLocation(), 3, GL_FLOAT, GL_FALSE, 0, v.data());
        glEnableVertexAttribArray(shader_->getPositionLocation());
        glDrawArrays(GL_LINES, 0, 2);
    };

    for (char c : text) {
        switch (c) {
            case 'T': drawLine(0, 1, 1, 1); drawLine(0.5f, 1, 0.5f, 0); break;
            case 'H': drawLine(0, 0, 0, 1); drawLine(1, 0, 1, 1); drawLine(0, 0.5f, 1, 0.5f); break;
            case 'R': drawLine(0, 0, 0, 1); drawLine(0, 1, 1, 1); drawLine(1, 1, 1, 0.5f); drawLine(1, 0.5f, 0, 0.5f); drawLine(0.5f, 0.5f, 1, 0); break;
            case 'J': drawLine(0, 0.2f, 0.5f, 0); drawLine(0.5f, 0, 1, 0); drawLine(1, 0, 1, 1); break;
            case 'O': drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 1); drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); break;
            case 'Y': drawLine(0, 1, 0.5f, 0.5f); drawLine(1, 1, 0.5f, 0.5f); drawLine(0.5f, 0.5f, 0.5f, 0); break;
            case 'Z': drawLine(0, 1, 1, 1); drawLine(1, 1, 0, 0); drawLine(0, 0, 1, 0); break;
            case 'M': drawLine(0, 0, 0, 1); drawLine(0, 1, 0.5f, 0.5f); drawLine(0.5f, 0.5f, 1, 1); drawLine(1, 1, 1, 0); break;
            case 'S': drawLine(0, 1, 1, 1); drawLine(0, 1, 0, 0.5f); drawLine(0, 0.5f, 1, 0.5f); drawLine(1, 0.5f, 1, 0); drawLine(1, 0, 0, 0); break;
            case 'I': drawLine(0.5f, 0, 0.5f, 1); drawLine(0.2f, 1, 0.8f, 1); drawLine(0.2f, 0, 0.8f, 0); break;
            case 'P': drawLine(0, 0, 0, 1); drawLine(0, 1, 1, 1); drawLine(1, 1, 1, 0.5f); drawLine(1, 0.5f, 0, 0.5f); break;
            case 'B': drawLine(0, 0, 0, 1); drawLine(0, 1, 1, 1); drawLine(1, 1, 1, 0.5f); drawLine(0, 0.5f, 1, 0.5f); drawLine(1, 0.5f, 1, 0); drawLine(0, 0, 1, 0); break;
            case 'D': drawLine(0, 0, 0, 1); drawLine(0, 1, 0.7f, 1); drawLine(0.7f, 1, 1, 0.5f); drawLine(1, 0.5f, 0.7f, 0); drawLine(0.7f, 0, 0, 0); break;
        }
        x += charW + gap;
    }
}

void Renderer::render() {
    updateRenderArea();

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double currentTime = now.tv_sec + now.tv_nsec * 1e-9;
    if (lastTime_ == 0) lastTime_ = currentTime;
    float dt = (float)(currentTime - lastTime_);
    lastTime_ = currentTime;

    game_.update(dt);

    glClear(GL_COLOR_BUFFER_BIT);
    shader_->activate();

    float aspect = float(width_) / height_;
    float zoom = game_.getZoom();
    const Ship& ship = game_.getShip();
    const auto& bodies = game_.getBodies();

    // Determine the dominant gravitational body (the one the ship's conic is drawn around)
    float dominantWeight = -1.0f;
    const CelestialBody* primary = nullptr;
    for (const auto& body : bodies) {
        Vec2 r = body.pos - ship.pos;
        float weight = body.mu / r.lengthSq();
        if (weight > dominantWeight) {
            dominantWeight = weight;
            primary = &body;
        }
    }

    // Camera center depends on the selected mode
    Vec2 camCenter = ship.pos;
    if (game_.getCameraMode() == CameraMode::Body && primary) camCenter = primary->pos;

    auto setMVP = [&](Vec2 pos, float scale, float angle) {
        float m[16];
        Utility::buildIdentityMatrix(m);
        float viewScaleX = zoom / aspect;
        float viewScaleY = zoom;
        Vec2 relPos = pos - camCenter;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        m[0] = (cosA * scale) * viewScaleX;
        m[1] = (sinA * scale) * viewScaleY;
        m[4] = (-sinA * scale) * viewScaleX;
        m[5] = (cosA * scale) * viewScaleY;
        m[12] = relPos.x * viewScaleX;
        m[13] = relPos.y * viewScaleY;
        shader_->setMVP(m);
    };

    // Global transform for orbits
    float globalMVP[16];
    Utility::buildIdentityMatrix(globalMVP);
    globalMVP[0] = zoom / aspect;
    globalMVP[5] = zoom;
    globalMVP[12] = -camCenter.x * zoom / aspect;
    globalMVP[13] = -camCenter.y * zoom;
    shader_->setMVP(globalMVP);

    // Draw Planet Orbits
    float planetOrbitColor[4] = {1, 1, 1, 0.15f};
    if (bodies.size() >= 2) drawOrbit(bodies[1].pos, bodies[1].vel, bodies[0], bodies[0].mu, planetOrbitColor);
    if (bodies.size() >= 3) drawOrbit(bodies[2].pos, bodies[2].vel, bodies[1], bodies[1].mu, planetOrbitColor);

    // Draw Ship Orbit
    if (primary) {
        float shipOrbitColor[4] = {1, 1, 1, 0.5f};
        drawOrbit(ship.pos, ship.vel, *primary, primary->mu, shipOrbitColor);
    }

    // Draw Bodies
    for (const auto& body : bodies) {
        setMVP(body.pos, body.radius, 0);
        float c[4]; std::copy(body.color, body.color+4, c);
        drawCircle({0,0}, 1.0f, 64, c);
    }

    // Draw Ship
    float shipScale = 10.0f + 0.02f / zoom;
    setMVP(ship.pos, shipScale, ship.angle);
    float shipColor[4] = {1, 1, 1, 1};
    drawPolygon({{1, 0}, {-0.6, 0.4}, {-0.6, -0.4}}, shipColor);

    // UI Rendering
    float uiMVP[16];
    Utility::buildIdentityMatrix(uiMVP);
    uiMVP[0] = 1.0f / aspect;
    shader_->setMVP(uiMVP);

    float uiColor[4] = {1, 1, 1, 0.2f};
    float activeColor[4] = {1, 1, 1, 0.6f};

    // Joystick Base
    Vec2 joyCenter = {-aspect + JOYSTICK_CENTER_X_OFFSET, JOYSTICK_CENTER_Y};
    drawCircle(joyCenter, JOYSTICK_RADIUS, 32, uiColor);

    // Throttle bar
    float tx1 = -aspect + THROTTLE_X_MIN_OFFSET;
    float tx2 = -aspect + THROTTLE_X_MAX_OFFSET;
    drawPolygon({{tx1, THROTTLE_Y_MIN}, {tx2, THROTTLE_Y_MIN}, {tx2, THROTTLE_Y_MAX}, {tx1, THROTTLE_Y_MAX}}, uiColor);
    float t = game_.getShip().throttle;
    drawPolygon({{tx1, THROTTLE_Y_MIN}, {tx2, THROTTLE_Y_MIN}, {tx2, THROTTLE_Y_MIN + (THROTTLE_Y_MAX - THROTTLE_Y_MIN) * t}, {tx1, THROTTLE_Y_MIN + (THROTTLE_Y_MAX - THROTTLE_Y_MIN) * t}}, activeColor);

    // Zoom bar
    float zx1 = aspect - ZOOM_X_MAX_OFFSET;
    float zx2 = aspect - ZOOM_X_MIN_OFFSET;
    drawPolygon({{zx1, ZOOM_Y_MIN}, {zx2, ZOOM_Y_MIN}, {zx2, ZOOM_Y_MAX}, {zx1, ZOOM_Y_MAX}}, uiColor);
    // Indicator for current zoom
    float zLevel = (std::log10(game_.getZoom() / MIN_ZOOM)) / std::log10(MAX_ZOOM_FACTOR);
    zLevel = std::max(0.0f, std::min(1.0f, zLevel));
    float zPos = ZOOM_Y_MIN + (ZOOM_Y_MAX - ZOOM_Y_MIN) * zLevel;
    drawPolygon({{zx1, zPos - 0.05f}, {zx2, zPos - 0.05f}, {zx2, zPos + 0.05f}, {zx1, zPos + 0.05f}}, activeColor);

    // Labels
    float textColor[4] = {1, 1, 1, 0.5f};
    drawText("THR", {-aspect + THROTTLE_X_MIN_OFFSET, THROTTLE_Y_MAX + 0.03f}, 0.05f, textColor);
    drawText("JOY", {joyCenter.x - 0.1f, joyCenter.y - JOYSTICK_RADIUS - 0.1f}, 0.05f, textColor);
    drawText("ZOOM", {aspect - ZOOM_X_MAX_OFFSET, ZOOM_Y_MAX + 0.03f}, 0.05f, textColor);

    // Camera mode button (top-center, translucent square with a camera icon)
    float bx1 = -CAM_BTN_HALF, bx2 = CAM_BTN_HALF;
    float by1 = CAM_BTN_CENTER_Y - CAM_BTN_HALF, by2 = CAM_BTN_CENTER_Y + CAM_BTN_HALF;
    drawPolygon({{bx1, by1}, {bx2, by1}, {bx2, by2}, {bx1, by2}}, uiColor);

    // Camera icon: body rectangle, viewfinder bump, and lens
    float iconW = CAM_BTN_HALF * 0.62f;  // half-width of camera body
    float iconH = CAM_BTN_HALF * 0.42f;  // half-height of camera body
    float cy = CAM_BTN_CENTER_Y;
    drawPolygon({{-iconW, cy - iconH}, {iconW, cy - iconH}, {iconW, cy + iconH}, {-iconW, cy + iconH}}, activeColor);
    float bumpW = CAM_BTN_HALF * 0.18f;
    drawPolygon({{-bumpW, cy + iconH}, {bumpW, cy + iconH}, {bumpW, cy + iconH + CAM_BTN_HALF * 0.16f}, {-bumpW, cy + iconH + CAM_BTN_HALF * 0.16f}}, activeColor);
    drawCircle({0, cy}, CAM_BTN_HALF * 0.26f, 24, uiColor);

    // Mode label above the button
    const char *modeLabel = game_.getCameraMode() == CameraMode::Ship ? "SHIP" : "BODY";
    drawText(modeLabel, {-0.1f, by2 + 0.03f}, 0.05f, textColor);

    eglSwapBuffers(display_, surface_);
}

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    float aspect = float(width_) / height_;

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        auto action = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
        auto pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        for (int p = 0; p < motionEvent.pointerCount; ++p) {
            float x = (GameActivityPointerAxes_getX(&motionEvent.pointers[p]) / width_ * 2.0f - 1.0f) * aspect;
            float y = -(GameActivityPointerAxes_getY(&motionEvent.pointers[p]) / height_ * 2.0f - 1.0f);

            bool isUp = (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) && (p == pointerIndex);
            bool isDown = (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) && (p == pointerIndex);

            // Camera mode button (top-center): toggle on tap
            if (isDown && x >= -CAM_BTN_HALF && x <= CAM_BTN_HALF &&
                y >= CAM_BTN_CENTER_Y - CAM_BTN_HALF && y <= CAM_BTN_CENTER_Y + CAM_BTN_HALF) {
                game_.toggleCameraMode();
            }

            // Joystick Logic
            Vec2 joyCenter = {-aspect + JOYSTICK_CENTER_X_OFFSET, JOYSTICK_CENTER_Y};
            Vec2 joyDelta = {x - joyCenter.x, y - joyCenter.y};
            if (joyDelta.length() < JOYSTICK_RADIUS) {
                if (isUp) {
                    game_.setJoystick({0, 0});
                } else {
                    game_.setJoystick(joyDelta.normalized() * (joyDelta.length() / JOYSTICK_RADIUS));
                }
            } else if (isUp && joyDelta.length() < JOYSTICK_RADIUS * 1.5f) {
                game_.setJoystick({0, 0});
            }

            // Throttle Logic
            float tx1 = -aspect + THROTTLE_X_MIN_OFFSET;
            float tx2 = -aspect + THROTTLE_X_MAX_OFFSET;
            if (x >= tx1 && x <= tx2 && y >= THROTTLE_Y_MIN - THROTTLE_SENSE_MARGIN && y <= THROTTLE_Y_MAX + THROTTLE_SENSE_MARGIN) {
                if (!isUp) {
                    float t = (y - THROTTLE_Y_MIN) / (THROTTLE_Y_MAX - THROTTLE_Y_MIN);
                    game_.setThrottle(std::max(0.0f, std::min(1.0f, t)));
                }
            }

            // Zoom Logic
            float zx1 = aspect - ZOOM_X_MAX_OFFSET;
            float zx2 = aspect - ZOOM_X_MIN_OFFSET;
            if (x >= zx1 && x <= zx2 && y >= ZOOM_Y_MIN && y <= ZOOM_Y_MAX) {
                if (!isUp) {
                    float z = (y - ZOOM_Y_MIN) / (ZOOM_Y_MAX - ZOOM_Y_MIN);
                    game_.setZoom(MIN_ZOOM * std::pow(MAX_ZOOM_FACTOR, std::max(0.0f, std::min(1.0f, z))));
                }
            }
        }
    }
    android_app_clear_motion_events(inputBuffer);
}
