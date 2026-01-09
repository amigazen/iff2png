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
LONG ReadYCHD(struct IFFPicture *picture);
LONG ReadCMAP(struct IFFPicture *picture);
LONG ReadCAMG(struct IFFPicture *picture);
LONG ReadBODY(struct IFFPicture *picture);
LONG ReadABIT(struct IFFPicture *picture);
LONG ReadFXHD(struct IFFPicture *picture);
LONG ReadPAGE(struct IFFPicture *picture);
LONG ReadDGBL(struct IFFPicture *picture);
LONG ReadDPEL(struct IFFPicture *picture);
LONG ReadDLOC(struct IFFPicture *picture);
LONG ReadDBOD(struct IFFPicture *picture);
LONG ReadDCHG(struct IFFPicture *picture);
LONG ReadTVDC(struct IFFPicture *picture);
static VOID FreeIFFPictureMeta(struct IFFPictureMeta *meta);

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
    picture->ychd = NULL;
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
    picture->fxhd = NULL;
    picture->gphd = NULL;
    picture->ychd = NULL;
    
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
    
    /* Free FAXX headers */
    if (picture->fxhd) {
        FreeMem(picture->fxhd, sizeof(struct FaxHeader));
        picture->fxhd = NULL;
    }
    if (picture->gphd) {
        FreeMem(picture->gphd, sizeof(struct GPHDHeader));
        picture->gphd = NULL;
    }
    
    /* Free YUVN header */
    if (picture->ychd) {
        FreeMem(picture->ychd, sizeof(struct YCHDHeader));
        picture->ychd = NULL;
    }
    
    /* Free DEEP headers */
    if (picture->dgbl) {
        FreeMem(picture->dgbl, sizeof(struct DGBLHeader));
        picture->dgbl = NULL;
    }
    if (picture->dpel) {
        if (picture->dpel->typedepth) {
            FreeMem(picture->dpel->typedepth, picture->dpel->nElements * sizeof(struct TypeDepth));
        }
        FreeMem(picture->dpel, sizeof(struct DPELHeader));
        picture->dpel = NULL;
    }
    if (picture->dloc) {
        FreeMem(picture->dloc, sizeof(struct DLOCHeader));
        picture->dloc = NULL;
    }
    if (picture->dchg) {
        FreeMem(picture->dchg, sizeof(struct DCHGHeader));
        picture->dchg = NULL;
    }
    if (picture->tvdc) {
        FreeMem(picture->tvdc, sizeof(struct TVDCHeader));
        picture->tvdc = NULL;
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
    
    /* Free metadata structure if allocated */
    if (picture->metadata) {
        FreeIFFPictureMeta(picture->metadata);
        picture->metadata = NULL;
    }
    
    /* Free picture structure */
    FreeMem(picture, sizeof(struct IFFPicture));
}

/*
** FreeIFFPictureMeta - Free metadata structure and all its contents (internal helper)
*/
static VOID FreeIFFPictureMeta(struct IFFPictureMeta *meta)
{
    ULONG i;
    
    if (!meta) {
        return;
    }
    
    /* Free standard metadata */
    if (meta->grab) {
        FreeMem(meta->grab, sizeof(struct Point2D));
    }
    if (meta->dest) {
        FreeMem(meta->dest, sizeof(struct DestMerge));
    }
    if (meta->sprt) {
        FreeMem(meta->sprt, sizeof(UWORD));
    }
    if (meta->crngArray) {
        FreeMem(meta->crngArray, meta->crngCount * sizeof(struct CRange));
    }
    if (meta->copyright) {
        FreeMem(meta->copyright, meta->copyrightSize);
    }
    if (meta->author) {
        FreeMem(meta->author, meta->authorSize);
    }
    if (meta->annotationArray) {
        for (i = 0; i < meta->annotationCount; i++) {
            if (meta->annotationArray[i] && meta->annotationSizes) {
                FreeMem(meta->annotationArray[i], meta->annotationSizes[i]);
            }
        }
        FreeMem(meta->annotationArray, meta->annotationCount * sizeof(STRPTR));
    }
    if (meta->annotationSizes) {
        FreeMem(meta->annotationSizes, meta->annotationCount * sizeof(ULONG));
    }
    if (meta->textArray) {
        for (i = 0; i < meta->textCount; i++) {
            if (meta->textArray[i] && meta->textSizes) {
                FreeMem(meta->textArray[i], meta->textSizes[i]);
            }
        }
        FreeMem(meta->textArray, meta->textCount * sizeof(STRPTR));
    }
    if (meta->textSizes) {
        FreeMem(meta->textSizes, meta->textCount * sizeof(ULONG));
    }
    if (meta->fver) {
        FreeMem(meta->fver, meta->fverSize);
    }
    /* Free extended metadata */
    if (meta->exifArray) {
        for (i = 0; i < meta->exifCount; i++) {
            if (meta->exifArray[i] && meta->exifSizes) {
                FreeMem(meta->exifArray[i], meta->exifSizes[i]);
            }
        }
        FreeMem(meta->exifArray, meta->exifCount * sizeof(UBYTE *));
    }
    if (meta->exifSizes) {
        FreeMem(meta->exifSizes, meta->exifCount * sizeof(ULONG));
    }
    if (meta->iptcArray) {
        for (i = 0; i < meta->iptcCount; i++) {
            if (meta->iptcArray[i] && meta->iptcSizes) {
                FreeMem(meta->iptcArray[i], meta->iptcSizes[i]);
            }
        }
        FreeMem(meta->iptcArray, meta->iptcCount * sizeof(UBYTE *));
    }
    if (meta->iptcSizes) {
        FreeMem(meta->iptcSizes, meta->iptcCount * sizeof(ULONG));
    }
    if (meta->xmp0Array) {
        for (i = 0; i < meta->xmp0Count; i++) {
            if (meta->xmp0Array[i] && meta->xmp0Sizes) {
                FreeMem(meta->xmp0Array[i], meta->xmp0Sizes[i]);
            }
        }
        FreeMem(meta->xmp0Array, meta->xmp0Count * sizeof(UBYTE *));
    }
    if (meta->xmp0Sizes) {
        FreeMem(meta->xmp0Sizes, meta->xmp0Count * sizeof(ULONG));
    }
    if (meta->xmp1) {
        FreeMem(meta->xmp1, meta->xmp1Size);
    }
    if (meta->iccpArray) {
        for (i = 0; i < meta->iccpCount; i++) {
            if (meta->iccpArray[i] && meta->iccpSizes) {
                FreeMem(meta->iccpArray[i], meta->iccpSizes[i]);
            }
        }
        FreeMem(meta->iccpArray, meta->iccpCount * sizeof(UBYTE *));
    }
    if (meta->iccpSizes) {
        FreeMem(meta->iccpSizes, meta->iccpCount * sizeof(ULONG));
    }
    if (meta->iccnArray) {
        for (i = 0; i < meta->iccnCount; i++) {
            if (meta->iccnArray[i] && meta->iccnSizes) {
                FreeMem(meta->iccnArray[i], meta->iccnSizes[i]);
            }
        }
        FreeMem(meta->iccnArray, meta->iccnCount * sizeof(STRPTR));
    }
    if (meta->iccnSizes) {
        FreeMem(meta->iccnSizes, meta->iccnCount * sizeof(ULONG));
    }
    if (meta->geotArray) {
        for (i = 0; i < meta->geotCount; i++) {
            if (meta->geotArray[i] && meta->geotSizes) {
                FreeMem(meta->geotArray[i], meta->geotSizes[i]);
            }
        }
        FreeMem(meta->geotArray, meta->geotCount * sizeof(UBYTE *));
    }
    if (meta->geotSizes) {
        FreeMem(meta->geotSizes, meta->geotCount * sizeof(ULONG));
    }
    if (meta->geofArray) {
        FreeMem(meta->geofArray, meta->geofCount * sizeof(ULONG));
    }
    
    /* Free the metadata structure itself */
    FreeMem(meta, sizeof(struct IFFPictureMeta));
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
        /* FAXX uses FXHD (required) and PAGE (required) chunks */
        if ((error = PropChunk(picture->iff, formType, ID_FXHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for FXHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_GPHD); /* Optional - additional header */
        PropChunk(picture->iff, formType, ID_FLOG); /* Optional - log information */
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
        PropChunk(picture->iff, formType, ID_FVER);
        /* Extended metadata chunks - can appear in any FORM type */
        CollectionChunk(picture->iff, formType, ID_EXIF);
        CollectionChunk(picture->iff, formType, ID_IPTC);
        CollectionChunk(picture->iff, formType, ID_XMP0);
        PropChunk(picture->iff, formType, ID_XMP1);  /* XMP1 may occur only once */
        CollectionChunk(picture->iff, formType, ID_ICCP);
        CollectionChunk(picture->iff, formType, ID_ICCN);
        CollectionChunk(picture->iff, formType, ID_GEOT);
        CollectionChunk(picture->iff, formType, ID_GEOF);
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
        /* Extended metadata chunks - can appear in any FORM type */
        CollectionChunk(picture->iff, formType, ID_EXIF);
        CollectionChunk(picture->iff, formType, ID_IPTC);
        CollectionChunk(picture->iff, formType, ID_XMP0);
        PropChunk(picture->iff, formType, ID_XMP1);  /* XMP1 may occur only once */
        CollectionChunk(picture->iff, formType, ID_ICCP);
        CollectionChunk(picture->iff, formType, ID_ICCN);
        CollectionChunk(picture->iff, formType, ID_GEOT);
        CollectionChunk(picture->iff, formType, ID_GEOF);
        if ((error = StopChunk(picture->iff, formType, ID_ABIT)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for ABIT");
            return RETURN_FAIL;
        }
    } else if (formType == ID_RGBN || formType == ID_RGB8) {
        /* RGBN, RGB8 have BMHD and BODY */
        if ((error = PropChunk(picture->iff, formType, ID_BMHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for BMHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_CMAP); /* Optional */
        /* Extended metadata chunks - can appear in any FORM type */
        CollectionChunk(picture->iff, formType, ID_EXIF);
        CollectionChunk(picture->iff, formType, ID_IPTC);
        CollectionChunk(picture->iff, formType, ID_XMP0);
        PropChunk(picture->iff, formType, ID_XMP1);  /* XMP1 may occur only once */
        CollectionChunk(picture->iff, formType, ID_ICCP);
        CollectionChunk(picture->iff, formType, ID_ICCN);
        CollectionChunk(picture->iff, formType, ID_GEOT);
        CollectionChunk(picture->iff, formType, ID_GEOF);
        if ((error = StopChunk(picture->iff, formType, ID_BODY)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for BODY");
            return RETURN_FAIL;
        }
    } else if (formType == ID_DEEP) {
        /* DEEP uses DGBL, DPEL, DLOC, DBOD, DCHG, TVDC chunks */
        if ((error = PropChunk(picture->iff, formType, ID_DGBL)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for DGBL");
            return RETURN_FAIL;
        }
        if ((error = PropChunk(picture->iff, formType, ID_DPEL)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for DPEL");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_DLOC);  /* Optional */
        PropChunk(picture->iff, formType, ID_DCHG);  /* Optional (animation) */
        PropChunk(picture->iff, formType, ID_TVDC);  /* Optional (TVPaint compression) */
        /* Extended metadata chunks - can appear in any FORM type */
        CollectionChunk(picture->iff, formType, ID_EXIF);
        CollectionChunk(picture->iff, formType, ID_IPTC);
        CollectionChunk(picture->iff, formType, ID_XMP0);
        PropChunk(picture->iff, formType, ID_XMP1);  /* XMP1 may occur only once */
        CollectionChunk(picture->iff, formType, ID_ICCP);
        CollectionChunk(picture->iff, formType, ID_ICCN);
        CollectionChunk(picture->iff, formType, ID_GEOT);
        CollectionChunk(picture->iff, formType, ID_GEOF);
        if ((error = StopChunk(picture->iff, formType, ID_DBOD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for DBOD");
            return RETURN_FAIL;
        }
    } else if (formType == ID_YUVN) {
        /* YUVN uses YCHD header and DATY, DATU, DATV, DATA chunks */
        if ((error = PropChunk(picture->iff, formType, ID_YCHD)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set PropChunk for YCHD");
            return RETURN_FAIL;
        }
        PropChunk(picture->iff, formType, ID_AUTH); /* Optional */
        CollectionChunk(picture->iff, formType, ID_ANNO); /* Optional, can appear multiple times */
        /* Extended metadata chunks - can appear in any FORM type */
        CollectionChunk(picture->iff, formType, ID_EXIF);
        CollectionChunk(picture->iff, formType, ID_IPTC);
        CollectionChunk(picture->iff, formType, ID_XMP0);
        PropChunk(picture->iff, formType, ID_XMP1);  /* XMP1 may occur only once */
        CollectionChunk(picture->iff, formType, ID_ICCP);
        CollectionChunk(picture->iff, formType, ID_ICCN);
        CollectionChunk(picture->iff, formType, ID_GEOT);
        CollectionChunk(picture->iff, formType, ID_GEOF);
        /* Stop at data chunks - DATY, DATU, DATV, DATA (optional alpha) must appear in this order */
        if ((error = StopChunk(picture->iff, formType, ID_DATY)) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to set StopChunk for DATY");
            return RETURN_FAIL;
        }
        /* DATA chunk is optional (alpha channel) - don't fail if missing */
        StopChunk(picture->iff, formType, ID_DATA);
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
        /* Read optional GPHD chunk if present */
        ReadGPHD(picture); /* Optional, so ignore return value */
        
        /* Read optional FLOG chunk if present */
        ReadFLOG(picture); /* Optional, so ignore return value */
        
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
    } else if (formType == ID_YUVN) {
        /* YUVN uses YCHD header */
        if (ReadYCHD(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        
        /* Read and store metadata chunks */
        ReadAllMeta(picture);
        
        /* YUVN data chunks are read during decoding, not during parsing */
        /* We just need to ensure the YCHD is loaded */
    } else if (formType == ID_DEEP) {
        /* DEEP uses DGBL, DPEL, DLOC, DBOD, DCHG, TVDC chunks */
        if (ReadDGBL(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        if (ReadDPEL(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        ReadDLOC(picture); /* DLOC is optional, don't fail if missing */
        ReadDCHG(picture); /* DCHG is optional, don't fail if missing */
        ReadTVDC(picture); /* TVDC is optional, don't fail if missing */
        
        /* Read and store metadata chunks */
        ReadAllMeta(picture);
        
        /* Read DBOD chunk */
        if (ReadDBOD(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
    } else {
        /* ILBM, PBM, RGBN, RGB8, ACBM use BMHD */
        if (ReadBMHD(picture) != RETURN_OK) {
            return RETURN_FAIL; /* Error already set */
        }
        ReadCMAP(picture); /* CMAP is optional, don't fail if missing */
        ReadCAMG(picture); /* CAMG is optional, don't fail if missing */
        
        /* 24-bit ILBM (nPlanes == 24) is true-color, not indexed */
        if (formType == ID_ILBM && picture->bmhd && picture->bmhd->nPlanes == 24) {
            picture->isIndexed = FALSE;
        }
        
        /* Read and store metadata chunks */
        ReadAllMeta(picture);
        
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
    if (!picture) {
        return 0;
    }
    if (picture->formtype == ID_YUVN && picture->ychd) {
        return picture->ychd->ychd_Width;
    }
    if (picture->bmhd) {
        return picture->bmhd->w;
    }
    return 0;
}

UWORD GetHeight(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    if (picture->formtype == ID_YUVN && picture->ychd) {
        return picture->ychd->ychd_Height;
    }
    if (picture->bmhd) {
        return picture->bmhd->h;
    }
    return 0;
}

UWORD GetDepth(struct IFFPicture *picture)
{
    if (!picture) {
        return 0;
    }
    if (picture->formtype == ID_YUVN) {
        /* YUVN is always 24-bit RGB equivalent */
        return 24;
    }
    if (picture->bmhd) {
        return picture->bmhd->nPlanes;
    }
    return 0;
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

struct FaxHeader *GetFXHD(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->fxhd;
}

struct GPHDHeader *GetGPHD(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->gphd;
}

struct YCHDHeader *GetYCHD(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    return picture->ychd;
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
** GetImageInfo - Get all core image properties in a single structure
** Returns: Pointer to IFFImageInfo structure, or NULL if picture is invalid
** The structure is allocated statically and remains valid until the next
** call to GetImageInfo() or until the IFFPicture is freed.
*/
struct IFFImageInfo *GetImageInfo(struct IFFPicture *picture)
{
    static struct IFFImageInfo info;
    
    if (!picture) {
        return NULL;
    }
    
    /* Populate structure with all core properties */
    info.width = GetWidth(picture);
    info.height = GetHeight(picture);
    info.depth = GetDepth(picture);
    info.formType = GetFormType(picture);
    info.viewportModes = GetVPModes(picture);
    info.compressedSize = picture->bodyChunkSize;      /* Compressed data size (BODY chunk) */
    info.decodedSize = GetPixelDataSize(picture);      /* Decoded pixel data size */
    info.hasAlpha = HasAlpha(picture);
    info.isHAM = IsHAM(picture);
    info.isEHB = IsEHB(picture);
    info.isCompressed = IsCompressed(picture);
    info.isIndexed = picture->isIndexed;
    info.isGrayscale = picture->isGrayscale;
    info.isLoaded = picture->isLoaded;
    info.isDecoded = picture->isDecoded;
    
    return &info;
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
** ReadYCHD - Read YCHD chunk (YUVN header)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadYCHD(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct YCHDHeader *ychd;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored YCHD property */
    sp = FindProp(picture->iff, picture->formtype, ID_YCHD);
    if (!sp) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "YCHD chunk not found");
        return RETURN_FAIL;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadYCHD - Found YCHD property, size=%ld\n", sp->sp_Size);
    
    /* Check size - YCHD should be 24 bytes */
    if (sp->sp_Size < 24) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "YCHD chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate YCHD structure - use public memory (not chip RAM) */
    ychd = (struct YCHDHeader *)AllocMem(sizeof(struct YCHDHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!ychd) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate YCHDHeader");
        return RETURN_FAIL;
    }
    
    /* Read fields individually from byte array to avoid structure alignment issues */
    /* IFF data is big-endian, Amiga is big-endian, so we can read directly */
    {
        UBYTE *src = (UBYTE *)sp->sp_Data;
        
        DEBUG_BYTE_ARRAY("YCHD raw data", src, 24);
        
        /* Read UWORD fields (big-endian, 2 bytes each) */
        ychd->ychd_Width = (UWORD)((src[0] << 8) | src[1]);
        ychd->ychd_Height = (UWORD)((src[2] << 8) | src[3]);
        ychd->ychd_PageWidth = (UWORD)((src[4] << 8) | src[5]);
        ychd->ychd_PageHeight = (UWORD)((src[6] << 8) | src[7]);
        ychd->ychd_LeftEdge = (UWORD)((src[8] << 8) | src[9]);
        ychd->ychd_TopEdge = (UWORD)((src[10] << 8) | src[11]);
        
        /* Read UBYTE fields (1 byte each) */
        ychd->ychd_AspectX = src[12];
        ychd->ychd_AspectY = src[13];
        ychd->ychd_Compress = src[14];
        ychd->ychd_Flags = src[15];
        ychd->ychd_Mode = src[16];
        ychd->ychd_Norm = src[17];
        
        /* Read WORD field (big-endian, 2 bytes) */
        ychd->ychd_reserved2 = (WORD)((src[18] << 8) | src[19]);
        
        /* Read LONG field (big-endian, 4 bytes) */
        ychd->ychd_reserved3 = (LONG)((src[20] << 24) | (src[21] << 16) | (src[22] << 8) | src[23]);
        
        DEBUG_PRINTF5("DEBUG: YCHD parsed - Width=%ld Height=%ld Mode=%ld Norm=%ld Compress=%ld\n",
                      ychd->ychd_Width, ychd->ychd_Height, ychd->ychd_Mode, ychd->ychd_Norm, ychd->ychd_Compress);
    }
    
    /* Store YCHD header */
    picture->ychd = ychd;
    
    /* Check compression - only COMPRESS_NONE is supported */
    if (ychd->ychd_Compress != YCHD_COMPRESS_NONE) {
        SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "YUVN compression not supported");
        return RETURN_FAIL;
    }
    
    /* Validate width constraints based on mode */
    switch (ychd->ychd_Mode) {
        case YCHD_MODE_411:
            /* Width must be a multiple of 4 */
            if ((ychd->ychd_Width % 4) != 0) {
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "YUVN mode 411 requires width to be multiple of 4");
                return RETURN_FAIL;
            }
            break;
        case YCHD_MODE_422:
        case YCHD_MODE_211:
            /* Width must be a multiple of 2 */
            if ((ychd->ychd_Width % 2) != 0) {
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "YUVN mode 422/211 requires width to be multiple of 2");
                return RETURN_FAIL;
            }
            break;
        case YCHD_MODE_400:
        case YCHD_MODE_444:
        case YCHD_MODE_200:
        case YCHD_MODE_222:
            /* No width constraints for these modes */
            break;
        default:
            /* Unknown mode - will be caught later */
            break;
    }
    
    /* Validate height constraint for full-frame/interlaced images */
    if ((ychd->ychd_Flags & YCHDF_LACE) != 0) {
        /* Full-frame/interlaced: height must be a multiple of 2 */
        if ((ychd->ychd_Height % 2) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "YUVN full-frame/interlaced requires height to be multiple of 2");
            return RETURN_FAIL;
        }
    }
    
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
    
    /* FXHD should be at least 20 bytes (complete FaxHeader structure) */
    if (sp->sp_Size < 20) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FXHD chunk too small (must be at least 20 bytes)");
        return RETURN_FAIL;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadFXHD - Found FXHD property, size=%ld\n", sp->sp_Size);
    
    /* Free existing FXHD and BMHD if present (shouldn't happen, but be safe) */
    if (picture->fxhd) {
        FreeMem(picture->fxhd, sizeof(struct FaxHeader));
        picture->fxhd = NULL;
    }
    if (picture->bmhd) {
        FreeMem(picture->bmhd, sizeof(struct BitMapHeader));
        picture->bmhd = NULL;
    }
    
    /* Allocate FaxHeader structure - use public memory (not chip RAM) */
    picture->fxhd = (struct FaxHeader *)AllocMem(sizeof(struct FaxHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->fxhd) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate FaxHeader");
        return RETURN_FAIL;
    }
    
    /* Allocate BMHD structure for compatibility - use public memory (not chip RAM) */
    bmhd = (struct BitMapHeader *)AllocMem(sizeof(struct BitMapHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!bmhd) {
        FreeMem(picture->fxhd, sizeof(struct FaxHeader));
        picture->fxhd = NULL;
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BitMapHeader");
        return RETURN_FAIL;
    }
    
    /* Read FXHD data byte-by-byte (big-endian) */
    src = (UBYTE *)sp->sp_Data;
    
    /* Read complete FaxHeader structure according to spec:
     * UWORD Width (offset 0-1)
     * UWORD Height (offset 2-3)
     * UWORD LineLength (offset 4-5)
     * UWORD VRes (offset 6-7)
     * UBYTE Compression (offset 8)
     * UBYTE Pad[11] (offset 9-19)
     */
    picture->fxhd->Width = (UWORD)((src[0] << 8) | src[1]);
    picture->fxhd->Height = (UWORD)((src[2] << 8) | src[3]);
    picture->fxhd->LineLength = (UWORD)((src[4] << 8) | src[5]);
    picture->fxhd->VRes = (UWORD)((src[6] << 8) | src[7]);
    picture->fxhd->Compression = src[8];
    /* Copy Pad[11] bytes (offset 9-19) */
    {
        ULONG i;
        for (i = 0; i < 11; i++) {
            picture->fxhd->Pad[i] = src[9 + i];
        }
    }
    
    /* Extract values for BMHD conversion */
    width = picture->fxhd->Width;
    height = picture->fxhd->Height;
    compression = picture->fxhd->Compression;
    
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
    
    DEBUG_PRINTF5("DEBUG: ReadFXHD - Width=%ld Height=%ld LineLength=%ld VRes=%ld Compression=%ld\n",
                 (ULONG)width, (ULONG)height, (ULONG)picture->fxhd->LineLength,
                 (ULONG)picture->fxhd->VRes, (ULONG)compression);
    
    return RETURN_OK;
}

/*
** ReadGPHD - Read GPHD chunk (optional additional FAXX header)
** Returns: RETURN_OK on success, RETURN_FAIL on error (or if not found)
** Follows iffparse.library pattern: FindProp
**
** GPHD is an optional additional header used by some software producers
** as an extension to FXHD. See FAXX.GPHD.doc specification.
*/
LONG ReadGPHD(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    UBYTE *src;
    ULONG i;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored GPHD property (optional) */
    sp = FindProp(picture->iff, picture->formtype, ID_GPHD);
    if (!sp) {
        /* GPHD is optional, so not finding it is not an error */
        return RETURN_OK;
    }
    
    /* GPHD should be at least 58 bytes (complete GPHDHeader structure) */
    if (sp->sp_Size < 58) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "GPHD chunk too small (must be at least 58 bytes)");
        return RETURN_FAIL;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadGPHD - Found GPHD property, size=%ld\n", sp->sp_Size);
    
    /* Free existing GPHD if present */
    if (picture->gphd) {
        FreeMem(picture->gphd, sizeof(struct GPHDHeader));
        picture->gphd = NULL;
    }
    
    /* Allocate GPHDHeader structure - use public memory (not chip RAM) */
    picture->gphd = (struct GPHDHeader *)AllocMem(sizeof(struct GPHDHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->gphd) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate GPHDHeader");
        return RETURN_FAIL;
    }
    
    /* Read GPHD data byte-by-byte (big-endian) */
    src = (UBYTE *)sp->sp_Data;
    
    /* Read GPHD structure according to spec */
    picture->gphd->gp_Width = (UWORD)((src[0] << 8) | src[1]);
    picture->gphd->gp_Length = (UWORD)((src[2] << 8) | src[3]);
    picture->gphd->gp_Page = (UWORD)((src[4] << 8) | src[5]);
    
    /* Read gp_ID[22] (20 chars NULL term) */
    for (i = 0; i < 22; i++) {
        picture->gphd->gp_ID[i] = src[6 + i];
    }
    
    picture->gphd->gp_VRes = src[28];
    picture->gphd->gp_BitRate = src[29];
    picture->gphd->gp_PageWidth = src[30];
    picture->gphd->gp_PageLength = src[31];
    picture->gphd->gp_Compression = src[32];
    picture->gphd->gp_ErrorCorrection = src[33];
    picture->gphd->gp_BinaryFileTransfer = src[34];
    picture->gphd->gp_ScanTime = src[35];
    
    /* Read DateStamp (12 bytes: 3 ULONGs - ds_Days, ds_Minute, ds_Tick) */
    picture->gphd->gp_Date.ds_Days = (ULONG)((src[36] << 24) | (src[37] << 16) | (src[38] << 8) | src[39]);
    picture->gphd->gp_Date.ds_Minute = (ULONG)((src[40] << 24) | (src[41] << 16) | (src[42] << 8) | src[43]);
    picture->gphd->gp_Date.ds_Tick = (ULONG)((src[44] << 24) | (src[45] << 16) | (src[46] << 8) | src[47]);
    
    /* Read gp_Pad[10] */
    for (i = 0; i < 10; i++) {
        picture->gphd->gp_Pad[i] = src[48 + i];
    }
    
    DEBUG_PRINTF3("DEBUG: ReadGPHD - Width=%ld Length=%ld Page=%ld\n",
                 (ULONG)picture->gphd->gp_Width, (ULONG)picture->gphd->gp_Length,
                 (ULONG)picture->gphd->gp_Page);
    
    return RETURN_OK;
}

/*
** ReadFLOG - Read FLOG chunk (optional FAXX log information)
** Returns: RETURN_OK on success, RETURN_FAIL on error (or if not found)
** Follows iffparse.library pattern: FindProp
**
** FLOG contains log information about a received fax.
** The specification for this chunk will be submitted at a later date.
** For now, we just note its presence if present.
*/
LONG ReadFLOG(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored FLOG property (optional) */
    sp = FindProp(picture->iff, picture->formtype, ID_FLOG);
    if (!sp) {
        /* FLOG is optional, so not finding it is not an error */
        return RETURN_OK;
    }
    
    DEBUG_PRINTF1("DEBUG: ReadFLOG - Found FLOG property, size=%ld\n", sp->sp_Size);
    
    /* FLOG specification is not yet finalized, so we just note its presence */
    /* Future: Store FLOG data when specification is available */
    
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
** ReadDGBL - Read DGBL chunk (Deep GloBaL information)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadDGBL(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct DGBLHeader *dgbl;
    UBYTE *src;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored DGBL property */
    sp = FindProp(picture->iff, picture->formtype, ID_DGBL);
    if (!sp) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DGBL chunk not found");
        return RETURN_FAIL;
    }
    
    /* Check size - DGBL should be 8 bytes */
    if (sp->sp_Size < 8) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DGBL chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate DGBL structure */
    dgbl = (struct DGBLHeader *)AllocMem(sizeof(struct DGBLHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!dgbl) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DGBLHeader");
        return RETURN_FAIL;
    }
    
    /* Read fields from byte array (big-endian) */
    src = (UBYTE *)sp->sp_Data;
    dgbl->DisplayWidth = (UWORD)((src[0] << 8) | src[1]);
    dgbl->DisplayHeight = (UWORD)((src[2] << 8) | src[3]);
    dgbl->Compression = (UWORD)((src[4] << 8) | src[5]);
    dgbl->xAspect = src[6];
    dgbl->yAspect = src[7];
    
    picture->dgbl = dgbl;
    picture->isCompressed = (dgbl->Compression != DEEP_COMPRESS_NONE);
    
    return RETURN_OK;
}

/*
** ReadDPEL - Read DPEL chunk (Deep Pixel Elements)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: FindProp
*/
LONG ReadDPEL(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct DPELHeader *dpel;
    UBYTE *src;
    ULONG i;
    ULONG expectedSize;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored DPEL property */
    sp = FindProp(picture->iff, picture->formtype, ID_DPEL);
    if (!sp) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DPEL chunk not found");
        return RETURN_FAIL;
    }
    
    /* Check minimum size - need at least 4 bytes for nElements */
    if (sp->sp_Size < 4) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DPEL chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate DPEL structure */
    dpel = (struct DPELHeader *)AllocMem(sizeof(struct DPELHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!dpel) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DPELHeader");
        return RETURN_FAIL;
    }
    
    /* Read nElements (ULONG, big-endian, 4 bytes) */
    src = (UBYTE *)sp->sp_Data;
    dpel->nElements = (ULONG)((src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3]);
    
    /* Check that we have enough data for nElements TypeDepth structures */
    expectedSize = 4 + (dpel->nElements * 4); /* 4 bytes per TypeDepth (2 UWORDs) */
    if (sp->sp_Size < expectedSize) {
        FreeMem(dpel, sizeof(struct DPELHeader));
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DPEL chunk too small for nElements");
        return RETURN_FAIL;
    }
    
    /* Allocate TypeDepth array */
    if (dpel->nElements > 0) {
        dpel->typedepth = (struct TypeDepth *)AllocMem(dpel->nElements * sizeof(struct TypeDepth), MEMF_PUBLIC | MEMF_CLEAR);
        if (!dpel->typedepth) {
            FreeMem(dpel, sizeof(struct DPELHeader));
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate TypeDepth array");
            return RETURN_FAIL;
        }
        
        /* Read TypeDepth structures */
        src += 4; /* Skip nElements */
        for (i = 0; i < dpel->nElements; i++) {
            dpel->typedepth[i].cType = (UWORD)((src[0] << 8) | src[1]);
            dpel->typedepth[i].cBitDepth = (UWORD)((src[2] << 8) | src[3]);
            src += 4;
        }
    } else {
        dpel->typedepth = NULL;
    }
    
    picture->dpel = dpel;
    
    return RETURN_OK;
}

/*
** ReadDLOC - Read DLOC chunk (Deep display LOCation)
** Returns: RETURN_OK on success, RETURN_FAIL on error (optional chunk)
** Follows iffparse.library pattern: FindProp
*/
LONG ReadDLOC(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct DLOCHeader *dloc;
    UBYTE *src;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored DLOC property (optional) */
    sp = FindProp(picture->iff, picture->formtype, ID_DLOC);
    if (!sp) {
        /* DLOC is optional, return OK if not found */
        return RETURN_OK;
    }
    
    /* Check size - DLOC should be 8 bytes */
    if (sp->sp_Size < 8) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DLOC chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate DLOC structure */
    dloc = (struct DLOCHeader *)AllocMem(sizeof(struct DLOCHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!dloc) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DLOCHeader");
        return RETURN_FAIL;
    }
    
    /* Read fields from byte array (big-endian) */
    src = (UBYTE *)sp->sp_Data;
    dloc->w = (UWORD)((src[0] << 8) | src[1]);
    dloc->h = (UWORD)((src[2] << 8) | src[3]);
    dloc->x = (WORD)((src[4] << 8) | src[5]);
    dloc->y = (WORD)((src[6] << 8) | src[7]);
    
    picture->dloc = dloc;
    
    return RETURN_OK;
}

/*
** ReadDBOD - Read DBOD chunk position and size
** Returns: RETURN_OK on success, RETURN_FAIL on error
** Follows iffparse.library pattern: CurrentChunk
*/
LONG ReadDBOD(struct IFFPicture *picture)
{
    struct ContextNode *cn;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Get current chunk - should be DBOD after ParseIFF stops */
    cn = CurrentChunk(picture->iff);
    if (!cn) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "No current chunk (DBOD not found)");
        return RETURN_FAIL;
    }
    
    /* Verify it's the DBOD chunk */
    if (cn->cn_ID != ID_DBOD || cn->cn_Type != picture->formtype) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Current chunk is not DBOD");
        return RETURN_FAIL;
    }
    
    /* Store DBOD chunk information for later reading */
    picture->dbodChunkSize = cn->cn_Size;
    picture->dbodChunkPosition = 0; /* We're positioned at start of DBOD chunk */
    
    return RETURN_OK;
}

/*
** ReadDCHG - Read DCHG chunk (Deep CHanGe buffer)
** Returns: RETURN_OK on success, RETURN_FAIL on error (optional chunk)
** Follows iffparse.library pattern: FindProp
*/
LONG ReadDCHG(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct DCHGHeader *dchg;
    UBYTE *src;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored DCHG property (optional) */
    sp = FindProp(picture->iff, picture->formtype, ID_DCHG);
    if (!sp) {
        /* DCHG is optional, return OK if not found */
        return RETURN_OK;
    }
    
    /* Check size - DCHG should be 4 bytes */
    if (sp->sp_Size < 4) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DCHG chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate DCHG structure */
    dchg = (struct DCHGHeader *)AllocMem(sizeof(struct DCHGHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!dchg) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DCHGHeader");
        return RETURN_FAIL;
    }
    
    /* Read FrameRate (LONG, big-endian, 4 bytes) */
    src = (UBYTE *)sp->sp_Data;
    dchg->FrameRate = (LONG)((src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3]);
    
    picture->dchg = dchg;
    
    return RETURN_OK;
}

/*
** ReadTVDC - Read TVDC chunk (TVPaint Deep Compression)
** Returns: RETURN_OK on success, RETURN_FAIL on error (optional chunk)
** Follows iffparse.library pattern: FindProp
*/
LONG ReadTVDC(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct TVDCHeader *tvdc;
    UBYTE *src;
    ULONG i;
    
    if (!picture || !picture->iff) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid picture or IFF handle");
        }
        return RETURN_FAIL;
    }
    
    /* Find stored TVDC property (optional) */
    sp = FindProp(picture->iff, picture->formtype, ID_TVDC);
    if (!sp) {
        /* TVDC is optional, return OK if not found */
        return RETURN_OK;
    }
    
    /* Check size - TVDC should be 32 bytes (16 words) */
    if (sp->sp_Size < 32) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "TVDC chunk too small");
        return RETURN_FAIL;
    }
    
    /* Allocate TVDC structure */
    tvdc = (struct TVDCHeader *)AllocMem(sizeof(struct TVDCHeader), MEMF_PUBLIC | MEMF_CLEAR);
    if (!tvdc) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate TVDCHeader");
        return RETURN_FAIL;
    }
    
    /* Read 16 words (big-endian, 2 bytes each) */
    src = (UBYTE *)sp->sp_Data;
    for (i = 0; i < 16; i++) {
        tvdc->table[i] = (WORD)((src[0] << 8) | src[1]);
        src += 2;
    }
    
    picture->tvdc = tvdc;
    
    return RETURN_OK;
}

/*
** AllocIFFPictureMeta - Allocate metadata structure on demand (internal helper)
** Returns: Pointer to allocated metadata structure or NULL on failure
*/
static struct IFFPictureMeta *AllocIFFPictureMeta(VOID)
{
    struct IFFPictureMeta *meta;
    
    meta = (struct IFFPictureMeta *)AllocMem(sizeof(struct IFFPictureMeta), MEMF_PUBLIC | MEMF_CLEAR);
    if (!meta) {
        return NULL;
    }
    
    /* All fields are already cleared by MEMF_CLEAR */
    return meta;
}

/*
** EnsureMeta - Ensure metadata structure exists, allocate if needed (internal helper)
** Returns: Pointer to metadata structure or NULL on allocation failure
*/
static struct IFFPictureMeta *EnsureMeta(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    
    if (!picture->metadata) {
        picture->metadata = AllocIFFPictureMeta();
    }
    
    return picture->metadata;
}

/*
** ReadAllMeta - Read and store all metadata chunks in IFFPicture structure
** This function is called after ParseIFFPicture() to extract metadata
** All memory is owned by IFFPicture and freed by FreeIFFPicture()
** Metadata structure is allocated on demand when first metadata chunk is found
*/
VOID ReadAllMeta(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    struct CollectionItem *ci;
    struct IFFPictureMeta *meta;
    ULONG i;
    ULONG count;
    UBYTE *src;
    
    if (!picture || !picture->iff) {
        return;
    }
    
    /* Metadata structure will be allocated on demand when first chunk is found */
    /* All fields in the metadata structure are initialized to NULL/0 by MEMF_CLEAR */
    
    /* Read GRAB chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_GRAB);
    if (sp && sp->sp_Size >= 4) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->grab = (struct Point2D *)AllocMem(sizeof(struct Point2D), MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->grab) {
                src = (UBYTE *)sp->sp_Data;
                meta->grab->x = (WORD)((src[0] << 8) | src[1]);
                meta->grab->y = (WORD)((src[2] << 8) | src[3]);
            }
        }
    }
    
    /* Read DEST chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_DEST);
    if (sp && sp->sp_Size >= 8) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->dest = (struct DestMerge *)AllocMem(sizeof(struct DestMerge), MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->dest) {
                src = (UBYTE *)sp->sp_Data;
                meta->dest->depth = src[0];
                meta->dest->pad1 = src[1];
                meta->dest->planePick = (UWORD)((src[2] << 8) | src[3]);
                meta->dest->planeOnOff = (UWORD)((src[4] << 8) | src[5]);
                meta->dest->planeMask = (UWORD)((src[6] << 8) | src[7]);
            }
        }
    }
    
    /* Read SPRT chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_SPRT);
    if (sp && sp->sp_Size >= 2) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->sprt = (UWORD *)AllocMem(sizeof(UWORD), MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->sprt) {
                src = (UBYTE *)sp->sp_Data;
                *meta->sprt = (UWORD)((src[0] << 8) | src[1]);
            }
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
            meta = EnsureMeta(picture);
            if (meta) {
                meta->crngCount = count;
                meta->crngArray = (struct CRange *)AllocMem(count * sizeof(struct CRange), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->crngArray) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_CRNG);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size >= 8) {
                            src = (UBYTE *)ci->ci_Data;
                            meta->crngArray[i].pad1 = (WORD)((src[0] << 8) | src[1]);
                            meta->crngArray[i].rate = (WORD)((src[2] << 8) | src[3]);
                            meta->crngArray[i].flags = (WORD)((src[4] << 8) | src[5]);
                            meta->crngArray[i].low = src[6];
                            meta->crngArray[i].high = src[7];
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->crng = meta->crngArray;
                }
            }
        }
    }
    
    /* Read Copyright chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_COPYRIGHT);
    if (sp && sp->sp_Size > 0) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->copyrightSize = sp->sp_Size + 1;
            meta->copyright = (STRPTR)AllocMem(meta->copyrightSize, MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->copyright) {
                CopyMem(sp->sp_Data, meta->copyright, sp->sp_Size);
                meta->copyright[sp->sp_Size] = '\0';
            }
        }
    }
    
    /* Read Author chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_AUTH);
    if (sp && sp->sp_Size > 0) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->authorSize = sp->sp_Size + 1;
            meta->author = (STRPTR)AllocMem(meta->authorSize, MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->author) {
                CopyMem(sp->sp_Data, meta->author, sp->sp_Size);
                meta->author[sp->sp_Size] = '\0';
            }
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
            meta = EnsureMeta(picture);
            if (meta) {
                meta->annotationCount = count;
                meta->annotationArray = (STRPTR *)AllocMem(count * sizeof(STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
                meta->annotationSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->annotationArray && meta->annotationSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_ANNO);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->annotationSizes[i] = ci->ci_Size + 1;
                            meta->annotationArray[i] = (STRPTR)AllocMem(meta->annotationSizes[i], MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->annotationArray[i]) {
                                CopyMem(ci->ci_Data, meta->annotationArray[i], ci->ci_Size);
                                meta->annotationArray[i][ci->ci_Size] = '\0';
                            }
                        } else {
                            meta->annotationSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->annotation = meta->annotationArray[0];
                }
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
            meta = EnsureMeta(picture);
            if (meta) {
                meta->textCount = count;
                meta->textArray = (STRPTR *)AllocMem(count * sizeof(STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
                meta->textSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->textArray && meta->textSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_TEXT);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->textSizes[i] = ci->ci_Size + 1;
                            meta->textArray[i] = (STRPTR)AllocMem(meta->textSizes[i], MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->textArray[i]) {
                                CopyMem(ci->ci_Data, meta->textArray[i], ci->ci_Size);
                                meta->textArray[i][ci->ci_Size] = '\0';
                            }
                        } else {
                            meta->textSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->text = meta->textArray[0];
                }
            }
        }
    }
    
    /* Read FVER chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_FVER);
    if (sp && sp->sp_Size > 0) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->fverSize = sp->sp_Size + 1;
            meta->fver = (STRPTR)AllocMem(meta->fverSize, MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->fver) {
                CopyMem(sp->sp_Data, meta->fver, sp->sp_Size);
                meta->fver[sp->sp_Size] = '\0';
            }
        }
    }
    
    /* Read EXIF chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_EXIF);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->exifCount = count;
                meta->exifArray = (UBYTE **)AllocMem(count * sizeof(UBYTE *), MEMF_PUBLIC | MEMF_CLEAR);
                meta->exifSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->exifArray && meta->exifSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_EXIF);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->exifSizes[i] = ci->ci_Size;
                            meta->exifArray[i] = (UBYTE *)AllocMem(ci->ci_Size, MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->exifArray[i]) {
                                CopyMem(ci->ci_Data, meta->exifArray[i], ci->ci_Size);
                            }
                        } else {
                            meta->exifSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->exif = meta->exifArray[0];
                    meta->exifSize = meta->exifSizes[0];
                }
            }
        }
    }
    
    /* Read IPTC chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_IPTC);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->iptcCount = count;
                meta->iptcArray = (UBYTE **)AllocMem(count * sizeof(UBYTE *), MEMF_PUBLIC | MEMF_CLEAR);
                meta->iptcSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->iptcArray && meta->iptcSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_IPTC);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->iptcSizes[i] = ci->ci_Size;
                            meta->iptcArray[i] = (UBYTE *)AllocMem(ci->ci_Size, MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->iptcArray[i]) {
                                CopyMem(ci->ci_Data, meta->iptcArray[i], ci->ci_Size);
                            }
                        } else {
                            meta->iptcSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->iptc = meta->iptcArray[0];
                    meta->iptcSize = meta->iptcSizes[0];
                }
            }
        }
    }
    
    /* Read XMP0 chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_XMP0);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->xmp0Count = count;
                meta->xmp0Array = (UBYTE **)AllocMem(count * sizeof(UBYTE *), MEMF_PUBLIC | MEMF_CLEAR);
                meta->xmp0Sizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->xmp0Array && meta->xmp0Sizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_XMP0);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->xmp0Sizes[i] = ci->ci_Size;
                            meta->xmp0Array[i] = (UBYTE *)AllocMem(ci->ci_Size, MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->xmp0Array[i]) {
                                CopyMem(ci->ci_Data, meta->xmp0Array[i], ci->ci_Size);
                            }
                        } else {
                            meta->xmp0Sizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->xmp0 = meta->xmp0Array[0];
                    meta->xmp0Size = meta->xmp0Sizes[0];
                }
            }
        }
    }
    
    /* Read XMP1 chunk (single instance) */
    sp = FindProp(picture->iff, picture->formtype, ID_XMP1);
    if (sp && sp->sp_Size > 0) {
        meta = EnsureMeta(picture);
        if (meta) {
            meta->xmp1Size = sp->sp_Size;
            meta->xmp1 = (UBYTE *)AllocMem(meta->xmp1Size, MEMF_PUBLIC | MEMF_CLEAR);
            if (meta->xmp1) {
                CopyMem(sp->sp_Data, meta->xmp1, sp->sp_Size);
            }
        }
    }
    
    /* Read ICCP chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_ICCP);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->iccpCount = count;
                meta->iccpArray = (UBYTE **)AllocMem(count * sizeof(UBYTE *), MEMF_PUBLIC | MEMF_CLEAR);
                meta->iccpSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->iccpArray && meta->iccpSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_ICCP);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->iccpSizes[i] = ci->ci_Size;
                            meta->iccpArray[i] = (UBYTE *)AllocMem(ci->ci_Size, MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->iccpArray[i]) {
                                CopyMem(ci->ci_Data, meta->iccpArray[i], ci->ci_Size);
                            }
                        } else {
                            meta->iccpSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->iccp = meta->iccpArray[0];
                    meta->iccpSize = meta->iccpSizes[0];
                }
            }
        }
    }
    
    /* Read ICCN chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_ICCN);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->iccnCount = count;
                meta->iccnArray = (STRPTR *)AllocMem(count * sizeof(STRPTR), MEMF_PUBLIC | MEMF_CLEAR);
                meta->iccnSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->iccnArray && meta->iccnSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_ICCN);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->iccnSizes[i] = ci->ci_Size + 1;
                            meta->iccnArray[i] = (STRPTR)AllocMem(meta->iccnSizes[i], MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->iccnArray[i]) {
                                CopyMem(ci->ci_Data, meta->iccnArray[i], ci->ci_Size);
                                meta->iccnArray[i][ci->ci_Size] = '\0';
                            }
                        } else {
                            meta->iccnSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->iccn = meta->iccnArray[0];
                    meta->iccnSize = meta->iccnSizes[0];
                }
            }
        }
    }
    
    /* Read GEOT chunks (multiple instances via CollectionChunk) */
    ci = FindCollection(picture->iff, picture->formtype, ID_GEOT);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->geotCount = count;
                meta->geotArray = (UBYTE **)AllocMem(count * sizeof(UBYTE *), MEMF_PUBLIC | MEMF_CLEAR);
                meta->geotSizes = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->geotArray && meta->geotSizes) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_GEOT);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size > 0) {
                            meta->geotSizes[i] = ci->ci_Size;
                            meta->geotArray[i] = (UBYTE *)AllocMem(ci->ci_Size, MEMF_PUBLIC | MEMF_CLEAR);
                            if (meta->geotArray[i]) {
                                CopyMem(ci->ci_Data, meta->geotArray[i], ci->ci_Size);
                            }
                        } else {
                            meta->geotSizes[i] = 0;
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->geot = meta->geotArray[0];
                    meta->geotSize = meta->geotSizes[0];
                }
            }
        }
    }
    
    /* Read GEOF chunks (multiple instances via CollectionChunk) - 4-byte chunk IDs */
    ci = FindCollection(picture->iff, picture->formtype, ID_GEOF);
    if (ci) {
        count = 0;
        while (ci) {
            count++;
            ci = ci->ci_Next;
        }
        
        if (count > 0) {
            meta = EnsureMeta(picture);
            if (meta) {
                meta->geofCount = count;
                meta->geofArray = (ULONG *)AllocMem(count * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
                if (meta->geofArray) {
                    ci = FindCollection(picture->iff, picture->formtype, ID_GEOF);
                    for (i = 0; i < count && ci; i++, ci = ci->ci_Next) {
                        if (ci->ci_Size >= 4) {
                            src = (UBYTE *)ci->ci_Data;
                            meta->geofArray[i] = (ULONG)((src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3]);
                        } else {
                            meta->geofArray[i] = 0x20202020UL;  /* '    ' - unknown source */
                        }
                    }
                    /* Store first instance pointer for convenience */
                    meta->geof = &meta->geofArray[0];
                }
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
    UWORD width, height;
    
    if (!picture || !picture->isLoaded) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not loaded");
        }
        return RETURN_FAIL;
    }
    
    /* Get dimensions based on format */
    if (picture->formtype == ID_YUVN) {
        if (!picture->ychd) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "YCHD missing");
            return RETURN_FAIL;
        }
        width = picture->ychd->ychd_Width;
        height = picture->ychd->ychd_Height;
    } else {
        if (!picture->bmhd) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "BMHD missing");
            return RETURN_FAIL;
        }
        width = picture->bmhd->w;
        height = picture->bmhd->h;
    }
    
    /* Allocate RGB pixel buffer - use public memory (not chip RAM, we're not rendering to display) */
    /* For YUVN, we'll check for alpha and reallocate if needed in DecodeYUVN() */
    picture->pixelDataSize = (ULONG)width * height * 3;
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
        case ID_YUVN:
            result = DecodeYUVN(picture);
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


