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

/*
** AllocIFFPicture - Allocate a new IFFPicture object
** Returns: Pointer to new object or NULL on failure
** Follows iffparse.library pattern: AllocIFF
*/
struct IFFPicture *AllocIFFPicture(VOID)
{
    struct IFFPicture *picture;
    
    /* Allocate memory for picture structure */
    picture = (struct IFFPicture *)AllocMem(sizeof(struct IFFPicture), MEMF_CLEAR);
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
    picture->filehandle = 0;
    picture->lastError = IFFPICTURE_OK;
    picture->errorString[0] = '\0';
    picture->isLoaded = FALSE;
    picture->isDecoded = FALSE;
    picture->bodyChunkSize = 0;
    picture->bodyChunkPosition = 0;
    
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
    
    /* Close file handle if open */
    if (picture->filehandle) {
        Close(picture->filehandle);
        picture->filehandle = 0;
    }
    
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
        FreeMem(picture->cmap, sizeof(struct ColorMap));
        picture->cmap = NULL;
    }
    
    /* Free pixel data */
    if (picture->pixelData) {
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelData = NULL;
        picture->pixelDataSize = 0;
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
** OpenIFFPicture - Open IFF file from filename or open IFF context
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: OpenIFF
** If filename is NULL, opens the already-initialized IFF context
*/
LONG OpenIFFPicture(struct IFFPicture *picture, const char *filename)
{
    BPTR filehandle = (BPTR)NULL;
    LONG error;
    
    if (!picture) {
        return RETURN_FAIL;
    }
    
    /* If filename provided, open file and initialize as DOS */
    if (filename) {
        printf("Opening file handle for: %s\n", filename);
        fflush(stdout);
        filehandle = Open((STRPTR)filename, MODE_OLDFILE);
        if (!filehandle) {
            printf("Open() failed\n");
            fflush(stdout);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Cannot open file");
            return RETURN_FAIL;
        }
        printf("File opened successfully, handle: %ld\n", (ULONG)filehandle);
        fflush(stdout);
        
        /* Initialize IFFPicture as DOS stream */
        printf("Calling InitIFFPictureasDOS...\n");
        fflush(stdout);
        InitIFFPictureasDOS(picture, filehandle);
        printf("InitIFFPictureasDOS completed, iff=%p\n", picture->iff);
        fflush(stdout);
    }
    
    /* Ensure IFF handle is allocated */
    if (!picture->iff) {
        printf("ERROR: IFF handle is NULL after InitIFFPictureasDOS\n");
        fflush(stdout);
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "IFFPicture not initialized");
        if (filename) {
            Close(filehandle);
        }
        return RETURN_FAIL;
    }
    
    /* Open IFF for reading */
    printf("Calling OpenIFF...\n");
    fflush(stdout);
    error = OpenIFF(picture->iff, IFFF_READ);
    printf("OpenIFF returned: %ld\n", error);
    fflush(stdout);
    if (error) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Cannot open IFF file");
        if (filename) {
            Close(filehandle);
        }
        return RETURN_FAIL;
    }
    
    picture->isLoaded = TRUE;
    return RETURN_OK;
}

/*
** InitIFFPictureasDOS - Initialize IFFPicture as DOS stream
** Follows iffparse.library pattern: InitIFFasDOS
*/
VOID InitIFFPictureasDOS(struct IFFPicture *picture, BPTR filehandle)
{
    struct IFFHandle *iff;
    
    if (!picture || !filehandle) {
        return;
    }
    
    /* Store file handle */
    picture->filehandle = filehandle;
    
    /* Allocate IFF handle */
    iff = AllocIFF();
    if (!iff) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Cannot allocate IFF handle");
        return;
    }
    
    picture->iff = iff;
    
    /* Set stream BEFORE InitIFFasDOS (as per examples) */
    iff->iff_Stream = (ULONG)filehandle;
    
    /* Initialize IFF as DOS stream */
    InitIFFasDOS(iff);
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
    if (formType == ID_ILBM || formType == ID_PBM) {
        /* Common chunks for ILBM, PBM */
        if ((error = PropChunk(picture->iff, formType, ID_BMHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for BMHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_CMAP);
        PropChunk(picture->iff, formType, ID_CAMG);
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
    
    /* Parse the file until we hit the BODY chunk */
    error = ParseIFF(picture->iff, IFFPARSE_SCAN);
    if (error != 0 && error != IFFERR_EOC) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to parse IFF file");
        return RETURN_FAIL;
    }
    
    /* Extract stored property chunks */
    if (ReadBMHD(picture) != RETURN_OK) {
        return RETURN_FAIL; /* Error already set */
    }
    ReadCMAP(picture); /* CMAP is optional, don't fail if missing */
    ReadCAMG(picture); /* CAMG is optional, don't fail if missing */
    
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
    
    picture->isLoaded = TRUE;
    return RETURN_OK;
}

/*
** Getter functions - return values from picture structure
** Following iffparse.library pattern: Get* functions
*/
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

ULONG GetViewportModes(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    return picture->viewportmodes;
}

struct BitMapHeader *GetBMHD(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->bmhd;
}

struct ColorMap *GetColorMap(struct IFFPicture *picture)
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
    
    /* Allocate BMHD structure */
    bmhd = (struct BitMapHeader *)AllocMem(sizeof(struct BitMapHeader), MEMF_CLEAR);
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
    struct ColorMap *cmap;
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
    
    /* Allocate ColorMap structure */
    cmap = (struct ColorMap *)AllocMem(sizeof(struct ColorMap), MEMF_CLEAR);
    if (!cmap) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate ColorMap");
        return RETURN_FAIL;
    }
    
    /* Allocate color data */
    data = (UBYTE *)AllocMem(sp->sp_Size, MEMF_CLEAR);
    if (!data) {
        FreeMem(cmap, sizeof(struct ColorMap));
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
    
    /* Allocate RGB pixel buffer */
    picture->pixelDataSize = (ULONG)picture->bmhd->w * picture->bmhd->h * 3;
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_CLEAR);
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

