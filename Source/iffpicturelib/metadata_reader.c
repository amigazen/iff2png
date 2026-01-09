/*
** metadata_reader.c - Metadata Chunk Reader Implementation (Internal to Library)
**
** Functions for reading IFF metadata chunks (GRAB, DEST, SPRT, CRNG, text chunks)
** All memory is owned by IFFPicture and freed by FreeIFFPicture()
** Pointers are valid until FreeIFFPicture() is called
*/

#include "iffpicture_private.h"
#include "iffpicture.h"  /* For struct definitions */
#include "/debug.h"
#include <proto/exec.h>

/*
** ReadGRAB - Read GRAB chunk (hotspot coordinates)
** Returns: Pointer to Point2D structure in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct Point2D *ReadGRAB(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored GRAB data */
    return picture->metadata->grab;
}

/*
** ReadDEST - Read DEST chunk (destination merge)
** Returns: Pointer to DestMerge structure in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct DestMerge *ReadDEST(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored DEST data */
    return picture->metadata->dest;
}

/*
** ReadSPRT - Read SPRT chunk (sprite precedence)
** Returns: Pointer to UWORD in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** Precedence 0 is the highest (foremost)
*/
UWORD *ReadSPRT(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored SPRT data */
    return picture->metadata->sprt;
}

/*
** ReadCRNG - Read CRNG chunk (color range, first instance)
** Returns: Pointer to CRange structure in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** Note: Multiple CRNG chunks can exist; this returns the first one
*/
struct CRange *ReadCRNG(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to first CRNG instance */
    return picture->metadata->crng;
}

/*
** ReadAllCRNG - Read all CRNG chunks
** Returns: Pointer to CRangeList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct CRangeList *ReadAllCRNG(struct IFFPicture *picture)
{
    static struct CRangeList result;
    
    if (!picture || !picture->metadata || picture->metadata->crngCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->crngCount;
    result.ranges = picture->metadata->crngArray;
    
    return &result;
}

/*
** ReadCopyright - Read Copyright chunk
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
STRPTR ReadCopyright(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored Copyright string */
    return picture->metadata->copyright;
}

/*
** ReadAuthor - Read AUTH chunk
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
STRPTR ReadAuthor(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored Author string */
    return picture->metadata->author;
}

/*
** ReadAnnotation - Read ANNO chunk (first instance)
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** Note: Multiple ANNO chunks can exist; this returns the first one
*/
STRPTR ReadAnnotation(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to first ANNO instance */
    return picture->metadata->annotation;
}

/*
** ReadAllAnnotations - Read all ANNO chunks
** Returns: Pointer to TextList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct TextList *ReadAllAnnotations(struct IFFPicture *picture)
{
    static struct TextList result;
    
    if (!picture || !picture->metadata || picture->metadata->annotationCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->annotationCount;
    result.texts = picture->metadata->annotationArray;
    
    return &result;
}

/*
** ReadText - Read TEXT chunk (first instance)
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** Note: Multiple TEXT chunks can exist; this returns the first one
*/
STRPTR ReadText(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to first TEXT instance */
    return picture->metadata->text;
}

/*
** ReadAllTexts - Read all TEXT chunks
** Returns: Pointer to TextList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct TextList *ReadAllTexts(struct IFFPicture *picture)
{
    static struct TextList result;
    
    if (!picture || !picture->metadata || picture->metadata->textCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->textCount;
    result.texts = picture->metadata->textArray;
    
    return &result;
}

/*
** ReadFVER - Read FVER chunk
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** FVER contains an AmigaOS version string in the format: $VER: name ver.rev
** Example: $VER: workbench.catalog 53.12
*/
STRPTR ReadFVER(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored FVER string */
    return picture->metadata->fver;
}

/*
** ReadEXIF - Read EXIF chunk (first instance)
** Returns: Pointer to EXIF data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** EXIF data is raw binary data (same payload as APP1 JPEG EXIF markers)
*/
UBYTE *ReadEXIF(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->exifSize;
    }
    
    /* Return pointer to stored EXIF data */
    return picture->metadata->exif;
}

/*
** ReadAllEXIF - Read all EXIF chunks
** Returns: Pointer to BinaryDataList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct BinaryDataList *ReadAllEXIF(struct IFFPicture *picture)
{
    static struct BinaryDataList result;
    
    if (!picture || !picture->metadata || picture->metadata->exifCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->exifCount;
    result.data = picture->metadata->exifArray;
    result.sizes = picture->metadata->exifSizes;
    
    return &result;
}

/*
** ReadIPTC - Read IPTC chunk (first instance)
** Returns: Pointer to IPTC data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** IPTC data is raw binary data (same payload as APP13 JPEG PS3 marker, IPTC block only)
*/
UBYTE *ReadIPTC(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->iptcSize;
    }
    
    /* Return pointer to stored IPTC data */
    return picture->metadata->iptc;
}

/*
** ReadAllIPTC - Read all IPTC chunks
** Returns: Pointer to BinaryDataList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct BinaryDataList *ReadAllIPTC(struct IFFPicture *picture)
{
    static struct BinaryDataList result;
    
    if (!picture || !picture->metadata || picture->metadata->iptcCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->iptcCount;
    result.data = picture->metadata->iptcArray;
    result.sizes = picture->metadata->iptcSizes;
    
    return &result;
}

/*
** ReadXMP0 - Read XMP0 chunk (first instance)
** Returns: Pointer to XMP0 data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** XMP0 data is raw binary data (same payload as APP1 JPEG XMP markers, pure XML without header)
** Size limit 64K (inherent), i.e. 65502 bytes
*/
UBYTE *ReadXMP0(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->xmp0Size;
    }
    
    /* Return pointer to stored XMP0 data */
    return picture->metadata->xmp0;
}

/*
** ReadAllXMP0 - Read all XMP0 chunks
** Returns: Pointer to BinaryDataList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct BinaryDataList *ReadAllXMP0(struct IFFPicture *picture)
{
    static struct BinaryDataList result;
    
    if (!picture || !picture->metadata || picture->metadata->xmp0Count == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->xmp0Count;
    result.data = picture->metadata->xmp0Array;
    result.sizes = picture->metadata->xmp0Sizes;
    
    return &result;
}

/*
** ReadXMP1 - Read XMP1 chunk (single instance)
** Returns: Pointer to XMP1 data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** XMP1 data is raw binary data (same payload as 'tXMP' PNG chunk, pure XML without header)
** No significant size limit (2-4 GB)
*/
UBYTE *ReadXMP1(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->xmp1Size;
    }
    
    /* Return pointer to stored XMP1 data */
    return picture->metadata->xmp1;
}

/*
** ReadICCP - Read ICCP chunk (first instance)
** Returns: Pointer to ICC profile data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** ICCP data is raw binary data (standard ICC profile embedded as-is)
*/
UBYTE *ReadICCP(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->iccpSize;
    }
    
    /* Return pointer to stored ICCP data */
    return picture->metadata->iccp;
}

/*
** ReadAllICCP - Read all ICCP chunks
** Returns: Pointer to BinaryDataList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct BinaryDataList *ReadAllICCP(struct IFFPicture *picture)
{
    static struct BinaryDataList result;
    
    if (!picture || !picture->metadata || picture->metadata->iccpCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->iccpCount;
    result.data = picture->metadata->iccpArray;
    result.sizes = picture->metadata->iccpSizes;
    
    return &result;
}

/*
** ReadICCN - Read ICCN chunk (first instance)
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** ICCN contains the name of the ICC profile
*/
STRPTR ReadICCN(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored ICCN string */
    return picture->metadata->iccn;
}

/*
** ReadAllICCN - Read all ICCN chunks
** Returns: Pointer to TextList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct TextList *ReadAllICCN(struct IFFPicture *picture)
{
    static struct TextList result;
    
    if (!picture || !picture->metadata || picture->metadata->iccnCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->iccnCount;
    result.texts = picture->metadata->iccnArray;
    
    return &result;
}

/*
** ReadGEOT - Read GEOT chunk (first instance)
** Returns: Pointer to GeoTIFF data in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** GEOT data is raw binary data (pure GeoTIFF file content, either starting with 'II' or 'MM')
*/
UBYTE *ReadGEOT(struct IFFPicture *picture, ULONG *size)
{
    if (!picture || !picture->metadata) {
        if (size) {
            *size = 0;
        }
        return NULL;
    }
    
    if (size) {
        *size = picture->metadata->geotSize;
    }
    
    /* Return pointer to stored GEOT data */
    return picture->metadata->geot;
}

/*
** ReadAllGEOT - Read all GEOT chunks
** Returns: Pointer to BinaryDataList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct BinaryDataList *ReadAllGEOT(struct IFFPicture *picture)
{
    static struct BinaryDataList result;
    
    if (!picture || !picture->metadata || picture->metadata->geotCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->geotCount;
    result.data = picture->metadata->geotArray;
    result.sizes = picture->metadata->geotSizes;
    
    return &result;
}

/*
** ReadGEOF - Read GEOF chunk (first instance)
** Returns: Pointer to ULONG in IFFPicture containing 4-byte chunk ID, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
** GEOF content is a 4-byte chunk ID (ASCII) indicating the origin of GEOT data
** Common values: 'JFIF', 'JP2K', 'PNG ', 'TIFF', 'GEOT', 'RGFX', '    ' (unknown)
*/
ULONG *ReadGEOF(struct IFFPicture *picture)
{
    if (!picture || !picture->metadata) {
        return NULL;
    }
    
    /* Return pointer to stored GEOF chunk ID */
    return picture->metadata->geof;
}

/*
** ReadAllGEOF - Read all GEOF chunks
** Returns: Pointer to GEOFList structure, or NULL if not found
** The structure contains count and array pointer into IFFPicture's memory
** Pointers are valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct GEOFList *ReadAllGEOF(struct IFFPicture *picture)
{
    static struct GEOFList result;
    
    if (!picture || !picture->metadata || picture->metadata->geofCount == 0) {
        return NULL;
    }
    
    result.count = picture->metadata->geofCount;
    result.ids = picture->metadata->geofArray;
    
    return &result;
}
