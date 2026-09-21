// The implementations of miniaudio and of stb_vorbis, which decodes Ogg Vorbis for it, compiled once
// with their warnings silenced. Encoding is left out: the engine only plays sounds.
#if defined(_MSC_VER)
#pragma warning(push, 0)
// Found while generating code, these are not silenced by the level above.
#pragma warning(disable : 4701 4702 4703)
#endif

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

// No pop: warnings found while generating code, such as C4701, are reported at the end of the file,
// which holds nothing else.
