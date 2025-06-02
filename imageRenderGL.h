#ifndef IMAGERENDERGL_H
#define IMAGERENDERGL_H
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on
#include "image.h"

namespace funnyface
{

class ImageRenderGL
{
  public:
    ImageRenderGL();
    ~ImageRenderGL();

    // Initialize the image renderer
    bool initialize();

    // Upload image data to GPU (minimal copy, reuses texture if possible)
    bool uploadImage(const Image& image);

    // Render the current image as background
    void renderBackground(int windowWidth, int windowHeight);

    // Cleanup
    void shutdown();

  private:
    // Shader creation helpers
    bool createShaders();
    GLuint compileShader(const char* source, GLenum type);
    GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource);

    GLuint textureId_;
    GLuint vao_, vbo_, ebo_;
    GLuint shaderProgram_;

    unsigned long currentWidth_;
    unsigned long currentHeight_;
};

} // namespace funnyface
#endif // IMAGERENDERGL_H
