#pragma once

// The part of OpenGL 3.3 and OpenGL ES 3.0 the renderer calls, as pointers that loadFunctions fills from the current
// context through SDL: no header of the system, which on Windows would bring windows.h, and the same names on a computer,
// a phone and in a browser. The names drop the gl prefix: gl::Clear is glClear.
#include <cstddef>
#include <cstdint>

#if defined(_WIN32) && !defined(_WIN64)
#define DEVEX_GL_CALL __stdcall
#else
#define DEVEX_GL_CALL
#endif

namespace devex::render::opengl::gl {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLDebugProc = void(DEVEX_GL_CALL*)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                         const GLchar* message, const void* user);

inline constexpr GLboolean GL_FALSE = 0;
inline constexpr GLboolean GL_TRUE = 1;
inline constexpr GLenum GL_NO_ERROR = 0;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_BYTE = 0x1400;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_INT = 0x1404;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_HALF_FLOAT = 0x140B;
inline constexpr GLenum GL_CULL_FACE = 0x0B44;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
inline constexpr GLenum GL_ZERO = 0;
inline constexpr GLenum GL_ONE = 1;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_FUNC_ADD = 0x8006;
inline constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_TEXTURE_WRAP_R = 0x8072;
inline constexpr GLenum GL_TEXTURE_BASE_LEVEL = 0x813C;
inline constexpr GLenum GL_TEXTURE_MAX_LEVEL = 0x813D;
inline constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY = 0x84FE;
inline constexpr GLenum GL_MAX_TEXTURE_MAX_ANISOTROPY = 0x84FF;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_LINEAR = 0x2601;
inline constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
inline constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
inline constexpr GLenum GL_REPEAT = 0x2901;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
inline constexpr GLenum GL_RED = 0x1903;
inline constexpr GLenum GL_RGB = 0x1907;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_RG = 0x8227;
inline constexpr GLenum GL_R8 = 0x8229;
inline constexpr GLenum GL_R32F = 0x822E;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
inline constexpr GLenum GL_RGBA16F = 0x881A;
inline constexpr GLenum GL_COMPRESSED_RG_RGTC2 = 0x8DBD;
inline constexpr GLenum GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C;
inline constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM = 0x8E8D;
inline constexpr GLenum GL_COMPRESSED_RGBA_ASTC_4X4 = 0x93B0;
inline constexpr GLenum GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4X4 = 0x93D0;
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11;
inline constexpr GLenum GL_STREAM_DRAW = 0x88E0;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_MAX_UNIFORM_BLOCK_SIZE = 0x8A30;
inline constexpr GLenum GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34;
inline constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
inline constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
inline constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
inline constexpr GLenum GL_VENDOR = 0x1F00;
inline constexpr GLenum GL_RENDERER = 0x1F01;
inline constexpr GLenum GL_VERSION = 0x1F02;
inline constexpr GLenum GL_EXTENSIONS = 0x1F03;
inline constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
inline constexpr GLenum GL_MAJOR_VERSION = 0x821B;
inline constexpr GLenum GL_MINOR_VERSION = 0x821C;
inline constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
inline constexpr GLenum GL_MAX_TEXTURE_SIZE = 0x0D33;
inline constexpr GLenum GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242;
inline constexpr GLenum GL_DEBUG_TYPE_ERROR = 0x824C;
inline constexpr GLenum GL_DEBUG_SEVERITY_NOTIFICATION = 0x826B;
inline constexpr GLenum GL_DEBUG_SEVERITY_HIGH = 0x9146;
inline constexpr GLenum GL_DEBUG_SEVERITY_MEDIUM = 0x9147;
inline constexpr GLenum GL_DEBUG_SEVERITY_LOW = 0x9148;
inline constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0;
inline constexpr GLenum GL_DONT_CARE = 0x1100;
inline constexpr GLenum GL_BACK_LEFT = 0x0402;
inline constexpr GLenum GL_BACK = 0x0405;
inline constexpr GLenum GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING = 0x8210;
inline constexpr GLenum GL_SRGB = 0x8C40;
inline constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;

// Every function, by its name without gl, what it returns and what it takes. The optional ones may stay null: the debug
// output, which OpenGL 4.3 and OpenGL ES 3.2 have.
#define DEVEX_GL_FUNCTIONS(REQUIRED, OPTIONAL)                                                                                     \
    REQUIRED(GetString, const GLubyte*, (GLenum name))                                                                              \
    REQUIRED(GetStringi, const GLubyte*, (GLenum name, GLuint index))                                                               \
    REQUIRED(GetIntegerv, void, (GLenum name, GLint * data))                                                                        \
    REQUIRED(GetFloatv, void, (GLenum name, GLfloat * data))                                                                        \
    REQUIRED(GetError, GLenum, ())                                                                                                  \
    REQUIRED(Enable, void, (GLenum capability))                                                                                     \
    REQUIRED(Disable, void, (GLenum capability))                                                                                    \
    REQUIRED(Viewport, void, (GLint x, GLint y, GLsizei width, GLsizei height))                                                     \
    REQUIRED(Scissor, void, (GLint x, GLint y, GLsizei width, GLsizei height))                                                      \
    REQUIRED(ClearColor, void, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))                                           \
    REQUIRED(Clear, void, (GLbitfield mask))                                                                                        \
    REQUIRED(ColorMask, void, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha))                                    \
    REQUIRED(DepthMask, void, (GLboolean flag))                                                                                     \
    REQUIRED(BlendFuncSeparate, void, (GLenum sourceColor, GLenum destinationColor, GLenum sourceAlpha, GLenum destinationAlpha))   \
    REQUIRED(BlendEquation, void, (GLenum mode))                                                                                    \
    REQUIRED(DrawArrays, void, (GLenum mode, GLint first, GLsizei count))                                                           \
    REQUIRED(DrawArraysInstanced, void, (GLenum mode, GLint first, GLsizei count, GLsizei instances))                               \
    REQUIRED(DrawElements, void, (GLenum mode, GLsizei count, GLenum type, const void* indices))                                    \
    REQUIRED(Flush, void, ())                                                                                                       \
    REQUIRED(Finish, void, ())                                                                                                      \
    REQUIRED(PixelStorei, void, (GLenum name, GLint value))                                                                         \
    REQUIRED(ReadPixels, void, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels))         \
    REQUIRED(GenTextures, void, (GLsizei count, GLuint * textures))                                                                 \
    REQUIRED(DeleteTextures, void, (GLsizei count, const GLuint* textures))                                                         \
    REQUIRED(BindTexture, void, (GLenum target, GLuint texture))                                                                    \
    REQUIRED(ActiveTexture, void, (GLenum unit))                                                                                    \
    REQUIRED(TexParameteri, void, (GLenum target, GLenum name, GLint value))                                                        \
    REQUIRED(TexImage2D, void,                                                                                                      \
             (GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format,        \
              GLenum type, const void* pixels))                                                                                     \
    REQUIRED(TexSubImage2D, void,                                                                                                   \
             (GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,             \
              const void* pixels))                                                                                                  \
    REQUIRED(CompressedTexImage2D, void,                                                                                            \
             (GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLint border, GLsizei size,        \
              const void* data))                                                                                                    \
    REQUIRED(GenSamplers, void, (GLsizei count, GLuint * samplers))                                                                 \
    REQUIRED(DeleteSamplers, void, (GLsizei count, const GLuint* samplers))                                                         \
    REQUIRED(BindSampler, void, (GLuint unit, GLuint sampler))                                                                      \
    REQUIRED(SamplerParameteri, void, (GLuint sampler, GLenum name, GLint value))                                                   \
    REQUIRED(SamplerParameterf, void, (GLuint sampler, GLenum name, GLfloat value))                                                 \
    REQUIRED(GenBuffers, void, (GLsizei count, GLuint * buffers))                                                                   \
    REQUIRED(DeleteBuffers, void, (GLsizei count, const GLuint* buffers))                                                           \
    REQUIRED(BindBuffer, void, (GLenum target, GLuint buffer))                                                                      \
    REQUIRED(BufferData, void, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                                    \
    REQUIRED(BufferSubData, void, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))                              \
    REQUIRED(BindBufferRange, void, (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size))                 \
    REQUIRED(GenVertexArrays, void, (GLsizei count, GLuint * arrays))                                                               \
    REQUIRED(DeleteVertexArrays, void, (GLsizei count, const GLuint* arrays))                                                       \
    REQUIRED(BindVertexArray, void, (GLuint array))                                                                                 \
    REQUIRED(EnableVertexAttribArray, void, (GLuint index))                                                                         \
    REQUIRED(VertexAttribPointer, void,                                                                                             \
             (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* offset))                    \
    REQUIRED(VertexAttribIPointer, void, (GLuint index, GLint size, GLenum type, GLsizei stride, const void* offset))              \
    REQUIRED(VertexAttribDivisor, void, (GLuint index, GLuint divisor))                                                             \
    REQUIRED(GenFramebuffers, void, (GLsizei count, GLuint * framebuffers))                                                         \
    REQUIRED(DeleteFramebuffers, void, (GLsizei count, const GLuint* framebuffers))                                                 \
    REQUIRED(BindFramebuffer, void, (GLenum target, GLuint framebuffer))                                                            \
    REQUIRED(FramebufferTexture2D, void, (GLenum target, GLenum attachment, GLenum textureTarget, GLuint texture, GLint level))     \
    REQUIRED(CheckFramebufferStatus, GLenum, (GLenum target))                                                                       \
    REQUIRED(GetFramebufferAttachmentParameteriv, void, (GLenum target, GLenum attachment, GLenum name, GLint * value))           \
    REQUIRED(CreateShader, GLuint, (GLenum type))                                                                                   \
    REQUIRED(DeleteShader, void, (GLuint shader))                                                                                   \
    REQUIRED(ShaderSource, void, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths))               \
    REQUIRED(CompileShader, void, (GLuint shader))                                                                                  \
    REQUIRED(GetShaderiv, void, (GLuint shader, GLenum name, GLint * value))                                                        \
    REQUIRED(GetShaderInfoLog, void, (GLuint shader, GLsizei capacity, GLsizei * length, GLchar * log))                             \
    REQUIRED(CreateProgram, GLuint, ())                                                                                             \
    REQUIRED(DeleteProgram, void, (GLuint program))                                                                                 \
    REQUIRED(AttachShader, void, (GLuint program, GLuint shader))                                                                   \
    REQUIRED(LinkProgram, void, (GLuint program))                                                                                   \
    REQUIRED(GetProgramiv, void, (GLuint program, GLenum name, GLint * value))                                                      \
    REQUIRED(GetProgramInfoLog, void, (GLuint program, GLsizei capacity, GLsizei * length, GLchar * log))                           \
    REQUIRED(UseProgram, void, (GLuint program))                                                                                    \
    REQUIRED(GetUniformLocation, GLint, (GLuint program, const GLchar* name))                                                       \
    REQUIRED(Uniform1i, void, (GLint location, GLint value))                                                                        \
    REQUIRED(GetUniformBlockIndex, GLuint, (GLuint program, const GLchar* name))                                                    \
    REQUIRED(UniformBlockBinding, void, (GLuint program, GLuint block, GLuint binding))                                             \
    OPTIONAL(DebugMessageCallback, void, (GLDebugProc callback, const void* user))                                                  \
    OPTIONAL(DebugMessageControl, void,                                                                                             \
             (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled))

#define DEVEX_GL_DECLARE(name, result, parameters) inline result(DEVEX_GL_CALL* name) parameters = nullptr;
DEVEX_GL_FUNCTIONS(DEVEX_GL_DECLARE, DEVEX_GL_DECLARE)
#undef DEVEX_GL_DECLARE

// Fills the pointers from the context current on this thread, through `find`, which gives the address of a function by
// its full name. Answers the first required function the context lacks, or null when it has them all.
[[nodiscard]] const char* loadFunctions(void* (*find)(const char* name));

} // namespace devex::render::opengl::gl
