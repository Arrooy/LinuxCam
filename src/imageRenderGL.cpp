#include "FunnyFace/imageRenderGL.h"

#include "FunnyFace/common.h"
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

using namespace funnyface;

ImageRenderGL::ImageRenderGL()
    : textureId_(0), vao_(0), vbo_(0), ebo_(0), shaderProgram_(0), currentWidth_(0), currentHeight_(0)
{
}

ImageRenderGL::~ImageRenderGL()
{
    shutdown();
}

bool ImageRenderGL::initialize()
{
    common::log_info("ImageRenderGL starting");
    // Create shaders
    if (!createShaders())
    {
        common::log_error("Failed to create shaders");
        return false;
    }

    // Create full-screen quad vertices
    float vertices[] = {
        // positions   // texture coords
        -1.0f, -1.0f, 0.0f, 1.0f, // bottom left
        1.0f,  -1.0f, 1.0f, 1.0f, // bottom right
        1.0f,  1.0f,  1.0f, 0.0f, // top right
        -1.0f, 1.0f,  0.0f, 0.0f  // top left
    };

    unsigned int indices[] = {
        0, 1, 2, // first triangle
        2, 3, 0  // second triangle
    };

    // Generate and bind VAO
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) 0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*) (2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Generate texture
    glGenTextures(1, &textureId_);

    // Unbind
    glBindVertexArray(0);

    common::log_info("ImageRenderGL initialized successfully");
    return true;
}

bool ImageRenderGL::uploadImage(const Image& image)
{
    if (!image.data() || image.info.width <= 0 || image.info.height <= 0)
    {
        common::log_error("Invalid image data");
        return false;
    }
    // TODO: FIXME: If input and output config sizes are different, this breaks. (core dumped)
    glBindTexture(GL_TEXTURE_2D, textureId_);

    // Determine format
    GLenum format = (image.info.pixelSizeBytes == 4) ? GL_RGBA : GL_RGB;

    // Only reallocate texture if size changed (avoids unnecessary GPU memory allocation)
    if (currentWidth_ != image.info.width || currentHeight_ != image.info.height)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, format, image.info.width, image.info.height, 0, format, GL_UNSIGNED_BYTE,
                     image.data());
        currentWidth_ = image.info.width;
        currentHeight_ = image.info.height;
        common::log_info("Texture reallocated: %d - %d", image.info.width, image.info.height);
    }
    else
    {
        // Just update the existing texture data (faster!)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.info.width, image.info.height, format, GL_UNSIGNED_BYTE,
                        image.data());
    }

    // Set texture parameters for optimal performance
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void ImageRenderGL::renderBackground(int windowWidth, int windowHeight)
{
    if (textureId_ == 0 || shaderProgram_ == 0)
    {
        return; // No texture or shader available
    }

    // TODO: Fix error black image. At start.
    // Disable depth testing for background
    glDisable(GL_DEPTH_TEST);

    // Use our shader program
    glUseProgram(shaderProgram_);

    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId_);

    // Render full-screen quad
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // Re-enable depth testing for UI
    glEnable(GL_DEPTH_TEST);
}

void ImageRenderGL::shutdown()
{
    if (textureId_)
    {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }

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
