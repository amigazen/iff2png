/*
** main.c - iff2png main program
**
** Command-line tool to convert IFF bitmap images to PNG format
** Uses ReadArgs() for command-line parsing
*/

#include "main.h"
#include "debug.h"

/* Amiga version strings - kept as static to prevent "unreachable" warnings */
/* These are referenced by the linker/loader, not by code */
static const char *verstag = "$VER: iff2png 1.4 (2/1/2025)";
static const char *stack_cookie = "$STACK: 4096";
long oslibversion  = 40L; 

/* Command-line template - two required positional file arguments and optional FORCE, QUIET, and OPAQUE switches */
static const char TEMPLATE[] = "SOURCE/A,TARGET/A,FORCE/S,QUIET/S,OPAQUE/S,STRIP=NOMETADATA/S";

/* Usage string */
static const char USAGE[] = "Usage: iff2png SOURCE/A TARGET/A [FORCE/S] [QUIET/S] [OPAQUE/S] [STRIP=NOMETADATA/S]\n"
                             "  SOURCE/A - Input IFF image file\n"
                             "  TARGET/A - Output PNG file\n"
                             "  FORCE/S - Overwrite existing output file\n"
                             "  QUIET/S - Suppress normal output messages\n"
                             "  OPAQUE/S - Keep color 0 opaque instead of transparent\n"
                             "  STRIP/S or NOMETADATA/S - Prevents any metadata text from the source being included in the target PNG\n";

/* Library base - needed for proto includes */
struct Library *IFFParseBase;

/*
** main - Entry point for AmigaDOS command
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
int main(int argc, char **argv)
{
    struct RDArgs *rdargs;
    LONG args[6]; /* SOURCE, TARGET, FORCE, QUIET, OPAQUE, STRIP */
    char sourceFile[256]; /* Local copy of source filename */
    char targetFile[256]; /* Local copy of target filename */
    struct IFFPicture *picture;
    UBYTE *rgbData;
    ULONG rgbSize;
    struct PNGConfig config;
    LONG result;
    BOOL forceOverwrite;
    BOOL quiet;
    BOOL opaque;
    BOOL stripMetadata;
    BPTR lock;
    BPTR targetLock;
    struct FileInfoBlock fib;
    ULONG sourceFileSize;
    ULONG targetFileSize;
    UBYTE outputBuffer[512];  /* Buffer for formatted output strings */
    
    /* Initialize config structure to zero */
    config.color_type = 0;
    config.bit_depth = 0;
    config.has_alpha = FALSE;
    config.palette = NULL;
    config.num_palette = 0;
    config.trans = NULL;
    config.num_trans = 0;
    
    /* Open iffparse.library */
    IFFParseBase = OpenLibrary("iffparse.library", 0);
    if (!IFFParseBase) {
        PutStr("Error: Cannot open iffparse.library\n");
        return (int)RETURN_FAIL;
    }
    
    /* Initialize args array - ReadArgs will fill with pointers to strings */
    args[0] = 0; /* SOURCE */
    args[1] = 0; /* TARGET */
    args[2] = 0; /* FORCE (boolean) */
    args[3] = 0; /* QUIET (boolean) */
    args[4] = 0; /* OPAQUE (boolean) */
    args[5] = 0; /* STRIP (boolean) */
    
    /* Parse command-line arguments */
    /* Template "SOURCE/A,TARGET/A,FORCE/S,QUIET/S,OPAQUE/S,STRIP/S" - two required files and optional switches */
    rdargs = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rdargs) {
        /* ReadArgs returns NULL on failure (e.g., missing required /A arguments) */
        PutStr((STRPTR)USAGE);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* With /A modifier, ReadArgs ensures args are filled, but check anyway */
    if (!args[0] || !args[1]) {
        PutStr("Error: Missing required arguments\n");
        PutStr((STRPTR)USAGE);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Copy strings from ReadArgs before calling FreeArgs() */
    /* ReadArgs returns pointers to strings that will be freed by FreeArgs() */
    /* We must copy them to local buffers if we need them after FreeArgs() */
    Strncpy(sourceFile, (STRPTR)args[0], sizeof(sourceFile) - 1);
    sourceFile[sizeof(sourceFile) - 1] = '\0';
    
    Strncpy(targetFile, (STRPTR)args[1], sizeof(targetFile) - 1);
    targetFile[sizeof(targetFile) - 1] = '\0';
    
    /* Get switch values (non-zero if set) - these are just booleans, no need to copy */
    forceOverwrite = (args[2] != 0);
    quiet = (args[3] != 0);
    opaque = (args[4] != 0);
    stripMetadata = (args[5] != 0);
    
    /* Free ReadArgs memory now that we've copied the strings we need */
    FreeArgs(rdargs);
    
    /* Check if input file exists */
    lock = Lock((STRPTR)sourceFile, ACCESS_READ);
    if (!lock) {
        PutStr("Error: Input file does not exist: ");
        PutStr((STRPTR)sourceFile);
        PutStr("\n");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Check if it's actually a file (not a directory) and get file size */
    sourceFileSize = 0;
    if (Examine(lock, &fib)) {
        if (fib.fib_DirEntryType > 0) {
            /* It's a directory, not a file */
            UnLock(lock);
            PutStr("Error: Input path is a directory, not a file: ");
            PutStr((STRPTR)sourceFile);
            PutStr("\n");
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        sourceFileSize = fib.fib_Size;
    }
    UnLock(lock);
    
    /* Check if output file already exists */
    lock = Lock((STRPTR)targetFile, ACCESS_READ);
    if (lock) {
        /* File exists - check if it's a directory */
        if (Examine(lock, &fib)) {
            if (fib.fib_DirEntryType > 0) {
                /* It's a directory */
                UnLock(lock);
                PutStr("Error: Output path is a directory: ");
                PutStr((STRPTR)targetFile);
                PutStr("\n");
                CloseLibrary(IFFParseBase);
                IFFParseBase = NULL;
                return (int)RETURN_FAIL;
            }
        }
        UnLock(lock);
        
        /* File exists and is not a directory */
        if (!forceOverwrite) {
            PutStr("Error: Output file already exists: ");
            PutStr((STRPTR)targetFile);
            PutStr("\n");
            PutStr("Use FORCE to overwrite existing file\n");
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
    }
    
    /* Create picture object */
    picture = AllocIFFPicture();
    if (!picture) {
        PutStr("Error: Cannot create picture object\n");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Open file with DOS - following iffparse.library pattern */
    {
        BPTR filehandle;
        filehandle = Open((STRPTR)sourceFile, MODE_OLDFILE);
        if (!filehandle) {
            PutStr("Error: Cannot open IFF file: ");
            PutStr((STRPTR)sourceFile);
            PutStr("\n");
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Initialize IFFPicture as DOS stream */
        InitIFFPictureasDOS(picture);
        
        /* Set the stream handle (must be done after InitIFFPictureasDOS) */
        /* Following iffparse.library pattern: user sets iff_Stream */
        {
            struct IFFHandle *iff;
            iff = GetIFFHandle(picture);
            if (!iff) {
                PutStr("Error: Cannot initialize IFFPicture\n");
                Close(filehandle);
                FreeIFFPicture(picture);
                CloseLibrary(IFFParseBase);
                IFFParseBase = NULL;
                return (int)RETURN_FAIL;
            }
            /* Set stream handle - user responsibility per iffparse pattern */
            iff->iff_Stream = (ULONG)filehandle;
        }
        
        /* Open IFF for reading */
        result = OpenIFFPicture(picture, IFFF_READ);
        if (result != RETURN_OK) {
            PutStr("Error: Cannot open IFF stream: ");
            PutStr((STRPTR)sourceFile);
            PutStr("\n");
            PutStr("  ");
            PutStr((STRPTR)GetErrorString(picture));
            PutStr("\n");
            /* Close file handle - user responsibility per iffparse pattern */
            Close(filehandle);
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Parse IFF structure */
        result = ParseIFFPicture(picture);
        if (result != RETURN_OK) {
            PutStr("Error: Invalid or corrupted IFF file: ");
            PutStr((STRPTR)sourceFile);
            PutStr("\n");
            PutStr("  ");
            PutStr((STRPTR)GetErrorString(picture));
            PutStr("\n");
            CloseIFFPicture(picture);
            Close(filehandle); /* Close file handle after CloseIFFPicture() */
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Analyze image format */
        result = AnalyzeFormat(picture);
        if (result != RETURN_OK) {
            PutStr("Error: Cannot analyze image format: ");
            PutStr((STRPTR)GetErrorString(picture));
            PutStr("\n");
            CloseIFFPicture(picture);
            Close(filehandle); /* Close file handle after CloseIFFPicture() */
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Decode image to RGB */
        result = DecodeToRGB(picture, &rgbData, &rgbSize);
        if (result != RETURN_OK) {
            PutStr("Error: Cannot decode image: ");
            PutStr((STRPTR)GetErrorString(picture));
            PutStr("\n");
            CloseIFFPicture(picture);
            Close(filehandle); /* Close file handle after CloseIFFPicture() */
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Get optimal PNG configuration */
        result = GetOptimalPNGConfig(picture, &config, opaque);
        if (result != RETURN_OK) {
            PutStr("Error: Cannot determine PNG configuration\n");
            CloseIFFPicture(picture);
            Close(filehandle); /* Close file handle after CloseIFFPicture() */
            FreeIFFPicture(picture);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        
        /* Close IFF context and file handle - following iffparse.library pattern */
        /* CloseIFFPicture() closes the IFF context but NOT the file handle */
        CloseIFFPicture(picture);
        Close(filehandle); /* User must close file handle after CloseIFFPicture() */
    }
    
    /* Output header and analysis information (unless quiet) */
    if (!quiet) {
        /* All variable declarations must be at the start of the block (C89 requirement) */
        struct BitMapHeader *bmhd;
        ULONG formType;
        const char *formName;
        const char *colorTypeName;
        const char *bitDepthName;
        ULONG width, height, depth;
        STRPTR compressionName;
        STRPTR maskingName;
        
        /* Print header with source/target names and libpng version */
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), 
                 "iff2png %s %s\n", sourceFile, targetFile);
        PutStr((STRPTR)outputBuffer);
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), 
                 "Using libpng version %s\n\n", PNG_LIBPNG_VER_STRING);
        PutStr((STRPTR)outputBuffer);
        
        bmhd = GetBMHD(picture);
        formType = GetFormType(picture);
        width = GetWidth(picture);
        height = GetHeight(picture);
        depth = GetDepth(picture);
        
        /* Check if BMHD is available - required for most operations */
        if (!bmhd) {
            PutStr("Error: BMHD chunk not available\n");
            PNGEncoder_FreeConfig(&config);
            FreeIFFPicture(picture);
            if (IFFParseBase) {
                CloseLibrary(IFFParseBase);
                IFFParseBase = NULL;
            }
            return (int)RETURN_FAIL;
        }
        
        /* Determine form type name */
        switch (formType) {
            case ID_ILBM: formName = "ILBM"; break;
            case ID_PBM: formName = "PBM"; break;
            case ID_RGBN: formName = "RGBN"; break;
            case ID_RGB8: formName = "RGB8"; break;
            case ID_DEEP: formName = "DEEP"; break;
            case ID_ACBM: formName = "ACBM"; break;
            case ID_FAXX: formName = "FAXX"; break;
            default: formName = "Unknown"; break;
        }
        
        /* Determine PNG color type name */
        switch (config.color_type) {
            case PNG_COLOR_TYPE_GRAY: colorTypeName = "Grayscale"; break;
            case PNG_COLOR_TYPE_PALETTE: colorTypeName = "Palette"; break;
            case PNG_COLOR_TYPE_RGB: colorTypeName = "RGB"; break;
            case PNG_COLOR_TYPE_RGBA: colorTypeName = "RGBA"; break;
            case PNG_COLOR_TYPE_GRAY_ALPHA: colorTypeName = "Grayscale+Alpha"; break;
            default: colorTypeName = "Unknown"; break;
        }
        
        /* Determine bit depth name */
        switch (config.bit_depth) {
            case 1: bitDepthName = "1-bit"; break;
            case 2: bitDepthName = "2-bit"; break;
            case 4: bitDepthName = "4-bit"; break;
            case 8: bitDepthName = "8-bit"; break;
            case 16: bitDepthName = "16-bit"; break;
            default: bitDepthName = "Unknown"; break;
        }
        
        /* Determine compression name */
        if (formType == ID_FAXX) {
            /* FAXX format has its own compression types */
            switch (GetFAXXCompression(picture)) {
                case 0: compressionName = "None"; break;
                case 1: compressionName = "Modified Huffman (MH)"; break;
                case 2: compressionName = "Modified READ (MR)"; break;
                case 4: compressionName = "Modified Modified READ (MMR)"; break;
                default: compressionName = "Unknown"; break;
            }
        } else if (IsCompressed(picture)) {
            compressionName = "ByteRun1";
        } else {
            compressionName = "None";
        }
        
        /* Determine masking name */
        switch (bmhd->masking) {
            case mskNone: maskingName = "None"; break;
            case mskHasMask: maskingName = "Mask plane"; break;
            case mskHasTransparentColor: maskingName = "Transparent color"; break;
            case mskLasso: maskingName = "Lasso"; break;
            default: maskingName = "Unknown"; break;
        }
        
        /* Output IFF source information */
        PutStr("IFF Source:\n");
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Format: %s\n", formName);
        PutStr((STRPTR)outputBuffer);
        
        /* File size */
        {
            LONG len;
            LONG newLen;
            len = SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  File size: %lu bytes", sourceFileSize);
            if (sourceFileSize >= 1024) {
                /* SNPrintf returns length including null, write at len-1 to overwrite null */
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                  " (%lu KB", sourceFileSize / 1024);
                len = (len - 1) + newLen;
                if (sourceFileSize >= 1024 * 1024) {
                    newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                      ", %lu MB", sourceFileSize / (1024 * 1024));
                    len = (len - 1) + newLen;
                }
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, ")");
                len = (len - 1) + newLen;
            }
            newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, "\n");
            len = (len - 1) + newLen;
            PutStr((STRPTR)outputBuffer);
        }
        
        /* Dimensions */
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Dimensions: %lu x %lu pixels\n", width, height);
        PutStr((STRPTR)outputBuffer);
        
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Bit planes: %lu\n", depth);
        PutStr((STRPTR)outputBuffer);
        
        if (bmhd->pageWidth != width || bmhd->pageHeight != height) {
            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Page size: %ld x %ld pixels\n", 
                     (LONG)bmhd->pageWidth, (LONG)bmhd->pageHeight);
            PutStr((STRPTR)outputBuffer);
        }
        
        if (bmhd->xAspect != 0 && bmhd->yAspect != 0) {
            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Aspect ratio: %lu:%lu\n", 
                     (ULONG)bmhd->xAspect, (ULONG)bmhd->yAspect);
            PutStr((STRPTR)outputBuffer);
        }
        
        if (IsHAM(picture)) {
            PutStr("  Mode: HAM (Hold And Modify)\n");
        } else if (IsEHB(picture)) {
            PutStr("  Mode: EHB (Extra Half-Brite)\n");
        }
        
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Compression: %s\n", compressionName);
        PutStr((STRPTR)outputBuffer);
        
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Masking: %s\n", maskingName);
        PutStr((STRPTR)outputBuffer);
        
        if (bmhd->masking == mskHasTransparentColor) {
            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Transparent color index: %lu\n", 
                     (ULONG)bmhd->transparentColor);
            PutStr((STRPTR)outputBuffer);
        }
        
        /* Output PNG target information */
        PutStr("\nPNG Target:\n");
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Color type: %s\n", colorTypeName);
        PutStr((STRPTR)outputBuffer);
        
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Bit depth: %s\n", bitDepthName);
        PutStr((STRPTR)outputBuffer);
        
        if (config.color_type == PNG_COLOR_TYPE_PALETTE && config.num_palette > 0) {
            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Palette entries: %lu\n", (ULONG)config.num_palette);
            PutStr((STRPTR)outputBuffer);
        }
        
        if (config.trans && config.num_trans > 0) {
            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Transparency: %lu palette entries\n", (ULONG)config.num_trans);
            PutStr((STRPTR)outputBuffer);
        } else if (config.has_alpha) {
            PutStr("  Transparency: Alpha channel\n");
        } else {
            PutStr("  Transparency: None\n");
        }
        
        PutStr("  Compression: Deflate (zlib)\n");
        PutStr("  Filter: Adaptive\n");
        PutStr("  Interlacing: None\n");
        
        PutStr("\n");
    }
    
    /* Write PNG file - use local copy of filename */
    result = PNGEncoder_Write((const char *)targetFile, rgbData, &config, picture, stripMetadata);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot write PNG file: ");
        PutStr((STRPTR)targetFile);
        PutStr("\n");
        PNGEncoder_FreeConfig(&config); /* Free palette/trans if allocated */
        /* Note: rgbData points to picture->pixelData, which is freed by FreeIFFPicture() */
        /* IFF context and file handle already closed in block above */
        FreeIFFPicture(picture);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    if (!quiet) {
        /* Get target file size */
        targetFileSize = 0;
        targetLock = Lock((STRPTR)targetFile, ACCESS_READ);
        if (targetLock) {
            if (Examine(targetLock, &fib)) {
                targetFileSize = fib.fib_Size;
            }
            UnLock(targetLock);
        }
        
        PutStr("Conversion complete");
        
        if (targetFileSize > 0) {
            LONG len;
            ULONG ratio;
            
            /* Build source file size string */
            LONG newLen;
            len = SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), 
                          "  Source: %lu bytes", sourceFileSize);
            if (sourceFileSize >= 1024) {
                /* SNPrintf returns length including null, write at len-1 to overwrite null */
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                  " (%lu KB", sourceFileSize / 1024);
                len = (len - 1) + newLen;
                if (sourceFileSize >= 1024 * 1024) {
                    newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                      ", %lu MB", sourceFileSize / (1024 * 1024));
                    len = (len - 1) + newLen;
                }
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, ")");
                len = (len - 1) + newLen;
            }
            
            /* Append target file size to same buffer */
            newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                              " -> Target: %lu bytes", targetFileSize);
            len = (len - 1) + newLen;
            if (targetFileSize >= 1024) {
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                  " (%lu KB", targetFileSize / 1024);
                len = (len - 1) + newLen;
                if (targetFileSize >= 1024 * 1024) {
                    newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                      ", %lu MB", targetFileSize / (1024 * 1024));
                    len = (len - 1) + newLen;
                }
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, ")");
                len = (len - 1) + newLen;
            }
            
            /* Append ratio */
            if (sourceFileSize > 0) {
                ratio = (targetFileSize * 100) / sourceFileSize;
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                                  " (ratio: %lu%%)\n", ratio);
                len = (len - 1) + newLen;
            } else {
                newLen = SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, "\n");
                len = (len - 1) + newLen;
            }
            
            /* Output the complete string */
            PutStr((STRPTR)outputBuffer);
        }
    }
    
    /* Cleanup - following iffparse.library pattern */
    PNGEncoder_FreeConfig(&config); /* Free palette/trans if allocated */
    /* Note: rgbData points to picture->pixelData, which is freed by FreeIFFPicture() */
    /* IFF context and file handle already closed in block above */
    FreeIFFPicture(picture);
    /* Note: FreeArgs() was already called earlier after copying strings */
    
    /* Close iffparse.library */
    if (IFFParseBase) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
    }
    
    return (int)RETURN_OK;
}

