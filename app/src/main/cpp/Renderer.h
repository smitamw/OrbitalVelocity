#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>
#include <string>
#include <vector>

#include "Shader.h"
#include "Game.h"

struct android_app;

class Renderer {
public:
    /*!
     * @param pApp the android_app this Renderer belongs to, needed to configure GL
     */
    inline Renderer(android_app *pApp) :
            app_(pApp),
            display_(EGL_NO_DISPLAY),
            surface_(EGL_NO_SURFACE),
            context_(EGL_NO_CONTEXT),
            width_(0),
            height_(0),
            lastTime_(0) {
        initRenderer();
    }

    virtual ~Renderer();

    /*!
     * Handles input from the android_app.
     *
     * Note: this will clear the input queue
     */
    void handleInput();

    /*!
     * Renders all the models in the renderer
     */
    void render();

private:
    /*!
     * Performs necessary OpenGL initialization. Customize this if you want to change your EGL
     * context or application-wide settings.
     */
    void initRenderer();

    /*!
     * @brief we have to check every frame to see if the framebuffer has changed in size. If it has,
     * update the viewport accordingly
     */
    void updateRenderArea();

    void drawCircle(Vec2 center, float radius, int segments, float color[4]);
    void drawCircleOutline(Vec2 center, float radius, int segments, float color[4]);
    void drawPolygon(const std::vector<Vec2>& points, float color[4]);
    void drawOrbit(Vec2 pos, Vec2 vel, const CelestialBody& primary, float mu, float color[4]);
    void drawText(const std::string& text, Vec2 pos, float size, float color[4]);

    // Screen flow: which screen we're on and the per-screen render/input handlers.
    enum class Screen { Start, Customize, Playing };
    void drawStartScreen();
    void drawCustomizeScreen();
    void renderGame();           // the in-world + HUD render (the original render body)
    void handleGameInput(const struct GameActivityMotionEvent& ev, float aspect);
    void handleMenuInput(const struct GameActivityMotionEvent& ev, float aspect);

    // Draws a ship's local-space geometry (call after the MVP is set). Used both in-game and
    // for the Customize previews. `thrust` (0..1) drives the exhaust plume's length/brightness;
    // pass 0 (the default) for a flameless preview.
    void drawShipShape(ShipType type, float alpha, float thrust = 0.0f);
    // Sets an MVP that places local geometry at a UI-space position/scale/rotation.
    void setUIMVP(Vec2 pos, float scale, float angle);
    // Centred label/button helpers for the menus.
    void drawTextCentered(const std::string& text, Vec2 center, float size, float color[4]);
    bool hitRect(float x, float y, float cx, float cy, float halfW, float halfH);

    Screen screen_ = Screen::Start;
    ShipType selectedShip_ = ShipType::Triangle; // session-only; not persisted

    float uiIdleTime_ = 0.0f;     // seconds since last gameplay touch (drives HUD fade)
    bool uiHidden_ = false;       // true once the HUD has fully faded out
    float lastPinchDist_ = -1.0f; // previous two-finger distance, in pixels (-1 = not pinching)

    // Combined floating joystick (steering + throttle): spawns at the touch point, sets
    // heading from drag direction and throttle from drag distance.
    bool joyActive_ = false;      // is a floating-joystick gesture in progress
    Vec2 joyOrigin_{0, 0};        // touch-down point, in UI coords [-aspect,aspect]x[-1,1]
    Vec2 joyKnob_{0, 0};          // current finger point, in UI coords (raw; clamped at render)

    android_app *app_;
    EGLDisplay display_;
    EGLSurface surface_;
    EGLContext context_;
    EGLint width_;
    EGLint height_;

    double lastTime_;

    std::unique_ptr<Shader> shader_;
    Game game_;

    struct TouchPoint {
        int id;
        float startX, startY;
        float x, y;
        bool active;
    };
    std::vector<TouchPoint> touches_;
};

#endif //ANDROIDGLINVESTIGATIONS_RENDERER_H