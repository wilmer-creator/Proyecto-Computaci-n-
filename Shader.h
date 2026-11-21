#pragma once
// Clase Shader: compila, enlaza y usa un programa de shaders de OpenGL.
// Archivo solo de cabecera: basta con ponerlo en la misma carpeta que tu .cpp.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>

class Shader {
public:
    GLuint id = 0;

    Shader(const char* vertexSrc, const char* fragmentSrc) {
        GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc);
        id = glCreateProgram();
        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glLinkProgram(id);

        GLint ok;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(id, 1024, nullptr, log);
            std::cerr << "Error al enlazar el shader:\n" << log << "\n";
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    void use() const { glUseProgram(id); }

    void setInt(const std::string& name, int v) const { glUniform1i(loc(name), v); }
    void setBool(const std::string& name, bool v) const { glUniform1i(loc(name), v ? 1 : 0); }
    void setVec3(const std::string& name, const glm::vec3& v) const { glUniform3fv(loc(name), 1, glm::value_ptr(v)); }
    void setVec4(const std::string& name, const glm::vec4& v) const { glUniform4fv(loc(name), 1, glm::value_ptr(v)); }
    void setMat4(const std::string& name, const glm::mat4& m) const { glUniformMatrix4fv(loc(name), 1, GL_FALSE, glm::value_ptr(m)); }

private:
    GLint loc(const std::string& name) const { return glGetUniformLocation(id, name.c_str()); }

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(s, 1024, nullptr, log);
            std::cerr << "Error al compilar el shader:\n" << log << "\n";
        }
        return s;
    }
};

// ---------- Código fuente de los shaders ----------

// Modelo 3D
inline const char* modelVertexSrc() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 model, view, proj;
out vec3 vNormal; out vec2 vUV;
void main() {
    vNormal = mat3(transpose(inverse(model))) * aNormal;
    vUV = aUV;
    gl_Position = proj * view * model * vec4(aPos, 1.0);
})";
}

inline const char* modelFragmentSrc() {
    return R"(#version 330 core
in vec3 vNormal; in vec2 vUV;
uniform sampler2D tex;
uniform bool hasTex;
uniform vec4 baseColor;
out vec4 color;
void main() {
    vec4 base = hasTex ? texture(tex, vUV) * baseColor : baseColor;
    vec3 L = normalize(vec3(0.5, 1.0, 0.8));
    float diff = max(dot(normalize(vNormal), L), 0.0);
    color = vec4(base.rgb * (0.3 + 0.7 * diff), 1.0);
})";
}

// Fondo: un triángulo que cubre toda la pantalla (no necesita buffers)
inline const char* backgroundVertexSrc() {
    return R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = vec2(pos.x, 1.0 - pos.y);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.999, 1.0);
})";
}

inline const char* backgroundFragmentSrc() {
    return R"(#version 330 core
in vec2 vUV;
uniform sampler2D tex;
out vec4 color;
void main() {
    color = texture(tex, vUV);
})";
}

// Líneas y puntos de color liso (curva Bézier y puntos de control)
inline const char* lineVertexSrc() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 view, proj;
void main() {
    gl_Position = proj * view * vec4(aPos, 1.0);
})";
}

inline const char* lineFragmentSrc() {
    return R"(#version 330 core
uniform vec3 color;
out vec4 outColor;
void main() {
    outColor = vec4(color, 1.0);
})";
}