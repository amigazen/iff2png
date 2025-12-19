/*
** png_encoder.c - PNG Encoder Implementation
**
** Encodes RGB image data to PNG format using libpng
** Uses custom I/O callbacks for AmigaOS file handles
*/

#include "main.h"
#include <proto/exec.h>
#include <proto/dos.h>

/*
** PNG write callback for AmigaOS file I/O
** Called by libpng to write data to file
*/
static VOID PNGWriteCallback(png_structp png_ptr, png_bytep data, png_size_t length)
{
    BPTR filehandle;
    LONG bytesWritten;
    
    filehandle = (BPTR)png_get_io_ptr(png_ptr);
    if (!filehandle) {
        png_error(png_ptr, "Invalid file handle in write callback");
        return;
    }
    
    bytesWritten = Write(filehandle, data, length);
    if (bytesWritten != (LONG)length) {
        png_error(png_ptr, "Write error in PNG write callback");
    }
}

/*
** PNG flush callback for AmigaOS file I/O
** Called by libpng to flush file buffers
*/
static VOID PNGFlushCallback(png_structp png_ptr)
{
    BPTR filehandle;
    
    filehandle = (BPTR)png_get_io_ptr(png_ptr);
    if (filehandle) {
        Flush(filehandle);
    }
}

/*
** PNGEncoder_FreeConfig - Free memory allocated in PNGConfig
** Call this after PNGEncoder_Write to clean up palette/transparency data
*/
VOID PNGEncoder_FreeConfig(struct PNGConfig *config)
{
    if (!config) {
        return;
    }
    
    if (config->palette) {
        FreeMem(config->palette, config->num_palette * sizeof(struct PNGColor));
        config->palette = NULL;
    }
    
    if (config->trans) {
        FreeMem(config->trans, config->num_trans * sizeof(UBYTE));
        config->trans = NULL;
    }
    
    config->num_palette = 0;
    config->num_trans = 0;
}

/*
** PNGEncoder_Write - Write RGB data to PNG file
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** Uses libpng with custom AmigaOS file I/O callbacks
*/
LONG PNGEncoder_Write(const char *filename, UBYTE *rgbData, 
                      struct PNGConfig *config, struct IFFPicture *picture)
{
    BPTR filehandle;
    png_structp png_ptr;
    png_infop info_ptr;
    png_colorp palette;
    png_bytep trans;
    UWORD width, height;
    UWORD row;
    png_bytep row_pointers[1];
    LONG result;
    struct BitMapHeader *bmhd;
    
    printf("PNGEncoder_Write: Starting, filename=%s\n", filename);
    fflush(stdout);
    
    if (!filename || !rgbData || !config || !picture) {
        printf("PNGEncoder_Write: Invalid parameters\n");
        fflush(stdout);
        return RETURN_FAIL;
    }
    
    bmhd = GetBMHD(picture);
    if (!bmhd) {
        printf("PNGEncoder_Write: No BMHD\n");
        fflush(stdout);
        return RETURN_FAIL;
    }
    
    width = bmhd->w;
    height = bmhd->h;
    
    printf("PNGEncoder_Write: Image size %lux%lu, color_type=%d, bit_depth=%d, num_palette=%lu\n",
           (ULONG)width, (ULONG)height, config->color_type, config->bit_depth, (ULONG)config->num_palette);
    fflush(stdout);
    result = RETURN_FAIL;
    png_ptr = NULL;
    info_ptr = NULL;
    palette = NULL;
    trans = NULL;
    filehandle = NULL;
    
    /* Open file for writing */
    filehandle = Open((STRPTR)filename, MODE_NEWFILE);
    if (!filehandle) {
        return RETURN_FAIL;
    }
    
    /* Initialize PNG write structure */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        Close(filehandle);
        return RETURN_FAIL;
    }
    
    /* Initialize PNG info structure */
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        Close(filehandle);
        return RETURN_FAIL;
    }
    
    /* Set error handling - if an error occurs, we'll jump here */
    if (setjmp(png_jmpbuf(png_ptr))) {
        /* Error occurred during PNG writing */
        if (palette) {
            FreeMem(palette, config->num_palette * sizeof(png_color));
        }
        if (trans) {
            FreeMem(trans, config->num_trans);
        }
        png_destroy_write_struct(&png_ptr, &info_ptr);
        Close(filehandle);
        return RETURN_FAIL;
    }
    
    /* Set up custom I/O callbacks for AmigaOS file handles */
    png_set_write_fn(png_ptr, (png_voidp)filehandle, PNGWriteCallback, PNGFlushCallback);
    
    /* Set PNG header information */
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 config->bit_depth, config->color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    /* Set palette if indexed color */
    if (config->color_type == PNG_COLOR_TYPE_PALETTE && config->palette && config->num_palette > 0) {
        palette = (png_colorp)AllocMem(config->num_palette * sizeof(png_color), MEMF_CLEAR);
        if (!palette) {
            png_destroy_write_struct(&png_ptr, &info_ptr);
            Close(filehandle);
            return RETURN_FAIL;
        }
        
        /* Copy palette data */
        {
            ULONG i;
            for (i = 0; i < (ULONG)config->num_palette; i++) {
                palette[i].red = config->palette[i].red;
                palette[i].green = config->palette[i].green;
                palette[i].blue = config->palette[i].blue;
            }
        }
        
        png_set_PLTE(png_ptr, info_ptr, palette, config->num_palette);
    }
    
    /* Set transparency if needed */
    if (config->trans && config->num_trans > 0) {
        trans = (png_bytep)AllocMem(config->num_trans, MEMF_CLEAR);
        if (!trans) {
            if (palette) {
                FreeMem(palette, config->num_palette * sizeof(png_color));
            }
            png_destroy_write_struct(&png_ptr, &info_ptr);
            Close(filehandle);
            return RETURN_FAIL;
        }
        
        /* Copy transparency data */
        {
            ULONG i;
            for (i = 0; i < (ULONG)config->num_trans; i++) {
                trans[i] = config->trans[i];
            }
        }
        
        png_set_tRNS(png_ptr, info_ptr, trans, config->num_trans, NULL);
    }
    
    /* Write PNG header */
    png_write_info(png_ptr, info_ptr);
    
    /* Write image data row by row */
    if (config->color_type == PNG_COLOR_TYPE_PALETTE) {
        /* For palette images, we need to convert RGB data to palette indices */
        /* Allocate temporary buffer for palette indices */
        UBYTE *paletteIndices;
        ULONG i, j;
        ULONG bestMatch;
        ULONG bestDist;
        ULONG dist;
        
        printf("PNGEncoder: Converting RGB to palette indices, palette has %lu entries\n", (ULONG)config->num_palette);
        fflush(stdout);
        
        paletteIndices = (UBYTE *)AllocMem(width * height, MEMF_CLEAR);
        if (!paletteIndices) {
            if (palette) {
                FreeMem(palette, config->num_palette * sizeof(png_color));
            }
            if (trans) {
                FreeMem(trans, config->num_trans);
            }
            png_destroy_write_struct(&png_ptr, &info_ptr);
            Close(filehandle);
            return RETURN_FAIL;
        }
        
        /* Convert RGB to palette indices by finding closest match */
        {
            UBYTE r, g, b;
            ULONG rgbIndex;
            LONG dr, dg, db;
            
            printf("PNGEncoder: Converting %lu pixels from RGB to palette indices\n", (ULONG)width * height);
            fflush(stdout);
            
            for (i = 0; i < (ULONG)width * height; i++) {
                rgbIndex = i * 3;
                r = rgbData[rgbIndex];
                g = rgbData[rgbIndex + 1];
                b = rgbData[rgbIndex + 2];
                
                /* Find closest palette entry */
                bestMatch = 0;
                bestDist = 0xFFFFFFFFUL;
                for (j = 0; j < (ULONG)config->num_palette; j++) {
                    dr = (LONG)r - (LONG)palette[j].red;
                    dg = (LONG)g - (LONG)palette[j].green;
                    db = (LONG)b - (LONG)palette[j].blue;
                    dist = (ULONG)(dr * dr + dg * dg + db * db);
                    
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestMatch = j;
                    }
                }
                
                paletteIndices[i] = (UBYTE)bestMatch;
                
                /* Debug first few pixels */
                if (i < 10) {
                    printf("PNGEncoder: Pixel %lu: RGB=(%d,%d,%d) -> palette index %lu (palette RGB=(%d,%d,%d))\n",
                           i, r, g, b, bestMatch, palette[bestMatch].red, palette[bestMatch].green, palette[bestMatch].blue);
                    fflush(stdout);
                }
            }
        }
        
        printf("PNGEncoder: Writing %lu rows of palette indices\n", (ULONG)height);
        fflush(stdout);
        
        /* For 4-bit palette, we need to pack indices (2 per byte) */
        if (config->bit_depth == 4) {
            UBYTE *packedRow;
            ULONG packedRowSize;
            ULONG col;
            
            packedRowSize = (width + 1) / 2; /* Round up for odd widths */
            packedRow = (UBYTE *)AllocMem(packedRowSize, MEMF_CLEAR);
            if (!packedRow) {
                FreeMem(paletteIndices, width * height);
                if (palette) {
                    FreeMem(palette, config->num_palette * sizeof(png_color));
                }
                if (trans) {
                    FreeMem(trans, config->num_trans);
                }
                png_destroy_write_struct(&png_ptr, &info_ptr);
                Close(filehandle);
                return RETURN_FAIL;
            }
            
            /* Write palette indices row by row, packing for 4-bit */
            for (row = 0; row < height; row++) {
                UBYTE *rowIndices = paletteIndices + (ULONG)row * width;
                
                /* Pack 2 indices per byte */
                for (col = 0; col < width; col += 2) {
                    UBYTE byte;
                    byte = (UBYTE)(rowIndices[col] << 4);
                    if (col + 1 < width) {
                        byte |= (rowIndices[col + 1] & 0x0F);
                    }
                    packedRow[col / 2] = byte;
                }
                
                row_pointers[0] = packedRow;
                png_write_row(png_ptr, row_pointers[0]);
            }
            
            FreeMem(packedRow, packedRowSize);
        } else {
            /* 1-bit, 2-bit, or 8-bit - write directly */
            /* Note: For 1-bit and 2-bit, libpng expects packed data too,
             * but let's handle 4-bit first and see if that fixes it */
            for (row = 0; row < height; row++) {
                row_pointers[0] = paletteIndices + (ULONG)row * width;
                png_write_row(png_ptr, row_pointers[0]);
            }
        }
        
        FreeMem(paletteIndices, width * height);
    } else if (config->color_type == PNG_COLOR_TYPE_GRAY) {
        /* For grayscale, convert RGB to grayscale */
        UBYTE *grayData;
        ULONG i;
        
        grayData = (UBYTE *)AllocMem(width * height, MEMF_CLEAR);
        if (!grayData) {
            if (palette) {
                FreeMem(palette, config->num_palette * sizeof(png_color));
            }
            if (trans) {
                FreeMem(trans, config->num_trans);
            }
            png_destroy_write_struct(&png_ptr, &info_ptr);
            Close(filehandle);
            return RETURN_FAIL;
        }
        
        /* Convert RGB to grayscale using standard formula */
        {
            ULONG rgbIndex;
            UBYTE r, g, b;
            
            for (i = 0; i < (ULONG)width * height; i++) {
                rgbIndex = i * 3;
                r = rgbData[rgbIndex];
                g = rgbData[rgbIndex + 1];
                b = rgbData[rgbIndex + 2];
                
                /* Standard grayscale conversion: 0.299*R + 0.587*G + 0.114*B */
                grayData[i] = (UBYTE)((77UL * r + 150UL * g + 29UL * b) >> 8);
            }
        }
        
        /* Write grayscale data row by row */
        for (row = 0; row < height; row++) {
            row_pointers[0] = grayData + (ULONG)row * width;
            png_write_row(png_ptr, row_pointers[0]);
        }
        
        FreeMem(grayData, width * height);
    } else {
        /* RGB or RGBA - write directly */
        for (row = 0; row < height; row++) {
            if (config->color_type == PNG_COLOR_TYPE_RGBA) {
                row_pointers[0] = rgbData + (ULONG)row * width * 4;
            } else {
                row_pointers[0] = rgbData + (ULONG)row * width * 3;
            }
            png_write_row(png_ptr, row_pointers[0]);
        }
    }
    
    /* Finish writing PNG file */
    png_write_end(png_ptr, info_ptr);
    
    /* Clean up */
    if (palette) {
        FreeMem(palette, config->num_palette * sizeof(png_color));
    }
    if (trans) {
        FreeMem(trans, config->num_trans);
    }
    png_destroy_write_struct(&png_ptr, &info_ptr);
    Close(filehandle);
    
    result = RETURN_OK;
    return result;
}

