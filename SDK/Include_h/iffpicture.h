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
struct ColorMap;        /* Public structure defined below */
struct PNGConfig;       /* Defined in png_encoder.h from libpng */
struct IFFHandle;       /* Defined in libraries/iffparse.h */

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
 */
LONG ReadBMHD(struct IFFPicture *picture);
LONG ReadCMAP(struct IFFPicture *picture);
LONG ReadCAMG(struct IFFPicture *picture);
LONG ReadBODY(struct IFFPicture *picture);
LONG ReadABIT(struct IFFPicture *picture);

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
 * GetViewportModes() - Returns the Amiga viewport mode flags from the CAMG
 *                      chunk (e.g., HAM, EHB, LACE). Returns 0 if CAMG not present.
 *
 * GetBMHD/ColorMap() - Return pointers to the internal BMHD and ColorMap
 *                      structures. These pointers remain valid until the
 *                      IFFPicture is freed. Returns NULL if not loaded.
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
 */
struct IFFHandle *GetIFFHandle(struct IFFPicture *picture);
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
 */
LONG Decode(struct IFFPicture *picture);
LONG DecodeToRGB(struct IFFPicture *picture, UBYTE **rgbData, ULONG *size);

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
 *                         Returns 0 on success or an error code on failure.
 */
LONG AnalyzeFormat(struct IFFPicture *picture);
LONG GetOptimalPNGConfig(struct IFFPicture *picture, struct PNGConfig *config);

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
    UBYTE masking;          /* masking technique:
                             *   0 = none
                             *   1 = has mask plane
                             *   2 = transparent color
                             *   3 = lasso (not commonly used) */
    UBYTE compression;      /* compression algorithm:
                             *   0 = none
                             *   1 = ByteRun1 (RLE) */
    UBYTE pad1;             /* unused; ignore on read, write as 0 */
    UWORD transparentColor; /* transparent "color number" (palette index)
                             *   only valid if masking == 2 */
    UBYTE xAspect, yAspect; /* pixel aspect ratio (width : height)
                             *   typically 10:11 for standard Amiga displays */
    WORD pageWidth, pageHeight; /* source "page" size in pixels
                                 *   usually matches w, h but may be larger */
};

/*****************************************************************************/

/* ColorMap structure - public
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
 */
struct ColorMap {
    UBYTE *data;        /* RGB triplets (r, g, b, r, g, b, ...)
                         *   Allocated by the library, freed by FreeIFFPicture() */
    ULONG numcolors;    /* Number of palette entries (colors) */
    BOOL is4Bit;        /* TRUE if 4-bit palette needs scaling to 8-bit
                         *   (4-bit palettes use 0-15 range, need *17 to get 0-255) */
};

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
 */
#define ID_ILBM    MAKE_ID('I','L','B','M')  /* InterLeaved BitMap */
#define ID_PBM     MAKE_ID('P','B','M',' ')  /* Packed BitMap */
#define ID_RGBN    MAKE_ID('R','G','B','N')  /* RGB with N planes */
#define ID_RGB8    MAKE_ID('R','G','B','8')  /* RGB 8-bit */
#define ID_DEEP    MAKE_ID('D','E','E','P')  /* Deep format */
#define ID_ACBM    MAKE_ID('A','C','B','M')  /* Amiga Continuous BitMap */
#define ID_FAXX    MAKE_ID('F','A','X','X')  /* Facsimile Image */

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
