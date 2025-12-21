/*
** main.c - iff2png main program
**
** Command-line tool to convert IFF bitmap images to PNG format
** Uses ReadArgs() for command-line parsing
*/

#include "main.h"
#include "debug.h"

static const char *verstag = "$VER: iff2png 1.2 (21.12.2025)";

static const char *stack_cookie = "$STACK: 4096";

/* Command-line template - two required positional file arguments and optional FORCE, QUIET, and OPAQUE switches */
static const char TEMPLATE[] = "SOURCE/A,TARGET/A,FORCE/S,QUIET/S,OPAQUE/S";

/* Usage string */
static const char USAGE[] = "Usage: iff2png SOURCE/A TARGET/A [FORCE/S] [QUIET/S] [OPAQUE/S]\n"
                             "  SOURCE/A - Input IFF image file\n"
                             "  TARGET/A - Output PNG file\n"
                             "  FORCE/S - Overwrite existing output file\n"
                             "  QUIET/S - Suppress normal output messages\n"
                             "  OPAQUE/S - Keep color 0 opaque instead of transparent\n";

/* Library base - needed for proto includes */
struct Library *IFFParseBase;

/*
** main - Entry point for AmigaDOS command
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
int main(int argc, char **argv)
{
    struct RDArgs *rdargs;
    LONG args[5]; /* SOURCE, TARGET, FORCE, QUIET, OPAQUE */
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
    BPTR lock;
    struct FileInfoBlock fib;
    
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
    
    /* Parse command-line arguments */
    /* Template "SOURCE/A,TARGET/A,FORCE/S,QUIET/S,OPAQUE/S" - two required files and optional switches */
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
    
    /* Free ReadArgs memory now that we've copied the strings we need */
    FreeArgs(rdargs);
    rdargs = NULL;
    
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
    
    /* Check if it's actually a file (not a directory) */
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
    
    /* Output analysis information (unless quiet) */
    if (!quiet) {
        struct BitMapHeader *bmhd;
        ULONG formType;
        const char *formName;
        const char *colorTypeName;
        const char *bitDepthName;
        
        bmhd = GetBMHD(picture);
        formType = GetFormType(picture);
        
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
        
        printf("Source file analysis:\n");
        printf("  Format: %s\n", formName);
        printf("  Dimensions: %lu x %lu\n", (ULONG)GetWidth(picture), (ULONG)GetHeight(picture));
        printf("  Planes/Depth: %lu\n", (ULONG)GetDepth(picture));
        
        if (IsHAM(picture)) {
            PutStr("  Mode: HAM (Hold And Modify)\n");
        } else if (IsEHB(picture)) {
            PutStr("  Mode: EHB (Extra Half-Brite)\n");
        }
        
        if (IsCompressed(picture)) {
            PutStr("  Compression: ByteRun1\n");
        } else {
            PutStr("  Compression: None\n");
        }
        
        if (HasAlpha(picture)) {
            PutStr("  Alpha channel: Yes\n");
        } else {
            PutStr("  Alpha channel: No\n");
        }
        
        printf("\nPNG output configuration:\n");
        printf("  Color type: %s\n", colorTypeName);
        printf("  Bit depth: %s\n", bitDepthName);
        
        if (config.color_type == PNG_COLOR_TYPE_PALETTE && config.num_palette > 0) {
            printf("  Palette entries: %lu\n", (ULONG)config.num_palette);
        }
        
        if (config.trans && config.num_trans > 0) {
            printf("  Transparency: Yes (%lu entries)\n", (ULONG)config.num_trans);
        } else if (config.has_alpha) {
            printf("  Transparency: Yes (alpha channel)\n");
        } else {
            printf("  Transparency: No\n");
        }
        
        printf("\n");
    }
    
    /* Write PNG file - use local copy of filename */
    result = PNGEncoder_Write((const char *)targetFile, rgbData, &config, picture);
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
        PutStr("Successfully converted ");
        PutStr((STRPTR)sourceFile);
        PutStr(" to ");
        PutStr((STRPTR)targetFile);
        PutStr("\n");
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

