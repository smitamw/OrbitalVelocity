#include "Shader.h"
#include "AndroidOut.h"
#include <vector>

Shader *Shader::loadShader(const std::string &vertexSource, const std::string &fragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) return nullptr;

    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return nullptr;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                std::vector<char> buf(bufLength);
                glGetProgramInfoLog(program, bufLength, nullptr, buf.data());
                aout << "Could not link program:\n" << buf.data() << std::endl;
            }
            glDeleteProgram(program);
            program = 0;
        }
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (program) {
        GLint position = glGetAttribLocation(program, "inPosition");
        GLint mvp = glGetUniformLocation(program, "uMVP");
        GLint color = glGetUniformLocation(program, "uColor");
        return new Shader(program, position, mvp, color);
    }
    return nullptr;
}

GLuint Shader::loadShader(GLenum shaderType, const std::string &shaderSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        const char *source = shaderSource.c_str();
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                std::vector<char> buf(infoLen);
                glGetShaderInfoLog(shader, infoLen, nullptr, buf.data());
                aout << "Could not compile shader " << shaderType << ":\n" << buf.data() << std::endl;
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

void Shader::activate() const { glUseProgram(program_); }
void Shader::deactivate() const { glUseProgram(0); }
void Shader::setMVP(float *matrix) const { glUniformMatrix4fv(mvp_, 1, GL_FALSE, matrix); }
void Shader::setColor(float *color) const { glUniform4fv(color_, 1, color); }
