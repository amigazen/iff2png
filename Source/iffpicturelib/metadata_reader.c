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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to stored GRAB data */
    return picture->grab;
}

/*
** ReadDEST - Read DEST chunk (destination merge)
** Returns: Pointer to DestMerge structure in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
struct DestMerge *ReadDEST(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to stored DEST data */
    return picture->dest;
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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to stored SPRT data */
    return picture->sprt;
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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to first CRNG instance */
    return picture->crng;
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
    
    if (!picture || picture->crngCount == 0) {
        return NULL;
    }
    
    result.count = picture->crngCount;
    result.ranges = picture->crngArray;
    
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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to stored Copyright string */
    return picture->copyright;
}

/*
** ReadAuthor - Read AUTH chunk
** Returns: Pointer to null-terminated string in IFFPicture, or NULL if not found
** Pointer is valid until FreeIFFPicture() is called
** Library owns the memory - caller must NOT free
*/
STRPTR ReadAuthor(struct IFFPicture *picture)
{
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to stored Author string */
    return picture->author;
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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to first ANNO instance */
    return picture->annotation;
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
    
    if (!picture || picture->annotationCount == 0) {
        return NULL;
    }
    
    result.count = picture->annotationCount;
    result.texts = picture->annotationArray;
    
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
    if (!picture) {
        return NULL;
    }
    
    /* Return pointer to first TEXT instance */
    return picture->text;
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
    
    if (!picture || picture->textCount == 0) {
        return NULL;
    }
    
    result.count = picture->textCount;
    result.texts = picture->textArray;
    
    return &result;
}
