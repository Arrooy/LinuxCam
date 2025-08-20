#include "LinuxFace/imageRenderGL.h"

#include "imgui.h"

#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include "LinuxFace/Image/text_renderer.h"
#include "LinuxFace/UI/layerManager.h"
#include "LinuxFace/common.h"
#include "LinuxFace/profiler.h"

// Vertex shader for full-screen quad
static const char* vertex_shader_source = R"(
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
static const char* fragment_shader_source = R"(
#version 400 core
out vec4 FragColor;

in vec2 TexCoord;
uniform sampler2D ourTexture;

void main() {
    FragColor = texture(ourTexture, TexCoord);
}
)";

using namespace linuxface;

ImageRenderGL::ImageRenderGL() = default;

ImageRenderGL::~ImageRenderGL()
{
    shutdown();
}

bool ImageRenderGL::initialize()
{
    if (!createShaders())
    {
        common::logError("Failed to create shaders");
        return false;
    }
    // Vertex data will be set per-image in renderLayers
    vao_ = vbo_ = ebo_ = 0;
    return true;
}

bool ImageRenderGL::setupQuadBuffers(TextureCacheEntry& entry, const RenderBounds& bounds, int window_width,
                                     int window_height)
{
    // Check if buffers need recreation (size OR position change)
    if (entry.vao != 0 && entry.vbo != 0 && entry.ebo != 0 && static_cast<float>(entry.width) == bounds.width
        && static_cast<float>(entry.height) == bounds.height && entry.lastX == bounds.x && entry.lastY == bounds.y)
    {
        return true; // Buffers are already set up correctly
    }

    // Clean up previous buffers if they exist
    if (entry.vao != 0u)
    {
        glDeleteVertexArrays(1, &entry.vao);
    }
    if (entry.vbo != 0u)
    {
        glDeleteBuffers(1, &entry.vbo);
    }
    if (entry.ebo != 0u)
    {
        glDeleteBuffers(1, &entry.ebo);
    }

    glGenVertexArrays(1, &entry.vao);
    glGenBuffers(1, &entry.vbo);
    glGenBuffers(1, &entry.ebo);

    const float x0 = -1.0f + (2.0f * (bounds.x / window_width));
    const float y0 = 1.0f - (2.0f * (bounds.y / window_height));
    const float x1 = -1.0f + (2.0f * ((bounds.x + bounds.width) / window_width));
    const float y1 = 1.0f - (2.0f * ((bounds.y + bounds.height) / window_height));

    float vertices[] = {x0, y1, 0.0f, 1.0f, x1, y1, 1.0f, 1.0f, x1, y0, 1.0f, 0.0f, x0, y0, 0.0f, 0.0f};
    unsigned int indices[] = {0, 1, 2, 2, 3, 0};

    glBindVertexArray(entry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, entry.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) (2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    entry.width = static_cast<int>(bounds.width);
    entry.height = static_cast<int>(bounds.height);
    entry.lastX = bounds.x; // Track position changes
    entry.lastY = bounds.y;

    return true;
}

// Accept a list of layers to render (images and text)
void ImageRenderGL::RenderLayers(const std::vector<Layer>& layers, int window_width, int window_height)
{
    if (!initializeRenderingContext(window_width, window_height))
    {
        return;
    }

    for (auto& layer : const_cast<std::vector<Layer>&>(layers))
    {
        // Update animation for GIF layers (moved to layer responsibility)
        layer.updateAnimation();

        LayerRenderInfo renderInfo;
        if (prepareLayerForRendering(layer, renderInfo))
        {
            renderLayer(layer, renderInfo, windowWidth, windowHeight);
            renderLayerNameOverlay(layer, renderInfo, windowWidth, windowHeight);
            renderSelectionIndicator(layer, renderInfo);
        }
    }

    finalizeRenderingContext();
}

bool ImageRenderGL::initializeRenderingContext(int window_width, int window_height) const
{
    if (shaderProgram_ == 0)
    {
        common::logError("No shader program available for rendering");
        return false;
    }

    glViewport(0, 0, window_width, window_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shaderProgram_);

    const GLint texture_location = glGetUniformLocation(shaderProgram_, "ourTexture");
    if (texture_location >= 0)
    {
        glUniform1i(texture_location, 0);
    }

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("OpenGL error after setting texture uniform: %d", err);
        return false;
    }

    return true;
}

bool ImageRenderGL::prepareLayerForRendering(const Layer& layer, LayerRenderInfo& render_info)
{
    render_info.image = getRenderableImage(layer);
    if (render_info.image == nullptr)
    {
        return false;
    }

    render_info.textureId = getOrCreateTexture(*render_info.image, layer.id, layer.dirty);
    if (render_info.textureId == 0u)
    {
        return false;
    }

    // Mark layer as clean after successful texture creation
    const_cast<Layer&>(layer).dirty = false;

    // Set render bounds
    render_info.bounds = RenderBounds(layer.x, layer.y, static_cast<float>(render_info.image->info.width),
                                     static_cast<float>(render_info.image->info.height));

    return true;
}

Image* ImageRenderGL::getRenderableImage(const Layer& layer)
{
    switch (layer.type)
    {
        case LayerType::Image:
            return layer.img ? layer.img.get() : nullptr;

        case LayerType::Gif:
            if (layer.gif && !layer.gif->frames().empty())
            {
                auto& frame = layer.gif->frames()[layer.gifFrameIndex % layer.gif->frames().size()];
                return frame ? frame.get() : nullptr;
            }
            return nullptr;

        case LayerType::Text:
            return layer.img ? layer.img.get() : nullptr;

        default:
            return nullptr;
    }
}

bool ImageRenderGL::renderLayer(const Layer& layer, const LayerRenderInfo& render_info, int window_width,
                                int window_height)
{
    if (!render_info.isValid())
    {
        return false;
    }

    const std::string key = std::to_string(layer.id);
    auto& entry = textureCache_[key];

    if (entry.texId != render_info.textureId)
    {
        common::logError("Texture ID mismatch for layer %s", key.c_str());
        return false;
    }

    if (!setupQuadBuffers(entry, render_info.bounds, window_width, window_height))
    {
        return false;
    }

    return executeOpenGlRender(entry, render_info.textureId);
}

bool ImageRenderGL::executeOpenGlRender(const TextureCacheEntry& entry, GLuint tex_id) const
{
    // Ensure shader program is active
    glUseProgram(shaderProgram_);

    const GLint texture_location = glGetUniformLocation(shaderProgram_, "ourTexture");
    if (texture_location >= 0)
    {
        glUniform1i(texture_location, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glBindVertexArray(entry.vao);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("OpenGL render error: %d", err);
        return false;
    }

    return true;
}

void ImageRenderGL::renderLayerNameOverlay(const Layer& layer, const LayerRenderInfo& /*renderInfo*/, int window_width,
                                           int window_height)
{
    if (layer.name.empty() || !layer.textOverlay.enabled)
    {
        return;
    }

    // Check if we need to regenerate the text overlay
    auto& overlay = const_cast<Layer&>(layer).textOverlay;
    const bool should_refresh_texture = overlay.needsRefresh;

    if (overlay.needsRefresh || !overlay.cachedImage)
    {
        // Calculate opacity based on selection state
        const unsigned char text_alpha = layer.selected ? 255 : 128;
        const unsigned char bg_alpha = layer.selected ? 180 : 90;

        TextRenderConfig name_config(layer.name, {255, 255, 255, text_alpha}, 1);
        name_config.useBackground = true;
        name_config.backgroundColor = {0, 0, 0, bg_alpha};
        name_config.padding = 3;
        name_config.wrapMode = TextWrapMode::NONE;
        name_config.horizontalAlign = TextAlignment::LEFT;
        name_config.verticalAlign = TextAlignment::TOP;

        overlay.cachedImage = TextRenderer::renderText(name_config);
        overlay.needsRefresh = false;
    }

    if (!overlay.cachedImage)
    {
        return;
    }

    // Position overlay using the layer's text overlay system
    const RenderBounds overlay_bounds(overlay.getAbsoluteX(layer.x), overlay.getAbsoluteY(layer.y),
                                     static_cast<float>(overlay.cachedImage->info.width),
                                     static_cast<float>(overlay.cachedImage->info.height));

    const std::string name_key = "name_" + std::to_string(layer.id);
    const GLuint name_tex_id =
        getOrCreateTexture(*overlay.cachedImage, static_cast<size_t>(std::hash<std::string>{}(name_key)),
                           should_refresh_texture); // Use saved refresh flag

    if (name_tex_id == 0u)
    {
        return;
    }

    auto& name_entry = textureCache_[name_key];
    if (setupQuadBuffers(nameEntry, overlay_bounds, window_width, window_height))
    {
        executeOpenGlRender(nameEntry, name_tex_id);
    }
}

void ImageRenderGL::renderSelectionIndicator(const Layer& layer, const LayerRenderInfo& render_info)
{
    if (!layer.selected)
    {
        return;
    }

    const ImVec2 p_min(render_info.bounds.x, render_info.bounds.y);
    const ImVec2 p_max(render_info.bounds.x + render_info.bounds.width, render_info.bounds.y + render_info.bounds.height);

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    draw_list->AddRect(p_min, p_max, IM_COL32(0, 255, 0, 255), 0.0f, 0, 3.0f);
}

void ImageRenderGL::finalizeRenderingContext()
{
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("OpenGL error during finalization: %d", err);
    }
}

void ImageRenderGL::shutdown()
{
    if (vao_ != 0u)
    {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    if (vbo_ != 0u)
    {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (ebo_ != 0u)
    {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }

    if (shaderProgram_ != 0u)
    {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }

    // Cleanup cached textures
    cleanupTextures();
}

bool ImageRenderGL::createShaders()
{
    shaderProgram_ = createShaderProgram(vertex_shader_source, fragment_shader_source);
    return shaderProgram_ != 0;
}

GLuint ImageRenderGL::compileShader(const char* source, GLenum type)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // Check compilation status
    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        common::logError("Shader compilation failed: %s", info_log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint ImageRenderGL::createShaderProgram(const char* vertex_source, const char* fragment_source)
{
    const GLuint vertex_shader = compileShader(vertex_source, GL_VERTEX_SHADER);
    const GLuint fragment_shader = compileShader(fragment_source, GL_FRAGMENT_SHADER);

    if (vertex_shader == 0 || fragment_shader == 0)
    {
        if (vertex_shader != 0u)
        {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0u)
        {
            glDeleteShader(fragment_shader);
        }
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    // Check linking status
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        char info_log[512];
        glGetProgramInfoLog(program, 512, nullptr, info_log);
        common::logError("Shader program linking failed: %s", info_log);
        glDeleteProgram(program);
        program = 0;
    }

    // Clean up shaders (they're linked into the program now)
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

// Get or create a cached OpenGL texture for an image
GLuint ImageRenderGL::getOrCreateTexture(Image& image, size_t layer_id, bool force)
{
    // Remove thread/context/image info logs
    if (image.info.width == 0 || image.info.height == 0 || (image.data() == nullptr))
    {
        common::logError("ImageRenderGL::getOrCreateTexture - Invalid image dimensions or "
                         "data");
        return 0;
    }
    if (image.info.pixelSizeBytes != 3 && image.info.pixelSizeBytes != 4)
    {
        common::logWarn("Image pixelSizeBytes is unusual: %d (should be 3 or 4)",
                        static_cast<int>(image.info.pixelSizeBytes));
    }
    const GLenum format = (image.info.pixelSizeBytes == 4) ? GL_RGBA : GL_RGB;
    // Use the image filename as the cache key
    const std::string key = std::to_string(layer_id); // Use layer id as key
    if (force && image.info.textureId != 0)
    {
        // Also delete VAO/VBO/EBO if present
        auto it = textureCache_.find(key);
        if (it != textureCache_.end())
        {
            if (it->second.vao != 0u)
            {
                glDeleteVertexArrays(1, &it->second.vao);
            }
            if (it->second.vbo != 0u)
            {
                glDeleteBuffers(1, &it->second.vbo);
            }
            if (it->second.ebo != 0u)
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
        if (static_cast<size_t>(it->second.width) == image.info.width
            && static_cast<size_t>(it->second.height) == image.info.height && !force)
        {
            glBindTexture(GL_TEXTURE_2D, it->second.texId);
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                                 "glBindTexture: %d",
                                 err);
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                                 "glPixelStorei: %d",
                                 err);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.info.width, image.info.height, format, GL_UNSIGNED_BYTE,
                            image.data());
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                                 "glTexSubImage2D: %d",
                                 err);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            image.setTextureId(it->second.texId);
            return it->second.texId;
        }

        // Also delete VAO/VBO/EBO if present
        if (it->second.vao != 0u)
        {
            glDeleteVertexArrays(1, &it->second.vao);
        }
        if (it->second.vbo != 0u)
        {
            glDeleteBuffers(1, &it->second.vbo);
        }
        if (it->second.ebo != 0u)
        {
            glDeleteBuffers(1, &it->second.ebo);
        }
        glDeleteTextures(1, &it->second.texId);
        textureCache_.erase(it);
    }
    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                         "glBindTexture (new): %d",
                         err);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                         "glPixelStorei (new): %d",
                         err);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, image.info.width, image.info.height, 0, format, GL_UNSIGNED_BYTE,
                 image.data());
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after "
                         "glTexImage2D (new): %d",
                         err);
    }
    // Set filtering to GL_NEAREST for pixel-perfect rendering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    textureCache_[key] = TextureCacheEntry{tex_id,
                                           static_cast<int>(image.info.width),
                                           static_cast<int>(image.info.height),
                                           image.info.layer,
                                           0,
                                           0,
                                           0,
                                           -999999.0f,
                                           -999999.0f};
    image.setTextureId(tex_id);
    return tex_id;
}

void ImageRenderGL::cleanupTextures()
{
    for (auto& pair : textureCache_)
    {
        if (pair.second.vao != 0u)
        {
            glDeleteVertexArrays(1, &pair.second.vao);
        }
        if (pair.second.vbo != 0u)
        {
            glDeleteBuffers(1, &pair.second.vbo);
        }
        if (pair.second.ebo != 0u)
        {
            glDeleteBuffers(1, &pair.second.ebo);
        }
        glDeleteTextures(1, &pair.second.texId);
    }
    textureCache_.clear();
}
