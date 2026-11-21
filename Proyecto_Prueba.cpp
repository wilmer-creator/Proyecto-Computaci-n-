// Visor de modelos glTF/GLB con texturas, fondo de imagen y control por teclado
// OpenGL 3.3 + GLFW + GLAD + GLM + tiny_gltf_v3 + stb_image
//
// Controles:  W/S = girar arriba/abajo   A/D = girar izquierda/derecha
//             Q = acercarse              E = alejarse        ESC = salir

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// Las implementaciones de stb_image (stb_image.cpp) y tiny_gltf_v3 (tiny_gltf_v3.c)
// ya se compilan por separado en el proyecto, así que aquí solo se incluyen los headers.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "tiny_gltf_v3.h"
#include "Shader.h"

#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ---------- Datos del modelo ----------
struct Mesh {
    GLuint vao = 0, count = 0, tex = 0;
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
};
std::vector<Mesh> meshes;
std::map<int, GLuint> texCache;
std::string gDir = ".";
glm::vec3 bmin(1e9f), bmax(-1e9f);

static bool eq(tg3_str s, const char* lit) {
    return s.len == strlen(lit) && std::memcmp(s.data, lit, s.len) == 0;
}

static int findAttr(const tg3_primitive& p, const char* name) {
    for (uint32_t i = 0; i < p.attributes_count; i++)
        if (eq(p.attributes[i].key, name)) return p.attributes[i].value;
    return -1;
}

// Devuelve puntero al primer elemento del accessor y el stride entre elementos
static const uint8_t* accData(const tg3_model& m, const tg3_accessor& a, size_t elemSize, size_t& stride) {
    if (a.buffer_view < 0) return nullptr;
    const tg3_buffer_view& bv = m.buffer_views[a.buffer_view];
    const tg3_buffer& b = m.buffers[bv.buffer];
    stride = bv.byte_stride ? bv.byte_stride : elemSize;
    return b.data.data + bv.byte_offset + a.byte_offset;
}

// Sube píxeles RGBA a una textura de OpenGL
GLuint createTexture(const unsigned char* pixels, int w, int h, GLint wrap) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return id;
}

// Textura del modelo. glTF usa la fila superior primero y v=0 arriba,
// justo lo que espera OpenGL al subirla tal cual, así que no se voltea nada.
GLuint getTexture(const tg3_model& m, int imgIdx) {
    auto it = texCache.find(imgIdx);
    if (it != texCache.end()) return it->second;

    const tg3_image& img = m.images[imgIdx];
    int w = 0, h = 0, c = 0;
    unsigned char* pixels = nullptr;

    if (img.image.data && img.image.count > 0) {
        // Bytes del archivo de imagen (png/jpg) ya cargados por la librería
        pixels = stbi_load_from_memory(img.image.data, (int)img.image.count, &w, &h, &c, 4);
    }
    else if (img.buffer_view >= 0) {
        // Imagen incrustada en el .glb
        const tg3_buffer_view& bv = m.buffer_views[img.buffer_view];
        const tg3_buffer& b = m.buffers[bv.buffer];
        pixels = stbi_load_from_memory(b.data.data + bv.byte_offset, (int)bv.byte_length, &w, &h, &c, 4);
    }
    else if (img.uri.len > 0) {
        // Archivo externo junto al modelo
        std::string path = gDir + "/" + std::string(img.uri.data, img.uri.len);
        pixels = stbi_load(path.c_str(), &w, &h, &c, 4);
    }
    if (!pixels) { std::cerr << "No se pudo cargar la imagen " << imgIdx << "\n"; return texCache[imgIdx] = 0; }

    GLuint id = createTexture(pixels, w, h, GL_REPEAT);
    stbi_image_free(pixels);
    return texCache[imgIdx] = id;
}

// Textura de fondo desde un archivo del PC (png, jpg, bmp...)
GLuint loadBackground(const std::string& path) {
    int w = 0, h = 0, c = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!pixels) {
        std::cerr << "No se pudo cargar el fondo: " << path << "\n";
        return 0;
    }
    GLuint id = createTexture(pixels, w, h, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);
    return id;
}

void loadPrimitive(const tg3_model& m, const tg3_primitive& p, const glm::mat4& world) {
    if (p.mode != -1 && p.mode != TG3_MODE_TRIANGLES) return;

    int posIdx = findAttr(p, "POSITION");
    if (posIdx < 0) return;
    const tg3_accessor& posAcc = m.accessors[posIdx];

    size_t n = (size_t)posAcc.count, ps = 0, ns = 0, us = 0;
    const uint8_t* pp = accData(m, posAcc, 12, ps);
    if (!pp) return;

    const uint8_t* np = nullptr;
    const uint8_t* up = nullptr;
    int nIdx = findAttr(p, "NORMAL");
    int uIdx = findAttr(p, "TEXCOORD_0");
    if (nIdx >= 0) np = accData(m, m.accessors[nIdx], 12, ns);
    if (uIdx >= 0) up = accData(m, m.accessors[uIdx], 8, us);

    // Límites del modelo (min/max del accessor transformados a mundo)
    if (posAcc.min_values_count == 3 && posAcc.max_values_count == 3) {
        for (int i = 0; i < 8; i++) {
            glm::vec3 c((i & 1) ? posAcc.max_values[0] : posAcc.min_values[0],
                (i & 2) ? posAcc.max_values[1] : posAcc.min_values[1],
                (i & 4) ? posAcc.max_values[2] : posAcc.min_values[2]);
            glm::vec3 w = glm::vec3(world * glm::vec4(c, 1.0f));
            bmin = glm::min(bmin, w); bmax = glm::max(bmax, w);
        }
    }

    // Vértices intercalados: pos(3) normal(3) uv(2)
    std::vector<float> v;
    v.reserve(n * 8);
    for (size_t i = 0; i < n; i++) {
        float pos[3], nor[3] = { 0, 0, 1 }, uv[2] = { 0, 0 };
        std::memcpy(pos, pp + i * ps, sizeof(pos));
        if (np) std::memcpy(nor, np + i * ns, sizeof(nor));
        if (up) std::memcpy(uv, up + i * us, sizeof(uv));
        v.insert(v.end(), { pos[0], pos[1], pos[2], nor[0], nor[1], nor[2], uv[0], uv[1] });
    }

    // Índices
    std::vector<unsigned> idx;
    if (p.indices >= 0) {
        const tg3_accessor& a = m.accessors[p.indices];
        size_t elem = a.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE ? 1
            : a.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? 2 : 4;
        size_t s = 0;
        const uint8_t* d = accData(m, a, elem, s);
        if (!d) return;
        for (size_t i = 0; i < (size_t)a.count; i++) {
            const uint8_t* e = d + i * s;
            if (elem == 1)      idx.push_back(*e);
            else if (elem == 2) { uint16_t t; std::memcpy(&t, e, 2); idx.push_back(t); }
            else { uint32_t t; std::memcpy(&t, e, 4); idx.push_back(t); }
        }
    }
    else {
        for (size_t i = 0; i < n; i++) idx.push_back((unsigned)i);
    }

    Mesh mesh;
    mesh.world = world;
    mesh.count = (GLuint)idx.size();

    // Material: color base + textura
    if (p.material >= 0) {
        const tg3_pbr_metallic_roughness& pbr = m.materials[p.material].pbr_metallic_roughness;
        mesh.color = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
            pbr.base_color_factor[2], pbr.base_color_factor[3]);
        int ti = pbr.base_color_texture.index;
        if (ti >= 0) {
            int src = m.textures[ti].source;
            if (src >= 0) mesh.tex = getTexture(m, src);
        }
    }

    GLuint vbo, ebo;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    meshes.push_back(mesh);
}

glm::mat4 nodeMatrix(const tg3_node& n) {
    if (n.has_matrix) {
        glm::mat4 m;
        for (int i = 0; i < 16; i++) m[i / 4][i % 4] = (float)n.matrix[i];
        return m;
    }
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(n.translation[0], n.translation[1], n.translation[2]));
    m *= glm::mat4_cast(glm::quat((float)n.rotation[3], (float)n.rotation[0],
        (float)n.rotation[1], (float)n.rotation[2]));
    m = glm::scale(m, glm::vec3(n.scale[0], n.scale[1], n.scale[2]));
    return m;
}

void processNode(const tg3_model& m, int nodeIdx, const glm::mat4& parent) {
    const tg3_node& node = m.nodes[nodeIdx];
    glm::mat4 world = parent * nodeMatrix(node);
    if (node.mesh >= 0) {
        const tg3_mesh& mesh = m.meshes[node.mesh];
        for (uint32_t i = 0; i < mesh.primitives_count; i++)
            loadPrimitive(m, mesh.primitives[i], world);
    }
    for (uint32_t i = 0; i < node.children_count; i++)
        processNode(m, node.children[i], world);
}

bool loadModel(const std::string& file) {
    size_t slash = file.find_last_of("/\\");
    gDir = (slash == std::string::npos) ? "." : file.substr(0, slash);

    tg3_parse_options opts;
    tg3_error_stack errors;
    tg3_model model;
    std::memset(&model, 0, sizeof(model));
    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);
    opts.images_as_is = 1;   // la librería no decodifica; lo hago yo con stb_image

    tg3_error_code rc = tg3_parse_file(&model, &errors, file.c_str(), (uint32_t)file.size(), &opts);

    for (uint32_t i = 0; i < errors.count; i++)
        std::cerr << "[" << (int)errors.entries[i].severity << "] "
        << (errors.entries[i].message ? errors.entries[i].message : "(sin mensaje)") << "\n";

    bool ok = (rc == TG3_OK) && model.scenes_count > 0;
    if (ok) {
        int s = model.default_scene >= 0 ? model.default_scene : 0;
        const tg3_scene& scene = model.scenes[s];
        for (uint32_t i = 0; i < scene.nodes_count; i++)
            processNode(model, scene.nodes[i], glm::mat4(1.0f));
    }
    tg3_model_free(&model);
    tg3_error_stack_free(&errors);
    return ok;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(900, 700, "Modelo glTF", nullptr, nullptr);
    glfwMakeContextCurrent(win);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glEnable(GL_DEPTH_TEST);

    // ===== RUTAS: cámbialas por las tuyas =====
    const std::string rutaModelo = "C:/Users/salam/source/repos/Original/Original/Caballero montado/source/CRUSADER_ON_HORSE.glb";
    const std::string rutaFondo = "C:/Users/salam/source/repos/Original/Original/Caballero montado/fondo.jpg";
    // ==========================================

    if (!loadModel(rutaModelo) || meshes.empty()) {
        std::cerr << "No se pudo cargar el modelo.\n";
        return 1;
    }

    GLuint bgTex = loadBackground(rutaFondo);   // 0 si no se pudo cargar (se usa el color liso)
    GLuint emptyVao;                            // el fondo no usa buffers, pero core profile exige un VAO
    glGenVertexArrays(1, &emptyVao);

    Shader modelShader(modelVertexSrc(), modelFragmentSrc());
    Shader bgShader(backgroundVertexSrc(), backgroundFragmentSrc());

    // Centrar y escalar el modelo para que siempre quepa en pantalla
    glm::vec3 center = (bmin + bmax) * 0.5f;
    glm::vec3 size = bmax - bmin;
    float scale = 2.0f / glm::max(size.x, glm::max(size.y, size.z));

    // Estado de la cámara / rotación controlada por teclado
    float yaw = 0.0f;        // izquierda / derecha (A / D)
    float pitch = 0.0f;      // arriba / abajo (W / S)
    float distancia = 4.0f;  // Q acerca, E aleja
    const float velGiro = 1.5f;   // radianes por segundo
    const float velZoom = 3.0f;   // unidades por segundo
    float ultimo = (float)glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        float ahora = (float)glfwGetTime();
        float dt = ahora - ultimo;
        ultimo = ahora;

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) yaw += velGiro * dt;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) yaw -= velGiro * dt;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) pitch += velGiro * dt;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) pitch -= velGiro * dt;
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) distancia -= velZoom * dt;
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) distancia += velZoom * dt;
        distancia = glm::clamp(distancia, 0.5f, 30.0f);

        int w, h; glfwGetFramebufferSize(win, &w, &h);
        if (w == 0 || h == 0) { glfwPollEvents(); continue; }   // ventana minimizada
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ----- Fondo -----
        if (bgTex) {
            glDisable(GL_DEPTH_TEST);
            bgShader.use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bgTex);
            bgShader.setInt("tex", 0);
            glBindVertexArray(emptyVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glEnable(GL_DEPTH_TEST);
        }

        // ----- Modelo -----
        glm::mat4 global(1.0f);
        global = glm::rotate(global, pitch, glm::vec3(1, 0, 0));
        global = glm::rotate(global, yaw, glm::vec3(0, 1, 0));
        global = glm::scale(global, glm::vec3(scale));
        global = glm::translate(global, -center);
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, distancia), glm::vec3(0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)w / h, 0.1f, 100.0f);

        modelShader.use();
        modelShader.setMat4("view", view);
        modelShader.setMat4("proj", proj);
        modelShader.setInt("tex", 0);

        for (auto& mesh : meshes) {
            modelShader.setMat4("model", global * mesh.world);
            modelShader.setVec4("baseColor", mesh.color);
            modelShader.setBool("hasTex", mesh.tex != 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mesh.tex);
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}