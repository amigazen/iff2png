/*
** iffpicture.c - IFFPicture Library Implementation
**
** Amiga-style function library for loading and decoding IFF bitmap images
*/

#include "iffpicture_private.h"
#include "/debug.h"
#include <stdio.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <proto/utility.h>

/* Library base is defined in main.c */
extern struct Library *IFFParseBase;

/* Forward declarations for internal functions */
LONG ReadBMHD(struct IFFPicture *picture);
LONG ReadCMAP(struct IFFPicture *picture);
LONG ReadCAMG(struct IFFPicture *picture);
LONG ReadBODY(struct IFFPicture *picture);
LONG ReadABIT(struct IFFPicture *picture);
LONG ReadFXHD(struct IFFPicture *picture);
LONG ReadPAGE(struct IFFPicture *picture);

/*
** AllocIFFPicture - Allocate a new IFFPicture object
** Returns: Pointer to new object or NULL on failure
** Follows iffparse.library pattern: AllocIFF
*/
struct IFFPicture *AllocIFFPicture(VOID)
{
    struct IFFPicture *picture;
    
    /* Allocate memory for picture structure - use public memory (not chip RAM) */
    picture = (struct IFFPicture *)AllocMem(sizeof(struct IFFPicture), MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture) {
        return NULL;
    }
    
    /* Initialize structure */
    picture->bmhd = NULL;
    picture->cmap = NULL;
    picture->viewportmodes = 0;
    picture->formtype = 0;
    picture->pixelData = NULL;
    picture->pixelDataSize = 0;
    picture->hasAlpha = FALSE;
    picture->isHAM = FALSE;
    picture->isEHB = FALSE;
    picture->isCompressed = FALSE;
    picture->isIndexed = FALSE;
    picture->isGrayscale = FALSE;
    picture->iff = NULL;
    picture->lastError = IFFPICTURE_OK;
    picture->errorString[0] = '\0';
    picture->isLoaded = FALSE;
    picture->isDecoded = FALSE;
    picture->bodyChunkSize = 0;
    picture->bodyChunkPosition = 0;
    picture->faxxCompression = 0;
    
    return picture;
}

/*
** FreeIFFPicture - Free an IFFPicture object
** Frees all allocated memory and closes any open files
** Follows iffparse.library pattern: FreeIFF
*/
VOID FreeIFFPicture(struct IFFPicture *picture)
{
    if (!picture) {
        return;
    }
    
    /* Close IFF handle if open */
    if (picture->iff) {
        CloseIFFPicture(picture);
    }
    
    /* Note: File handle management is the caller's responsibility, following
     * iffparse.library pattern. The caller must close the file handle with Close()
     * after calling CloseIFFPicture(). */
    
    /* Free bitmap header */
    if (picture->bmhd) {
        FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
        picture->bmhd = NULL;
    }
    
    /* Free color map */
    if (picture->cmap) {
        if (picture->cmap->data) {
            FreeMem(picture->cmap->data, picture->cmap->numcolors * 3);
        }
        FreeMem(picture->cmap, sizeof(struct IFFColorMap));
        picture->cmap = NULL;
    }
    
    /* Free pixel data */
    if (picture->pixelData) {
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelData = NULL;
        picture->pixelDataSize = 0;
    }
    
    /* Free palette indices */
    if (picture->paletteIndices) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
    }
    
    /* Free metadata - library owns all memory */
    if (picture->grab) {
        FreeMem(picture->grab, sizeof(struct Point2D));
    }
    if (picture->dest) {
        FreeMem(picture->dest, sizeof(struct DestMerge));
    }
    if (picture->sprt) {
        FreeMem(picture->sprt, sizeof(UWORD));
    }
    if (picture->crngArray) {
        FreeMem(picture->crngArray, picture->crngCount * sizeof(struct CRange));
    }
    if (picture->copyright) {
        FreeMem(picture->copyright, picture->copyrightSize);
    }
    if (picture->author) {
        FreeMem(picture->author, picture->authorSize);
    }
    if (picture->annotationArray) {
        ULONG i;
        for (i = 0; i < picture->annotationCount; i++) {
            if (picture->annotationArray[i] && picture->annotationSizes) {
                FreeMem(picture->annotationArray[i], picture->annotationSizes[i]);
            }
        }
        FreeMem(picture->annotationArray, picture->annotationCount * sizeof(STRPTR));
    }
    if (picture->annotationSizes) {
        FreeMem(picture->annotationSizes, picture->annotationCount * sizeof(ULONG));
    }
    if (picture->textArray) {
        ULONG i;
        for (i = 0; i < picture->textCount; i++) {
            if (picture->textArray[i] && picture->textSizes) {
                FreeMem(picture->textArray[i], picture->textSizes[i]);
            }
        }
        FreeMem(picture->textArray, picture->textCount * sizeof(STRPTR));
    }
    if (picture->textSizes) {
        FreeMem(picture->textSizes, picture->textCount * sizeof(ULONG));
    }
    
    /* Free picture structure */
    FreeMem(picture, sizeof(struct IFFPicture));
}

/*
** SetIFFPictureError - Set error code and message (internal)
*/
VOID SetIFFPictureError(struct IFFPicture *picture, LONG error, const char *message)
{
    if (!picture) {
        return;
    }
    
    picture->lastError = error;
    if (message) {
        Strncpy(picture->errorString, message, sizeof(picture->errorString) - 1);
        picture->errorString[sizeof(picture->errorString) - 1] = '\0';
    } else {
        picture->errorString[0] = '\0';
    }
}

/*
** InitIFFPictureasDOS - Initialize IFFPicture as DOS stream
** Follows iffparse.library pattern: InitIFFasDOS
** 
** Initializes the IFFPicture to operate on DOS streams.
** The iff_Stream field must be set by the caller after calling Open()
** to get a BPTR file handle.
** 
** Example usage:
**   picture = AllocIFFPicture();
**   filehandle = Open("file.iff", MODE_OLDFILE);
**   InitIFFPictureasDOS(picture);
**   picture->iff->iff_Stream = (ULONG)filehandle;
**   OpenIFFPicture(picture, IFFF_READ);
*/
VOID InitIFFPictureasDOS(struct IFFPicture *picture)
{
    struct IFFHandle *iff;
    
    if (!picture) {
        return;
    }
    
    /* Allocate IFF handle */
    iff = AllocIFF();
    if (!iff) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Cannot allocate IFF handle");
        return;
    }
    
    picture->iff = iff;
    
    /* Initialize IFF as DOS stream */
    /* Note: iff_Stream must be set by caller after calling Open() */
    InitIFFasDOS(iff);
}

/*
** OpenIFFPicture - Prepare IFFPicture to read or write a new IFF stream
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: OpenIFF
** 
** The IFFPicture must have been initialized with InitIFFPictureasDOS()
** and iff_Stream must be set to a valid BPTR file handle.
** 
** rwMode: IFFF_READ or IFFF_WRITE
*/
LONG OpenIFFPicture(struct IFFPicture *picture, LONG rwMode)
{
    LONG error;
    
    if (!picture) {
        return RETURN_FAIL;
    }
    
    /* Ensure IFF handle is allocated and initialized */
    if (!picture->iff) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "IFFPicture not initialized - call InitIFFPictureasDOS() first");
        return RETURN_FAIL;
    }
    
    /* Ensure stream is set */
    if (!picture->iff->iff_Stream) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "IFF stream not set - set iff_Stream to file handle after Open()");
        return RETURN_FAIL;
    }
    
    /* Open IFF for reading or writing */
    error = OpenIFF(picture->iff, rwMode);
    if (error) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Cannot open IFF stream");
        return RETURN_FAIL;
    }
    
    picture->isLoaded = TRUE;
    return RETURN_OK;
}

/*
** CloseIFFPicture - Close IFFPicture and free IFF handle
** Follows iffparse.library pattern: CloseIFF
*/
VOID CloseIFFPicture(struct IFFPicture *picture)
{
    if (!picture) {
        return;
    }
    
    /* Close IFF handle if open */
    if (picture->iff) {
        CloseIFF(picture->iff);
        FreeIFF(picture->iff);
        picture->iff = NULL;
    }
}

/*
** ParseIFFPicture - Parse IFF structure and read chunks
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: ParseIFF
*/
LONG ParseIFFPicture(struct IFFPicture *picture)
{
    LONG error;
    struct ContextNode *cn;
    ULONG formType;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not opened");
        }
        return RETURN_FAIL;
    }
    
    /* First, parse one step to get FORM type */
    error = ParseIFF(picture->iff, IFFPARSE_STEP);
    if (error != 0) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to parse FORM chunk");
        return RETURN_FAIL;
    }
    
    cn = CurrentChunk(picture->iff);
    if (!cn || cn->cn_ID != ID_FORM) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Not a valid IFF FORM file");
        return RETURN_FAIL;
    }
    
    formType = cn->cn_Type;
    picture->formtype = formType;
    
    DEBUG_PRINTF1("DEBUG: ParseIFFPicture - FORM type = 0x%08lx\n", formType);
    
    /* Set up property chunks based on form type */
    if (formType == ID_FAXX) {
        /* FAXX uses FXHD and PAGE chunks */
        if ((error = PropChunk(picture->iff, formType, ID_FXHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for FXHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_FLOG); /* Optional */
        if ((error = StopChunk(picture->iff, formType, ID_PAGE)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for PAGE");
            return RETURN_FAIL;
        }
    } else if (formType == ID_ILBM || formType == ID_PBM) {
        /* Common chunks for ILBM, PBM */
        if ((error = PropChunk(picture->iff, formType, ID_BMHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for BMHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_CMAP);
        PropChunk(picture->iff, formType, ID_CAMG);
        /* Metadata chunks (optional) - single instance */
        PropChunk(picture->iff, formType, ID_GRAB);
        PropChunk(picture->iff, formType, ID_DEST);
        PropChunk(picture->iff, formType, ID_SPRT);
        PropChunk(picture->iff, formType, ID_COPYRIGHT);
        PropChunk(picture->iff, formType, ID_AUTH);
        /* Metadata chunks that can appear multiple times - use CollectionChunk */
        CollectionChunk(picture->iff, formType, ID_CRNG);
        CollectionChunk(picture->iff, formType, ID_ANNO);
        CollectionChunk(picture->iff, formType, ID_TEXT);
        if ((error = StopChunk(picture->iff, formType, ID_BODY)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for BODY");
            return RETURN_FAIL;
        }
    } else if (formType == ID_ACBM) {
        /* ACBM uses ABIT chunk instead of BODY */
        if ((error = PropChunk(picture->iff, formType, ID_BMHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for BMHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_CMAP);
        PropChunk(picture->iff, formType, ID_CAMG);
        if ((error = StopChunk(picture->iff, formType, ID_ABIT)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for ABIT");
            return RETURN_FAIL;
        }
    } else if (formType == ID_RGBN || formType == ID_RGB8 || formType == ID_DEEP) {
        /* RGBN, RGB8, DEEP have BMHD and BODY */
        if ((error = PropChunk(picture->iff, formType, ID_BMHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for BMHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_CMAP); /* Optional for DEEP */
        if ((error = StopChunk(picture->iff, formType, ID_BODY)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for BODY");
            return RETURN_FAIL;
        }
    } else {
        SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Unsupported IFF FORM type");
        return RETURN_FAIL;
    }
    
    /* Parse the file until we hit the data chunk */
    error = ParseIFF(picture->iff, IFFPARSE_SCAN);
    if (error != 0 && error != IFFERR_EOC) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to parse IFF file");
        return RETURN_FAIL;
    }
    
    /* Extract stored property chunks based on format */
    if (formType == ID_FAXX) {
        /* FAXX uses FXHD and PAGE */
        if (ReadFXHD(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        /* Create default black/white CMAP for FAXX if not already present */
        if (!picture->cmap) {
            struct IFFColorMap *cmap;
            UBYTE *data;
            
            /* Allocate ColorMap structure - use public memory (not chip RAM) */
            cmap = (struct IFFColorMap *)AllocMem(sizeof(struct IFFColorMap), MEMF_PUBLIC | MEMF_CLEAR);
            if (!cmap) {
                /* Clean up BMHD allocated by ReadFXHD */
                if (picture->bmhd) {
                    FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
                    picture->bmhd = NULL;
                }
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate default ColorMap for FAXX");
                return RETURN_FAIL;
            }
            
            /* Allocate 2-color palette (black and white) - 6 bytes */
            data = (UBYTE *)AllocMem(6, MEMF_PUBLIC | MEMF_CLEAR);
            if (!data) {
                FreeMem(cmap, sizeof(struct IFFColorMap));
                /* Clean up BMHD allocated by ReadFXHD */
                if (picture->bmhd) {
                    FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
                    picture->bmhd = NULL;
                }
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate default ColorMap data for FAXX");
                return RETURN_FAIL;
            }
            
            /* Create black (index 0) and white (index 1) palette */
            data[0] = 0;   /* Black R */
            data[1] = 0;   /* Black G */
            data[2] = 0;   /* Black B */
            data[3] = 255; /* White R */
            data[4] = 255; /* White G */
            data[5] = 255; /* White B */
            
            cmap->data = data;
            cmap->numcolors = 2;
            cmap->is4Bit = FALSE;
            
            picture->cmap = cmap;
            picture->isIndexed = TRUE;
            
            DEBUG_PUTSTR("DEBUG: ReadCMAP - Created default black/white CMAP for FAXX\n");
        }
        if (ReadPAGE(picture) != RETURN_OK) {
            /* Clean up on error */
            if (picture->cmap) {
                if (picture->cmap->data) {
                    FreeMem(picture->cmap->data, picture->cmap->numcolors * 3);
                }
                FreeMem(picture->cmap, sizeof(struct IFFColorMap));
                picture->cmap = NULL;
            }
            if (picture->bmhd) {
                FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
                picture->bmhd = NULL;
            }
            return RETURN_FAIL; /* Error already set */
        }
    } else {
        /* ILBM, PBM, RGBN, RGB8, DEEP, ACBM use BMHD */
        if (ReadBMHD(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        ReadCMAP(picture); /* CMAP is optional, don't fail if missing */
        ReadCAMG(picture); /* CAMG is optional, don't fail if missing */
        
        /* Read and store metadata chunks */
        ReadMetadata(picture);
        
        /* Read BODY or ABIT chunk depending on format */
        if (formType == ID_ACBM) {
            if (ReadABIT(picture) != RETURN_OK) {
                return RETURN_FAIL; /* Error already set */
            }
        } else {
            if (ReadBODY(picture) != RETURN_OK) {
                return RETURN_FAIL; /* Error already set */
            }
        }
    }
    
    picture->isLoaded = TRUE;
    return RETURN_OK;
}

/*
** Getter functions - return values from picture structure
** Following iffparse.library pattern: Get* functions
*/
/*
** GetIFFHandle - Get the IFFHandle pointer for direct access
** Returns: Pointer to IFFHandle or NULL
** Follows iffparse.library pattern - allows user to set iff_Stream
*/
struct IFFHandle *GetIFFHandle(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->iff;
}

UWORD GetWidth(struct IFFPicture *picture)
{
    if (!picture || !picture->bmhd) {
        return 0;
    }
    return picture->bmhd->w;
}

UWORD GetHeight(struct IFFPicture *picture)
{
    if (!picture || !picture->bmhd) {
        return 0;
    }
    return picture->bmhd->h;
}

UWORD GetDepth(struct IFFPicture *picture)
{
    if (!picture || !picture->bmhd) {
        return 0;
    }
    return picture->bmhd->nPlanes;
}

ULONG GetFormType(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    return picture->formtype;
}

ULONG GetVPModes(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    return picture->viewportmodes;
}

/*
** GetFAXXCompression - Get FAXX compression type
** Returns: Compression type (0=None, 1=MH, 2=MR, 4=MMR) or 0 if not FAXX
*/
UBYTE GetFAXXCompression(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    return picture->faxxCompression;
}

struct BitMapHeader *GetBMHD(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->bmhd;
}

struct IFFColorMap *GetIFFColorMap(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->cmap;
}

UBYTE *GetPixelData(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->pixelData;
}

ULONG GetPixelDataSize(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    return picture->pixelDataSize;
}

BOOL HasAlpha(struct IFFPicture *picture)
{
    if (!picture) {
        return FALSE;
    }
    return picture->hasAlpha;
}

BOOL IsHAM(struct IFFPicture *picture)
{
    if (!picture) {
        return FALSE;
    }
    return picture->isHAM;
}

BOOL IsEHB(struct IFFPicture *picture)
{
    if (!picture) {
        return FALSE;
    }
    return picture->isEHB;
}

BOOL IsCompressed(struct IFFPicture *picture)
{
    if (!picture) {
        return FALSE;
    }
    return picture->isCompressed;
}

/*
** ReadBMHD - Read BMHD chunk
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadBMHD(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct BitMapHeader *bmhd;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored BMHD property */
    sp = FindProp(picture->iff, picture->formtype, ID_BMHD);
    if (!sp) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "BMHD chunk not found");
        return RETURN_FAIL;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadBMHD - Found BMHD property, size=%ld\n", sp->sp_Size);
    
    /* Check size - BMHD should be 20 bytes */
    if (sp->sp_Size < 20) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "BMHD chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate BMHD structure - use public memory (not chip RAM) */
    bmhd = (struct BitMapHeader *)AllocMem(sizeof(struct BitMapHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BitMapHeader");
        return RETURN_FAIL;
    }
    
    /* Read fields individually from byte array to avoid structure alignment issues */
    /* IFF data is big-endian, Amiga is big-endian, so we can read directly */
    {
        UBYTE *src = (UBYTE *)sp->sp_Data;
        
        DEBUG_BYTE_ARRAY("BMHD raw data", src, 20);
        
        /* Read UWORD fields (big-endian, 2 bytes each) */
        bmhd->w = (UWORD)((src[0] << 8) | src[1]);
        bmhd->h = (UWORD)((src[2] << 8) | src[3]);
        
        /* Read WORD fields (big-endian, 2 bytes each) */
        bmhd->x = (WORD)((src[4] << 8) | src[5]);
        bmhd->y = (WORD)((src[6] << 8) | src[7]);
        
        /* Read UBYTE fields (1 byte each) */
        bmhd->nPlanes = src[8];
        bmhd->masking = src[9];
        bmhd->compression = src[10];
        bmhd->pad1 = src[11];
        
        /* Read UWORD transparentColor (big-endian, 2 bytes) */
        bmhd->transparentColor = (UWORD)((src[12] << 8) | src[13]);
        
        /* Read UBYTE fields (1 byte each) */
        bmhd->xAspect = src[14];
        bmhd->yAspect = src[15];
        
        /* Read WORD fields (big-endian, 2 bytes each) */
        bmhd->pageWidth = (WORD)((src[16] << 8) | src[17]);
        bmhd->pageHeight = (WORD)((src[18] << 8) | src[19]);
        
        DEBUG_PRINTF5("DEBUG: BMHD parsed - w=%ld h=%ld nPlanes=%ld masking=%ld compression=%ld\n",
                      bmhd->w, bmhd->h, bmhd->nPlanes, bmhd->masking, bmhd->compression);
    }
    
    /* Set flags based on BMHD */
    picture->bmhd = bmhd;
    picture->isCompressed = (bmhd->compression != cmpNone);
    picture->hasAlpha = (bmhd->masking == mskHasMask);
    
    return RETURN_OK;
}

/*
** ReadCMAP - Read CMAP chunk
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadCMAP(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct IFFColorMap *cmap;
    ULONG numcolors;
    ULONG i;
    UBYTE *data;
    BOOL allShifted;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored CMAP property */
    sp = FindProp(picture->iff, picture->formtype, ID_CMAP);
    if (!sp) {
        /* CMAP is optional for some formats (e.g., DEEP, RGBN, RGB8) */
        DEBUG_PUTSTR("DEBUG: ReadCMAP - No CMAP chunk found (optional)\n");
        return RETURN_OK;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadCMAP - Found CMAP property, size=%ld\n", sp->sp_Size);
    
    /* CMAP size must be multiple of 3 (RGB triplets) */
    if (sp->sp_Size % 3 != 0) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "CMAP chunk size not multiple of 3");
        return RETURN_FAIL;
    }
    
    numcolors = sp->sp_Size / 3;
    if (numcolors == 0) {
        /* Empty CMAP, skip it */
        return RETURN_OK;
    }
    
    /* Allocate IFFColorMap structure - use public memory (not chip RAM) */
    cmap = (struct IFFColorMap *)AllocMem(sizeof(struct IFFColorMap), MEMF_PUBLIC | MEMF_CLEAR);
    if (!cmap) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate ColorMap");
        return RETURN_FAIL;
    }
    
    /* Allocate color data - use public memory (not chip RAM) */
    data = (UBYTE *)AllocMem(sp->sp_Size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!data) {
        FreeMem(cmap, sizeof(struct IFFColorMap));
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate ColorMap data");
        return RETURN_FAIL;
    }
    
    /* Copy color data */
    CopyMem(sp->sp_Data, data, sp->sp_Size);
    
    /* Check if it's a 4-bit palette (common for older ILBMs) */
    /* 4-bit palettes have values shifted left by 4 bits */
    allShifted = TRUE;
    for (i = 0; i < sp->sp_Size; ++i) {
        if (data[i] & 0x0F) {
            allShifted = FALSE;
            break;
        }
    }
    
    cmap->data = data;
    cmap->numcolors = numcolors;
    cmap->is4Bit = allShifted;
    
    picture->cmap = cmap;
    picture->isIndexed = TRUE;
    
    DEBUG_PRINTF2("DEBUG: ReadCMAP - Loaded %ld colors, is4Bit=%ld\n",
                 numcolors, (ULONG)(allShifted ? 1 : 0));
    
    return RETURN_OK;
}

/*
** ReadCAMG - Read CAMG chunk
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadCAMG(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    ULONG *mode;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored CAMG property */
    sp = FindProp(picture->iff, picture->formtype, ID_CAMG);
    if (!sp) {
        /* CAMG is optional */
        DEBUG_PUTSTR("DEBUG: ReadCAMG - No CAMG chunk found (optional)\n");
        picture->viewportmodes = 0;
        return RETURN_OK;
    }
    
    /* CAMG should be 4 bytes (ULONG) */
    if (sp->sp_Size < sizeof(ULONG)) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "CAMG chunk too small");
        picture->viewportmodes = 0;
        return RETURN_FAIL;
    }
    
    /* Extract viewport modes */
    mode = (ULONG *)sp->sp_Data;
    picture->viewportmodes = *mode;
    
    DEBUG_PRINTF1("DEBUG: ReadCAMG - Viewport modes = 0x%08lx\n", picture->viewportmodes);
    
    /* Detect HAM and EHB modes */
    if (picture->viewportmodes & vmHAM) {
        picture->isHAM = TRUE;
    }
    if (picture->viewportmodes & vmEXTRA_HALFBRITE) {
        picture->isEHB = TRUE;
    }
    
    return RETURN_OK;
}

/*
** ReadBODY - Read BODY chunk position and size
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: CurrentChunk
*/
LONG ReadBODY(struct IFFPicture *picture)
{
    struct ContextNode *cn;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Get current chunk - should be BODY after ParseIFF stops */
    cn = CurrentChunk(picture->iff);
    if (!cn) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "No current chunk (BODY not found)");
        return RETURN_FAIL;
    }
    
    /* Verify it's the BODY chunk */
    if (cn->cn_ID != ID_BODY || cn->cn_Type != picture->formtype) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Current chunk is not BODY");
        return RETURN_FAIL;
    }
    
    /* Store BODY chunk information for later reading */
    picture->bodyChunkSize = cn->cn_Size;
    picture->bodyChunkPosition = 0; /* We're positioned at start of BODY chunk */
    
    return RETURN_OK;
}

/*
** ReadABIT - Read ABIT chunk position and size (for ACBM)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: CurrentChunk
*/
LONG ReadABIT(struct IFFPicture *picture)
{
    struct ContextNode *cn;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Get current chunk - should be ABIT after ParseIFF stops */
    cn = CurrentChunk(picture->iff);
    if (!cn) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "No current chunk (ABIT not found)");
        return RETURN_FAIL;
    }
    
    /* Verify it's the ABIT chunk */
    if (cn->cn_ID != ID_ABIT || cn->cn_Type != picture->formtype) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Current chunk is not ABIT");
        return RETURN_FAIL;
    }
    
    /* Store ABIT chunk information for later reading */
    picture->bodyChunkSize = cn->cn_Size;
    picture->bodyChunkPosition = 0; /* We're positioned at start of ABIT chunk */
    
    return RETURN_OK;
}

/*
** ReadFXHD - Read FXHD chunk and convert to BMHD structure (for FAXX)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
**
** FAXX uses FXHD (FaxHeader) instead of BMHD (BitmapHeader)
** We convert FXHD to BMHD structure for compatibility with rest of code
*/
LONG ReadFXHD(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct BitMapHeader *bmhd;
    UBYTE *src;
    UWORD width, height;
    UBYTE compression;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored FXHD property */
    sp = FindProp(picture->iff, picture->formtype, ID_FXHD);
    if (!sp) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FAXX file missing required FXHD chunk");
        return RETURN_FAIL;
    }
    
    /* FXHD should be at least 8 bytes (Width, Height, LineLength, VRes, Compression) */
    if (sp->sp_Size < 8) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FXHD chunk too small");
        return RETURN_FAIL;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadFXHD - Found FXHD property, size=%ld\n", sp->sp_Size);
    
    /* Free existing BMHD if present (shouldn't happen, but be safe) */
    if (picture->bmhd) {
        FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
        picture->bmhd = NULL;
    }
    
    /* Allocate BMHD structure - use public memory (not chip RAM) */
    bmhd = (struct BitMapHeader *)AllocMem(sizeof(struct BitMapHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BitMapHeader");
        return RETURN_FAIL;
    }
    
    /* Read FXHD data byte-by-byte (big-endian) */
    src = (UBYTE *)sp->sp_Data;
    
    /* Read Width and Height (UWORD, big-endian, 2 bytes each) */
    width = (UWORD)((src[0] << 8) | src[1]);
    height = (UWORD)((src[2] << 8) | src[3]);
    
    /* Read Compression (UBYTE at offset 7) */
    compression = src[7];
    
    /* Convert FXHD to BMHD structure */
    bmhd->w = width;
    bmhd->h = height;
    bmhd->x = 0;
    bmhd->y = 0;
    bmhd->nPlanes = 1; /* FAXX is always 1-bit (black and white) */
    bmhd->masking = 0; /* No masking for FAXX */
    
    /* Map FAXX compression to BMHD compression */
    /* FAXX: FXCMPNONE=0, FXCMPMH=1, FXCMPMR=2, FXCMPMMR=4 */
    /* BMHD: cmpNone=0, cmpByteRun1=1 */
    /* For now, treat all FAXX compression as special (we'll handle in decoder) */
    bmhd->compression = (compression == 0) ? cmpNone : cmpByteRun1;
    
    bmhd->pad1 = 0;
    bmhd->transparentColor = 0;
    bmhd->xAspect = 1;
    bmhd->yAspect = 1;
    bmhd->pageWidth = width;
    bmhd->pageHeight = height;
    
    picture->bmhd = bmhd;
    
    /* Store FAXX compression type for decoder */
    picture->isCompressed = (compression != 0);
    picture->faxxCompression = compression;
    
    DEBUG_PRINTF3("DEBUG: ReadFXHD - Width=%ld Height=%ld Compression=%ld\n",
                 (ULONG)width, (ULONG)height, (ULONG)compression);
    
    return RETURN_OK;
}

/*
** ReadPAGE - Read PAGE chunk position and size (for FAXX)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: CurrentChunk
*/
LONG ReadPAGE(struct IFFPicture *picture)
{
    struct ContextNode *cn;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Get current chunk - should be PAGE after ParseIFF stops */
    cn = CurrentChunk(picture->iff);
    if (!cn) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "No current chunk (PAGE not found)");
        return RETURN_FAIL;
    }
    
    /* Verify it's the PAGE chunk */
    if (cn->cn_ID != ID_PAGE || cn->cn_Type != picture->formtype) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Current chunk is not PAGE");
        return RETURN_FAIL;
    }
    
    /* Store PAGE chunk information for later reading */
    picture->bodyChunkSize = cn->cn_Size;
    picture->bodyChunkPosition = 0; /* We're positioned at start of PAGE chunk */
    
    return RETURN_OK;
}

/*
** ReadMetadata - Read and store all metadata chunks in IFFPicture structure
** This function is called after ParseIFFPicture() to extract metadata
** All memory is owned by IFFPicture and freed by FreeIFFPicture()
*/
VOID ReadMetadata(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct CollectionItem *ci;
    ULONG i;
    ULONG count;
    UBYTE *src;
    
    if (!picture || !picture->iff) {
        return;
    }
    
    /* Initialize all metadata pointers to NULL */
    picture->grab = NULL;
    picture->dest = NULL;
    picture->sprt = NULL;
    picture->crng = NULL;
    picture->crngCount = 0;
    picture->crngArray = NULL;
    picture->copyright = NULL;
    picture->copyrightSize = 0;
    picture->author = NULL;
    picture->authorSize = 0;
    picture->annotation = NULL;
    picture->annotationCount = 0;
    picture->annotationArray = NULL;
    picture->annotationSizes = NULL;
    picture->text = NULL;
    picture->textCount = 0;
    picture->textArray = NULL;
    picture->textSizes = NULL;
    
    /* Read GRAB chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_GRAB);
    if (sp && sp->sp_Size >= 4) {
        picture->grab = (struct Point2D *)AllocMem(sizeof(struct Point2D), MEMF_PUBLIC | MEMF_CLEAR);
        if (picture->grab) {
            src = (UBYTE *)sp->sp_Data;
            picture->grab->x = (WORD)((src[0] << 8) | src[1]);
            picture->grab->y = (WORD)((src[2] << 8) | src[3]);
        }
    }
    
    /* Read DEST chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_DEST);
    if (sp && sp->sp_Size >= 8) {
        picture->dest = (struct DestMerge *)AllocMem(sizeof(struct DestMerge), MEMF_PUBLIC | MEMF_CLEAR);
        if (picture->dest) {
            src = (UBYTE *)sp->sp_Data;
            picture->dest->depth = src[0];
            picture->dest->pad1 = src[1];
            picture->dest->planePick = (UWORD)((src[2] << 8) | src[3]);
            picture->dest->planeOnOff = (UWORD)((src[4] << 8) | src[5]);
            picture->dest->planeMask = (UWORD)((src[6] << 8) | src[7]);
        }
    }
    
    /* Read SPRT chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_SPRT);
    if (sp && sp->sp_Size >= 2) {
        picture->sprt = (UWORD *)AllocMem(sizeof(UWORD), MEMF_PUBLIC | MEMF_CLEAR);
        if (picture->sprt) {
            src = (UBYTE *)sp->sp_Data;
            *picture->sprt = (UWORD)((src[0] << 8) | src[1]);
        }
    }
    
    /* Read CRNG chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_CRNG);
    if (ci) {
        /* Count items in collection */
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            picture->crngCount = count;
            picture->crngArray = (struct CRange *)AllocMem(count * sizeof(struct CRange), MEMF_PUBLIC | MEMF_CLEAR);
            if (picture->crngArray) {
                ci = FindCollection(picture->iff, picture->formtype, ID_CRNG);
                for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                    if (ci->ci_Size >= 8) {
                        src = (UBYTE *)ci->ci_Data;
                        picture->crngArray[i].pad1 = (WORD)((src[0] << 8) | src[1]);
                        picture->crngArray[i].rate = (WORD)((src[2] << 8) | src[3]);
                        picture->crngArray[i].flags = (WORD)((src[4] << 8) | src[5]);
                        picture->crngArray[i].low = src[6];
                        picture->crngArray[i].high = src[7];
                    }
                }
                /* Store first instance pointer for convenience */
                picture->crng = picture->crngArray;
            }
        }
    }
    
    /* Read Copyright chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_COPYRIGHT);
    if (sp && sp->sp_Size > 0) {
        picture->copyrightSize = sp->sp_Size + 1;
        picture->copyright = (STRPTR)AllocMem(picture->copyrightSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (picture->copyright) {
            CopyMem(sp->sp_Data, picture->copyright, sp->sp_Size);
            picture->copyright[sp->sp_Size] = '\0';
        }
    }
    
    /* Read Author chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_AUTH);
    if (sp && sp->sp_Size > 0) {
        picture->authorSize = sp->sp_Size + 1;
        picture->author = (STRPTR)AllocMem(picture->authorSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (picture->author) {
            CopyMem(sp->sp_Data, picture->author, sp->sp_Size);
            picture->author[sp->sp_Size] = '\0';
        }
    }
    
    /* Read ANNO chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_ANNO);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            picture->annotationCount = count;
            picture->annotationArray = (STRPTR *)AllocMem(count * sizeof(STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
            picture->annotationSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
            if (picture->annotationArray && picture->annotationSizes) {
                ci = FindCollection(picture->iff, picture->formtype, ID_ANNO);
                for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                    if (ci->ci_Size > 0) {
                        picture->annotationSizes[i] = ci->ci_Size + 1;
                        picture->annotationArray[i] = (STRPTR)AllocMem(picture->annotationSizes[i], MEMF_PUBLIC | MEMF_CLEAR);
                        if (picture->annotationArray[i]) {
                            CopyMem(ci->ci_Data, picture->annotationArray[i], ci->ci_Size);
                            picture->annotationArray[i][ci->ci_Size] = '\0';
                        }
                    } else {
                        picture->annotationSizes[i] = 0;
                    }
                }
                /* Store first instance pointer for convenience */
                picture->annotation = picture->annotationArray[0];
            }
        }
    }
    
    /* Read TEXT chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_TEXT);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            picture->textCount = count;
            picture->textArray = (STRPTR *)AllocMem(count * sizeof(STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
            picture->textSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
            if (picture->textArray && picture->textSizes) {
                ci = FindCollection(picture->iff, picture->formtype, ID_TEXT);
                for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                    if (ci->ci_Size > 0) {
                        picture->textSizes[i] = ci->ci_Size + 1;
                        picture->textArray[i] = (STRPTR)AllocMem(picture->textSizes[i], MEMF_PUBLIC | MEMF_CLEAR);
                        if (picture->textArray[i]) {
                            CopyMem(ci->ci_Data, picture->textArray[i], ci->ci_Size);
                            picture->textArray[i][ci->ci_Size] = '\0';
                        }
                    } else {
                        picture->textSizes[i] = 0;
                    }
                }
                /* Store first instance pointer for convenience */
                picture->text = picture->textArray[0];
            }
        }
    }
}

/*
** Decode - Decode image data to RGB
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
LONG Decode(struct IFFPicture *picture)
{
    LONG result;
    
    if (!picture || !picture->isLoaded || !picture->bmhd) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not loaded or BMHD missing");
        }
        return RETURN_FAIL;
    }
    
    /* Allocate RGB pixel buffer - use public memory (not chip RAM, we're not rendering to display) */
    picture->pixelDataSize = (ULONG)picture->bmhd->w * picture->bmhd->h * 3;
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->pixelData) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel data buffer");
        return RETURN_FAIL;
    }
    
    /* Dispatch to format-specific decoder */
    switch (picture->formtype) {
        case ID_ILBM:
            if (picture->isHAM) {
                result = DecodeHAM(picture);
            } else if (picture->isEHB) {
                result = DecodeEHB(picture);
            } else {
                result = DecodeILBM(picture);
            }
            break;
        case ID_PBM:
            result = DecodePBM(picture);
            break;
        case ID_FAXX:
            result = DecodeFAXX(picture);
            break;
        case ID_RGBN:
            result = DecodeRGBN(picture);
            break;
        case ID_RGB8:
            result = DecodeRGB8(picture);
            break;
        case ID_DEEP:
            result = DecodeDEEP(picture);
            break;
        case ID_ACBM:
            result = DecodeACBM(picture);
            break;
        default:
            SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Unsupported format for decoding");
            result = RETURN_FAIL;
            break;
    }
    
    if (result == RETURN_OK) {
        picture->isDecoded = TRUE;
    } else {
        /* Clean up on error */
        if (picture->pixelData) {
            FreeMem(picture->pixelData, picture->pixelDataSize);
            picture->pixelData = NULL;
            picture->pixelDataSize = 0;
        }
    }
    
    return result;
}

/*
** DecodeToRGB - Decode image data to RGB and return pointer
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
LONG DecodeToRGB(struct IFFPicture *picture, UBYTE **rgbData, ULONG *size)
{
    LONG result;
    
    if (!picture || !rgbData || !size) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid parameters");
        }
        return RETURN_FAIL;
    }
    
    /* Decode if not already decoded */
    if (!picture->isDecoded) {
        result = Decode(picture);
        if (result != RETURN_OK) {
            return result;
        }
    }
    
    *rgbData = picture->pixelData;
    *size = picture->pixelDataSize;
    
    return RETURN_OK;
}

/*
** AnalyzeFormat and GetOptimalPNGConfig are implemented in image_analyzer.c
** They are declared in iffpicture.h as part of the public API
*/

/*
** GetLastError - Get last error code
*/
LONG GetLastError(struct IFFPicture *picture)
{
    if (!picture) {
        return IFFPICTURE_INVALID;
    }
    return picture->lastError;
}

/*
** GetErrorString - Get last error message
*/
const char *GetErrorString(struct IFFPicture *picture)
{
    if (!picture) {
        return "Invalid picture object";
    }
    return picture->errorString;
}

