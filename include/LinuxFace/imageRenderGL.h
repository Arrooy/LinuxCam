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
    GLuint texId;
    int width;
    int height;
    int layer;
    // Add buffer objects for VAO/VBO/EBO caching
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
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
    // Cleanup all cached textures
    void cleanupTextures();

    // Get or create a cached OpenGL texture for an image
    GLuint getOrCreateTexture(Image& image, size_t layerId, bool force = false);
    // Shader creation helpers
    bool createShaders();
    GLuint compileShader(const char* source, GLenum type);
    GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);

    // Helper to setup VAO/VBO/EBO for a quad
    void setupQuadBuffers(TextureCacheEntry& entry, float px, float py, float imgW, float imgH, int windowWidth, int windowHeight);

    GLuint vao_, vbo_, ebo_;
    GLuint shaderProgram_;

    // Texture cache: maps layer id (as string) to OpenGL texture ID and VAO/VBO/EBO
    std::unordered_map<std::string, TextureCacheEntry> textureCache_;
};

} // namespace linuxface
#endif // IMAGERENDERGL_H
