/*
** bitmap_renderer.c - BitMap and RastPort Creation Implementation
**
** Creates Amiga BitMap and RastPort structures from decoded IFF images
** Supports both planar (bitplane) and chunky (8-bit per pixel) modes
*/

#include "iffpicture_private.h"
#include "/debug.h"
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>

/* Forward declarations */
static LONG ConvertRGBToBitPlanes(struct IFFPicture *picture, struct BitMap *bitmap);
static LONG ConvertRGBToChunky(struct IFFPicture *picture, struct BitMap *bitmap, struct RastPort *rp);

/*
** ConvertRGBToBitPlanes - Convert RGB data to bitplane format
** Writes RGB pixel data directly into bitplanes
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
static LONG ConvertRGBToBitPlanes(struct IFFPicture *picture, struct BitMap *bitmap)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UWORD row, col, plane;
    UBYTE *rgbData;
    UBYTE *cmapData;
    ULONG numColors;
    UBYTE pixelIndex;
    UBYTE bitMask;
    ULONG rgbIndex;
    UBYTE *planePtr;
    ULONG i;
    
    if (!picture || !bitmap || !picture->pixelData || !picture->bmhd) {
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = bitmap->Depth;
    rgbData = picture->pixelData;
    rowBytes = bitmap->BytesPerRow;
    
    /* Get palette if available */
    cmapData = NULL;
    numColors = 0;
    if (picture->cmap && picture->cmap->data) {
        cmapData = picture->cmap->data;
        numColors = picture->cmap->numcolors;
    }
    
    /* Clear all bitplanes first */
    for (plane = 0; plane < depth; plane++) {
        if (bitmap->Planes[plane]) {
            for (row = 0; row < height; row++) {
                planePtr = (UBYTE *)((ULONG)bitmap->Planes[plane] + (ULONG)row * rowBytes);
                for (i = 0; i < rowBytes; i++) {
                    planePtr[i] = 0;
                }
            }
        }
    }
    
    /* Convert RGB to bitplanes */
    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            rgbIndex = (ULONG)row * width * 3 + (ULONG)col * 3;
            
            /* Get RGB values */
            {
                UBYTE r, g, b;
                r = rgbData[rgbIndex];
                g = rgbData[rgbIndex + 1];
                b = rgbData[rgbIndex + 2];
                
                /* Find closest palette match if palette available */
                if (cmapData && numColors > 0) {
                    ULONG bestMatch;
                    ULONG bestDist;
                    ULONG dist;
                    ULONG j;
                    LONG dr, dg, db;
                    
                    bestMatch = 0;
                    bestDist = 0xFFFFFFFFUL;
                    
                    for (j = 0; j < numColors; j++) {
                        UBYTE pr, pg, pb;
                        pr = cmapData[j * 3];
                        pg = cmapData[j * 3 + 1];
                        pb = cmapData[j * 3 + 2];
                        
                        /* Handle 4-bit palette scaling */
                        if (picture->cmap->is4Bit) {
                            pr |= (pr >> 4);
                            pg |= (pg >> 4);
                            pb |= (pb >> 4);
                        }
                        
                        dr = (LONG)r - (LONG)pr;
                        dg = (LONG)g - (LONG)pg;
                        db = (LONG)b - (LONG)pb;
                        dist = (ULONG)(dr * dr + dg * dg + db * db);
                        
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestMatch = j;
                        }
                    }
                    
                    pixelIndex = (UBYTE)bestMatch;
                } else {
                    /* No palette - convert RGB to index based on depth */
                    /* Simple quantization */
                    {
                        UBYTE rBits, gBits, bBits;
                        rBits = (UBYTE)(r >> (8 - depth));
                        gBits = (UBYTE)(g >> (8 - depth));
                        bBits = (UBYTE)(b >> (8 - depth));
                        pixelIndex = (UBYTE)((rBits << (depth * 2 / 3)) | (gBits << (depth / 3)) | bBits);
                        if (pixelIndex >= (1UL << depth)) {
                            pixelIndex = (UBYTE)((1UL << depth) - 1);
                        }
                    }
                }
            }
            
            /* Write pixel to bitplanes */
            /* Calculate bit position (MSB first) */
            bitMask = (UBYTE)(0x80 >> (col & 7));
            planePtr = (UBYTE *)((ULONG)bitmap->Planes[0] + (ULONG)row * rowBytes + (col >> 3));
            
            for (plane = 0; plane < depth; plane++) {
                if (bitmap->Planes[plane]) {
                    planePtr = (UBYTE *)((ULONG)bitmap->Planes[plane] + (ULONG)row * rowBytes + (col >> 3));
                    if (pixelIndex & (1UL << plane)) {
                        *planePtr |= bitMask;
                    } else {
                        *planePtr &= ~bitMask;
                    }
                }
            }
        }
    }
    
    return RETURN_OK;
}

/*
** ConvertRGBToChunky - Convert RGB data to chunky format using WriteChunkyPixels
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
static LONG ConvertRGBToChunky(struct IFFPicture *picture, struct BitMap *bitmap, struct RastPort *rp)
{
    UWORD width, height;
    UBYTE *rgbData;
    UBYTE *chunkyData;
    ULONG row;
    ULONG rgbIndex;
    ULONG chunkyIndex;
    UBYTE *cmapData;
    ULONG numColors;
    struct Library *GraphicsBase;
    ULONG gfxVersion;
    
    if (!picture || !bitmap || !rp || !picture->pixelData || !picture->bmhd) {
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    rgbData = picture->pixelData;
    
    GraphicsBase = OpenLibrary("graphics.library", 0);
    if (!GraphicsBase) {
        return RETURN_FAIL;
    }
    
    gfxVersion = GraphicsBase->lib_Version;
    CloseLibrary(GraphicsBase);
    
    /* Get palette if available */
    cmapData = NULL;
    numColors = 0;
    if (picture->cmap && picture->cmap->data) {
        cmapData = picture->cmap->data;
        numColors = picture->cmap->numcolors;
    }
    
    /* Allocate chunky buffer (8-bit per pixel) */
    chunkyData = (UBYTE *)AllocMem((ULONG)width * height, MEMF_PUBLIC | MEMF_CLEAR);
    if (!chunkyData) {
        return RETURN_FAIL;
    }
    
    /* Convert RGB to chunky (palette indices) */
    for (row = 0; row < height; row++) {
        for (chunkyIndex = 0; chunkyIndex < width; chunkyIndex++) {
            rgbIndex = (ULONG)row * width * 3 + chunkyIndex * 3;
            
            /* Get RGB values */
            {
                UBYTE r, g, b;
                UBYTE pixelIndex;
                
                r = rgbData[rgbIndex];
                g = rgbData[rgbIndex + 1];
                b = rgbData[rgbIndex + 2];
                
                /* Find closest palette match if palette available */
                if (cmapData && numColors > 0) {
                    ULONG bestMatch;
                    ULONG bestDist;
                    ULONG dist;
                    ULONG j;
                    LONG dr, dg, db;
                    
                    bestMatch = 0;
                    bestDist = 0xFFFFFFFFUL;
                    
                    for (j = 0; j < numColors; j++) {
                        UBYTE pr, pg, pb;
                        pr = cmapData[j * 3];
                        pg = cmapData[j * 3 + 1];
                        pb = cmapData[j * 3 + 2];
                        
                        /* Handle 4-bit palette scaling */
                        if (picture->cmap->is4Bit) {
                            pr |= (pr >> 4);
                            pg |= (pg >> 4);
                            pb |= (pb >> 4);
                        }
                        
                        dr = (LONG)r - (LONG)pr;
                        dg = (LONG)g - (LONG)pg;
                        db = (LONG)b - (LONG)pb;
                        dist = (ULONG)(dr * dr + dg * dg + db * db);
                        
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestMatch = j;
                        }
                    }
                    
                    pixelIndex = (UBYTE)bestMatch;
                } else {
                    /* No palette - use RGB directly (quantize to 8-bit) */
                    pixelIndex = (UBYTE)((r >> 5) * 32 + (g >> 5) * 4 + (b >> 6));
                }
                
                chunkyData[(ULONG)row * width + chunkyIndex] = pixelIndex;
            }
        }
    }
    
    /* Use WriteChunkyPixels if available (V40+) */
    if (gfxVersion >= 40) {
        /* WriteChunkyPixels is available */
        WriteChunkyPixels(rp, 0, 0, width - 1, height - 1, chunkyData, width);
    } else {
        /* Fall back to direct bitplane conversion for older systems */
        /* This is a simplified fallback - in practice you might want to use WritePixelArray8 */
        ConvertRGBToBitPlanes(picture, bitmap);
    }
    
    FreeMem(chunkyData, (ULONG)width * height);
    return RETURN_OK;
}

/*
** DecodeToBitMap - Create BitMap from decoded IFF image
** Returns: Pointer to BitMap or NULL on failure
*/
struct BitMap *DecodeToBitMap(struct IFFPicture *picture, ULONG modeID, struct BitMap *friendBitmap)
{
    struct BitMap *bitmap;
    struct RastPort *tempRP;
    UWORD width, height;
    UBYTE depth;
    ULONG flags;
    BOOL isChunky;
    struct Library *GraphicsBase;
    ULONG gfxVersion;
    
    if (!picture || !picture->isDecoded || !picture->bmhd || !picture->pixelData) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Picture not decoded or missing data");
        }
        return NULL;
    }
    
    if (modeID == INVALID_ID) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid modeID");
        }
        return NULL;
    }
    
    GraphicsBase = OpenLibrary("graphics.library", 39);
    if (!GraphicsBase) {
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_ERROR, "Cannot open graphics.library");
        }
        return NULL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    
    /* Check if mode is chunky (RTG modes typically use chunky format) */
    /* Query DisplayInfo to determine pixel format */
    isChunky = FALSE;
    {
        DisplayInfoHandle handle;
        struct DisplayInfo displayInfoStruct;
        
        handle = FindDisplayInfo(modeID);
        if (handle) {
            if (GetDisplayInfoData(handle, (UBYTE *)&displayInfoStruct, sizeof(displayInfoStruct), DTAG_DISP, NULL)) {
                /* Check if this is a foreign (RTG) mode - typically chunky */
                if (displayInfoStruct.PropertyFlags & DIPF_IS_FOREIGN) {
                    isChunky = TRUE;
                }
            }
            FreeDisplayInfoData(handle);
        }
    }
    
    /* For now, assume depth <= 8 is planar, depth > 8 or RTG is chunky */
    /* This is a simplification - in practice you'd check the actual mode properties */
    if (depth > 8) {
        isChunky = TRUE;
    }
    
    gfxVersion = GraphicsBase->lib_Version;
    
    /* Allocate BitMap */
    flags = BMF_DISPLAYABLE | BMF_CLEAR;
    if (!isChunky) {
        /* Planar mode - can use interleaved if available */
        if (gfxVersion >= 39) {
            flags |= BMF_INTERLEAVED;
        }
    }
    
    bitmap = AllocBitMap(width, height, depth, flags, friendBitmap);
    if (!bitmap) {
        CloseLibrary(GraphicsBase);
        if (picture) {
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BitMap");
        }
        return NULL;
    }
    
    /* Convert RGB data to BitMap format */
    if (isChunky && gfxVersion >= 40) {
        /* Chunky mode - create temporary RastPort for WriteChunkyPixels */
        tempRP = (struct RastPort *)AllocMem(sizeof(struct RastPort), MEMF_PUBLIC | MEMF_CLEAR);
        if (tempRP) {
            InitRastPort(tempRP);
            tempRP->BitMap = bitmap;
            
            if (ConvertRGBToChunky(picture, bitmap, tempRP) != RETURN_OK) {
                FreeMem(tempRP, sizeof(struct RastPort));
                FreeBitMap(bitmap);
                CloseLibrary(GraphicsBase);
                if (picture) {
                    SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to convert RGB to chunky format");
                }
                return NULL;
            }
            
            FreeMem(tempRP, sizeof(struct RastPort));
        } else {
            /* Fall back to planar conversion */
            if (ConvertRGBToBitPlanes(picture, bitmap) != RETURN_OK) {
                FreeBitMap(bitmap);
                CloseLibrary(GraphicsBase);
                if (picture) {
                    SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to convert RGB to bitplanes");
                }
                return NULL;
            }
        }
    } else {
        /* Planar mode - direct bitplane conversion */
        if (ConvertRGBToBitPlanes(picture, bitmap) != RETURN_OK) {
            FreeBitMap(bitmap);
            CloseLibrary(GraphicsBase);
            if (picture) {
                SetIFFPictureError(picture, IFFPICTURE_ERROR, "Failed to convert RGB to bitplanes");
            }
            return NULL;
        }
    }
    
    CloseLibrary(GraphicsBase);
    return bitmap;
}

/*
** DecodeToRastPort - Create off-screen RastPort with BitMap from decoded IFF image
** Returns: Pointer to RastPort or NULL on failure
*/
struct RastPort *DecodeToRastPort(struct IFFPicture *picture, ULONG modeID, struct BitMap *friendBitmap)
{
    struct RastPort *rp;
    struct BitMap *bitmap;
    
    if (!picture) {
        return NULL;
    }
    
    /* Allocate RastPort structure */
    rp = (struct RastPort *)AllocMem(sizeof(struct RastPort), MEMF_PUBLIC | MEMF_CLEAR);
    if (!rp) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate RastPort");
        return NULL;
    }
    
    /* Create BitMap */
    bitmap = DecodeToBitMap(picture, modeID, friendBitmap);
    if (!bitmap) {
        FreeMem(rp, sizeof(struct RastPort));
        return NULL;
    }
    
    /* Initialize RastPort and attach BitMap */
    InitRastPort(rp);
    rp->BitMap = bitmap;
    
    return rp;
}

/*
** FreeRastPort - Free RastPort and its attached BitMap
*/
VOID FreeRastPort(struct RastPort *rp)
{
    struct Library *GraphicsBase;
    
    if (!rp) {
        return;
    }
    
    /* Free BitMap if attached */
    if (rp->BitMap) {
        GraphicsBase = OpenLibrary("graphics.library", 39);
        if (GraphicsBase) {
            FreeBitMap(rp->BitMap);
            CloseLibrary(GraphicsBase);
        }
        rp->BitMap = NULL;
    }
    
    /* Free RastPort structure */
    FreeMem(rp, sizeof(struct RastPort));
}

