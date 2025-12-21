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
#include <proto/graphics.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <utility/tagitem.h>

/* Forward declarations for getter functions */
UWORD GetWidth(struct IFFPicture *picture);
UWORD GetHeight(struct IFFPicture *picture);

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
** opaque: If TRUE, skip transparency for index 0 (legacy behavior).
**         If FALSE, honor transparentColor including index 0 (ILBM spec).
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config, BOOL opaque)
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
            /* Use public memory (not chip RAM, we're not rendering to display) */
            config->palette = (struct PNGColor *)AllocMem(numColors * sizeof(struct PNGColor), MEMF_PUBLIC | MEMF_CLEAR);
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
        /* Only set tRNS if the transparent color index is actually used in the image */
        /* AND if the image has been decoded (so we can check usage) */
        if (picture->bmhd->masking == mskHasTransparentColor && picture->paletteIndices) {
            UBYTE transparentIndex;
            ULONG pixelCount;
            ULONG i;
            BOOL transparentColorUsed = FALSE;
            
            transparentIndex = (UBYTE)picture->bmhd->transparentColor;
            
            /* Check if transparent color index is actually used in the image */
            pixelCount = (ULONG)GetWidth(picture) * (ULONG)GetHeight(picture);
            for (i = 0; i < pixelCount; i++) {
                if (picture->paletteIndices[i] == transparentIndex) {
                    transparentColorUsed = TRUE;
                    break;
                }
            }
            
            /* Only set tRNS if the transparent color is actually used */
            /* Per ILBM specification, when transparentColor is set, that color
             * register should be ignored (treated as transparent). However, if
             * opaque flag is set, we skip transparency for index 0 to keep
             * black visible (legacy behavior). */
            if (transparentColorUsed) {
                /* Check if we should skip transparency for index 0 */
                if (opaque && transparentIndex == 0) {
                    DEBUG_PRINTF1("DEBUG: GetOptimalPNGConfig - Transparent color index = %ld (black, used in image, skipping tRNS per OPAQUE flag)\n", 
                                 (ULONG)transparentIndex);
                } else {
                    /* Set tRNS for the transparent color index */
                    config->num_trans = 1;
                    config->trans = (UBYTE *)AllocMem(sizeof(UBYTE), MEMF_PUBLIC | MEMF_CLEAR);
                    if (!config->trans) {
                        if (config->palette) {
                            FreeMem(config->palette, config->num_palette * sizeof(struct PNGColor));
                            config->palette = NULL;
                        }
                        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate PNG transparency");
                        return RETURN_FAIL;
                    }
                    config->trans[0] = transparentIndex;
                    DEBUG_PRINTF1("DEBUG: GetOptimalPNGConfig - Transparent color index = %ld (used in image, setting tRNS)\n", 
                                 (ULONG)transparentIndex);
                }
            } else {
                DEBUG_PRINTF1("DEBUG: GetOptimalPNGConfig - Transparent color index = %ld (not used in image, skipping tRNS)\n", 
                             (ULONG)transparentIndex);
            }
        } else if (picture->bmhd->masking == mskHasTransparentColor) {
            /* Image not decoded yet - don't set tRNS, we'll check after decoding */
            /* This prevents setting tRNS for colors that aren't actually used */
            DEBUG_PRINTF1("DEBUG: GetOptimalPNGConfig - Transparent color index = %ld (image not decoded yet, skipping tRNS)\n", 
                         (ULONG)picture->bmhd->transparentColor);
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

/*
** BestPictureModeID - Get best Amiga screenmode for displaying image
** Uses graphics.library BestModeIDA() to find best matching ModeID
** Returns: ModeID on success, INVALID_ID on failure
**
** This function determines the best screenmode for displaying an IFF image:
** - If CAMG chunk is present, uses those viewport mode flags (HAM, EHB, LACE, HIRES)
** - If CAMG is missing, infers requirements from image properties (width, height, depth)
** - Optionally matches to a specific monitor or ViewPort
**
** Parameters:
**   picture - IFFPicture structure (must have been parsed)
**   sourceViewPort - Optional ViewPort to match monitor type (NULL if not used)
**   sourceModeID - Optional ModeID to use as source (0 if not used, overrides ViewPort)
**   monitorID - Optional monitor ID to restrict search (0 if not used)
**
** Returns:
**   ModeID - Best matching screenmode ID, or INVALID_ID if no match found
*/
ULONG BestPictureModeID(struct IFFPicture *picture, struct ViewPort *sourceViewPort, ULONG sourceModeID, ULONG monitorID)
{
    struct TagItem tags[16];
    struct TagItem *tagPtr;
    ULONG dipfMustHave;
    ULONG dipfMustNotHave;
    ULONG viewportModes;
    UWORD width;
    UWORD height;
    UBYTE depth;
    ULONG modeID;
    struct Library *GraphicsBase;
    
    if (!picture || !picture->isLoaded || !picture->bmhd) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not loaded or BMHD missing");
        }
        return INVALID_ID;
    }
    
    /* Open graphics.library */
    GraphicsBase = OpenLibrary("graphics.library", 39);
    if (!GraphicsBase) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Cannot open graphics.library");
        }
        return INVALID_ID;
    }
    
    /* Get image dimensions and depth */
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    
    /* Validate dimensions (BestModeIDA requires non-zero) */
    if (width == 0 || height == 0) {
        CloseLibrary(GraphicsBase);
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid image dimensions");
        return INVALID_ID;
    }
    
    /* Initialize tag list pointer */
    tagPtr = tags;
    
    /* Set up DIPF flags based on CAMG viewport modes */
    viewportModes = picture->viewportmodes;
    dipfMustHave = 0;
    dipfMustNotHave = SPECIAL_FLAGS; /* Default: exclude special flags */
    
    /* Map CAMG flags to DIPF flags */
    if (viewportModes & vmHAM) {
        dipfMustHave |= DIPF_IS_HAM;
    }
    if (viewportModes & vmEXTRA_HALFBRITE) {
        dipfMustHave |= DIPF_IS_EXTRAHALFBRITE;
    }
    if (viewportModes & vmLACE) {
        dipfMustHave |= DIPF_IS_LACE;
    }
    /* Note: vmHIRES is a CAMG viewport mode flag, not a DIPF flag */
    /* HIRES modes are identified by their resolution, not a DIPF property */
    
    /* If no CAMG chunk, infer requirements from image properties */
    if (viewportModes == 0) {
        /* For HAM images detected during analysis, require HAM mode */
        if (picture->isHAM) {
            dipfMustHave |= DIPF_IS_HAM;
        }
        /* For EHB images detected during analysis, require EHB mode */
        if (picture->isEHB) {
            dipfMustHave |= DIPF_IS_EXTRAHALFBRITE;
        }
    }
    
    /* Set up tag list for BestModeIDA() */
    
    /* Nominal dimensions (aspect ratio) */
    tagPtr->ti_Tag = BIDTAG_NominalWidth;
    tagPtr->ti_Data = width;
    tagPtr++;
    tagPtr->ti_Tag = BIDTAG_NominalHeight;
    tagPtr->ti_Data = height;
    tagPtr++;
    
    /* Desired dimensions (for distinguishing between modes with same aspect) */
    tagPtr->ti_Tag = BIDTAG_DesiredWidth;
    tagPtr->ti_Data = width;
    tagPtr++;
    tagPtr->ti_Tag = BIDTAG_DesiredHeight;
    tagPtr->ti_Data = height;
    tagPtr++;
    
    /* Depth requirement */
    tagPtr->ti_Tag = BIDTAG_Depth;
    tagPtr->ti_Data = depth;
    tagPtr++;
    
    /* DIPF flags */
    if (dipfMustHave != 0) {
        tagPtr->ti_Tag = BIDTAG_DIPFMustHave;
        tagPtr->ti_Data = dipfMustHave;
        tagPtr++;
    }
    if (dipfMustNotHave != 0) {
        tagPtr->ti_Tag = BIDTAG_DIPFMustNotHave;
        tagPtr->ti_Data = dipfMustNotHave;
        tagPtr++;
    }
    
    /* Optional: Source ViewPort or ModeID */
    if (sourceModeID != 0) {
        tagPtr->ti_Tag = BIDTAG_SourceID;
        tagPtr->ti_Data = sourceModeID;
        tagPtr++;
    } else if (sourceViewPort != NULL) {
        tagPtr->ti_Tag = BIDTAG_ViewPort;
        tagPtr->ti_Data = (ULONG)sourceViewPort;
        tagPtr++;
    }
    
    /* Optional: Monitor ID */
    if (monitorID != 0) {
        tagPtr->ti_Tag = BIDTAG_MonitorID;
        tagPtr->ti_Data = monitorID;
        tagPtr++;
    }
    
    /* Terminate tag list */
    tagPtr->ti_Tag = TAG_END;
    
    /* Call BestModeIDA() */
    modeID = BestModeIDA(tags);
    
    /* Check if the mode is actually available before returning it */
    if (modeID != INVALID_ID) {
        ULONG notAvailable;
        notAvailable = ModeNotAvailable(modeID);
        if (notAvailable != 0) {
            /* Mode is not available - return INVALID_ID */
            /* ModeNotAvailable returns error code if unavailable, 0 if available */
            modeID = INVALID_ID;
            if (notAvailable & DI_AVAIL_NOCHIPS) {
                SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Recommended screenmode requires chips not available");
            } else if (notAvailable & DI_AVAIL_NOMONITOR) {
                SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Recommended screenmode requires monitor not available");
            } else if (notAvailable & DI_AVAIL_NOTWITHGENLOCK) {
                SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Recommended screenmode not available with genlock");
            } else {
                SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Recommended screenmode is not available");
            }
        }
    }
    
    /* Close graphics.library */
    CloseLibrary(GraphicsBase);
    
    if (modeID == INVALID_ID) {
        if (picture->lastError == IFFPICTURE_OK) {
            SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "No matching screenmode found");
        }
    }
    
    DEBUG_PRINTF5("DEBUG: BestPictureModeID - width=%ld height=%ld depth=%ld viewportModes=0x%08lx modeID=0x%08lx\n",
                  (ULONG)width, (ULONG)height, (ULONG)depth, viewportModes, modeID);
    
    return modeID;
}
