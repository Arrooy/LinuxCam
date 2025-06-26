#include "LinuxFace/imageRenderGL.h"

#include <unistd.h>

#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"
#include "imgui.h"

// Vertex shader for full-screen quad
const char* vertexShaderSource = R"(
#version 400 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// Fragment shader for texture rendering
const char* fragmentShaderSource = R"(
#version 400 core
out vec4 FragColor;

in vec2 TexCoord;
uniform sampler2D ourTexture;

void main() {
    FragColor = texture(ourTexture, TexCoord);
}
)";

using namespace linuxface;

ImageRenderGL::ImageRenderGL() : vao_(0), vbo_(0), ebo_(0), shaderProgram_(0)
{
}

ImageRenderGL::~ImageRenderGL()
{
    shutdown();
}

bool ImageRenderGL::initialize()
{
    if (!createShaders())
    {
        common::log_error("Failed to create shaders");
        return false;
    }
    // Vertex data will be set per-image in renderLayers
    vao_ = vbo_ = ebo_ = 0;
    return true;
}
bool ImageRenderGL::uploadImage(Image& image, bool force)
{
    GLuint texId = getOrCreateTexture(image, force);
    return texId != 0;
}

// Accept a list of layers to render (images and text)
void ImageRenderGL::renderLayers(const std::vector<Layer>& layers, int windowWidth, int windowHeight)
{
    if (shaderProgram_ == 0)
    {
        common::log_error("No shader program available for rendering");
        return; // No shader available
    }
    glViewport(0, 0, windowWidth, windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shaderProgram_);
    GLint textureLocation = glGetUniformLocation(shaderProgram_, "ourTexture");
    if (textureLocation >= 0)
    {
        glUniform1i(textureLocation, 0);
    }
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::log_error("OpenGL error after setting texture uniform: %d", err);
    }
    for (auto& layer : const_cast<std::vector<Layer>&>(layers))
    {
        if (layer.type == LayerType::Image && layer.img)
        {
            GLuint texId = getOrCreateTexture(*layer.img, layer.dirty);
            if (layer.dirty)
            {
                layer.dirty = false;
            }
            if (texId)
            {
                std::string key = layer.img->info.filename;
                auto& entry = textureCache_[key];
                // If VAO/VBO/EBO not created or window/image size changed, (re)create them
                bool needRecreate = (entry.vao == 0 || entry.vbo == 0 || entry.ebo == 0
                                     || entry.width != layer.img->info.width || entry.height != layer.img->info.height);
                if (needRecreate)
                {
                    if (entry.vao)
                    {
                        glDeleteVertexArrays(1, &entry.vao);
                    }
                    if (entry.vbo)
                    {
                        glDeleteBuffers(1, &entry.vbo);
                    }
                    if (entry.ebo)
                    {
                        glDeleteBuffers(1, &entry.ebo);
                    }
                    glGenVertexArrays(1, &entry.vao);
                    glGenBuffers(1, &entry.vbo);
                    glGenBuffers(1, &entry.ebo);
                    float imgW = static_cast<float>(layer.img->info.width);
                    float imgH = static_cast<float>(layer.img->info.height);
                    float winW = static_cast<float>(windowWidth);
                    float winH = static_cast<float>(windowHeight);
                    float px = static_cast<float>(layer.img->info.x); // image x position in pixels
                    float py = static_cast<float>(layer.img->info.y); // image y position in pixels
                    // Convert pixel coordinates to normalized device coordinates (NDC), with (0,0) at top-left
                    float x0 = -1.0f + 2.0f * (px / winW);
                    float y0 = 1.0f - 2.0f * (py / winH);
                    float x1 = -1.0f + 2.0f * ((px + imgW) / winW);
                    float y1 = 1.0f - 2.0f * ((py + imgH) / winH);
                    float vertices[] = {
                        x0, y1, 0.0f, 1.0f, // bottom left (note: y1)
                        x1, y1, 1.0f, 1.0f, // bottom right
                        x1, y0, 1.0f, 0.0f, // top right (note: y0)
                        x0, y0, 0.0f, 0.0f  // top left
                    };
                    unsigned int indices[] = {0, 1, 2, 2, 3, 0};
                    glBindVertexArray(entry.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, entry.vbo);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) 0);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) (2 * sizeof(float)));
                    glEnableVertexAttribArray(1);
                    glBindVertexArray(0);
                    // Update cached width/height
                    entry.width = layer.img->info.width;
                    entry.height = layer.img->info.height;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texId);
                glBindVertexArray(entry.vao);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        else if (layer.type == LayerType::Text)
        {
            // Use ImGui for text rendering
            ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
            ImVec2 pos = ImVec2(layer.x, layer.y);
            draw_list->AddText(nullptr, layer.fontSize, pos, layer.textColor, layer.textContent.c_str());
        }
    }
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::log_error("OpenGL error: %d", err);
    }
}

void ImageRenderGL::shutdown()
{
    if (vao_)
    {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    if (vbo_)
    {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (ebo_)
    {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }

    if (shaderProgram_)
    {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }

    // Cleanup cached textures
    cleanupTextures();
}

bool ImageRenderGL::createShaders()
{
    shaderProgram_ = createShaderProgram(vertexShaderSource, fragmentShaderSource);
    return shaderProgram_ != 0;
}

GLuint ImageRenderGL::compileShader(const char* source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // Check compilation status
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        common::log_error("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint ImageRenderGL::createShaderProgram(const char* vertexSource, const char* fragmentSource)
{
    GLuint vertexShader = compileShader(vertexSource, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentSource, GL_FRAGMENT_SHADER);

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader)
        {
            glDeleteShader(fragmentShader);
        }
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    // Check linking status
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        common::log_error("Shader program linking failed: %s", infoLog);
        glDeleteProgram(program);
        program = 0;
    }

    // Clean up shaders (they're linked into the program now)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// Get or create a cached OpenGL texture for an image
GLuint ImageRenderGL::getOrCreateTexture(Image& image, bool force)
{
    // Remove thread/context/image info logs
    if (image.info.width == 0 || image.info.height == 0 || !image.data())
    {
        common::log_error("[getOrCreateTexture] Invalid image dimensions or data");
        return 0;
    }
    if (image.info.pixelSizeBytes != 3 && image.info.pixelSizeBytes != 4)
    {
        common::log_warn("Image pixelSizeBytes is unusual: %d (should be 3 or 4)", image.info.pixelSizeBytes);
    }
    GLenum format = (image.info.pixelSizeBytes == 4) ? GL_RGBA : GL_RGB;
    std::string key = image.info.filename;
    if (force && image.info.textureId != 0)
    {
        // Also delete VAO/VBO/EBO if present
        auto it = textureCache_.find(key);
        if (it != textureCache_.end())
        {
            if (it->second.vao)
            {
                glDeleteVertexArrays(1, &it->second.vao);
            }
            if (it->second.vbo)
            {
                glDeleteBuffers(1, &it->second.vbo);
            }
            if (it->second.ebo)
            {
                glDeleteBuffers(1, &it->second.ebo);
            }
        }
        glDeleteTextures(1, &image.info.textureId);
        image.info.textureId = 0;
        textureCache_.erase(key);
    }
    auto it = textureCache_.find(key);
    if (it != textureCache_.end())
    {
        if (it->second.layer != image.info.layer)
        {
            it->second.layer = image.info.layer;
        }
        if (it->second.width == image.info.width && it->second.height == image.info.height && !force)
        {
            glBindTexture(GL_TEXTURE_2D, it->second.texId);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::log_error("[getOrCreateTexture] OpenGL error after glBindTexture: %d", err);
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::log_error("[getOrCreateTexture] OpenGL error after glPixelStorei: %d", err);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.info.width, image.info.height, format, GL_UNSIGNED_BYTE,
                            image.data());
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::log_error("[getOrCreateTexture] OpenGL error after glTexSubImage2D: %d", err);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            image.setTextureId(it->second.texId);
            return it->second.texId;
        }
        else
        {
            // Also delete VAO/VBO/EBO if present
            if (it->second.vao)
            {
                glDeleteVertexArrays(1, &it->second.vao);
            }
            if (it->second.vbo)
            {
                glDeleteBuffers(1, &it->second.vbo);
            }
            if (it->second.ebo)
            {
                glDeleteBuffers(1, &it->second.ebo);
            }
            glDeleteTextures(1, &it->second.texId);
            textureCache_.erase(it);
        }
    }
    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::log_error("[getOrCreateTexture] OpenGL error after glBindTexture (new): %d", err);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::log_error("[getOrCreateTexture] OpenGL error after glPixelStorei (new): %d", err);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, image.info.width, image.info.height, 0, format, GL_UNSIGNED_BYTE,
                 image.data());
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::log_error("[getOrCreateTexture] OpenGL error after glTexImage2D (new): %d", err);
    }
    // Set filtering to GL_NEAREST for pixel-perfect rendering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    // VAO/VBO/EBO will be created in renderLayers when needed
    textureCache_[key] = TextureCacheEntry{
        texId, static_cast<int>(image.info.width), static_cast<int>(image.info.height), image.info.layer, 0, 0, 0};
    image.setTextureId(texId);
    return texId;
}

void ImageRenderGL::cleanupTextures()
{
    for (auto& pair : textureCache_)
    {
        if (pair.second.vao)
        {
            glDeleteVertexArrays(1, &pair.second.vao);
        }
        if (pair.second.vbo)
        {
            glDeleteBuffers(1, &pair.second.vbo);
        }
        if (pair.second.ebo)
        {
            glDeleteBuffers(1, &pair.second.ebo);
        }
        glDeleteTextures(1, &pair.second.texId);
    }
    textureCache_.clear();
}
