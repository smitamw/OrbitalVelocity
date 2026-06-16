#ifndef ANDROIDGLINVESTIGATIONS_SHADER_H
#define ANDROIDGLINVESTIGATIONS_SHADER_H

#include <string>
#include <GLES3/gl3.h>

class Shader {
public:
    static Shader *loadShader(
            const std::string &vertexSource,
            const std::string &fragmentSource);

    inline ~Shader() {
        if (program_) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    void activate() const;
    void deactivate() const;

    void setMVP(float *matrix) const;
    void setColor(float *color) const;

    GLint getPositionLocation() const { return position_; }

private:
    static GLuint loadShader(GLenum shaderType, const std::string &shaderSource);

    constexpr Shader(GLuint program, GLint position, GLint mvp, GLint color)
            : program_(program), position_(position), mvp_(mvp), color_(color) {}

    GLuint program_;
    GLint position_;
    GLint mvp_;
    GLint color_;
};

#endif //ANDROIDGLINVESTIGATIONS_SHADER_H
