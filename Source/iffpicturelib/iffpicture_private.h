/*
** iffpicture_private.h - IFFPicture Library Private Definitions
**
** Internal structures and functions used by the library implementation.
** Only library implementation sources include this header, not application code.
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
#define ID_GPHD    0x47504844UL  /* 'GPHD' */
#define ID_PCHG    0x50434847UL  /* 'PCHG' */
#define ID_PLTP    0x504C5450UL  /* 'PLTP' - NewTek Video Toaster bitplane-to-component mapping */
#define ID_SHAM    0x5348414DUL  /* 'SHAM' */
#define ID_CTBL    0x4354424CUL  /* 'CTBL' */
#define ID_CLUT    0x434C5554UL  /* 'CLUT' */
#define ID_CMYK    0x434D594BUL  /* 'CMYK' */
#define ID_DCOL    0x44434F4CUL  /* 'DCOL' */
#define ID_DPI     0x44504920UL  /* 'DPI ' */
/* YUVN chunk IDs */
#define ID_YCHD    0x59434844UL  /* 'YCHD' - YUVN header */
#define ID_DATY    0x44415459UL  /* 'DATY' - Y (luminance) data */
#define ID_DATU    0x44415455UL  /* 'DATU' - U (color difference) data */
#define ID_DATV    0x44415456UL  /* 'DATV' - V (color difference) data */
#define ID_DATA    0x44415441UL  /* 'DATA' - Alpha channel data */
/* Metadata chunk IDs */
#define ID_GRAB    0x47524142UL  /* 'GRAB' - hotspot coordinates */
#define ID_DEST    0x44455354UL  /* 'DEST' - destination merge */
#define ID_SPRT    0x53505254UL  /* 'SPRT' - sprite precedence */
#define ID_CRNG    0x43524E47UL  /* 'CRNG' - color range */
#define ID_CCRT    0x43435254UL  /* 'CCRT' - Graphicraft color cycle timing */
/* ID_CAT, ID_LIST: use libraries/iffparse.h (universal IFF container IDs) */
#define ID_DGVW    0x44475657UL  /* 'DGVW' - Digi-View (often with 21-plane ILBM) */
#define ID_DYCP    0x44594350UL  /* 'DYCP' - dynamic color (stored; decode not applied) */
#define ID_COPYRIGHT 0x28632920UL  /* '(c) ' - copyright text */
#define ID_AUTH    0x41555448UL  /* 'AUTH' - author text */
#define ID_ANNO    0x414E4E4FUL  /* 'ANNO' - annotation text */
#define ID_TEXT    0x54455854UL  /* 'TEXT' - unformatted text */
#define ID_FVER    0x46564552UL  /* 'FVER' - AmigaOS version string */
/* Extended metadata chunk IDs (IFF-EXIF/IPTC/XMP/ICCP/GeoTIFF) */
#define ID_EXIF    0x45584946UL  /* 'EXIF' - EXIF Image Meta Data */
#define ID_IPTC    0x49505443UL  /* 'IPTC' - IPTC Image Meta Data */
#define ID_XMP0    0x584D5030UL  /* 'XMP0' - XMP Image Meta Data (JPEG-style, 64K limit) */
#define ID_XMP1    0x584D5031UL  /* 'XMP1' - XMP Image Meta Data (PNG-style, larger) */
#define ID_ICCP    0x49434350UL  /* 'ICCP' - ICC Profile Data */
#define ID_ICCN    0x4943434EUL  /* 'ICCN' - ICC Profile Name */
#define ID_GEOT    0x47454F54UL  /* 'GEOT' - GeoTIFF Meta Data */
#define ID_GEOF    0x47454F46UL  /* 'GEOF' - GeoTIFF Meta Data Flags */
#define ID_META    0x4D455441UL  /* 'META' - indicates metadata-only file (FORM type) */
/* DEEP chunk IDs */
#define ID_DGBL    0x4447424CUL  /* 'DGBL' - Deep GloBaL information */
#define ID_DPEL    0x4450454CUL  /* 'DPEL' - Deep Pixel Elements */
#define ID_DLOC    0x444C4F43UL  /* 'DLOC' - Deep display LOCation */
#define ID_DBOD    0x44424F44UL  /* 'DBOD' - Deep data BODy */
#define ID_DCHG    0x44434847UL  /* 'DCHG' - Deep CHanGe buffer */
#define ID_TVDC    0x54564443UL  /* 'TVDC' - TVPaint Deep Compression */

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
#define mskHasAlpha             4  /* separate alpha plane (some ILBM extensions; rejected for DCTV) */

/* Compression types */
#define cmpNone            0
#define cmpByteRun1        1
#define cmpByteRun1Column  2  

/* DEEP compression types */
#define DEEP_COMPRESS_NONE         0
#define DEEP_COMPRESS_RUNLENGTH    1
#define DEEP_COMPRESS_HUFFMAN     2
#define DEEP_COMPRESS_DYNAMICHUFF 3
#define DEEP_COMPRESS_JPEG        4
#define DEEP_COMPRESS_TVDC        5

/* DEEP component types (cType) */
#define DEEP_TYPE_RED        1
#define DEEP_TYPE_GREEN      2
#define DEEP_TYPE_BLUE       3
#define DEEP_TYPE_ALPHA      4
#define DEEP_TYPE_YELLOW     5
#define DEEP_TYPE_CYAN       6
#define DEEP_TYPE_MAGENTA    7
#define DEEP_TYPE_BLACK      8
#define DEEP_TYPE_MASK       9
#define DEEP_TYPE_ZBUFFER   10
#define DEEP_TYPE_OPACITY   11
#define DEEP_TYPE_LINEARKEY 12
#define DEEP_TYPE_BINARYKEY 13

/* HAM codes */
#define HAMCODE_CMAP    0
#define HAMCODE_BLUE    1
#define HAMCODE_RED     2
#define HAMCODE_GREEN   3

/* NewTek Video Toaster Framestore (16-plane ILBM with PLTP) */
#define VT_FRAMESTORE_NPLANES  16
#define VT_PLTP_COMP6          6
#define VT_PLTP_COMP7          7
#define VT_PLTP_SIZE           32

/* IFFPictureMeta structure - metadata storage, allocated on demand */
struct IFFPictureMeta {
    /* Standard metadata storage - library owns all memory */
    struct Point2D *grab;              /* GRAB chunk (hotspot) */
    struct DestMerge *dest;             /* DEST chunk */
    UWORD *sprt;                        /* SPRT chunk (sprite precedence) */
    struct CRange *crng;                /* CRNG chunk (first instance) */
    ULONG crngCount;                    /* Number of CRNG chunks */
    struct CRange *crngArray;           /* Array of all CRNG chunks */
    struct CycleInfo *ccrt;             /* CCRT chunk (first instance) */
    ULONG ccrtCount;                    /* Number of CCRT chunks */
    struct CycleInfo *ccrtArray;        /* Array of all CCRT chunks */
    UBYTE *clut;                        /* CLUT chunk raw payload (if present) */
    ULONG clutSize;
    UBYTE *dgvw;                        /* DGVW chunk raw payload (if present) */
    ULONG dgvwSize;
    UBYTE *dycp;                        /* DYCP chunk raw payload (if present) */
    ULONG dycpSize;
    STRPTR copyright;                   /* Copyright chunk */
    ULONG copyrightSize;                /* Size of copyright string (including null) */
    STRPTR author;                      /* AUTH chunk */
    ULONG authorSize;                   /* Size of author string (including null) */
    STRPTR annotation;                  /* ANNO chunk (first instance) */
    ULONG annotationCount;              /* Number of ANNO chunks */
    STRPTR *annotationArray;            /* Array of all ANNO strings */
    ULONG *annotationSizes;             /* Array of sizes for each ANNO string */
    STRPTR text;                        /* TEXT chunk (first instance) */
    ULONG textCount;                    /* Number of TEXT chunks */
    STRPTR *textArray;                  /* Array of all TEXT strings */
    ULONG *textSizes;                   /* Array of sizes for each TEXT string */
    STRPTR fver;                        /* FVER chunk (AmigaOS version string) */
    ULONG fverSize;                     /* Size of FVER string (including null) */
    /* Extended metadata storage - library owns all memory */
    UBYTE *exif;                        /* EXIF chunk (first instance) */
    ULONG exifSize;                     /* Size of first EXIF chunk */
    ULONG exifCount;                    /* Number of EXIF chunks */
    UBYTE **exifArray;                  /* Array of all EXIF chunks */
    ULONG *exifSizes;                   /* Array of sizes for each EXIF chunk */
    UBYTE *iptc;                        /* IPTC chunk (first instance) */
    ULONG iptcSize;                     /* Size of first IPTC chunk */
    ULONG iptcCount;                    /* Number of IPTC chunks */
    UBYTE **iptcArray;                  /* Array of all IPTC chunks */
    ULONG *iptcSizes;                   /* Array of sizes for each IPTC chunk */
    UBYTE *xmp0;                        /* XMP0 chunk (first instance) */
    ULONG xmp0Size;                     /* Size of first XMP0 chunk */
    ULONG xmp0Count;                    /* Number of XMP0 chunks */
    UBYTE **xmp0Array;                  /* Array of all XMP0 chunks */
    ULONG *xmp0Sizes;                   /* Array of sizes for each XMP0 chunk */
    UBYTE *xmp1;                        /* XMP1 chunk (single instance) */
    ULONG xmp1Size;                     /* Size of XMP1 chunk */
    UBYTE *iccp;                        /* ICCP chunk (first instance) */
    ULONG iccpSize;                     /* Size of first ICCP chunk */
    ULONG iccpCount;                    /* Number of ICCP chunks */
    UBYTE **iccpArray;                  /* Array of all ICCP chunks */
    ULONG *iccpSizes;                   /* Array of sizes for each ICCP chunk */
    STRPTR iccn;                        /* ICCN chunk (first instance) */
    ULONG iccnSize;                     /* Size of first ICCN string (including null) */
    ULONG iccnCount;                    /* Number of ICCN chunks */
    STRPTR *iccnArray;                  /* Array of all ICCN strings */
    ULONG *iccnSizes;                   /* Array of sizes for each ICCN string */
    UBYTE *geot;                        /* GEOT chunk (first instance) */
    ULONG geotSize;                     /* Size of first GEOT chunk */
    ULONG geotCount;                    /* Number of GEOT chunks */
    UBYTE **geotArray;                  /* Array of all GEOT chunks */
    ULONG *geotSizes;                   /* Array of sizes for each GEOT chunk */
    ULONG *geof;                        /* GEOF chunk (first instance) - 4-byte chunk ID */
    ULONG geofCount;                    /* Number of GEOF chunks */
    ULONG *geofArray;                   /* Array of all GEOF chunk IDs */
};

/* Complete IFFPicture structure - private implementation */
struct IFFPicture {
    /* Public members */
    struct BitMapHeader *bmhd;
    struct FaxHeader *fxhd;   /* FAXX header (for FAXX format) */
    struct GPHDHeader *gphd;  /* GPHD header (optional, for FAXX format) */
    struct YCHDHeader *ychd;  /* YUVN header (for YUVN format) */
    struct IFFColorMap *cmap;
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
    BOOL isFramestore;   /* TRUE if NewTek Video Toaster framestore (ILBM 16-plane + PLTP 6/7) */
    BOOL isDCTV;         /* TRUE if DCTV ILBM (LFSR signature, YUV body decode) */
    BOOL isDigiViewRgb;  /* TRUE if 21-plane Digi-View RGB body and/or DGVW chunk present */
    
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
    
    /* ILBM compression 2 (column-wise ByteRun1): pre-decoded BODY in row-major order */
    UBYTE *bodyDecodeBuffer;
    ULONG bodyDecodeOffset;
    ULONG bodyDecodeSize;

    /* Cached compressed BODY (e.g. after DCTV probe); ILBM decode reads from here if set */
    UBYTE *bodyReadCache;
    ULONG bodyReadCacheSize;
    ULONG bodyReadRawOffset;
    
    /* FAXX-specific: store original compression type */
    UBYTE faxxCompression;
    
    /* DEEP-specific fields */
    struct DGBLHeader *dgbl;      /* DEEP global header */
    struct DPELHeader *dpel;       /* DEEP pixel elements */
    struct DLOCHeader *dloc;       /* DEEP display location (current) */
    ULONG dbodChunkSize;           /* DBOD chunk size */
    ULONG dbodChunkPosition;       /* DBOD chunk position */
    struct DCHGHeader *dchg;      /* DEEP change buffer (for animation) */
    struct TVDCHeader *tvdc;       /* TVPaint compression table */
    
    /* Video Toaster Framestore: PLTP chunk (32 bytes), NULL if not present */
    UBYTE *pltp;
    
    /* ILBM/PBM multipalette: SHAM, PCHG, CTBL (optional; see pchg_palette.c) */
    BOOL mpalSham;
    BOOL mpalPchg;
    BOOL mpalCtbl;
    UBYTE *mpalShamAlloc;      /* Full SHAM chunk copy; body starts at +2 (leading UWORD 0) */
    ULONG mpalShamAllocSize;
    UBYTE *mpalPchgAlloc;      /* Owns uncompressed chunk or Huffman output; FreeMem this + mpalPchgAllocSize */
    ULONG mpalPchgAllocSize;
    UBYTE *mpalPchgPayload;    /* Line mask + change data (inside or equal to mpalPchgAlloc) */
    ULONG mpalPchgPayloadSize;
    UWORD mpalPchgCompression;
    UWORD mpalPchgFlags;
    WORD mpalPchgStartLine;
    UWORD mpalPchgLineCount;
    UWORD mpalPchgChangedLines;
    UWORD mpalPchgMinReg;
    UWORD mpalPchgMaxReg;
    UWORD mpalPchgMaxChanges;
    ULONG mpalPchgTotalChanges;
    UBYTE *mpalCtblData;       /* Raw CTBL chunk bytes (big-endian UWORDs: 12-bit 0RGB per entry) */
    ULONG mpalCtblSize;
    
    /* Metadata storage - allocated on demand */
    struct IFFPictureMeta *metadata;    /* Metadata structure, NULL if no metadata */
};

/* Per-decode scanline palette state (stack or AllocMem in decoder) */
#define IFF_MPAL_MODE_NONE 0
#define IFF_MPAL_MODE_SHAM 1
#define IFF_MPAL_MODE_PCHG 2

struct IFFMultipaletteState {
    UBYTE ms_mode;
    UBYTE ms_lace;
    const UBYTE *ms_sham;
    ULONG ms_shamRemain;
    UWORD ms_flags;
    WORD ms_startLine;
    UWORD ms_lineCount;
    UWORD ms_maskRowsLeft; /* PCHG: mask bits left to consume (LineCount), incl. trailing zeros */
    UWORD ms_changedLines;
    ULONG ms_totalChanges;
    UBYTE *ms_data;
    ULONG ms_dataRemain;
    UBYTE ms_bits;
    UBYTE ms_thismask;
    const UBYTE *ms_maskPtr;
};

/* Internal function prototypes - declared in image_decoder.c */
LONG DecodeILBM(struct IFFPicture *picture);
LONG DecodeHAM(struct IFFPicture *picture);
LONG DecodeEHB(struct IFFPicture *picture);
LONG DecodeFramestore(struct IFFPicture *picture);
LONG DecodeDCTV(struct IFFPicture *picture);
BOOL IsDCTVCandidate(struct IFFPicture *picture);
LONG CacheBODY(struct IFFPicture *picture);

/* DecodeDCTV returns this when LFSR signature absent (fall back to DecodeILBM) */
#define IFFPICTURE_NOT_DCTV  (-6)
LONG DecodeDEEP(struct IFFPicture *picture);
LONG DecodePBM(struct IFFPicture *picture);
LONG DecodeRGBN(struct IFFPicture *picture);
LONG DecodeRGB8(struct IFFPicture *picture);
LONG DecodeACBM(struct IFFPicture *picture);
LONG DecodeFAXX(struct IFFPicture *picture);
LONG DecodeYUVN(struct IFFPicture *picture);
LONG AnalyzeFormat(struct IFFPicture *picture);
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config, BOOL opaque);
VOID SetIFFPictureError(struct IFFPicture *picture, LONG error, const char *message);
VOID ReadAllMeta(struct IFFPicture *picture);

/* FAXX chunk reader function prototypes - declared in iffpicture.c */
LONG ReadGPHD(struct IFFPicture *picture);
LONG ReadFLOG(struct IFFPicture *picture);

/* Video Toaster Framestore: ReadPLTP - declared in iffpicture.c */
LONG ReadPLTP(struct IFFPicture *picture);

/* DEEP chunk reader function prototypes - declared in iffpicture.c */
LONG ReadDGBL(struct IFFPicture *picture);
LONG ReadDPEL(struct IFFPicture *picture);
LONG ReadDLOC(struct IFFPicture *picture);
LONG ReadDBOD(struct IFFPicture *picture);
LONG ReadDCHG(struct IFFPicture *picture);
LONG ReadTVDC(struct IFFPicture *picture);

/* ILBM/PBM multipalette chunk load (PCHG, SHAM, CTBL) */
LONG ReadILBMMultipalette(struct IFFPicture *picture);

/* Working palette 768 bytes (256*3); init from CMAP+CTBL; PCHG prestart; then each row */
LONG IFFMultipalette_Init(struct IFFPicture *picture, UBYTE *pal768,
    struct IFFMultipaletteState *st);
LONG IFFMultipalette_ApplyScanline(struct IFFMultipaletteState *st, UWORD row,
    UBYTE *pal768);
BOOL IFFMultipalette_Active(const struct IFFPicture *picture);
ULONG IFFMultipalette_SourceChunkId(const struct IFFPicture *picture);

#endif /* IFFPICTURE_PRIVATE_H */

