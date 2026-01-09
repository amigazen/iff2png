/*
**	$VER: iffpicture.h 1.0 (19.12.2025)
**
**      IFFPicture library structures and constants
**
*/

#ifndef IFFPICTURE_H
#define IFFPICTURE_H

/*****************************************************************************/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef DOS_DOS_H
#include <dos/dos.h>
#endif

/*****************************************************************************/

/* Forward declarations for opaque and external types */
struct IFFPicture;      /* Opaque structure - use accessor functions */
struct BitMapHeader;    /* Public structure defined below */
struct IFFColorMap;      /* Public structure defined below */
struct PNGConfig;        /* Defined in png_encoder.h from libpng */
/* Note: struct IFFHandle is defined in libraries/iffparse.h */

/*****************************************************************************/

/* MAKE_ID macro for creating IFF chunk identifiers */
#define MAKE_ID(a,b,c,d) \
        ((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))

/*****************************************************************************/

/* Factory Functions - following iffparse.library pattern
 *
 * AllocIFFPicture() - Creates and initializes a new IFFPicture structure.
 *                     This is the only supported way to create an IFFPicture
 *                     since there are private fields that need initialization.
 *                     Returns NULL if allocation fails.
 *
 * FreeIFFPicture() - Deallocates all resources associated with an IFFPicture
 *                    structure. The structure MUST have already been closed
 *                    with CloseIFFPicture(). Frees all allocated memory
 *                    including BMHD, CMAP, and pixel data.
 */
struct IFFPicture *AllocIFFPicture(VOID);
VOID FreeIFFPicture(struct IFFPicture *picture);

/*****************************************************************************/

/* Loading Functions - following iffparse.library pattern
 *
 * InitIFFPictureasDOS() - Initializes the IFFPicture to operate on DOS streams.
 *                         Allocates and initializes an internal IFFHandle structure.
 *                         The iff_Stream field must be set by the caller after
 *                         calling Open() to get a BPTR file handle.
 *
 * OpenIFFPicture() - Prepares an IFFPicture to read or write a new IFF stream.
 *                    The direction of I/O is given by rwMode (IFFF_READ or
 *                    IFFF_WRITE). The IFFPicture must have been initialized
 *                    with InitIFFPictureasDOS() and iff_Stream must be set
 *                    to a valid BPTR file handle before calling this function.
 *                    Returns 0 on success or an error code on failure.
 *
 * CloseIFFPicture() - Completes an IFF read or write operation by closing the
 *                    IFF context. The IFFHandle structure is freed. The file
 *                    handle (iff_Stream) is NOT closed - the caller is
 *                    responsible for closing it with Close(). This matches
 *                    iffparse.library behavior where CloseIFF() doesn't close
 *                    the file handle.
 *
 * ParseIFFPicture() - Parses the IFF file structure and reads property chunks
 *                    (BMHD, CMAP, CAMG, etc.) into the IFFPicture structure.
 *                    Must be called after OpenIFFPicture(). Returns 0 on
 *                    success or an error code on failure.
 */
VOID InitIFFPictureasDOS(struct IFFPicture *picture);
LONG OpenIFFPicture(struct IFFPicture *picture, LONG rwMode);
VOID CloseIFFPicture(struct IFFPicture *picture);
LONG ParseIFFPicture(struct IFFPicture *picture);

/*****************************************************************************/

/* Chunk Reading Functions - following iffparse.library pattern
 *
 * These functions read specific IFF chunks from an opened IFFPicture.
 * They use iffparse.library's FindProp() to locate stored property chunks
 * that were declared with PropChunk() during ParseIFFPicture().
 *
 * ReadBMHD() - Reads the BMHD (Bitmap Header) chunk and stores it in the
 *              IFFPicture structure. This chunk contains image dimensions,
 *              bitplane count, compression type, masking, and other metadata.
 *
 * ReadCMAP() - Reads the CMAP (Color Map) chunk containing palette data.
 *             The palette consists of RGB triplets (r, g, b, r, g, b, ...).
 *             For 4-bit palettes, colors are automatically scaled to 8-bit.
 *
 * ReadCAMG() - Reads the CAMG (Amiga Viewport Modes) chunk containing
 *              display mode flags such as HAM, EHB, LACE, HIRES, etc.
 *
 * ReadBODY() - Reads the BODY chunk header information. The BODY chunk
 *              contains the actual image pixel data. This function stores
 *              the chunk size and position for later reading during decoding.
 *
 * ReadABIT() - Reads the ABIT (Alpha Bitmap) chunk header information.
 *              Similar to ReadBODY() but for alpha channel data in ACBM
 *              format images.
 *
 * Metadata Chunk Reading Functions:
 *
 * ReadGRAB() - Reads the GRAB chunk (hotspot coordinates). Returns a pointer
 *              to a Point2D structure in IFFPicture's memory, or NULL if not
 *              found. Pointer is valid until FreeIFFPicture() is called.
 *              Library owns the memory - caller must NOT free.
 *
 * ReadDEST() - Reads the DEST chunk (destination merge information). Returns
 *              a pointer to a DestMerge structure in IFFPicture's memory, or
 *              NULL if not found. Pointer is valid until FreeIFFPicture() is
 *              called. Library owns the memory - caller must NOT free.
 *
 * ReadSPRT() - Reads the SPRT chunk (sprite precedence). Returns a pointer to
 *              a UWORD in IFFPicture's memory containing the precedence value,
 *              or NULL if not found. Precedence 0 is the highest (foremost).
 *              Pointer is valid until FreeIFFPicture() is called. Library owns
 *              the memory - caller must NOT free.
 *
 * ReadCRNG() - Reads the CRNG chunk (color range, first instance). Returns a
 *              pointer to a CRange structure in IFFPicture's memory, or NULL
 *              if not found. Multiple CRNG chunks can exist; this returns the
 *              first one. Pointer is valid until FreeIFFPicture() is called.
 *              Library owns the memory - caller must NOT free.
 *
 * ReadAllCRNG() - Reads all CRNG chunks. Returns a pointer to a CRangeList
 *                 structure containing count and array pointer into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadCopyright() - Reads the Copyright chunk. Returns a pointer to a
 *                   null-terminated string in IFFPicture's memory, or NULL if
 *                   not found. Pointer is valid until FreeIFFPicture() is
 *                   called. Library owns the memory - caller must NOT free.
 *
 * ReadAuthor() - Reads the AUTH chunk. Returns a pointer to a null-terminated
 *                string in IFFPicture's memory, or NULL if not found. Pointer
 *                is valid until FreeIFFPicture() is called. Library owns the
 *                memory - caller must NOT free.
 *
 * ReadAnnotation() - Reads the ANNO chunk (first instance). Returns a pointer
 *                    to a null-terminated string in IFFPicture's memory, or
 *                    NULL if not found. Multiple ANNO chunks can exist; this
 *                    returns the first one. Pointer is valid until
 *                    FreeIFFPicture() is called. Library owns the memory -
 *                    caller must NOT free.
 *
 * ReadAllAnnotations() - Reads all ANNO chunks. Returns a pointer to a
 *                        TextList structure containing count and array pointer
 *                        into IFFPicture's memory, or NULL if not found.
 *                        Pointers are valid until FreeIFFPicture() is called.
 *                        Library owns the memory - caller must NOT free.
 *
 * ReadText() - Reads the TEXT chunk (first instance). Returns a pointer to a
 *              null-terminated string in IFFPicture's memory, or NULL if not
 *              found. Multiple TEXT chunks can exist; this returns the first
 *              one. Pointer is valid until FreeIFFPicture() is called. Library
 *              owns the memory - caller must NOT free.
 *
 * ReadAllTexts() - Reads all TEXT chunks. Returns a pointer to a TextList
 *                  structure containing count and array pointer into
 *                  IFFPicture's memory, or NULL if not found. Pointers are
 *                  valid until FreeIFFPicture() is called. Library owns the
 *                  memory - caller must NOT free.
 *
 * Extended Metadata Chunk Reading Functions (IFF-EXIF/IPTC/XMP/ICCP/GeoTIFF):
 *
 * ReadEXIF() - Reads the EXIF chunk (first instance). Returns a pointer to
 *              EXIF data in IFFPicture's memory, or NULL if not found.
 *              EXIF data is raw binary data (same payload as APP1 JPEG EXIF
 *              markers). The size parameter is optional and will be set to
 *              the data size if provided. Pointer is valid until FreeIFFPicture()
 *              is called. Library owns the memory - caller must NOT free.
 *
 * ReadAllEXIF() - Reads all EXIF chunks. Returns a pointer to a BinaryDataList
 *                 structure containing count and array pointers into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadIPTC() - Reads the IPTC chunk (first instance). Returns a pointer to
 *              IPTC data in IFFPicture's memory, or NULL if not found.
 *              IPTC data is raw binary data (same payload as APP13 JPEG PS3
 *              marker, IPTC block only). The size parameter is optional and
 *              will be set to the data size if provided. Pointer is valid until
 *              FreeIFFPicture() is called. Library owns the memory - caller
 *              must NOT free.
 *
 * ReadAllIPTC() - Reads all IPTC chunks. Returns a pointer to a BinaryDataList
 *                 structure containing count and array pointers into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadXMP0() - Reads the XMP0 chunk (first instance). Returns a pointer to
 *              XMP0 data in IFFPicture's memory, or NULL if not found.
 *              XMP0 data is raw binary data (same payload as APP1 JPEG XMP
 *              markers, pure XML without header). Size limit 64K (inherent),
 *              i.e. 65502 bytes. The size parameter is optional and will be set
 *              to the data size if provided. Pointer is valid until
 *              FreeIFFPicture() is called. Library owns the memory - caller
 *              must NOT free.
 *
 * ReadAllXMP0() - Reads all XMP0 chunks. Returns a pointer to a BinaryDataList
 *                 structure containing count and array pointers into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadXMP1() - Reads the XMP1 chunk (single instance). Returns a pointer to
 *              XMP1 data in IFFPicture's memory, or NULL if not found.
 *              XMP1 data is raw binary data (same payload as 'tXMP' PNG chunk,
 *              pure XML without header). No significant size limit (2-4 GB).
 *              The size parameter is optional and will be set to the data size
 *              if provided. Pointer is valid until FreeIFFPicture() is called.
 *              Library owns the memory - caller must NOT free.
 *
 * ReadICCP() - Reads the ICCP chunk (first instance). Returns a pointer to
 *              ICC profile data in IFFPicture's memory, or NULL if not found.
 *              ICCP data is raw binary data (standard ICC profile embedded
 *              as-is). The size parameter is optional and will be set to the
 *              data size if provided. Pointer is valid until FreeIFFPicture()
 *              is called. Library owns the memory - caller must NOT free.
 *
 * ReadAllICCP() - Reads all ICCP chunks. Returns a pointer to a BinaryDataList
 *                 structure containing count and array pointers into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadICCN() - Reads the ICCN chunk (first instance). Returns a pointer to a
 *              null-terminated string in IFFPicture's memory, or NULL if not
 *              found. ICCN contains the name of the ICC profile. Pointer is
 *              valid until FreeIFFPicture() is called. Library owns the
 *              memory - caller must NOT free.
 *
 * ReadAllICCN() - Reads all ICCN chunks. Returns a pointer to a TextList
 *                 structure containing count and array pointer into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadGEOT() - Reads the GEOT chunk (first instance). Returns a pointer to
 *              GeoTIFF data in IFFPicture's memory, or NULL if not found.
 *              GEOT data is raw binary data (pure GeoTIFF file content, either
 *              starting with 'II' or 'MM'). The size parameter is optional and
 *              will be set to the data size if provided. Pointer is valid until
 *              FreeIFFPicture() is called. Library owns the memory - caller
 *              must NOT free.
 *
 * ReadAllGEOT() - Reads all GEOT chunks. Returns a pointer to a BinaryDataList
 *                 structure containing count and array pointers into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 *
 * ReadGEOF() - Reads the GEOF chunk (first instance). Returns a pointer to a
 *              ULONG in IFFPicture's memory containing a 4-byte chunk ID, or
 *              NULL if not found. GEOF content is a 4-byte chunk ID (ASCII)
 *              indicating the origin of GEOT data. Common values: 'JFIF', 'JP2K',
 *              'PNG ', 'TIFF', 'GEOT', 'RGFX', '    ' (unknown). Pointer is
 *              valid until FreeIFFPicture() is called. Library owns the memory -
 *              caller must NOT free.
 *
 * ReadAllGEOF() - Reads all GEOF chunks. Returns a pointer to a GEOFList
 *                 structure containing count and array pointer into IFFPicture's
 *                 memory, or NULL if not found. Pointers are valid until
 *                 FreeIFFPicture() is called. Library owns the memory - caller
 *                 must NOT free.
 */
LONG ReadBMHD(struct IFFPicture *picture);
LONG ReadCMAP(struct IFFPicture *picture);
LONG ReadCAMG(struct IFFPicture *picture);
LONG ReadBODY(struct IFFPicture *picture);
LONG ReadABIT(struct IFFPicture *picture);

/* Metadata chunk structures */
struct Point2D {
    WORD x, y;  /* relative coordinates (pixels) */
};

struct DestMerge {
    UBYTE depth;      /* # bitplanes in the original source */
    UBYTE pad1;       /* unused; for consistency put 0 here */
    UWORD planePick;  /* how to scatter source bitplanes into destination */
    UWORD planeOnOff; /* default bitplane data for planePick */
    UWORD planeMask;  /* selects which bitplanes to store into */
};

struct CRange {
    WORD  pad1;      /* reserved for future use; store 0 here */
    WORD  rate;      /* color cycle rate */
    WORD  flags;     /* flags: RNG_ACTIVE (1), RNG_REVERSE (2) */
    UBYTE low, high; /* lower and upper color registers selected */
};

/* CRNG flags */
#define RNG_ACTIVE  1
#define RNG_REVERSE 2

/* Metadata chunk list structures for multiple instances */
struct CRangeList {
    ULONG count;                /* Number of CRange entries */
    struct CRange *ranges;     /* Array of CRange structures */
};

struct TextList {
    ULONG count;                /* Number of text strings */
    STRPTR *texts;              /* Array of null-terminated strings */
};

struct BinaryDataList {
    ULONG count;                /* Number of binary data entries */
    UBYTE **data;               /* Array of pointers to binary data */
    ULONG *sizes;               /* Array of sizes for each data entry */
};

struct GEOFList {
    ULONG count;                /* Number of GEOF chunk IDs */
    ULONG *ids;                 /* Array of 4-byte chunk IDs (ULONG) */
};

/* Metadata chunk reading functions
 * 
 * All functions return pointers into IFFPicture's memory.
 * Pointers are valid until FreeIFFPicture() is called.
 * Library owns all memory - caller must NOT free.
 * 
 * For chunks that can appear multiple times (CRNG, ANNO, TEXT),
 * use ReadAllX() functions to get all instances.
 */
struct Point2D *ReadGRAB(struct IFFPicture *picture);
struct DestMerge *ReadDEST(struct IFFPicture *picture);
UWORD *ReadSPRT(struct IFFPicture *picture);
struct CRange *ReadCRNG(struct IFFPicture *picture);
struct CRangeList *ReadAllCRNG(struct IFFPicture *picture);
STRPTR ReadCopyright(struct IFFPicture *picture);
STRPTR ReadAuthor(struct IFFPicture *picture);
STRPTR ReadAnnotation(struct IFFPicture *picture);
struct TextList *ReadAllAnnotations(struct IFFPicture *picture);
STRPTR ReadText(struct IFFPicture *picture);
struct TextList *ReadAllTexts(struct IFFPicture *picture);

/* Extended metadata chunk reading functions (IFF-EXIF/IPTC/XMP/ICCP/GeoTIFF) */
UBYTE *ReadEXIF(struct IFFPicture *picture, ULONG *size);
struct BinaryDataList *ReadAllEXIF(struct IFFPicture *picture);
UBYTE *ReadIPTC(struct IFFPicture *picture, ULONG *size);
struct BinaryDataList *ReadAllIPTC(struct IFFPicture *picture);
UBYTE *ReadXMP0(struct IFFPicture *picture, ULONG *size);
struct BinaryDataList *ReadAllXMP0(struct IFFPicture *picture);
UBYTE *ReadXMP1(struct IFFPicture *picture, ULONG *size);
UBYTE *ReadICCP(struct IFFPicture *picture, ULONG *size);
struct BinaryDataList *ReadAllICCP(struct IFFPicture *picture);
STRPTR ReadICCN(struct IFFPicture *picture);
struct TextList *ReadAllICCN(struct IFFPicture *picture);
UBYTE *ReadGEOT(struct IFFPicture *picture, ULONG *size);
struct BinaryDataList *ReadAllGEOT(struct IFFPicture *picture);
ULONG *ReadGEOF(struct IFFPicture *picture);
struct GEOFList *ReadAllGEOF(struct IFFPicture *picture);

/*****************************************************************************/

/* Getter Functions - following iffparse.library pattern
 *
 * These functions provide read-only access to IFFPicture data. They return
 * values from the internal structure without exposing implementation details.
 *
 * GetIFFHandle() - Returns a pointer to the internal IFFHandle structure.
 *                 This allows the caller to set iff_Stream after calling
 *                 InitIFFPictureasDOS() and before calling OpenIFFPicture().
 *                 Returns NULL if the IFFPicture is not initialized.
 *
 * GetWidth/Height/Depth() - Return basic image dimensions and bitplane count
 *                           from the BMHD chunk. Return 0 if BMHD is not loaded.
 *
 * GetFormType() - Returns the IFF FORM type identifier (e.g., ID_ILBM, ID_PBM).
 *                 This identifies the image format variant.
 *
 * GetVPModes() - Returns the Amiga viewport mode flags from the CAMG chunk.
 *                The returned ULONG contains flags such as vmHAM, vmEXTRA_HALFBRITE,
 *                vmLACE, vmHIRES, etc. Returns 0 if CAMG chunk is not present.
 *                This is the raw CAMG value that can be used with BestPictureModeID()
 *                or for direct mode matching.
 *
 * GetBMHD() - Return pointer to the internal BMHD structure.
 *             This pointer remains valid until the IFFPicture is freed.
 *             Returns NULL if not loaded.
 *
 * GetYCHD() - Return pointer to the internal YCHD structure (YUVN format).
 *             This pointer remains valid until the IFFPicture is freed.
 *             Returns NULL if not loaded or not YUVN format.
 *
 * GetIFFColorMap() - Return pointer to the internal IFFColorMap structure
 *                    (palette data). This pointer remains valid until the
 *                    IFFPicture is freed. Returns NULL if not loaded.
 *                    Note: Renamed from GetColorMap to avoid conflict with
 *                    graphics.library function.
 *
 * GetPixelData() - Returns a pointer to the decoded pixel data buffer.
 *                  The data format depends on the image type (RGB, indexed, etc.).
 *                  Returns NULL if image has not been decoded.
 *
 * GetPixelDataSize() - Returns the size in bytes of the decoded pixel data.
 *                      This is useful for calculating buffer sizes.
 *
 * HasAlpha/IsHAM/IsEHB/IsCompressed() - Boolean queries about image properties.
 *                                       These are determined during format analysis.
 *
 * GetImageInfo() - Returns a pointer to an IFFImageInfo structure containing
 *                  all core image properties (dimensions, format, flags, etc.)
 *                  in a single structure. This is useful for getting a complete
 *                  overview of the image without making multiple function calls.
 *                  The structure is allocated statically and remains valid until
 *                  the next call to GetImageInfo() or until the IFFPicture is freed.
 *                  Returns NULL if the picture is invalid or not loaded.
 */
struct IFFHandle *GetIFFHandle(struct IFFPicture *picture);
UWORD GetWidth(struct IFFPicture *picture);
UWORD GetHeight(struct IFFPicture *picture);
UWORD GetDepth(struct IFFPicture *picture);
ULONG GetFormType(struct IFFPicture *picture);
ULONG GetVPModes(struct IFFPicture *picture);
UBYTE GetFAXXCompression(struct IFFPicture *picture);  /* Returns FAXX compression type (0=None, 1=MH, 2=MR, 4=MMR) */
struct BitMapHeader *GetBMHD(struct IFFPicture *picture);
struct YCHDHeader *GetYCHD(struct IFFPicture *picture);
struct IFFColorMap *GetIFFColorMap(struct IFFPicture *picture);
UBYTE *GetPixelData(struct IFFPicture *picture);
ULONG GetPixelDataSize(struct IFFPicture *picture);
BOOL HasAlpha(struct IFFPicture *picture);
BOOL IsHAM(struct IFFPicture *picture);
BOOL IsEHB(struct IFFPicture *picture);
BOOL IsCompressed(struct IFFPicture *picture);

/* IFFImageInfo structure - aggregate of core image properties */
struct IFFImageInfo {
    UWORD width;                /* Image width in pixels */
    UWORD height;               /* Image height in pixels */
    UWORD depth;                /* Number of bitplanes */
    ULONG formType;             /* IFF FORM type (ID_ILBM, ID_PBM, etc.) */
    ULONG viewportModes;        /* Amiga viewport mode flags (CAMG) */
    ULONG compressedSize;       /* Size of compressed image data in bytes (BODY chunk size, 0 if not loaded) */
    ULONG decodedSize;          /* Size of decoded pixel data in bytes (0 if not decoded) */
    BOOL hasAlpha;              /* TRUE if image has alpha channel */
    BOOL isHAM;                 /* TRUE if image uses HAM mode */
    BOOL isEHB;                 /* TRUE if image uses Extra Half-Brite mode */
    BOOL isCompressed;          /* TRUE if image data is compressed */
    BOOL isIndexed;             /* TRUE if image uses indexed color (palette) */
    BOOL isGrayscale;           /* TRUE if image is grayscale */
    BOOL isLoaded;              /* TRUE if image has been loaded/parsed */
    BOOL isDecoded;             /* TRUE if image has been decoded */
};

struct IFFImageInfo *GetImageInfo(struct IFFPicture *picture);

/*****************************************************************************/

/* Decoding Functions
 *
 * Decode() - Decodes the IFF image data into an internal format suitable
 *            for further processing. Handles all IFF formats (ILBM, PBM,
 *            RGBN, RGB8, DEEP, ACBM, FAXX) and compression methods
 *            (ByteRun1, Modified Huffman, Modified READ, etc.). Also handles
 *            special modes like HAM and EHB. Must be called after ParseIFFPicture().
 *            Returns 0 on success or an error code on failure.
 *
 * DecodeToRGB() - Decodes the IFF image and converts it to RGB format.
 *                Allocates a buffer containing 24-bit RGB data (r, g, b, r, g, b, ...)
 *                suitable for writing to PNG or other RGB-based formats.
 *                The caller is responsible for freeing the returned buffer.
 *                Returns 0 on success or an error code on failure.
 *                Note: The returned rgbData may point to picture->pixelData,
 *                which is freed by FreeIFFPicture(). Do not free it separately.
 *
 * DecodeToBitMap() - Decodes the IFF image and creates an Amiga BitMap structure
 *                    ready for display. The BitMap is allocated using AllocBitMap()
 *                    with the specified modeID. Supports both planar (bitplane) and
 *                    chunky (8-bit per pixel) display modes.
 *                    
 *                    For planar modes: Converts RGB data directly to bitplanes.
 *                    For chunky modes: Uses WriteChunkyPixels() if available (V40+),
 *                    otherwise falls back to direct conversion.
 *                    
 *                    The caller is responsible for freeing the BitMap using
 *                    FreeBitMap() when no longer needed.
 *                    
 *                    Parameters:
 *                    - picture: IFFPicture structure (must be decoded)
 *                    - modeID: Display mode ID (use BestPictureModeID() to find)
 *                    - friendBitmap: Optional friend bitmap for efficient allocation
 *                      (NULL if not needed)
 *                    
 *                    Returns: Pointer to allocated BitMap, or NULL on failure.
 *                    Must be called after Decode() or DecodeToRGB().
 *
 * DecodeToRastPort() - Decodes the IFF image and creates an off-screen RastPort
 *                      with attached BitMap. This is a convenience function that
 *                      calls DecodeToBitMap() and then initializes a RastPort.
 *                      
 *                      The RastPort is allocated and initialized with InitRastPort().
 *                      The BitMap is attached and ready for rendering operations.
 *                      
 *                      The caller is responsible for freeing the RastPort using
 *                      FreeRastPort() when no longer needed. This will also free
 *                      the attached BitMap.
 *                      
 *                      Parameters:
 *                      - picture: IFFPicture structure (must be decoded)
 *                      - modeID: Display mode ID (use BestPictureModeID() to find)
 *                      - friendBitmap: Optional friend bitmap for efficient allocation
 *                        (NULL if not needed)
 *                      
 *                      Returns: Pointer to allocated RastPort, or NULL on failure.
 *                      Must be called after Decode() or DecodeToRGB().
 *
 * FreeRastPort() - Frees a RastPort and its attached BitMap created by
 *                  DecodeToRastPort(). This function properly cleans up both
 *                  the RastPort structure and the BitMap memory.
 *                  
 *                  Parameters:
 *                  - rp: RastPort to free (must have been created by DecodeToRastPort())
 */
LONG Decode(struct IFFPicture *picture);
LONG DecodeToRGB(struct IFFPicture *picture, UBYTE **rgbData, ULONG *size);
struct BitMap *DecodeToBitMap(struct IFFPicture *picture, ULONG modeID, struct BitMap *friendBitmap);
struct RastPort *DecodeToRastPort(struct IFFPicture *picture, ULONG modeID, struct BitMap *friendBitmap);
VOID FreeRastPort(struct RastPort *rp);

/*****************************************************************************/

/* Analysis Functions
 *
 * AnalyzeFormat() - Analyzes the loaded IFF image to determine its properties
 *                   such as color type (indexed, grayscale, RGB), special modes
 *                   (HAM, EHB), compression, and alpha channel presence.
 *                   This information is used to optimize PNG encoding.
 *                   Must be called after ParseIFFPicture() and before decoding.
 *                   Returns 0 on success or an error code on failure.
 *
 * GetOptimalPNGConfig() - Determines the optimal PNG encoding configuration
 *                         for the IFF image. This includes color type, bit depth,
 *                         palette (if indexed), and transparency settings.
 *                         The configuration is stored in the provided PNGConfig
 *                         structure. The caller is responsible for freeing any
 *                         allocated palette or transparency data using
 *                         PNGEncoder_FreeConfig(). Must be called after
 *                         AnalyzeFormat() and before DecodeToRGB().
 *                         
 *                         The opaque parameter controls transparency handling:
 *                         - If FALSE (default): Honors transparentColor from BMHD
 *                           when masking == mskHasTransparentColor, including
 *                           index 0 (per ILBM specification).
 *                         - If TRUE: Skips transparency for index 0 to keep
 *                           black visible (legacy behavior).
 *                         
 *                         Returns 0 on success or an error code on failure.
 *
 * BestPictureModeID() - Determines the best Amiga screenmode for displaying
 *                      the IFF image. Uses graphics.library BestModeIDA() to
 *                      find a ModeID that matches the image properties.
 *                      
 *                      If a CAMG chunk is present, it uses those viewport
 *                      mode flags (HAM, EHB, LACE, HIRES) as requirements.
 *                      If no CAMG chunk exists, it infers requirements from
 *                      the image properties (width, height, depth).
 *                      
 *                      The function takes optional parameters:
 *                      - sourceViewPort: If provided, matches to the same
 *                        monitor type as this ViewPort
 *                      - sourceModeID: If provided, uses this ModeID as
 *                        the source for matching (alternative to ViewPort)
 *                      - monitorID: If provided, restricts search to this
 *                        specific monitor
 *                      
 *                      Returns a ModeID on success, or INVALID_ID if no
 *                      matching screenmode could be found. Must be called
 *                      after ParseIFFPicture(). The returned ModeID can be
 *                      used with OpenScreenTagList() or similar functions.
 */
LONG AnalyzeFormat(struct IFFPicture *picture);
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config, BOOL opaque);
ULONG BestPictureModeID(struct IFFPicture *picture, struct ViewPort *sourceViewPort, ULONG sourceModeID, ULONG monitorID);

/*****************************************************************************/

/* Error Handling Functions
 *
 * GetLastError() - Returns the last error code that occurred during an
 *                  IFFPicture operation. Error codes are negative values
 *                  (IFFPICTURE_* constants) or 0 for success.
 *
 * GetErrorString() - Returns a pointer to a null-terminated string describing
 *                    the last error. The string is stored in the IFFPicture
 *                    structure and remains valid until the next operation
 *                    or until the IFFPicture is freed.
 */
LONG GetLastError(struct IFFPicture *picture);
const char *GetErrorString(struct IFFPicture *picture);

/*****************************************************************************/

/* BitMapHeader structure - public
 *
 * This structure represents the IFF BMHD (Bitmap Header) chunk. It contains
 * all the metadata needed to interpret the image data, including dimensions,
 * bitplane count, compression method, masking technique, and display hints.
 *
 * Note: The structure must match the IFF BMHD chunk layout exactly (20 bytes,
 * byte-packed) to allow direct reading from IFF files. Field order and types
 * are critical for correct parsing.
 */
struct BitMapHeader {
    UWORD w, h;             /* raster width & height in pixels */
    WORD x, y;              /* pixel position for this image (usually 0,0) */
    UBYTE nPlanes;          /* # source bitplanes (1-8 for standard images) */
    UBYTE masking;          /* masking technique (use msk* constants below) */
    UBYTE compression;      /* compression algorithm:
                             *   0 = none
                             *   1 = ByteRun1 (RLE) */
    UBYTE pad1;             /* unused; ignore on read, write as 0 */
    UWORD transparentColor; /* transparent "color number" (palette index)
                             *   only valid if masking == mskHasTransparentColor */
    UBYTE xAspect, yAspect; /* pixel aspect ratio (width : height)
                             *   typically 10:11 for standard Amiga displays */
    WORD pageWidth, pageHeight; /* source "page" size in pixels
                                 *   usually matches w, h but may be larger */
};

/* Masking type constants for BitMapHeader.masking field */
#define mskNone                 0  /* No masking */
#define mskHasMask              1  /* Has separate mask plane */
#define mskHasTransparentColor  2  /* Uses transparentColor palette index */
#define mskLasso                3  /* Lasso masking (not commonly used) */

/*****************************************************************************/

/* IFFColorMap structure - public
 *
 * This structure represents the IFF CMAP (Color Map) chunk containing
 * palette data. The palette consists of RGB triplets stored sequentially
 * in the data buffer. For standard 8-bit palettes, each color uses 3 bytes
 * (r, g, b). For 4-bit palettes, colors are stored as 3 bytes but may need
 * scaling (indicated by is4Bit flag).
 *
 * The numcolors field indicates how many palette entries are present.
 * Standard palettes have 2^nPlanes colors (e.g., 16 colors for 4 planes,
 * 256 colors for 8 planes).
 *
 * Note: Renamed from ColorMap to IFFColorMap to avoid conflict with
 * graphics/view.h which also defines a ColorMap structure.
 */
struct IFFColorMap {
    UBYTE *data;        /* RGB triplets (r, g, b, r, g, b, ...)
                         *   Allocated by the library, freed by FreeIFFPicture() */
    ULONG numcolors;    /* Number of palette entries (colors) */
    BOOL is4Bit;        /* TRUE if 4-bit palette needs scaling to 8-bit
                         *   (4-bit palettes use 0-15 range, need *17 to get 0-255) */
};

/*****************************************************************************/

/* YCHD Header structure - public
 *
 * This structure represents the IFF YCHD (YUVN Header) chunk. It contains
 * all the metadata needed to interpret YUV image data, including dimensions,
 * YUV mode, compression, and TV system information.
 *
 * Note: The structure must match the IFF YCHD chunk layout exactly (24 bytes,
 * byte-packed) to allow direct reading from IFF files. Field order and types
 * are critical for correct parsing.
 */
struct YCHDHeader {
    UWORD ychd_Width;        /* picture width in Y-pixels */
    UWORD ychd_Height;       /* picture height (rows) */
    UWORD ychd_PageWidth;    /* source page width & height */
    UWORD ychd_PageHeight;   /* currently same as Width and Height */
    UWORD ychd_LeftEdge;     /* position within the source page */
    UWORD ychd_TopEdge;      /* currently 0,0 */
    UBYTE ychd_AspectX;      /* pixel aspect (width : height) */
    UBYTE ychd_AspectY;
    UBYTE ychd_Compress;     /* compression type (0 = none) */
    UBYTE ychd_Flags;        /* flags (bit 0 = LACE) */
    UBYTE ychd_Mode;         /* YUV mode (see YCHD_MODE_* constants) */
    UBYTE ychd_Norm;         /* TV system (see YCHD_NORM_* constants) */
    WORD ychd_reserved2;     /* must be 0 */
    LONG ychd_reserved3;      /* must be 0 */
};

/* YUVN Mode constants for YCHDHeader.ychd_Mode field */
#define YCHD_MODE_400    0  /* black-and-white picture (no DATU and DATV) */
#define YCHD_MODE_411    1  /* YUV-411 picture */
#define YCHD_MODE_422    2  /* YUV-422 picture */
#define YCHD_MODE_444    3  /* YUV-444 picture */
#define YCHD_MODE_200    8  /* lores black-and-white picture */
#define YCHD_MODE_211    9  /* lores color picture (422, but lores) */
#define YCHD_MODE_222   10  /* lores color picture (444, but lores) */

/* YUVN Norm constants for YCHDHeader.ychd_Norm field */
#define YCHD_NORM_UNKNOWN  0  /* unknown, try to avoid this */
#define YCHD_NORM_PAL      1  /* PAL 4.433 MHz */
#define YCHD_NORM_NTSC     2  /* NTSC 3.579 MHz */

/* YUVN Compression constants for YCHDHeader.ychd_Compress field */
#define YCHD_COMPRESS_NONE  0  /* no compression */

/* YUVN Flags constants for YCHDHeader.ychd_Flags field */
#define YCHDF_LACE  1  /* if set the data-chunks contain a full-frame (interlaced) picture */

/*****************************************************************************/

/* IFF Form Type IDs
 *
 * These constants identify the different IFF image format variants.
 * ID_FORM is defined in iffparse.h and is the container for all these types.
 *
 * ID_ILBM - InterLeaved BitMap: Standard Amiga bitmap format with interleaved
 *           bitplanes. Supports HAM, EHB, and various bitplane counts.
 *
 * ID_PBM  - Packed BitMap: Similar to ILBM but with packed pixels (one byte
 *           per pixel) instead of bitplanes. Simpler format, less common.
 *
 * ID_RGBN - RGB with N planes: True-color format with separate RGB channels.
 *           N indicates the number of bits per channel.
 *
 * ID_RGB8 - RGB 8-bit: True-color format with 8 bits per RGB channel
 *           (24-bit color, 32-bit with alpha).
 *
 * ID_DEEP - Deep format: High bit-depth format for professional graphics.
 *           Supports more than 8 bits per channel.
 *
 * ID_ACBM - Amiga Continuous BitMap: Format with separate alpha channel data
 *           in an ABIT chunk. Similar to ILBM but with transparency support.
 *
 * ID_FAXX - Facsimile Image: Fax image format using ITU-T T.4 compression
 *           (Modified Huffman, Modified READ, Modified Modified READ).
 *           Typically black and white images for fax transmission.
 *
 * ID_YUVN - YUV format: MacroSystem VLab YUV format for broadcast television.
 *           Supports CCIR-601-2 standard for PAL and NTSC. Stores Y (luminance)
 *           and optional U, V (color difference) channels. Supports various
 *           subsampling modes (411, 422, 444) and grayscale (400).
 */
#define ID_ILBM    MAKE_ID('I','L','B','M')  /* InterLeaved BitMap */
#define ID_PBM     MAKE_ID('P','B','M',' ')  /* Packed BitMap */
#define ID_RGBN    MAKE_ID('R','G','B','N')  /* RGB with N planes */
#define ID_RGB8    MAKE_ID('R','G','B','8')  /* RGB 8-bit */
#define ID_DEEP    MAKE_ID('D','E','E','P')  /* Deep format */
#define ID_ACBM    MAKE_ID('A','C','B','M')  /* Amiga Continuous BitMap */
#define ID_FAXX    MAKE_ID('F','A','X','X')  /* Facsimile Image */
#define ID_YUVN    MAKE_ID('Y','U','V','N')  /* YUV format (MacroSystem VLab) */

/*****************************************************************************/

/* Error codes
 *
 * These constants represent error conditions that can occur during IFFPicture
 * operations. Functions return 0 for success or one of these negative values
 * for errors. Use GetErrorString() to get a human-readable error message.
 *
 * IFFPICTURE_OK          - Operation completed successfully
 * IFFPICTURE_ERROR       - General error (check GetErrorString() for details)
 * IFFPICTURE_NOMEM       - Memory allocation failed
 * IFFPICTURE_BADFILE     - File I/O error or invalid IFF file structure
 * IFFPICTURE_UNSUPPORTED - Image format or feature not supported
 * IFFPICTURE_INVALID     - Invalid operation or uninitialized structure
 */
#define IFFPICTURE_OK           0
#define IFFPICTURE_ERROR       -1
#define IFFPICTURE_NOMEM       -2
#define IFFPICTURE_BADFILE     -3
#define IFFPICTURE_UNSUPPORTED -4
#define IFFPICTURE_INVALID     -5

/*****************************************************************************/

#endif /* IFFPICTURELIB_IFFPICTURE_H */
