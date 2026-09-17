// The stb_image implementation, compiled once with its warnings silenced. Only the formats that
// Devex imports are enabled, which keeps the decoder small.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
