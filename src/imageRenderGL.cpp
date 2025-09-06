#include "LinuxFace/imageRenderGL.h"

#include "imgui.h"

#include <cmath>
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

bool ImageRenderGL::setupQuadBuffers(TextureCacheEntry& entry, const RenderBounds& bounds, int windowWidth,
                                     int windowHeight)
{
    // Check if buffers need recreation (size, position, OR window size change)
    if (entry.vao != 0 && entry.vbo != 0 && entry.ebo != 0 && static_cast<float>(entry.width) == bounds.width
        && static_cast<float>(entry.height) == bounds.height && entry.lastX == bounds.x && entry.lastY == bounds.y
        && entry.lastWindowWidth == windowWidth && entry.lastWindowHeight == windowHeight)
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

    const float x0 = -1.0f + 2.0f * (bounds.x / windowWidth);
    const float y0 = 1.0f - 2.0f * (bounds.y / windowHeight);
    const float x1 = -1.0f + 2.0f * ((bounds.x + bounds.width) / windowWidth);
    const float y1 = 1.0f - 2.0f * ((bounds.y + bounds.height) / windowHeight);

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
    entry.lastX = bounds.x;
    entry.lastY = bounds.y;
    entry.lastWindowWidth = windowWidth;
    entry.lastWindowHeight = windowHeight;
    // Bump generation since we (re)created buffers
    entry.bufferGeneration++;

    return true;
}

// Accept a list of layers to render (images and text)
void ImageRenderGL::renderLayers(const std::vector<Layer>& layers, int windowWidth, int windowHeight)
{
    if (!initializeRenderingContext(windowWidth, windowHeight))
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

bool ImageRenderGL::initializeRenderingContext(int windowWidth, int windowHeight) const
{
    if (shaderProgram_ == 0)
    {
        common::logError("No shader program available for rendering");
        return false;
    }

    glViewport(0, 0, windowWidth, windowHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shaderProgram_);

    const GLint textureLocation = glGetUniformLocation(shaderProgram_, "ourTexture");
    if (textureLocation >= 0)
    {
        glUniform1i(textureLocation, 0);
    }

    const GLenum err = glGetError();
    return err == GL_NO_ERROR;
}

bool ImageRenderGL::prepareLayerForRendering(const Layer& layer, LayerRenderInfo& renderInfo)
{
    renderInfo.image = getRenderableImage(layer);
    if (renderInfo.image == nullptr)
    {
        return false;
    }

    renderInfo.textureId = getOrCreateTexture(*renderInfo.image, layer.id, layer.dirty);
    if (renderInfo.textureId == 0u)
    {
        return false;
    }

    // Mark layer as clean after successful texture creation
    const_cast<Layer&>(layer).dirty = false;

    // Set render bounds
    renderInfo.bounds = RenderBounds(layer.x, layer.y, static_cast<float>(renderInfo.image->info.width),
                                     static_cast<float>(renderInfo.image->info.height));

    return true;
}

Image* ImageRenderGL::getRenderableImage(const Layer& layer)
{
    switch (layer.type)
    {
        case LayerType::IMAGE:
            return layer.img ? layer.img.get() : nullptr;

        case LayerType::GIF:
            if (layer.gif && !layer.gif->frames().empty())
            {
                auto& frame = layer.gif->frames()[layer.gifFrameIndex % layer.gif->frames().size()];
                return frame ? frame.get() : nullptr;
            }
            return nullptr;

        case LayerType::TEXT:
            return layer.img ? layer.img.get() : nullptr;

        case LayerType::VIDEO:
            return layer.currentVideoFrame ? layer.currentVideoFrame.get() : nullptr;

        default:
            return nullptr;
    }
}

bool ImageRenderGL::renderLayer(const Layer& layer, const LayerRenderInfo& renderInfo, int windowWidth,
                                int windowHeight)
{
    if (!renderInfo.isValid())
    {
        return false;
    }

    const std::string key = std::to_string(layer.id);
    auto& entry = textureCache_[key];

    if (entry.texId != renderInfo.textureId)
    {
        common::logError("Texture ID mismatch for layer %s", key.c_str());
        return false;
    }

    if (!setupQuadBuffers(entry, renderInfo.bounds, windowWidth, windowHeight))
    {
        return false;
    }

    return executeOpenGLRender(entry, renderInfo.textureId);
}

bool ImageRenderGL::executeOpenGLRender(const TextureCacheEntry& entry, GLuint texId) const
{
    // Ensure shader program is active
    glUseProgram(shaderProgram_);

    const GLint textureLocation = glGetUniformLocation(shaderProgram_, "ourTexture");
    if (textureLocation >= 0)
    {
        glUniform1i(textureLocation, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    glBindVertexArray(entry.vao);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    const GLenum err = glGetError();
    return err == GL_NO_ERROR;
}

void ImageRenderGL::renderLayerNameOverlay(const Layer& layer, const LayerRenderInfo& /*renderInfo*/, int windowWidth,
                                           int windowHeight)
{
    if (layer.name.empty() || !layer.textOverlay.enabled)
    {
        return;
    }

    // Check if we need to regenerate the text overlay
    auto& overlay = const_cast<Layer&>(layer).textOverlay;
    const bool shouldRefreshTexture = overlay.needsRefresh;

    if (overlay.needsRefresh || !overlay.cachedImage)
    {
        // Calculate opacity based on selection state
        const unsigned char textAlpha = layer.selected ? 255 : 128;
        const unsigned char bgAlpha = layer.selected ? 180 : 90;

        TextRenderConfig nameConfig(layer.name, {255, 255, 255, textAlpha}, 1);
        nameConfig.useBackground = true;
        nameConfig.backgroundColor = {0, 0, 0, bgAlpha};
        nameConfig.padding = 3;
        nameConfig.wrapMode = TextWrapMode::NONE;
        nameConfig.horizontalAlign = TextAlignment::LEFT;
        nameConfig.verticalAlign = TextAlignment::TOP;

        overlay.cachedImage = TextRenderer::renderText(nameConfig);
        overlay.needsRefresh = false;
    }

    if (!overlay.cachedImage)
    {
        return;
    }

    // Position overlay using the layer's text overlay system
    const RenderBounds overlayBounds(overlay.getAbsoluteX(layer.x), overlay.getAbsoluteY(layer.y),
                                     static_cast<float>(overlay.cachedImage->info.width),
                                     static_cast<float>(overlay.cachedImage->info.height));

    const std::string nameKey = "name_" + std::to_string(layer.id);
    // Use a stable hashed key for both texture creation and cache lookup
    const size_t nameKeyHash = std::hash<std::string>{}(nameKey);
    const GLuint nameTexId = getOrCreateTexture(*overlay.cachedImage, nameKeyHash,
                                                shouldRefreshTexture); // Use saved refresh flag

    if (nameTexId == 0u)
    {
        return;
    }

    // getOrCreateTexture uses std::to_string(layerId) as the cache key
    const std::string textureCacheKey = std::to_string(nameKeyHash);
    auto& nameEntry = textureCache_[textureCacheKey];
    if (setupQuadBuffers(nameEntry, overlayBounds, windowWidth, windowHeight))
    {
        executeOpenGLRender(nameEntry, nameTexId);
    }
}

void ImageRenderGL::renderSelectionIndicator(const Layer& layer, const LayerRenderInfo& renderInfo)
{
    if (!layer.selected)
    {
        return;
    }

    const ImVec2 pMin(renderInfo.bounds.x, renderInfo.bounds.y);
    const ImVec2 pMax(renderInfo.bounds.x + renderInfo.bounds.width, renderInfo.bounds.y + renderInfo.bounds.height);

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddRect(pMin, pMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 3.0f);
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
    shaderProgram_ = createShaderProgram(vertexShaderSource, fragmentShaderSource);
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
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        common::logError("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint ImageRenderGL::createShaderProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertexShader = compileShader(vertexSource, GL_VERTEX_SHADER);
    const GLuint fragmentShader = compileShader(fragmentSource, GL_FRAGMENT_SHADER);

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0u)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0u)
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
    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        common::logError("Shader program linking failed: %s", infoLog);
        glDeleteProgram(program);
        program = 0;
    }

    // Clean up shaders (they're linked into the program now)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// Get or create a cached OpenGL texture for an image
GLuint ImageRenderGL::getOrCreateTexture(Image& image, size_t layerId, bool force)
{
    // Remove thread/context/image info logs
    if (image.info.width == 0 || image.info.height == 0 || (image.data() == nullptr))
    {
        common::logError("ImageRenderGL::getOrCreateTexture - Invalid image dimensions or data");
        return 0;
    }
    if (image.info.pixelSizeBytes != 3 && image.info.pixelSizeBytes != 4)
    {
        common::logWarn("Image pixelSizeBytes is unusual: %d (should be 3 or 4)",
                        static_cast<int>(image.info.pixelSizeBytes));
    }
    const GLenum format = (image.info.pixelSizeBytes == 4) ? GL_RGBA : GL_RGB;
    // Use the image filename as the cache key
    const std::string key = std::to_string(layerId); // Use layer id as key
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
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glBindTexture: %d", err);
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glPixelStorei: %d", err);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.info.width, image.info.height, format, GL_UNSIGNED_BYTE,
                            image.data());
            err = glGetError();
            if (err != GL_NO_ERROR)
            {
                common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glTexSubImage2D: %d", err);
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
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glBindTexture (new): %d", err);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glPixelStorei (new): %d", err);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, format, image.info.width, image.info.height, 0, format, GL_UNSIGNED_BYTE,
                 image.data());
    err = glGetError();
    if (err != GL_NO_ERROR)
    {
        common::logError("ImageRenderGL::getOrCreateTexture - OpenGL error after glTexImage2D (new): %d", err);
    }
    // Set filtering to GL_NEAREST for pixel-perfect rendering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    textureCache_[key] = TextureCacheEntry{texId,
                                           static_cast<int>(image.info.width),
                                           static_cast<int>(image.info.height),
                                           image.info.layer,
                                           0,
                                           0,
                                           0,
                                           0,
                                           -999999.0f,
                                           -999999.0f,
                                           0,
                                           0};
    image.setTextureId(texId);
    return texId;
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

bool ImageRenderGL::getRenderInfoForLayer(size_t layerId, RenderInfo& out)
{
    auto it = textureCache_.find(std::to_string(layerId));
    if (it == textureCache_.end())
    {
        return false;
    }
    out = RenderInfo{it->second.bufferGeneration, it->second.lastWindowWidth, it->second.lastWindowHeight};
    return true;
}

bool ImageRenderGL::getRenderInfoForKey(const std::string& key, RenderInfo& out)
{
    auto it = textureCache_.find(key);
    if (it == textureCache_.end())
    {
        return false;
    }
    out = RenderInfo{it->second.bufferGeneration, it->second.lastWindowWidth, it->second.lastWindowHeight};
    return true;
}
