/*
** iffpicture_private.h - IFFPicture Library Private Definitions
**
** Internal structures and functions used by the library implementation
** This header is only included by library source files, not by clients
*/

#ifndef IFFPICTURE_PRIVATE_H
#define IFFPICTURE_PRIVATE_H

#include "iffpicture.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <libraries/iffparse.h>
#include <dos/dos.h>

/* IFF Chunk IDs */
#define ID_BMHD    0x424D4844UL  /* 'BMHD' */
#define ID_CMAP    0x434D4150UL  /* 'CMAP' */
#define ID_CAMG    0x43414D47UL  /* 'CAMG' */
#define ID_BODY    0x424F4459UL  /* 'BODY' */
#define ID_ABIT    0x41424954UL  /* 'ABIT' */
#define ID_FXHD    0x46584844UL  /* 'FXHD' */
#define ID_PAGE    0x50414745UL  /* 'PAGE' */
#define ID_FLOG    0x464C4F47UL  /* 'FLOG' */
#define ID_PCHG    0x50434847UL  /* 'PCHG' */
#define ID_SHAM    0x5348414DUL  /* 'SHAM' */
#define ID_CTBL    0x4354424CUL  /* 'CTBL' */
#define ID_CLUT    0x434C5554UL  /* 'CLUT' */
#define ID_CMYK    0x434D594BUL  /* 'CMYK' */
#define ID_DCOL    0x44434F4CUL  /* 'DCOL' */
#define ID_DPI     0x44504920UL  /* 'DPI ' */

/* Viewport mode flags */
#define vmLACE              0x0004UL
#define vmEXTRA_HALFBRITE   0x0080UL
#define vmHAM               0x0800UL
#define vmHIRES             0x8000UL

/* Masking types */
#define mskNone                 0
#define mskHasMask              1
#define mskHasTransparentColor  2
#define mskLasso                3

/* Compression types */
#define cmpNone         0
#define cmpByteRun1     1

/* HAM codes */
#define HAMCODE_CMAP    0
#define HAMCODE_BLUE    1
#define HAMCODE_RED     2
#define HAMCODE_GREEN   3

/* Complete IFFPicture structure - private implementation */
struct IFFPicture {
    /* Public members */
    struct BitMapHeader *bmhd;
    struct ColorMap *cmap;
    ULONG viewportmodes;
    ULONG formtype;
    
    /* Decoded image data */
    UBYTE *pixelData;
    ULONG pixelDataSize;
    BOOL hasAlpha;
    
    /* For indexed images: store original palette indices */
    UBYTE *paletteIndices;
    ULONG paletteIndicesSize;
    
    /* Format analysis */
    BOOL isHAM;
    BOOL isEHB;
    BOOL isCompressed;
    BOOL isIndexed;
    BOOL isGrayscale;
    
    /* Private members - internal to library */
    struct IFFHandle *iff;
    BPTR filehandle;
    LONG lastError;
    char errorString[256];
    
    /* Internal state */
    BOOL isLoaded;
    BOOL isDecoded;
    ULONG bodyChunkSize;
    ULONG bodyChunkPosition;
    
    /* FAXX-specific: store original compression type */
    UBYTE faxxCompression;
};

/* Internal function prototypes - declared in image_decoder.c */
LONG DecodeILBM(struct IFFPicture *picture);
LONG DecodeHAM(struct IFFPicture *picture);
LONG DecodeEHB(struct IFFPicture *picture);
LONG DecodeDEEP(struct IFFPicture *picture);
LONG DecodePBM(struct IFFPicture *picture);
LONG DecodeRGBN(struct IFFPicture *picture);
LONG DecodeRGB8(struct IFFPicture *picture);
LONG DecodeACBM(struct IFFPicture *picture);
LONG DecodeFAXX(struct IFFPicture *picture);
LONG AnalyzeFormat(struct IFFPicture *picture);
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config);
VOID SetIFFPictureError(struct IFFPicture *picture, LONG error, const char *message);

#endif /* IFFPICTURE_PRIVATE_H */

