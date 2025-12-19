/*
** iffpicture.h - IFFPicture Library Public Interface
**
** Amiga-style function library for loading and decoding IFF bitmap images
** All functions use CamelCase naming convention
*/

#ifndef IFFPICTURE_H
#define IFFPICTURE_H

#include <exec/types.h>
#include <dos/dos.h>

/* Forward declarations */
struct IFFPicture;
struct BitMapHeader;
struct ColorMap;
struct PNGConfig; /* Defined in png_encoder.h */

/* Factory Functions - following iffparse.library pattern */
struct IFFPicture *AllocIFFPicture(VOID);
VOID FreeIFFPicture(struct IFFPicture *picture);

/* Loading Functions - following iffparse.library pattern */
LONG OpenIFFPicture(struct IFFPicture *picture, const char *filename);
VOID InitIFFPictureasDOS(struct IFFPicture *picture, BPTR filehandle);
VOID CloseIFFPicture(struct IFFPicture *picture);
LONG ParseIFFPicture(struct IFFPicture *picture);

/* Chunk Reading Functions - following iffparse.library pattern */
LONG ReadBMHD(struct IFFPicture *picture);
LONG ReadCMAP(struct IFFPicture *picture);
LONG ReadCAMG(struct IFFPicture *picture);
LONG ReadBODY(struct IFFPicture *picture);
LONG ReadABIT(struct IFFPicture *picture);

/* Getter Functions - following iffparse.library pattern */
UWORD GetWidth(struct IFFPicture *picture);
UWORD GetHeight(struct IFFPicture *picture);
UWORD GetDepth(struct IFFPicture *picture);
ULONG GetFormType(struct IFFPicture *picture);
ULONG GetViewportModes(struct IFFPicture *picture);
struct BitMapHeader *GetBMHD(struct IFFPicture *picture);
struct ColorMap *GetColorMap(struct IFFPicture *picture);
UBYTE *GetPixelData(struct IFFPicture *picture);
ULONG GetPixelDataSize(struct IFFPicture *picture);
BOOL HasAlpha(struct IFFPicture *picture);
BOOL IsHAM(struct IFFPicture *picture);
BOOL IsEHB(struct IFFPicture *picture);
BOOL IsCompressed(struct IFFPicture *picture);

/* Decoding Functions */
LONG Decode(struct IFFPicture *picture);
LONG DecodeToRGB(struct IFFPicture *picture, UBYTE **rgbData, ULONG *size);

/* Analysis Functions */
LONG AnalyzeFormat(struct IFFPicture *picture);
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config);

/* Error Handling */
LONG GetLastError(struct IFFPicture *picture);
const char *GetErrorString(struct IFFPicture *picture);

/* BitMapHeader structure - public */
/* Note: Structure must match IFF BMHD chunk layout exactly (20 bytes, byte-packed) */
struct BitMapHeader {
    UWORD w, h;             /* raster width & height in pixels */
    WORD x, y;              /* pixel position for this image */
    UBYTE nPlanes;          /* # source bitplanes */
    UBYTE masking;          /* masking technique */
    UBYTE compression;      /* compression algorithm */
    UBYTE pad1;             /* unused; ignore on read, write as 0 */
    UWORD transparentColor;  /* transparent "color number" */
    UBYTE xAspect, yAspect; /* pixel aspect, a ratio width : height */
    WORD pageWidth, pageHeight; /* source "page" size in pixels */
};

/* ColorMap structure - public */
struct ColorMap {
    UBYTE *data;        /* RGB triplets (r, g, b, r, g, b, ...) */
    ULONG numcolors;
    BOOL is4Bit;        /* TRUE if 4-bit palette needs scaling */
};

/* IFF Form Type IDs - ID_FORM is defined in iffparse.h */
#define ID_ILBM    0x494C424DUL  /* 'ILBM' */
#define ID_PBM     0x50424D20UL  /* 'PBM ' */
#define ID_RGBN    0x5247424EUL  /* 'RGBN' */
#define ID_RGB8    0x52474238UL  /* 'RGB8' */
#define ID_DEEP    0x44454550UL  /* 'DEEP' */
#define ID_ACBM    0x4143424DUL  /* 'ACBM' */

/* Error codes */
#define IFFPICTURE_OK           0
#define IFFPICTURE_ERROR       -1
#define IFFPICTURE_NOMEM       -2
#define IFFPICTURE_BADFILE     -3
#define IFFPICTURE_UNSUPPORTED -4
#define IFFPICTURE_INVALID     -5

#endif /* IFFPICTURE_H */

