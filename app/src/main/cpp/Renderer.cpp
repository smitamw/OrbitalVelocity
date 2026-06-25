#include "Renderer.h"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <vector>
#include <time.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// UI Layout Constants
// Combined floating joystick: it spawns wherever the finger touches. Heading comes from the
// drag direction; throttle from the drag distance (past a small deadzone, up to a max reach).
static const float JOY_TOLERANCE    = 0.1f; // deadzone radius: throttle stays 0 within this
static const float JOY_MAX_RANGE    = 0.3f; // drag distance for full throttle
static const float JOY_DIR_DEADZONE = 0.02f; // min drag before heading updates
static const float JOY_KNOB_RADIUS  = 0.05f; // rendered knob size

// Fuel gauge: a vertical bar on the right edge (where the zoom slider used to be; zoom is
// now pinch-driven).
static const float FUEL_X_MIN_OFFSET = 0.05f;  // right edge gap
static const float FUEL_X_MAX_OFFSET = 0.10f;  // left edge (visual width 0.05)
static const float FUEL_Y_MIN = -0.3f;
static const float FUEL_Y_MAX = 0.5f;

// HUD auto-fade: the in-game controls stay fully opaque until HUD_FADE_START seconds of
// inactivity, then fade out over HUD_FADE_DUR seconds. Any touch resets the timer.
static const float HUD_FADE_START = 5.0f;
static const float HUD_FADE_DUR = 1.0f;

// Timewarp buttons: small squares in the bottom-left (below the joystick), each labeled
// with its multiplier (e.g. "1X", "50X", "500X"). They wrap into two rows so the full set
// fits in portrait orientation (a single row overflows the right edge when aspect < 1).
static const float TW_BTN_HALF       = 0.07f;  // half side of each warp button
static const float TW_BTN_SPACING    = 0.15f;  // horizontal distance between button centers
static const float TW_ROW_SPACING    = 0.16f;  // vertical distance between rows (stack upward)
static const float TW_ROW_Y          = -0.88f; // bottom row center y
static const float TW_FIRST_X_OFFSET = 0.10f;  // first column center x = -aspect + this
static const float TW_LABEL_SIZE     = 0.032f; // label text size (fits the widest, "100X"/"500X")
static const int   TW_PER_ROW        = 4;       // buttons per row (bottom row of 4, upper of 3)
static const int   TW_LEVELS[]       = {1, 2, 5, 10, 50, 100, 500};
static constexpr int TW_LEVEL_COUNT  = sizeof(TW_LEVELS) / sizeof(TW_LEVELS[0]);

// MIN_ZOOM sets the maximum zoom-out; lowered so the outer planets (Neptune ~601400
// units) fit on screen. MAX_ZOOM_FACTOR is raised in step to preserve the zoom-in limit
// (max zoom = MIN_ZOOM * MAX_ZOOM_FACTOR = 10, unchanged).
static const float MIN_ZOOM = 0.0000001f;
static const float MAX_ZOOM_FACTOR = 1000000.0f;

// Camera mode button: a small square tucked into the upper-right corner.
static const float CAM_BTN_CENTER_Y = 0.82f; // near the top
static const float CAM_BTN_X_OFFSET = 0.12f; // center x = aspect - this (right corner)
static const float CAM_BTN_HALF = 0.08f;     // half side length (small square)

// Thrust limiter: a horizontal slider in the upper-left that caps engine thrust for precise
// maneuvers. Full (right end) = uncapped thrust, enough to lift off Earth.
static const float TL_X_OFFSET     = 0.10f;  // left end x = -aspect + this
static const float TL_LENGTH       = 0.50f;  // horizontal length
static const float TL_Y            = 0.88f;  // vertical center (upper area)
static const float TL_HALF_H       = 0.025f; // bar half-thickness
static const float TL_SENSE_MARGIN = 0.08f;  // easier to grab

// "Change ship" button: appears below the thrust limiter only while the ship is parked on
// Earth (its home base). Left-aligned under the slider; opens the ship-selection screen.
static const float CS_BTN_HALF_W = 0.23f;   // half width (fits "CHANGE SHIP" at CS_LABEL_SIZE)
static const float CS_BTN_HALF_H = 0.06f;   // half height
static const float CS_BTN_Y      = 0.66f;   // center y (just below the THRUST LIMIT label ~0.78)
static const float CS_LABEL_SIZE = 0.045f;  // label text size

// "Upgrades" button: stacked just below "Change ship" (same width/height), also Earth-only.
static const float UP_BTN_Y = 0.52f;        // center y (below the Change ship button)

// Science-points readout: lower-right, tucked below the fuel gauge (which ends at y -0.3) and
// above the timewarp rows (whose top row tops out near y -0.65). Sitting between them keeps it
// clear of both even in portrait, where the 4-wide timewarp row reaches past screen center.
static const float SCI_X_OFFSET   = 0.26f;  // center x = aspect - this
static const float SCI_Y          = -0.45f;
static const float SCI_LABEL_SIZE = 0.05f;

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
    // Backstop save: the destructor runs on APP_CMD_TERM_WINDOW (always before the process is
    // torn down on backgrounding). The primary trigger is APP_CMD_PAUSE (see main.cpp).
    saveState();
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        eglTerminate(display_);
    }
}

std::string Renderer::savePath() const {
    if (!app_ || !app_->activity || !app_->activity->internalDataPath) return std::string();
    return std::string(app_->activity->internalDataPath) + "/savegame.dat";
}

void Renderer::saveState() {
    if (!gameStarted_) return; // nothing to persist until a game is actually in progress
    std::string p = savePath();
    if (!p.empty()) game_.saveTo(p);
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

void Renderer::drawCircleOutline(Vec2 center, float radius, int segments, float color[4]) {
    shader_->setColor(color);
    std::vector<float> vertices;
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        vertices.push_back(center.x + std::cos(angle) * radius);
        vertices.push_back(center.y + std::sin(angle) * radius);
        vertices.push_back(0.0f);
    }
    glVertexAttribPointer(shader_->getPositionLocation(), 3, GL_FLOAT, GL_FALSE, 0, vertices.data());
    glEnableVertexAttribArray(shader_->getPositionLocation());
    glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)segments);
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
            case 'A': drawLine(0, 0, 0.5f, 1); drawLine(0.5f, 1, 1, 0); drawLine(0.2f, 0.4f, 0.8f, 0.4f); break;
            case 'C': drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); break;
            case 'E': drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); drawLine(0, 0.5f, 0.7f, 0.5f); break;
            case 'F': drawLine(0, 0, 0, 1); drawLine(0, 1, 1, 1); drawLine(0, 0.5f, 0.7f, 0.5f); break;
            case 'G': drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 0.5f); drawLine(1, 0.5f, 0.5f, 0.5f); break;
            case 'K': drawLine(0, 0, 0, 1); drawLine(0, 0.5f, 1, 1); drawLine(0, 0.5f, 1, 0); break;
            case 'L': drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); break;
            case 'N': drawLine(0, 0, 0, 1); drawLine(0, 1, 1, 0); drawLine(1, 0, 1, 1); break;
            case 'Q': drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 1); drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0.6f, 0.4f, 1, 0); break;
            case 'U': drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 1); break;
            case 'V': drawLine(0, 1, 0.5f, 0); drawLine(0.5f, 0, 1, 1); break;
            case 'W': drawLine(0, 1, 0.25f, 0); drawLine(0.25f, 0, 0.5f, 0.6f); drawLine(0.5f, 0.6f, 0.75f, 0); drawLine(0.75f, 0, 1, 1); break;
            case 'X': drawLine(0, 0, 1, 1); drawLine(0, 1, 1, 0); break;
            case '0': drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 1); drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 1); break;
            case '1': drawLine(0.3f, 0.7f, 0.5f, 1); drawLine(0.5f, 1, 0.5f, 0); drawLine(0.2f, 0, 0.8f, 0); break;
            case '2': drawLine(0, 1, 1, 1); drawLine(1, 1, 1, 0.5f); drawLine(1, 0.5f, 0, 0); drawLine(0, 0, 1, 0); break;
            case '3': drawLine(0, 1, 1, 1); drawLine(1, 1, 1, 0); drawLine(1, 0, 0, 0); drawLine(0.4f, 0.5f, 1, 0.5f); break;
            case '4': drawLine(0, 1, 0, 0.5f); drawLine(0, 0.5f, 1, 0.5f); drawLine(1, 1, 1, 0); break;
            case '5': drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0.5f); drawLine(0, 0.5f, 1, 0.5f); drawLine(1, 0.5f, 1, 0); drawLine(1, 0, 0, 0); break;
            case '6': drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 0.5f); drawLine(1, 0.5f, 0, 0.5f); break;
            case '7': drawLine(0, 1, 1, 1); drawLine(1, 1, 0.4f, 0); break;
            case '8': drawLine(0, 0, 1, 0); drawLine(1, 0, 1, 1); drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0); drawLine(0, 0.5f, 1, 0.5f); break;
            case '9': drawLine(1, 0, 1, 1); drawLine(1, 1, 0, 1); drawLine(0, 1, 0, 0.5f); drawLine(0, 0.5f, 1, 0.5f); break;
            case '.': drawLine(0.35f, 0.0f, 0.55f, 0.0f); break;
            case '+': drawLine(0.2f, 0.5f, 0.8f, 0.5f); drawLine(0.5f, 0.2f, 0.5f, 0.8f); break;
            case '-': drawLine(0.2f, 0.5f, 0.8f, 0.5f); break;
        }
        x += charW + gap;
    }
}

// Draws text horizontally centered on `center` (and roughly vertically centered too).
void Renderer::drawTextCentered(const std::string& text, Vec2 center, float size, float color[4]) {
    float advance = size * 0.7f + size * 0.2f; // charW + gap, matching drawText
    float width = text.empty() ? 0.0f : (text.size() * advance - size * 0.2f);
    drawText(text, {center.x - width * 0.5f, center.y - size * 0.5f}, size, color);
}

bool Renderer::hitRect(float x, float y, float cx, float cy, float halfW, float halfH) {
    return x >= cx - halfW && x <= cx + halfW && y >= cy - halfH && y <= cy + halfH;
}

// Sets an MVP that maps local geometry to a UI-space position/scale/rotation (no camera),
// matching the [-aspect,aspect] x [-1,1] convention used for the HUD and menus.
void Renderer::setUIMVP(Vec2 pos, float scale, float angle) {
    float aspect = float(width_) / height_;
    float m[16];
    Utility::buildIdentityMatrix(m);
    float cosA = std::cos(angle), sinA = std::sin(angle);
    m[0] = (cosA * scale) / aspect;
    m[1] = (sinA * scale);
    m[4] = (-sinA * scale) / aspect;
    m[5] = (cosA * scale);
    m[12] = pos.x / aspect;
    m[13] = pos.y;
    shader_->setMVP(m);
}

// Cheap per-call pseudo-random float in [0,1) (xorshift32). Used only for exhaust flicker,
// so the quality/seeding doesn't matter; it just needs to vary every frame.
static float flickerRand() {
    static uint32_t s = 2463534242u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFFu) / float(0x1000000);
}

// Draws a ship's local-space geometry. All variants point +x (nose forward), matching the
// original triangle, so the same shape works in-world (via setMVP) and in menu previews
// (via setUIMVP). The caller sets the MVP first.
//
// `thrust` is the ship's current thrust as a fraction of full (0..1). When it's >0 the ship
// emits an exhaust plume — unique in size/shape/color per variant — whose length and brightness
// scale with thrust and flicker slightly every frame. Menu previews pass 0 (no flame).
void Renderer::drawShipShape(ShipType type, float alpha, float thrust) {
    // Per-frame flame modulation. lenF drives plume length, bright drives opacity; each gets an
    // independent random jitter so the two flicker separately, like a real exhaust.
    const bool flaming = thrust > 0.0f;
    const float lenF   = thrust * (0.9f + 0.2f * flickerRand());
    const float bright = alpha * std::min(1.0f, (0.55f + 0.45f * thrust) * (0.85f + 0.15f * flickerRand()));

    switch (type) {
        case ShipType::Triangle: {
            // Red, narrow, long single-point plume from the flat rear edge at x=-0.6.
            if (flaming) {
                float outer[4] = {1.00f, 0.20f, 0.08f, bright};
                float core[4]  = {1.00f, 0.55f, 0.20f, bright};
                drawPolygon({{-0.6f, 0.22f}, {-0.6f - 0.7f * lenF, 0.0f}, {-0.6f, -0.22f}}, outer);
                drawPolygon({{-0.6f, 0.11f}, {-0.6f - 0.55f * lenF, 0.0f}, {-0.6f, -0.11f}}, core);
            }
            float white[4] = {1, 1, 1, alpha};
            drawPolygon({{1, 0}, {-0.6f, 0.4f}, {-0.6f, -0.4f}}, white);
            break;
        }
        case ShipType::Rocket: {
            // Orange, medium plume with a yellow-white core, from the fuselage tail at x=-0.6.
            if (flaming) {
                float outer[4] = {1.00f, 0.50f, 0.08f, bright};
                float core[4]  = {1.00f, 0.85f, 0.45f, bright};
                drawPolygon({{-0.6f, 0.18f}, {-0.6f - 0.6f * lenF, 0.0f}, {-0.6f, -0.18f}}, outer);
                drawPolygon({{-0.6f, 0.09f}, {-0.6f - 0.4f * lenF, 0.0f}, {-0.6f, -0.09f}}, core);
            }
            float body[4]  = {0.95f, 0.95f, 1.0f, alpha};
            float nose[4]  = {0.90f, 0.30f, 0.25f, alpha};
            float fin[4]   = {0.85f, 0.25f, 0.20f, alpha};
            float win[4]   = {0.40f, 0.70f, 1.00f, alpha};
            drawPolygon({{-0.6f, 0.28f}, {0.45f, 0.28f}, {0.45f, -0.28f}, {-0.6f, -0.28f}}, body); // fuselage
            drawPolygon({{0.45f, 0.28f}, {1.0f, 0.0f}, {0.45f, -0.28f}}, nose);                    // nose cone
            drawPolygon({{-0.6f, 0.28f}, {-0.95f, 0.55f}, {-0.45f, 0.28f}}, fin);                  // top fin
            drawPolygon({{-0.6f, -0.28f}, {-0.95f, -0.55f}, {-0.45f, -0.28f}}, fin);               // bottom fin
            drawCircle({0.05f, 0.0f}, 0.16f, 20, win);                                             // porthole
            break;
        }
        case ShipType::Falcon: {
            // Blue, wide & short engine glow (a flat trapezoid, not a point) from the saucer rear.
            if (flaming) {
                float outer[4] = {0.35f, 0.55f, 1.00f, bright};
                float core[4]  = {0.70f, 0.85f, 1.00f, bright};
                drawPolygon({{-0.70f, 0.45f}, {-0.70f - 0.35f * lenF, 0.18f},
                             {-0.70f - 0.35f * lenF, -0.18f}, {-0.70f, -0.45f}}, outer);
                drawPolygon({{-0.70f, 0.26f}, {-0.70f - 0.28f * lenF, 0.10f},
                             {-0.70f - 0.28f * lenF, -0.10f}, {-0.70f, -0.26f}}, core);
            }
            float hull[4]    = {0.70f, 0.72f, 0.75f, alpha};
            float dish[4]    = {0.55f, 0.57f, 0.60f, alpha};
            float cockpit[4] = {0.50f, 0.65f, 0.80f, alpha};
            drawCircle({-0.05f, 0.0f}, 0.7f, 28, hull);                                            // saucer
            drawPolygon({{0.3f, 0.18f}, {1.0f, 0.12f}, {1.0f, 0.02f}, {0.3f, 0.02f}}, hull);       // top mandible
            drawPolygon({{0.3f, -0.18f}, {1.0f, -0.12f}, {1.0f, -0.02f}, {0.3f, -0.02f}}, hull);   // bottom mandible
            drawCircle({-0.25f, 0.30f}, 0.16f, 16, dish);                                          // radar dish
            drawCircle({0.30f, -0.40f}, 0.15f, 16, cockpit);                                       // cockpit pod
            break;
        }
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

    glClear(GL_COLOR_BUFFER_BIT);
    shader_->activate();

    if (screen_ == Screen::Playing) {
        game_.update(dt);
        uiIdleTime_ += dt;   // drives the HUD fade-out
        renderGame();
    } else if (screen_ == Screen::Start) {
        drawStartScreen();
    } else if (screen_ == Screen::Upgrades) {
        drawUpgradesScreen();
    } else {
        drawCustomizeScreen();
    }

    eglSwapBuffers(display_, surface_);
}

void Renderer::renderGame() {
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

    // Draw Planet/Moon Orbits — each body is drawn around its parent (the Sun has none)
    float planetOrbitColor[4] = {1, 1, 1, 0.15f};
    for (const auto& body : bodies) {
        if (body.parent >= 0) {
            const CelestialBody& primaryBody = bodies[body.parent];
            drawOrbit(body.pos, body.vel, primaryBody, primaryBody.mu, planetOrbitColor);
        }
    }

    // Draw Ship Orbit
    if (primary) {
        float shipOrbitColor[4] = {1, 1, 1, 0.5f};
        drawOrbit(ship.pos, ship.vel, *primary, primary->mu, shipOrbitColor);
    }

    // HUD fade fraction (1 opaque → 0 hidden). Computed here as well as for the HUD below so the
    // planet/moon position markers fade on the same 5s timer as the rest of the UI.
    float uiFade = std::max(0.0f, std::min(1.0f, 1.0f - (uiIdleTime_ - HUD_FADE_START) / HUD_FADE_DUR));

    // Draw Bodies. Each disc gets a translucent gray "night side" shadow: a semicircle covering
    // the half facing away from the Sun (bodies[0], the light source), drawn on top of the body.
    const Vec2 sunPos = bodies.empty() ? Vec2{0, 0} : bodies[0].pos;
    for (size_t i = 0; i < bodies.size(); ++i) {
        const auto& body = bodies[i];
        setMVP(body.pos, body.radius, 0);
        float c[4]; std::copy(body.color, body.color+4, c);
        drawCircle({0,0}, 1.0f, 64, c);

        // Shadow: skip the Sun itself. Orient local +x away from the Sun and fill the +x half
        // (a unit-radius semicircle), so the shaded side always points into deep space.
        if (i != 0) {
            Vec2 away = body.pos - sunPos;
            float shadowAngle = std::atan2(away.y, away.x);
            setMVP(body.pos, body.radius, shadowAngle);
            float shadow[4] = {0.05f, 0.05f, 0.05f, 0.45f};
            std::vector<Vec2> arc = {{0.0f, 0.0f}};
            const int seg = 32;
            for (int s = 0; s <= seg; ++s) {
                float a = -M_PI * 0.5f + M_PI * s / seg;
                arc.push_back({std::cos(a), std::sin(a)});
            }
            drawPolygon(arc, shadow);
        }
    }

    // Position markers: a translucent disc in each body's colour at a fixed minimum on-screen
    // size, so planets and moons stay findable when zoomed far out (where their real discs shrink
    // below a pixel). Skipped once a body's actual disc is already large enough. Fades with the HUD.
    if (uiFade > 0.0f) {
        const float kMarkerScreenR = 0.018f; // minimum on-screen radius, in NDC (vertical) units
        for (const auto& body : bodies) {
            if (body.radius * zoom >= kMarkerScreenR) continue; // disc already big enough
            float scale = kMarkerScreenR / zoom;                // world scale → fixed screen size
            setMVP(body.pos, scale, 0);
            float m[4] = {body.color[0], body.color[1], body.color[2], 0.5f * uiFade};
            drawCircle({0,0}, 1.0f, 24, m);
        }
    }

    // Draw Ship (shape depends on the selected variant). The exhaust plume reflects the engine's
    // actual thrust: throttle scaled by the limiter, but off while time-warping or out of fuel.
    float thrustVis = ship.throttle * game_.getThrustLimit();
    if (game_.getTimeWarp() != 1) thrustVis = 0.0f;
    if (game_.getFuelFraction() <= 0.0f) thrustVis = 0.0f;
    float shipScale = 10.0f + 0.02f / zoom;
    setMVP(ship.pos, shipScale, ship.angle);
    drawShipShape(ship.type, 1.0f, thrustVis);

    // UI Rendering
    float uiMVP[16];
    Utility::buildIdentityMatrix(uiMVP);
    uiMVP[0] = 1.0f / aspect;
    shader_->setMVP(uiMVP);

    // HUD fade: fully opaque until FADE_START seconds of inactivity, then fades over FADE_DUR.
    // uiFade was already computed above (it also drives the body position markers).
    uiHidden_ = (uiFade <= 0.0f);

    float uiColor[4] = {1, 1, 1, 0.2f * uiFade};
    float activeColor[4] = {1, 1, 1, 0.6f * uiFade};

    // Combined floating joystick: only shown while the finger is down. The base disc marks
    // the full-throttle reach; the green outlined ring marks where thrust starts (the
    // deadzone radius); the knob tracks the finger. The whole stick turns green while thrusting.
    if (joyActive_) {
        bool thrusting = game_.getShip().throttle > 0.0f;

        float idleBase[4]  = {1, 1, 1, 0.2f * uiFade};
        float greenBase[4] = {0.25f, 0.85f, 0.40f, 0.30f * uiFade};
        drawCircle(joyOrigin_, JOY_MAX_RANGE, 32, thrusting ? greenBase : idleBase);

        // Green outlined ring at the thrust-start (deadzone) radius
        float thrustRing[4] = {0.30f, 0.95f, 0.40f, 0.85f * uiFade};
        drawCircleOutline(joyOrigin_, JOY_TOLERANCE, 28, thrustRing);

        Vec2 knob = {joyKnob_.x - joyOrigin_.x, joyKnob_.y - joyOrigin_.y};
        float knobDist = knob.length();
        if (knobDist > JOY_MAX_RANGE) knob = knob.normalized() * JOY_MAX_RANGE;
        float greenKnob[4] = {0.30f, 0.95f, 0.40f, 0.85f * uiFade};
        drawCircle({joyOrigin_.x + knob.x, joyOrigin_.y + knob.y}, JOY_KNOB_RADIUS, 24, thrusting ? greenKnob : activeColor);
    }

    // Fuel gauge (right edge — where the zoom bar used to be; zoom is now pinch-driven)
    float fx1 = aspect - FUEL_X_MAX_OFFSET;
    float fx2 = aspect - FUEL_X_MIN_OFFSET;
    drawPolygon({{fx1, FUEL_Y_MIN}, {fx2, FUEL_Y_MIN}, {fx2, FUEL_Y_MAX}, {fx1, FUEL_Y_MAX}}, uiColor);
    float fuelFrac = game_.getFuelFraction();
    float fuelTop = FUEL_Y_MIN + (FUEL_Y_MAX - FUEL_Y_MIN) * fuelFrac;
    // Green normally; turns red when the tank runs low.
    float fuelFill[4] = {0.30f, 0.90f, 0.40f, 0.7f * uiFade};
    if (fuelFrac < 0.25f) { fuelFill[0] = 0.95f; fuelFill[1] = 0.30f; fuelFill[2] = 0.25f; }
    drawPolygon({{fx1, FUEL_Y_MIN}, {fx2, FUEL_Y_MIN}, {fx2, fuelTop}, {fx1, fuelTop}}, fuelFill);

    // Labels
    float textColor[4] = {1, 1, 1, 0.5f * uiFade};
    drawText("FUEL", {fx1 - 0.22f, FUEL_Y_MAX + 0.03f}, 0.05f, textColor);

    // Science-points readout (lower-right corner)
    drawTextCentered("SCI " + std::to_string(game_.getScience()), {aspect - SCI_X_OFFSET, SCI_Y}, SCI_LABEL_SIZE, textColor);

    // Timewarp buttons (bottom-left, two rows): each labeled with its multiplier; active one
    // is highlighted. Bottom row holds the first TW_PER_ROW levels; the rest stack above.
    for (int i = 0; i < TW_LEVEL_COUNT; ++i) {
        int row = i / TW_PER_ROW, col = i % TW_PER_ROW;
        float cx = -aspect + TW_FIRST_X_OFFSET + col * TW_BTN_SPACING;
        float cy = TW_ROW_Y + row * TW_ROW_SPACING;
        float bx1 = cx - TW_BTN_HALF, bx2 = cx + TW_BTN_HALF;
        float by1 = cy - TW_BTN_HALF, by2 = cy + TW_BTN_HALF;
        float* bg = (game_.getTimeWarp() == TW_LEVELS[i]) ? activeColor : uiColor;
        drawPolygon({{bx1, by1}, {bx2, by1}, {bx2, by2}, {bx1, by2}}, bg);

        // Label: the multiplier, e.g. "1X", "50X", "500X"
        drawTextCentered(std::to_string(TW_LEVELS[i]) + "X", {cx, cy}, TW_LABEL_SIZE, textColor);
    }

    // Thrust limiter slider (upper-left, horizontal): caps engine thrust for precision.
    float tlx1 = -aspect + TL_X_OFFSET;
    float tlx2 = tlx1 + TL_LENGTH;
    drawPolygon({{tlx1, TL_Y - TL_HALF_H}, {tlx2, TL_Y - TL_HALF_H}, {tlx2, TL_Y + TL_HALF_H}, {tlx1, TL_Y + TL_HALF_H}}, uiColor);
    float lim = game_.getThrustLimit();
    float tlFillX = tlx1 + TL_LENGTH * lim;
    drawPolygon({{tlx1, TL_Y - TL_HALF_H}, {tlFillX, TL_Y - TL_HALF_H}, {tlFillX, TL_Y + TL_HALF_H}, {tlx1, TL_Y + TL_HALF_H}}, activeColor);
    // Handle marker at the current value
    float tlHandleW = 0.012f, tlHandleH = TL_HALF_H * 2.2f;
    drawPolygon({{tlFillX - tlHandleW, TL_Y - tlHandleH}, {tlFillX + tlHandleW, TL_Y - tlHandleH}, {tlFillX + tlHandleW, TL_Y + tlHandleH}, {tlFillX - tlHandleW, TL_Y + tlHandleH}}, activeColor);
    drawText("THRUST LIMIT", {tlx1, TL_Y - 0.1f}, 0.04f, textColor);

    // "Change ship" button (below the thrust limiter): only while parked on Earth, the home
    // base. Left-aligned under the slider; opens the ship-selection screen. Hit-test mirrored
    // in handleGameInput.
    if (game_.isOnEarth()) {
        float csCx = tlx1 + CS_BTN_HALF_W;
        float cbx1 = csCx - CS_BTN_HALF_W, cbx2 = csCx + CS_BTN_HALF_W;
        float cby1 = CS_BTN_Y - CS_BTN_HALF_H, cby2 = CS_BTN_Y + CS_BTN_HALF_H;
        drawPolygon({{cbx1, cby1}, {cbx2, cby1}, {cbx2, cby2}, {cbx1, cby2}}, uiColor);
        drawTextCentered("CHANGE SHIP", {csCx, CS_BTN_Y}, CS_LABEL_SIZE, textColor);

        // "Upgrades" button just below (same left-aligned column / size)
        float uby1 = UP_BTN_Y - CS_BTN_HALF_H, uby2 = UP_BTN_Y + CS_BTN_HALF_H;
        drawPolygon({{cbx1, uby1}, {cbx2, uby1}, {cbx2, uby2}, {cbx1, uby2}}, uiColor);
        drawTextCentered("UPGRADES", {csCx, UP_BTN_Y}, CS_LABEL_SIZE, textColor);
    }

    // Camera mode button (upper-right corner, translucent square with a camera icon)
    float camCx = aspect - CAM_BTN_X_OFFSET;
    float bx1 = camCx - CAM_BTN_HALF, bx2 = camCx + CAM_BTN_HALF;
    float by1 = CAM_BTN_CENTER_Y - CAM_BTN_HALF, by2 = CAM_BTN_CENTER_Y + CAM_BTN_HALF;
    drawPolygon({{bx1, by1}, {bx2, by1}, {bx2, by2}, {bx1, by2}}, uiColor);

    // Camera icon: body rectangle, viewfinder bump, and lens
    float iconW = CAM_BTN_HALF * 0.62f;  // half-width of camera body
    float iconH = CAM_BTN_HALF * 0.42f;  // half-height of camera body
    float cy = CAM_BTN_CENTER_Y;
    drawPolygon({{camCx - iconW, cy - iconH}, {camCx + iconW, cy - iconH}, {camCx + iconW, cy + iconH}, {camCx - iconW, cy + iconH}}, activeColor);
    float bumpW = CAM_BTN_HALF * 0.18f;
    drawPolygon({{camCx - bumpW, cy + iconH}, {camCx + bumpW, cy + iconH}, {camCx + bumpW, cy + iconH + CAM_BTN_HALF * 0.16f}, {camCx - bumpW, cy + iconH + CAM_BTN_HALF * 0.16f}}, activeColor);
    drawCircle({camCx, cy}, CAM_BTN_HALF * 0.26f, 24, uiColor);

    // Mode label below the button (centered, so it stays on-screen in the corner)
    const char *modeLabel = game_.getCameraMode() == CameraMode::Ship ? "SHIP" : "BODY";
    drawTextCentered(modeLabel, {camCx, by1 - 0.06f}, 0.05f, textColor);
}

void Renderer::drawStartScreen() {
    float aspect = float(width_) / height_;
    float uiMVP[16];
    Utility::buildIdentityMatrix(uiMVP);
    uiMVP[0] = 1.0f / aspect;
    shader_->setMVP(uiMVP);

    float titleColor[4] = {1, 1, 1, 0.9f};
    float btnColor[4]   = {1, 1, 1, 0.2f};
    float textColor[4]  = {1, 1, 1, 0.85f};

    // Title stacked on two lines so it fits a narrow phone screen
    drawTextCentered("ORBITAL",  {0.0f, 0.64f}, 0.11f, titleColor);
    drawTextCentered("VELOCITY", {0.0f, 0.50f}, 0.11f, titleColor);

    // Title buttons (hit-tests mirrored in handleMenuInput). With a valid save, offer
    // CONTINUE (resume) and NEW GAME (wipe + fresh start); otherwise a single PLAY. Ship
    // customization now lives in-game (the "Change ship" button on Earth).
    if (hasSave_) {
        drawPolygon({{-0.35f, 0.20f}, {0.35f, 0.20f}, {0.35f, 0.40f}, {-0.35f, 0.40f}}, btnColor);
        drawTextCentered("CONTINUE", {0.0f, 0.30f}, 0.06f, textColor);
        drawPolygon({{-0.35f, -0.05f}, {0.35f, -0.05f}, {0.35f, 0.15f}, {-0.35f, 0.15f}}, btnColor);
        drawTextCentered("NEW GAME", {0.0f, 0.05f}, 0.06f, textColor);
    } else {
        drawPolygon({{-0.35f, 0.03f}, {0.35f, 0.03f}, {0.35f, 0.27f}, {-0.35f, 0.27f}}, btnColor);
        drawTextCentered("PLAY", {0.0f, 0.15f}, 0.08f, textColor);
    }

    // Preview of the currently selected ship
    setUIMVP({0.0f, -0.62f}, 0.14f, 0.0f);
    drawShipShape(selectedShip_, 0.9f);
}

void Renderer::drawCustomizeScreen() {
    float aspect = float(width_) / height_;
    float uiMVP[16];
    auto resetUI = [&]() {
        Utility::buildIdentityMatrix(uiMVP);
        uiMVP[0] = 1.0f / aspect;
        shader_->setMVP(uiMVP);
    };
    resetUI();

    float titleColor[4] = {1, 1, 1, 0.9f};
    float textColor[4]  = {1, 1, 1, 0.9f};
    float btnColor[4]   = {1, 1, 1, 0.2f};
    float arrowColor[4] = {1, 1, 1, 0.7f};

    drawTextCentered("CHANGE SHIP", {0.0f, 0.82f}, 0.08f, titleColor);

    const char* names[3] = {"TRIANGLE", "ROCKET", "FALCON"};
    int sel = (int)selectedShip_;
    bool unlocked = game_.isShipUnlocked(selectedShip_);

    // Science balance (so the player can gauge affordability of unlocks)
    drawTextCentered("SCIENCE " + std::to_string(game_.getScience()), {0.0f, 0.66f}, 0.05f, textColor);

    // Centered ship sprite (scale capped so it doesn't balloon on tall/portrait screens)
    float spriteScale = 0.26f * std::min(1.0f, aspect);
    setUIMVP({0.0f, 0.30f}, spriteScale, 0.0f);
    drawShipShape(selectedShip_, 1.0f);

    // Name + lock status under the sprite (ships are cosmetic; variants are unlocked with science)
    resetUI();
    drawTextCentered(names[sel], {0.0f, -0.05f}, 0.07f, textColor);
    drawTextCentered(unlocked ? "UNLOCKED" : "LOCKED", {0.0f, -0.18f}, 0.05f, textColor);

    // Left/right arrows flanking the sprite to switch ships (kept on-screen; clamped so they
    // don't drift too far out on wide/landscape screens). Hit-tests mirror these positions.
    float arrowX = std::min(aspect - 0.18f, 0.62f);
    float ay = 0.30f, ah = 0.13f, aw = 0.10f;
    drawPolygon({{-arrowX - aw, ay}, {-arrowX + aw, ay + ah}, {-arrowX + aw, ay - ah}}, arrowColor); // points left
    drawPolygon({{arrowX + aw, ay}, {arrowX - aw, ay + ah}, {arrowX - aw, ay - ah}}, arrowColor);    // points right

    // Smaller BACK / action buttons, close together near the bottom. The right button is
    // SELECT for an unlocked ship, or UNLOCK (cost) for a locked one.
    drawPolygon({{-0.40f, -0.78f}, {-0.04f, -0.78f}, {-0.04f, -0.62f}, {-0.40f, -0.62f}}, btnColor);
    drawTextCentered("BACK", {-0.22f, -0.70f}, 0.05f, textColor);
    drawPolygon({{0.04f, -0.78f}, {0.40f, -0.78f}, {0.40f, -0.62f}, {0.04f, -0.62f}}, btnColor);
    if (unlocked) {
        drawTextCentered("SELECT", {0.22f, -0.70f}, 0.05f, textColor);
    } else {
        drawTextCentered("UNLOCK " + std::to_string(game_.shipUnlockCost(selectedShip_)), {0.22f, -0.70f}, 0.042f, textColor);
    }
}

void Renderer::drawUpgradesScreen() {
    float aspect = float(width_) / height_;
    float uiMVP[16];
    Utility::buildIdentityMatrix(uiMVP);
    uiMVP[0] = 1.0f / aspect;
    shader_->setMVP(uiMVP);

    float titleColor[4] = {1, 1, 1, 0.9f};
    float textColor[4]  = {1, 1, 1, 0.9f};
    float btnColor[4]   = {1, 1, 1, 0.2f};

    drawTextCentered("UPGRADES", {0.0f, 0.82f}, 0.09f, titleColor);

    int sci = game_.getScience();
    drawTextCentered("SCIENCE " + std::to_string(sci), {0.0f, 0.58f}, 0.06f, textColor);

    char cap[32];
    snprintf(cap, sizeof cap, "FUEL CAP %.1f", game_.getFuelCapacity());
    drawTextCentered(cap, {0.0f, 0.42f}, 0.06f, textColor);

    // BUY FUEL button (dimmed when there isn't enough science). Hit-test mirrored in handleMenuInput.
    bool affordable = sci >= 1;
    float buyBtn[4]  = {1, 1, 1, affordable ? 0.25f : 0.10f};
    float buyText[4] = {1, 1, 1, affordable ? 0.9f : 0.4f};
    drawPolygon({{-0.34f, 0.07f}, {0.34f, 0.07f}, {0.34f, 0.23f}, {-0.34f, 0.23f}}, buyBtn);
    drawTextCentered("BUY FUEL +0.1", {0.0f, 0.15f}, 0.045f, buyText);
    drawTextCentered("COST 1 SCI", {0.0f, -0.02f}, 0.04f, textColor);

    // BACK button
    drawPolygon({{-0.18f, -0.83f}, {0.18f, -0.83f}, {0.18f, -0.67f}, {-0.18f, -0.67f}}, btnColor);
    drawTextCentered("BACK", {0.0f, -0.75f}, 0.05f, textColor);
}

void Renderer::handleInput() {
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) return;

    float aspect = float(width_) / height_;

    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        if (screen_ == Screen::Playing) {
            handleGameInput(motionEvent, aspect);
        } else {
            handleMenuInput(motionEvent, aspect);
        }
    }
    android_app_clear_motion_events(inputBuffer);
}

void Renderer::handleGameInput(const GameActivityMotionEvent& motionEvent, float aspect) {
    auto action = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
    auto pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    // Pinch-to-zoom: with two or more fingers down, scale the zoom by the change in finger
    // spacing. This replaces the old zoom slider. A pinch never drives the single-touch
    // controls, so the joystick/throttle don't jerk while zooming.
    if (motionEvent.pointerCount >= 2 &&
        action != AMOTION_EVENT_ACTION_POINTER_UP && action != AMOTION_EVENT_ACTION_UP) {
        float x0 = GameActivityPointerAxes_getX(&motionEvent.pointers[0]);
        float y0 = GameActivityPointerAxes_getY(&motionEvent.pointers[0]);
        float x1 = GameActivityPointerAxes_getX(&motionEvent.pointers[1]);
        float y1 = GameActivityPointerAxes_getY(&motionEvent.pointers[1]);
        float dist = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (lastPinchDist_ > 0.0f && dist > 0.0f) {
            float maxZoom = MIN_ZOOM * MAX_ZOOM_FACTOR;
            float z = game_.getZoom() * (dist / lastPinchDist_);
            game_.setZoom(std::max(MIN_ZOOM, std::min(maxZoom, z)));
        }
        lastPinchDist_ = dist;
        uiIdleTime_ = 0.0f;   // keep the HUD awake while interacting
        uiHidden_ = false;
        joyActive_ = false;   // suspend the flight stick during a pinch
        game_.setThrottle(0.0f);
        return;
    }
    lastPinchDist_ = -1.0f;   // fewer than two fingers: pinch is over

    // If the HUD had fully faded out, the first touch only wakes it (no control actuation),
    // so you can't accidentally yank the throttle while reaching for a hidden control.
    bool wasHidden = uiHidden_;

    for (int p = 0; p < motionEvent.pointerCount; ++p) {
        float x = (GameActivityPointerAxes_getX(&motionEvent.pointers[p]) / width_ * 2.0f - 1.0f) * aspect;
        float y = -(GameActivityPointerAxes_getY(&motionEvent.pointers[p]) / height_ * 2.0f - 1.0f);

        bool isUp = (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) && (p == pointerIndex);
        bool isDown = (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) && (p == pointerIndex);

        // Any active touch wakes the HUD.
        if (!isUp) { uiIdleTime_ = 0.0f; uiHidden_ = false; }
        // Reveal-only on the waking gesture.
        if (wasHidden) continue;

        // Buttons take priority over the flight stick: a down that lands on one of them
        // should not also spawn a joystick.
        bool onButton = false;

        // Camera mode button (upper-right corner): toggle on tap
        float camCx = aspect - CAM_BTN_X_OFFSET;
        if (isDown && x >= camCx - CAM_BTN_HALF && x <= camCx + CAM_BTN_HALF &&
            y >= CAM_BTN_CENTER_Y - CAM_BTN_HALF && y <= CAM_BTN_CENTER_Y + CAM_BTN_HALF) {
            game_.toggleCameraMode();
            onButton = true;
        }

        // "Change ship" button (below the thrust limiter): only tappable while parked on
        // Earth, where the button is drawn. Opens the ship-selection screen preset to the
        // current ship. Mirrors the rect in renderGame.
        if (isDown && game_.isOnEarth()) {
            float csCx = (-aspect + TL_X_OFFSET) + CS_BTN_HALF_W;
            if (x >= csCx - CS_BTN_HALF_W && x <= csCx + CS_BTN_HALF_W &&
                y >= CS_BTN_Y - CS_BTN_HALF_H && y <= CS_BTN_Y + CS_BTN_HALF_H) {
                selectedShip_ = game_.getShip().type;
                screen_ = Screen::Customize;
                onButton = true;
            } else if (x >= csCx - CS_BTN_HALF_W && x <= csCx + CS_BTN_HALF_W &&
                       y >= UP_BTN_Y - CS_BTN_HALF_H && y <= UP_BTN_Y + CS_BTN_HALF_H) {
                screen_ = Screen::Upgrades;
                onButton = true;
            }
        }

        // Timewarp buttons (bottom-left, two rows): select speed on tap. Must mirror the
        // draw loop's row/col layout exactly.
        if (isDown) {
            for (int b = 0; b < TW_LEVEL_COUNT; ++b) {
                int row = b / TW_PER_ROW, col = b % TW_PER_ROW;
                float cx = -aspect + TW_FIRST_X_OFFSET + col * TW_BTN_SPACING;
                float cy = TW_ROW_Y + row * TW_ROW_SPACING;
                if (x >= cx - TW_BTN_HALF && x <= cx + TW_BTN_HALF &&
                    y >= cy - TW_BTN_HALF && y <= cy + TW_BTN_HALF) {
                    game_.setTimeWarp(TW_LEVELS[b]);
                    onButton = true;
                }
            }
        }

        // Thrust limiter slider (upper-left, horizontal). Adjusts on touch/drag within its
        // band. The !joyActive_ guard stops a roaming flight gesture from nudging it.
        if (!joyActive_) {
            float tlx1 = -aspect + TL_X_OFFSET;
            float tlx2 = tlx1 + TL_LENGTH;
            if (x >= tlx1 - TL_SENSE_MARGIN && x <= tlx2 + TL_SENSE_MARGIN &&
                y >= TL_Y - TL_HALF_H - TL_SENSE_MARGIN && y <= TL_Y + TL_HALF_H + TL_SENSE_MARGIN) {
                if (isDown) onButton = true; // claim the touch; don't spawn the joystick
                if (!isUp) {
                    float v = (x - tlx1) / TL_LENGTH;
                    game_.setThrustLimit(std::max(0.0f, std::min(1.0f, v)));
                }
            }
        }

        // Combined floating joystick (steering + throttle). Single finger only, so it never
        // collides with the two-finger pinch.
        if (motionEvent.pointerCount == 1) {
            if (isDown) {
                if (!onButton) {
                    joyActive_ = true;
                    joyOrigin_ = {x, y};
                    joyKnob_ = {x, y};
                    game_.setThrottle(0.0f);
                }
            } else if (isUp) {
                if (joyActive_) {
                    joyActive_ = false;
                    game_.setThrottle(0.0f);
                    game_.setJoystick({0, 0}); // leaves heading unchanged on release
                }
            } else if (joyActive_) { // drag
                Vec2 d = {x - joyOrigin_.x, y - joyOrigin_.y};
                float dist = d.length();
                if (dist > JOY_DIR_DEADZONE) {
                    game_.setJoystick(d.normalized()); // heading, as before
                }
                float thr = (dist - JOY_TOLERANCE) / (JOY_MAX_RANGE - JOY_TOLERANCE);
                game_.setThrottle(std::max(0.0f, std::min(1.0f, thr)));
                joyKnob_ = {x, y};
            }
        }
    }
}

void Renderer::handleMenuInput(const GameActivityMotionEvent& motionEvent, float aspect) {
    auto action = motionEvent.action & AMOTION_EVENT_ACTION_MASK;
    // Menus react to a finger-down (tap) only.
    if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_POINTER_DOWN) return;
    auto pointerIndex = (motionEvent.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    float x = (GameActivityPointerAxes_getX(&motionEvent.pointers[pointerIndex]) / width_ * 2.0f - 1.0f) * aspect;
    float y = -(GameActivityPointerAxes_getY(&motionEvent.pointers[pointerIndex]) / height_ * 2.0f - 1.0f);

    auto startPlaying = [&]() {
        game_.startWithShip(selectedShip_);
        gameStarted_ = true;
        screen_ = Screen::Playing;
        uiIdleTime_ = 0.0f;
        uiHidden_ = false;
        lastPinchDist_ = -1.0f;
    };

    if (screen_ == Screen::Start) {
        if (hasSave_) {
            if (hitRect(x, y, 0.0f, 0.30f, 0.35f, 0.10f)) {        // CONTINUE: resume the save
                if (game_.loadFrom(savePath())) {
                    selectedShip_ = game_.getShip().type;
                    gameStarted_ = true;
                    screen_ = Screen::Playing;
                    uiIdleTime_ = 0.0f;
                    uiHidden_ = false;
                    lastPinchDist_ = -1.0f;
                } else {                                            // corrupt save: wipe + fresh
                    std::remove(savePath().c_str());
                    hasSave_ = false;
                    selectedShip_ = ShipType::Triangle;
                    startPlaying();
                }
            } else if (hitRect(x, y, 0.0f, 0.05f, 0.35f, 0.10f)) { // NEW GAME: wipe + fresh start
                std::remove(savePath().c_str());
                hasSave_ = false;
                selectedShip_ = ShipType::Triangle;
                startPlaying();
            }
        } else {
            if (hitRect(x, y, 0.0f, 0.15f, 0.35f, 0.12f)) startPlaying();
        }
    } else if (screen_ == Screen::Customize) { // reached from the in-game "Change ship" button
        float arrowX = std::min(aspect - 0.18f, 0.62f);
        if (hitRect(x, y, -arrowX, 0.30f, 0.16f, 0.20f)) {           // left arrow: previous ship
            selectedShip_ = (ShipType)(((int)selectedShip_ + 2) % 3);
        } else if (hitRect(x, y, arrowX, 0.30f, 0.16f, 0.20f)) {     // right arrow: next ship
            selectedShip_ = (ShipType)(((int)selectedShip_ + 1) % 3);
        } else if (hitRect(x, y, -0.22f, -0.70f, 0.18f, 0.10f)) {    // BACK: resume, keep ship
            screen_ = Screen::Playing;
            uiIdleTime_ = 0.0f;
            uiHidden_ = false;
        } else if (hitRect(x, y, 0.22f, -0.70f, 0.18f, 0.10f)) {     // SELECT (unlocked) / UNLOCK (locked)
            if (game_.isShipUnlocked(selectedShip_)) {
                startPlaying();                  // relaunch the chosen cosmetic on the Earth pad
            } else {
                game_.unlockShip(selectedShip_); // spend science; label flips to SELECT on success
            }
        }
    } else if (screen_ == Screen::Upgrades) {
        if (hitRect(x, y, 0.0f, 0.15f, 0.34f, 0.08f)) {              // BUY FUEL +0.1
            game_.buyFuelUpgrade();
        } else if (hitRect(x, y, 0.0f, -0.75f, 0.18f, 0.08f)) {      // BACK: resume
            screen_ = Screen::Playing;
            uiIdleTime_ = 0.0f;
            uiHidden_ = false;
        }
    }
}
