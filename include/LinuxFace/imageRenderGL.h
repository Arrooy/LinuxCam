#ifndef IMAGERENDERGL_H
#define IMAGERENDERGL_H
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on
#include <memory>
#include <unordered_map>

#include "LinuxFace/Image/image.h"
#include "LinuxFace/UI/layerManager.h"

namespace linuxface
{

// Helper struct for cache
struct TextureCacheEntry
{
    GLuint texId{};
    int width{};
    int height{};
    int layer{};
    // Add buffer objects for VAO/VBO/EBO caching
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    // Add position tracking for text overlays
    float lastX = -999999.0f; // Use impossible values to force initial setup
    float lastY = -999999.0f;
};

class ImageRenderGL
{
  public:
    ImageRenderGL();
    ~ImageRenderGL();

    // Initialize the image renderer
    bool initialize();

    // Cleanup
    void shutdown();

    // Render a list of layers (images and text)
    void renderLayers(const std::vector<Layer>& layers, int windowWidth, int windowHeight);

  private:
    // Simplified rendering structures
    struct RenderBounds
    {
        float x, y, width, height;
        RenderBounds() : x(0), y(0), width(0), height(0) {}
        RenderBounds(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}
    };

    struct LayerRenderInfo
    {
        Image* image = nullptr;
        GLuint textureId = 0;
        RenderBounds bounds;
        bool isValid() const { return image != nullptr && textureId != 0; }
    };

    // Core rendering pipeline
    bool initializeRenderingContext(int window_width, int window_height) const;
    bool prepareLayerForRendering(const Layer& layer, LayerRenderInfo& render_info);
    static Image* getRenderableImage(const Layer& layer);

    // Layer rendering (unified interface with consistent parameters)
    bool renderLayer(const Layer& layer, const LayerRenderInfo& render_info, int window_width, int window_height);
    void
    renderLayerNameOverlay(const Layer& layer, const LayerRenderInfo& renderInfo, int window_width, int window_height);
    static void renderSelectionIndicator(const Layer& layer, const LayerRenderInfo& render_info);
    static void finalizeRenderingContext();

    // Internal OpenGL helpers (unified interface)
    GLuint getOrCreateTexture(Image& image, size_t layer_id, bool force = false);
    static bool
    setupQuadBuffers(TextureCacheEntry& entry, const RenderBounds& bounds, int window_width, int window_height);
    bool executeOpenGlRender(const TextureCacheEntry& entry, GLuint tex_id) const;
    void cleanupTextures();

    // Shader management
    bool createShaders();
    static GLuint compileShader(const char* source, GLenum type);
    static GLuint createShaderProgram(const char* vertex_source, const char* fragment_source);

    GLuint vao_{0}, vbo_{0}, ebo_{0};
    GLuint shaderProgram_{0};

    // Texture cache: maps layer id (as string) to OpenGL texture ID and VAO/VBO/EBO
    std::unordered_map<std::string, TextureCacheEntry> textureCache_;
};

} // namespace linuxface
#endif // IMAGERENDERGL_H
