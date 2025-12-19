/*
** image_decoder.c - Image Decoder Implementation (Internal to Library)
**
** Decodes IFF bitmap formats to RGB pixel data
*/

#include "iffpicture_private.h"
#include "/debug.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <proto/utility.h>

/* Helper macros */
#define RowBytes(w) ((((w) + 15) >> 4) << 1)  /* Round up to 16-bit boundary */

/* Bit masks for extracting bits from bytes - LSB to MSB order (index 0=LSB, 7=MSB) */
/* Used with bitIndex = 7 - (col % 8) to get MSB first */
static const UBYTE bit_mask[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

/*
** DecompressByteRun1 - Decompress ByteRun1 RLE data
** Returns: Number of bytes decompressed, or -1 on error
*/
static LONG DecompressByteRun1(struct IFFHandle *iff, UBYTE *dest, LONG destBytes)
{
    LONG bytesLeft = destBytes;
    UBYTE *out = dest;
    LONG bytesRead;
    UBYTE code;
    LONG count;
    UBYTE value;
    
    while (bytesLeft > 0) {
        /* Read control byte */
        bytesRead = ReadChunkBytes(iff, &code, 1);
        if (bytesRead != 1) {
            return -1; /* Error reading */
        }
        
        if (code <= 127) {
            /* Literal run: (code+1) bytes follow */
            count = code + 1;
            if (count > bytesLeft) {
                return -1; /* Would overflow */
            }
            bytesRead = ReadChunkBytes(iff, out, count);
            if (bytesRead != count) {
                return -1; /* Error reading */
            }
            out += count;
            bytesLeft -= count;
        } else if (code != 128) {
            /* Repeat run: next byte repeated (256-code)+1 times */
            /* For code 129-255: count = 256-code, we write count+1 bytes */
            count = 256 - code;
            if ((count + 1) > bytesLeft) {
                return -1; /* Would overflow */
            }
            bytesRead = ReadChunkBytes(iff, &value, 1);
            if (bytesRead != 1) {
                return -1; /* Error reading */
            }
            /* Write count+1 bytes (loop from count down to 0 inclusive) */
            while (count >= 0) {
                *out++ = value;
                bytesLeft--;
                count--;
            }
        }
        /* code == 128 is NOP, continue */
    }
    
    return destBytes - bytesLeft;
}

/*
** DecodeILBM - Decode ILBM format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** ILBM format stores pixels as interleaved bitplanes:
** - Each row consists of all planes for that row
** - Planes are stored sequentially (plane 0, plane 1, ..., plane N-1)
** - Each plane is RowBytes(width) bytes
** - Bits are stored MSB first (bit 7 = leftmost pixel)
** - Pixel index is built from bits across all planes
** - RGB is looked up from CMAP using pixel index
*/
LONG DecodeILBM(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE pixelIndex;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE *alphaValues; /* For mask plane alpha channel */
    
    if (!picture || !picture->bmhd || !picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD or CMAP for ILBM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    
    printf("DecodeILBM: Starting decode %ldx%ld, %ld planes\n", width, height, depth);
    fflush(stdout);
    
    DEBUG_PRINTF4("DEBUG: DecodeILBM - Starting decode: %ldx%ld, %ld planes, masking=%ld\n",
                  width, height, depth, picture->bmhd->masking);
    rowBytes = RowBytes(width);
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    /* Allocate pixel data buffer */
    if (picture->bmhd->masking == mskHasMask) {
        picture->pixelDataSize = (ULONG)width * height * 4; /* RGBA */
        picture->hasAlpha = TRUE;
    } else {
        picture->pixelDataSize = (ULONG)width * height * 3; /* RGB */
        picture->hasAlpha = FALSE;
    }
    
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_CLEAR);
    if (!picture->pixelData) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel data buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate alpha buffer if mask plane present */
    alphaValues = NULL;
    if (picture->bmhd->masking == mskHasMask) {
        alphaValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
        if (!alphaValues) {
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate alpha buffer");
            return RETURN_FAIL;
        }
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_CLEAR);
        if (!pixelIndices) {
            FreeMem(planeBuffer, rowBytes);
            if (alphaValues) FreeMem(alphaValues, width);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel indices");
            return RETURN_FAIL;
        }
        
        /* Read all data planes for this row (planes 0 through nPlanes-1) */
        for (plane = 0; plane < depth; plane++) {
            /* Read/decompress plane data */
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed");
                    return RETURN_FAIL;
                }
            } else {
                /* Uncompressed */
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
            }
            
            /* Extract bits from this plane to build pixel indices */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8); /* MSB first */
                UBYTE bit = (planeBuffer[byteIndex] & bit_mask[bitIndex]) ? 1 : 0;
                
                if (bit) {
                    pixelIndices[col] |= (1 << plane);
                }
            }
        }
        
        /* Read mask plane if present (comes after all data planes) */
        if (picture->bmhd->masking == mskHasMask) {
            /* Read/decompress mask plane */
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed for mask");
                    return RETURN_FAIL;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read mask plane");
                    return RETURN_FAIL;
                }
            }
            
            /* Extract mask bits to alpha channel */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8); /* MSB first */
                alphaValues[col] = (planeBuffer[byteIndex] & bit_mask[bitIndex]) ? 0xFF : 0x00;
            }
        }
        
        /* Convert pixel indices to RGB using CMAP */
        for (col = 0; col < width; col++) {
            pixelIndex = pixelIndices[col];
            
            /* Clamp to valid CMAP range */
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            
            /* Look up RGB from CMAP */
            rgbOut[0] = cmapData[pixelIndex * 3];     /* R */
            rgbOut[1] = cmapData[pixelIndex * 3 + 1]; /* G */
            rgbOut[2] = cmapData[pixelIndex * 3 + 2]; /* B */
            
            /* Handle 4-bit palette scaling if needed */
            if (picture->cmap->is4Bit) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            /* Add alpha channel if mask plane present */
            if (picture->bmhd->masking == mskHasMask) {
                rgbOut[3] = alphaValues[col];
                rgbOut += 4;
            } else {
                rgbOut += 3;
            }
        }
        
        FreeMem(pixelIndices, width);
    }
    
    printf("DecodeILBM: Completed all rows\n");
    fflush(stdout);
    
    FreeMem(planeBuffer, rowBytes);
    if (alphaValues) {
        FreeMem(alphaValues, width);
    }
    
    printf("DecodeILBM: Returning OK\n");
    fflush(stdout);
    return RETURN_OK;
}

/*
** DecodeHAM - Decode HAM format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** HAM (Hold And Modify) mode:
** - Uses top 2 bits as control codes (00=CMAP, 01=BLUE, 10=RED, 11=GREEN)
** - Lower (nPlanes-2) bits are index/value
** - HAMCODE_CMAP: Look up color from CMAP
** - HAMCODE_BLUE/RED/GREEN: Modify that component, keep others from previous pixel
*/
LONG DecodeHAM(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE pixelValue;
    UBYTE hamCode;
    UBYTE hamIndex;
    UBYTE hambits;
    UBYTE hammask;
    UBYTE hamshift;
    UBYTE hammask2;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE r, g, b;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for HAM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    
    /* HAM requires at least 6 planes (4 for color + 2 for HAM codes) */
    if (depth < 6) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "HAM requires at least 6 planes");
        return RETURN_FAIL;
    }
    
    hambits = depth - 2; /* Bits used for index/value */
    hammask = (1 << hambits) - 1; /* Mask for lower bits */
    hamshift = 8 - hambits; /* Shift amount */
    hammask2 = (1 << hamshift) - 1; /* Mask for upper bits */
    
    if (picture->cmap && picture->cmap->data) {
        cmapData = picture->cmap->data;
        maxColors = picture->cmap->numcolors;
    } else {
        cmapData = NULL;
        maxColors = 0;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel values for this row */
        UBYTE *pixelValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
        if (!pixelValues) {
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel values");
            return RETURN_FAIL;
        }
        
        /* Read all planes for this row */
        for (plane = 0; plane < depth; plane++) {
            /* Read/decompress plane data */
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed");
                    return RETURN_FAIL;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
            }
            
            /* Extract bits from this plane */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                UBYTE bit = (planeBuffer[byteIndex] & bit_mask[bitIndex]) ? 1 : 0;
                
                if (bit) {
                    pixelValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode HAM pixels */
        r = g = b = 0; /* Initialize to black */
        for (col = 0; col < width; col++) {
            pixelValue = pixelValues[col];
            hamCode = (pixelValue >> hambits) & 0x03; /* Top 2 bits */
            hamIndex = pixelValue & hammask; /* Lower bits */
            
            switch (hamCode) {
                case HAMCODE_CMAP:
                    /* Look up color from CMAP */
                    if (cmapData && hamIndex < maxColors) {
                        r = cmapData[hamIndex * 3];
                        g = cmapData[hamIndex * 3 + 1];
                        b = cmapData[hamIndex * 3 + 2];
                        
                        /* Handle 4-bit palette scaling */
                        if (picture->cmap && picture->cmap->is4Bit) {
                            r |= (r >> 4);
                            g |= (g >> 4);
                            b |= (b >> 4);
                        }
                    } else {
                        /* No CMAP, use grayscale */
                        r = g = b = (hamIndex << hamshift) | ((hamIndex << hamshift) >> hambits);
                    }
                    break;
                    
                case HAMCODE_BLUE:
                    /* Modify blue component */
                    b = ((b & hammask2) | (hamIndex << hamshift));
                    break;
                    
                case HAMCODE_RED:
                    /* Modify red component */
                    r = ((r & hammask2) | (hamIndex << hamshift));
                    break;
                    
                case HAMCODE_GREEN:
                    /* Modify green component */
                    g = ((g & hammask2) | (hamIndex << hamshift));
                    break;
            }
            
            /* Write RGB output */
            rgbOut[0] = r;
            rgbOut[1] = g;
            rgbOut[2] = b;
            rgbOut += 3;
        }
        
        FreeMem(pixelValues, width);
    }
    
    FreeMem(planeBuffer, rowBytes);
    return RETURN_OK;
}

/*
** DecodeEHB - Decode EHB format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** EHB (Extra Half-Brite) mode:
** - Uses 6 bitplanes (64 colors)
** - First 32 colors are normal CMAP colors
** - Colors 32-63 are half-brightness versions of colors 0-31
** - Similar to ILBM but track pixel indices to apply EHB scaling
*/
LONG DecodeEHB(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE pixelIndex;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    
    if (!picture || !picture->bmhd || !picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD or CMAP for EHB decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    /* EHB uses 6 planes */
    if (depth != 6) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "EHB requires 6 planes");
        return RETURN_FAIL;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_CLEAR);
        if (!pixelIndices) {
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel indices");
            return RETURN_FAIL;
        }
        
        /* Read all planes for this row */
        for (plane = 0; plane < depth; plane++) {
            /* Read/decompress plane data */
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed");
                    return RETURN_FAIL;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    FreeMem(pixelIndices, width);
                    FreeMem(planeBuffer, rowBytes);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
            }
            
            /* Extract bits from this plane to build pixel indices */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                UBYTE bit = (planeBuffer[byteIndex] & bit_mask[bitIndex]) ? 1 : 0;
                
                if (bit) {
                    pixelIndices[col] |= (1 << plane);
                }
            }
        }
        
        /* Convert pixel indices to RGB using CMAP, applying EHB scaling */
        for (col = 0; col < width; col++) {
            pixelIndex = pixelIndices[col];
            
            /* Clamp to valid CMAP range */
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            
            /* Look up RGB from CMAP */
            rgbOut[0] = cmapData[pixelIndex * 3];
            rgbOut[1] = cmapData[pixelIndex * 3 + 1];
            rgbOut[2] = cmapData[pixelIndex * 3 + 2];
            
            /* Handle 4-bit palette scaling if needed */
            if (picture->cmap->is4Bit) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            /* Apply EHB scaling: colors 32-63 are half-brightness versions of 0-31 */
            if (pixelIndex >= 32) {
                rgbOut[0] = rgbOut[0] >> 1;
                rgbOut[1] = rgbOut[1] >> 1;
                rgbOut[2] = rgbOut[2] >> 1;
            }
            
            rgbOut += 3;
        }
        
        FreeMem(pixelIndices, width);
    }
    
    FreeMem(planeBuffer, rowBytes);
    return RETURN_OK;
}

/*
** DecodeDEEP - Decode DEEP format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** DEEP format stores true-color RGB as separate bitplanes:
** - Planes 0-7: Red component (8 bits)
** - Planes 8-15: Green component (8 bits)
** - Planes 16-23: Blue component (8 bits)
** - For 24-bit: nPlanes = 24
** - Each component is decoded separately from bitplanes
*/
LONG DecodeDEEP(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE planesPerColor;
    UBYTE *rValues, *gValues, *bValues;
    LONG bytesRead;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for DEEP decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    
    /* DEEP typically uses 24 planes (8 per color) */
    if (depth % 3 != 0) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "DEEP requires nPlanes divisible by 3");
        return RETURN_FAIL;
    }
    
    planesPerColor = depth / 3;
    
    /* Allocate buffers */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    
    if (!planeBuffer || !rValues || !gValues || !bValues) {
        if (planeBuffer) FreeMem(planeBuffer, rowBytes);
        if (rValues) FreeMem(rValues, width);
        if (gValues) FreeMem(gValues, width);
        if (bValues) FreeMem(bValues, width);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DEEP buffers");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear component values */
        for (col = 0; col < width; col++) {
            rValues[col] = 0;
            gValues[col] = 0;
            bValues[col] = 0;
        }
        
        /* Decode Red component (planes 0 to planesPerColor-1) */
        for (plane = 0; plane < planesPerColor; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    rValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Green component (planes planesPerColor to 2*planesPerColor-1) */
        for (plane = 0; plane < planesPerColor; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    gValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Blue component (planes 2*planesPerColor to 3*planesPerColor-1) */
        for (plane = 0; plane < planesPerColor; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    bValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Write RGB output */
        for (col = 0; col < width; col++) {
            rgbOut[0] = rValues[col];
            rgbOut[1] = gValues[col];
            rgbOut[2] = bValues[col];
            rgbOut += 3;
        }
    }
    
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    return RETURN_OK;
    
cleanup_error:
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read DEEP plane data");
    return RETURN_FAIL;
}

/*
** DecodePBM - Decode PBM format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** PBM (Planar BitMap) format stores pixels as chunky 8-bit indexed color:
** - Each pixel is a single byte (index into CMAP)
** - Pixels are stored row by row
** - No bitplane interleaving
** - RGB is looked up from CMAP using pixel index
*/
LONG DecodePBM(struct IFFPicture *picture)
{
    UWORD width, height;
    UBYTE *rowBuffer;
    UBYTE *rgbOut;
    UWORD row, col;
    UBYTE pixelIndex;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for PBM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    
    /* PBM requires CMAP */
    if (!picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing CMAP for PBM decoding");
        return RETURN_FAIL;
    }
    
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    /* Allocate buffer for one row */
    rowBuffer = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    if (!rowBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate row buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Read/decompress row data */
        if (picture->bmhd->compression == cmpByteRun1) {
            bytesRead = DecompressByteRun1(picture->iff, rowBuffer, width);
            if (bytesRead != width) {
                FreeMem(rowBuffer, width);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed");
                return RETURN_FAIL;
            }
        } else {
            /* Uncompressed */
            bytesRead = ReadChunkBytes(picture->iff, rowBuffer, width);
            if (bytesRead != width) {
                FreeMem(rowBuffer, width);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read row data");
                return RETURN_FAIL;
            }
        }
        
        /* Convert pixel indices to RGB using CMAP */
        for (col = 0; col < width; col++) {
            pixelIndex = rowBuffer[col];
            
            /* Clamp to valid CMAP range */
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            
            /* Look up RGB from CMAP */
            rgbOut[0] = cmapData[pixelIndex * 3];     /* R */
            rgbOut[1] = cmapData[pixelIndex * 3 + 1]; /* G */
            rgbOut[2] = cmapData[pixelIndex * 3 + 2]; /* B */
            
            /* Handle 4-bit palette scaling if needed */
            if (picture->cmap->is4Bit) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            rgbOut += 3;
        }
    }
    
    FreeMem(rowBuffer, width);
    return RETURN_OK;
}

/*
** DecodeRGBN - Decode RGBN format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** RGBN format stores 4-bit per channel RGB (12-bit color):
** - 4 planes for Red (nibble 0-15)
** - 4 planes for Green (nibble 0-15)
** - 4 planes for Blue (nibble 0-15)
** - 1 plane for Alpha (optional)
** - Total: 13 planes (or 12 without alpha)
** - Uses run-length compression
*/
LONG DecodeRGBN(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE *rValues, *gValues, *bValues;
    LONG bytesRead;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for RGBN decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    
    /* RGBN uses 12 or 13 planes (4 per color + optional alpha) */
    if (depth < 12 || depth > 13) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "RGBN requires 12 or 13 planes");
        return RETURN_FAIL;
    }
    
    /* Allocate buffers */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    
    if (!planeBuffer || !rValues || !gValues || !bValues) {
        if (planeBuffer) FreeMem(planeBuffer, rowBytes);
        if (rValues) FreeMem(rValues, width);
        if (gValues) FreeMem(gValues, width);
        if (bValues) FreeMem(bValues, width);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate RGBN buffers");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear component values */
        for (col = 0; col < width; col++) {
            rValues[col] = 0;
            gValues[col] = 0;
            bValues[col] = 0;
        }
        
        /* Decode Red component (planes 0-3) */
        for (plane = 0; plane < 4; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    rValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Green component (planes 4-7) */
        for (plane = 0; plane < 4; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    gValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Blue component (planes 8-11) */
        for (plane = 0; plane < 4; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    bValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Scale 4-bit values to 8-bit (multiply by 17) */
        for (col = 0; col < width; col++) {
            rgbOut[0] = rValues[col] * 17;
            rgbOut[1] = gValues[col] * 17;
            rgbOut[2] = bValues[col] * 17;
            rgbOut += 3;
        }
        
        /* Skip alpha plane if present (plane 12) */
        if (depth == 13) {
            if (picture->bmhd->compression == cmpByteRun1) {
                DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
            } else {
                ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
            }
        }
    }
    
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    return RETURN_OK;
    
cleanup_error:
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read RGBN plane data");
    return RETURN_FAIL;
}

/*
** DecodeRGB8 - Decode RGB8 format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** RGB8 format stores 8-bit per channel RGB (24-bit color):
** - 8 planes for Red
** - 8 planes for Green
** - 8 planes for Blue
** - 1 plane for Alpha (optional)
** - Total: 25 planes (or 24 without alpha)
** - Uses run-length compression
*/
LONG DecodeRGB8(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, plane, col;
    UBYTE *rValues, *gValues, *bValues;
    LONG bytesRead;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for RGB8 decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    
    /* RGB8 uses 24 or 25 planes (8 per color + optional alpha) */
    if (depth < 24 || depth > 25) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "RGB8 requires 24 or 25 planes");
        return RETURN_FAIL;
    }
    
    /* Allocate buffers */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_CLEAR);
    
    if (!planeBuffer || !rValues || !gValues || !bValues) {
        if (planeBuffer) FreeMem(planeBuffer, rowBytes);
        if (rValues) FreeMem(rValues, width);
        if (gValues) FreeMem(gValues, width);
        if (bValues) FreeMem(bValues, width);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate RGB8 buffers");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear component values */
        for (col = 0; col < width; col++) {
            rValues[col] = 0;
            gValues[col] = 0;
            bValues[col] = 0;
        }
        
        /* Decode Red component (planes 0-7) */
        for (plane = 0; plane < 8; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    rValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Green component (planes 8-15) */
        for (plane = 0; plane < 8; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    gValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Decode Blue component (planes 16-23) */
        for (plane = 0; plane < 8; plane++) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            } else {
                bytesRead = ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
                if (bytesRead != rowBytes) {
                    goto cleanup_error;
                }
            }
            
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8);
                if (planeBuffer[byteIndex] & bit_mask[bitIndex]) {
                    bValues[col] |= (1 << plane);
                }
            }
        }
        
        /* Write RGB output */
        for (col = 0; col < width; col++) {
            rgbOut[0] = rValues[col];
            rgbOut[1] = gValues[col];
            rgbOut[2] = bValues[col];
            rgbOut += 3;
        }
        
        /* Skip alpha plane if present (plane 24) */
        if (depth == 25) {
            if (picture->bmhd->compression == cmpByteRun1) {
                DecompressByteRun1(picture->iff, planeBuffer, rowBytes);
            } else {
                ReadChunkBytes(picture->iff, planeBuffer, rowBytes);
            }
        }
    }
    
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    return RETURN_OK;
    
cleanup_error:
    FreeMem(planeBuffer, rowBytes);
    FreeMem(rValues, width);
    FreeMem(gValues, width);
    FreeMem(bValues, width);
    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read RGB8 plane data");
    return RETURN_FAIL;
}

/*
** DecodeACBM - Decode ACBM format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** ACBM (Amiga Contiguous Bitmap) format:
** - Similar to ILBM but stores planes contiguously (all of plane 0, then all of plane 1, etc.)
** - Uses ABIT chunk instead of BODY for image data
** - ACBM does NOT support compression (must be cmpNone)
** - Planes are stored sequentially: all rows of plane 0, then all rows of plane 1, etc.
*/
LONG DecodeACBM(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UWORD row, col, plane;
    UBYTE pixelIndex;
    UBYTE *cmapData;
    ULONG maxColors;
    LONG bytesRead;
    UBYTE *planeData; /* Temporary buffer to store all plane data */
    ULONG planeDataSize;
    ULONG planeOffset;
    UBYTE *pixelIndices;
    
    if (!picture || !picture->bmhd || !picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Invalid ACBM picture or missing CMAP");
        return RETURN_FAIL;
    }
    
    /* ACBM does not support compression */
    if (picture->bmhd->compression != cmpNone) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "ACBM format does not support compression");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    /* Handle mask plane if present */
    if (picture->bmhd->masking == mskHasMask) {
        depth++; /* Mask plane is additional plane */
    }
    
    /* Allocate buffer to store all plane data (contiguous storage) */
    planeDataSize = (ULONG)depth * height * rowBytes;
    planeData = (UBYTE *)AllocMem(planeDataSize, MEMF_CLEAR);
    if (!planeData) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane data buffer");
        return RETURN_FAIL;
    }
    
    /* Read all plane data from ABIT chunk (contiguous: all rows of plane 0, then plane 1, etc.) */
    planeOffset = 0;
    for (plane = 0; plane < depth; plane++) {
        for (row = 0; row < height; row++) {
            bytesRead = ReadChunkBytes(picture->iff, planeData + planeOffset, rowBytes);
            if (bytesRead != rowBytes) {
                FreeMem(planeData, planeDataSize);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read ACBM plane data");
                return RETURN_FAIL;
            }
            planeOffset += rowBytes;
        }
    }
    
    /* Allocate buffer for one plane row (for decoding) */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_CLEAR);
    if (!planeBuffer) {
        FreeMem(planeData, planeDataSize);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row - extract interleaved plane data from contiguous storage */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        pixelIndices = (UBYTE *)AllocMem(width, MEMF_CLEAR);
        if (!pixelIndices) {
            FreeMem(planeBuffer, rowBytes);
            FreeMem(planeData, planeDataSize);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel indices");
            return RETURN_FAIL;
        }
        
        /* Extract all planes for this row from contiguous storage */
        for (plane = 0; plane < depth; plane++) {
            /* Skip mask plane - we'll handle it separately if needed */
            if (picture->bmhd->masking == mskHasMask && plane == depth - 1) {
                /* This is the mask plane, skip for now */
                continue;
            }
            
            /* Copy row from contiguous plane data */
            planeOffset = (ULONG)plane * height * rowBytes + (ULONG)row * rowBytes;
            CopyMem(planeData + planeOffset, planeBuffer, rowBytes);
            
            /* Extract bits from this plane to build pixel indices */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex = col / 8;
                UBYTE bitIndex = 7 - (col % 8); /* MSB first */
                UBYTE bit = (planeBuffer[byteIndex] & bit_mask[bitIndex]) ? 1 : 0;
                
                if (bit) {
                    pixelIndices[col] |= (1 << plane);
                }
            }
        }
        
        /* Convert pixel indices to RGB using CMAP */
        for (col = 0; col < width; col++) {
            pixelIndex = pixelIndices[col];
            
            /* Clamp to valid CMAP range */
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            
            /* Look up RGB from CMAP */
            rgbOut[0] = cmapData[pixelIndex * 3];     /* R */
            rgbOut[1] = cmapData[pixelIndex * 3 + 1]; /* G */
            rgbOut[2] = cmapData[pixelIndex * 3 + 2]; /* B */
            
            /* Handle 4-bit palette scaling if needed */
            if (picture->cmap->is4Bit) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            rgbOut += 3;
        }
        
        FreeMem(pixelIndices, width);
    }
    
    FreeMem(planeBuffer, rowBytes);
    FreeMem(planeData, planeDataSize);
    return RETURN_OK;
}

