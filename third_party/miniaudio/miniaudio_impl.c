/* miniaudio (0.11.25) and stb_vorbis (v1.22), compiled once without Tefra's warning flags.
 *
 * Order matters. miniaudio's Ogg Vorbis decoder is switched on by STB_VORBIS_INCLUDE_STB_VORBIS_H, which the stb_vorbis
 * header defines, so the declarations come first. The stb_vorbis implementation defines the single-letter macros
 * L, C and R, which clash with the Windows headers that miniaudio includes on MinGW, so the implementation is included
 * after miniaudio. */
#define STB_VORBIS_HEADER_ONLY
#include "../stb_vorbis/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"

#include "../stb_vorbis/stb_vorbis.c"
