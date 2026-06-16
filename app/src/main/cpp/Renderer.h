#ifndef ANDROIDGLINVESTIGATIONS_RENDERER_H
#define ANDROIDGLINVESTIGATIONS_RENDERER_H

#include <EGL/egl.h>
#include <memory>

#include "Model.h"
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
    void drawPolygon(const std::vector<Vec2>& points, float color[4]);
    void drawOrbit(Vec2 pos, Vec2 vel, const CelestialBody& primary, float mu, float color[4]);
    void drawText(const std::string& text, Vec2 pos, float size, float color[4]);

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