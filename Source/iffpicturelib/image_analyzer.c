/*
** image_analyzer.c - Image Analyzer Implementation (Internal to Library)
**
** Analyzes IFF image properties and determines optimal PNG settings
*/

#include "iffpicture_private.h"
#include "/debug.h"
#include "png_encoder.h" /* do NOT add .. to this path */
#include <png.h> /* For PNG_COLOR_TYPE_* constants */
#include <proto/exec.h>
#include <proto/utility.h>

/*
** AnalyzeFormat - Analyze image format and properties (implementation)
** Sets internal flags based on image characteristics
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
LONG AnalyzeFormat(struct IFFPicture *picture)
{
    ULONG i;
    BOOL isGray;
    
    if (!picture || !picture->isLoaded || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not loaded or BMHD missing");
        return RETURN_FAIL;
    }
    
    /* Compression flag already set in ReadBMHD */
    /* HAM/EHB flags already set in ReadCAMG */
    /* Alpha flag already set in ReadBMHD */
    
    /* Determine if grayscale */
    if (picture->isIndexed && picture->cmap && picture->cmap->data) {
        isGray = TRUE;
        for (i = 0; i < picture->cmap->numcolors; ++i) {
            UBYTE r = picture->cmap->data[i * 3];
            UBYTE g = picture->cmap->data[i * 3 + 1];
            UBYTE b = picture->cmap->data[i * 3 + 2];
            
            /* Handle 4-bit palette scaling for comparison */
            if (picture->cmap->is4Bit) {
                r |= (r >> 4);
                g |= (g >> 4);
                b |= (b >> 4);
            }
            
            if (r != g || g != b) {
                isGray = FALSE;
                break;
            }
        }
        picture->isGrayscale = isGray;
    } else if (!picture->isIndexed && picture->bmhd->nPlanes == 1) {
        /* 1-bit non-indexed images are typically grayscale */
        picture->isGrayscale = TRUE;
    } else if (picture->formtype == ID_DEEP || picture->formtype == ID_RGBN || 
               picture->formtype == ID_RGB8 || picture->isHAM) {
        /* True-color formats are not grayscale by default */
        picture->isGrayscale = FALSE;
    }
    
    return RETURN_OK;
}

/*
** GetOptimalPNGConfig - Get optimal PNG configuration (implementation)
** Determines the best PNG color type, bit depth, and other settings
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config)
{
    ULONG i;
    ULONG numColors;
    
    if (!picture || !config || !picture->isLoaded || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid parameters for PNG config");
        return RETURN_FAIL;
    }
    
    DEBUG_PUTSTR("DEBUG: GetOptimalPNGConfig - Starting analysis\n");
    DEBUG_PRINTF5("DEBUG: isHAM=%ld isEHB=%ld isIndexed=%ld isGrayscale=%ld hasAlpha=%ld\n",
                  (ULONG)(picture->isHAM ? 1 : 0), (ULONG)(picture->isEHB ? 1 : 0),
                  (ULONG)(picture->isIndexed ? 1 : 0), (ULONG)(picture->isGrayscale ? 1 : 0),
                  (ULONG)(picture->hasAlpha ? 1 : 0));
    
    /* Initialize config with defaults */
    config->color_type = PNG_COLOR_TYPE_RGB;
    config->bit_depth = 8;
    config->has_alpha = picture->hasAlpha;
    config->palette = NULL;
    config->num_palette = 0;
    config->trans = NULL;
    config->num_trans = 0;
    
    /* Determine optimal PNG format based on image characteristics */
    if (picture->isHAM || picture->isEHB || picture->formtype == ID_DEEP || 
        picture->formtype == ID_RGBN || picture->formtype == ID_RGB8) {
        /* True-color formats - use RGB or RGBA */
        config->color_type = PNG_COLOR_TYPE_RGB;
        config->bit_depth = 8;
        if (picture->hasAlpha) {
            config->color_type = PNG_COLOR_TYPE_RGBA;
        }
    } else if (picture->isIndexed && picture->cmap && picture->cmap->data) {
        /* Indexed color image */
        numColors = picture->cmap->numcolors;
        
        if (picture->isGrayscale) {
            /* Grayscale indexed */
            config->color_type = PNG_COLOR_TYPE_GRAY;
            
            /* Determine optimal bit depth for grayscale */
            if (numColors <= 2) {
                config->bit_depth = 1;
            } else if (numColors <= 4) {
                config->bit_depth = 2;
            } else if (numColors <= 16) {
                config->bit_depth = 4;
            } else {
                config->bit_depth = 8;
            }
        } else {
            /* Color indexed */
            config->color_type = PNG_COLOR_TYPE_PALETTE;
            
            /* Determine optimal bit depth for palette */
            if (numColors <= 2) {
                config->bit_depth = 1;
            } else if (numColors <= 4) {
                config->bit_depth = 2;
            } else if (numColors <= 16) {
                config->bit_depth = 4;
            } else {
                config->bit_depth = 8;
            }
            
            /* Allocate and copy palette */
            config->num_palette = numColors;
            DEBUG_PRINTF1("DEBUG: GetOptimalPNGConfig - Allocating palette with %ld entries\n", numColors);
            config->palette = (struct PNGColor *)AllocMem(numColors * sizeof(struct PNGColor), MEMF_CLEAR);
            if (!config->palette) {
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate PNG palette");
                return RETURN_FAIL;
            }
            
            for (i = 0; i < numColors; ++i) {
                config->palette[i].red = picture->cmap->data[i * 3];
                config->palette[i].green = picture->cmap->data[i * 3 + 1];
                config->palette[i].blue = picture->cmap->data[i * 3 + 2];
                
                /* Handle 4-bit palette scaling */
                if (picture->cmap->is4Bit) {
                    config->palette[i].red |= (config->palette[i].red >> 4);
                    config->palette[i].green |= (config->palette[i].green >> 4);
                    config->palette[i].blue |= (config->palette[i].blue >> 4);
                }
            }
        }
        
        /* Handle transparent color for indexed images */
        if (picture->bmhd->masking == mskHasTransparentColor) {
            config->num_trans = 1;
            config->trans = (UBYTE *)AllocMem(sizeof(UBYTE), MEMF_CLEAR);
            if (!config->trans) {
                if (config->palette) {
                    FreeMem(config->palette, config->num_palette * sizeof(struct PNGColor));
                    config->palette = NULL;
                }
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate PNG transparency");
                return RETURN_FAIL;
            }
            config->trans[0] = (UBYTE)picture->bmhd->transparentColor;
        }
    } else {
        /* Non-indexed, non-true-color (e.g., 1-bit B/W without CMAP) */
        if (picture->isGrayscale) {
            config->color_type = PNG_COLOR_TYPE_GRAY;
            if (picture->bmhd->nPlanes == 1) {
                config->bit_depth = 1;
            } else if (picture->bmhd->nPlanes <= 8) {
                config->bit_depth = picture->bmhd->nPlanes;
            } else {
                config->bit_depth = 8; /* Fallback */
            }
        } else {
            /* Fallback to RGB */
            config->color_type = PNG_COLOR_TYPE_RGB;
            config->bit_depth = 8;
        }
    }
    
    DEBUG_PRINTF3("DEBUG: GetOptimalPNGConfig - Final config: color_type=%ld bit_depth=%ld num_palette=%ld\n",
                  (ULONG)config->color_type, (ULONG)config->bit_depth, (ULONG)config->num_palette);
    
    return RETURN_OK;
}
