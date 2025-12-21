/*
** png_encoder.h - PNG Encoder Interface
**
** Functions for encoding RGB data to PNG format using libpng
*/

#ifndef PNG_ENCODER_H
#define PNG_ENCODER_H

#include <exec/types.h>
#include "iffpicturelib/iffpicture.h"
/* PNG color type constants are defined in libpng/png.h */

/* PNG color structure (matches libpng png_color) */
struct PNGColor {
    UBYTE red;
    UBYTE green;
    UBYTE blue;
};

/* PNG configuration structure */
struct PNGConfig {
    int color_type;      /* PNG_COLOR_TYPE_* */
    int bit_depth;       /* 1, 2, 4, 8, 16 */
    int has_alpha;       /* TRUE/FALSE */
    struct PNGColor *palette;  /* For indexed color */
    int num_palette;     /* Number of palette entries */
    UBYTE *trans;        /* Transparency array */
    int num_trans;       /* Number of transparent entries */
};

/* Function prototypes */
LONG PNGEncoder_Write(const char *filename, UBYTE *rgbData, 
                      struct PNGConfig *config, struct IFFPicture *picture, BOOL stripMetadata);
VOID PNGEncoder_FreeConfig(struct PNGConfig *config);

#endif /* PNG_ENCODER_H */

