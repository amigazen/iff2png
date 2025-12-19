/*
** main.c - iff2png main program
**
** Command-line tool to convert IFF bitmap images to PNG format
** Uses ReadArgs() for command-line parsing
*/

#include "main.h"
#include "debug.h"

static const char *verstag = "$VER: iff2png 1.0 (19.12.2025)";

/* Command-line template - two required positional file arguments (no keyword names) */
static const char TEMPLATE[] = "SOURCE/A,TARGET/A";

/* Usage string */
static const char USAGE[] = "Usage: iff2png INPUT OUTPUT\n";

/* Library base - needed for proto includes */
struct Library *IFFParseBase;

/*
** main - Entry point for AmigaDOS command
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
int main(int argc, char **argv)
{
    struct RDArgs *rdargs;
    LONG args[2];
    struct IFFPicture *picture;
    UBYTE *rgbData;
    ULONG rgbSize;
    struct PNGConfig config;
    LONG result;
    
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
    args[0] = 0;
    args[1] = 0;
    
    /* Parse command-line arguments */
    /* Template "FILE/A,FILE/A" - two required positional file arguments */
    /* Usage: iff2png input.iff output.png */
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
        PutStr((STRPTR)USAGE);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Create picture object */
    picture = AllocIFFPicture();
    if (!picture) {
        PutStr("Error: Cannot create picture object\n");
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Debug: Show what file we're trying to open */
    printf("Opening file: %s\n", (char *)args[0]);
    fflush(stdout);
    
    /* Open IFF file - args[0] is a STRPTR (pointer to string) from ReadArgs */
    printf("Calling OpenIFFPicture...\n");
    fflush(stdout);
    result = OpenIFFPicture(picture, (const char *)args[0]);
    printf("OpenIFFPicture returned: %ld\n", result);
    fflush(stdout);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot open IFF file: ");
        PutStr((STRPTR)GetErrorString(picture));
        PutStr("\n");
        CloseIFFPicture(picture);
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Parse IFF structure */
    result = ParseIFFPicture(picture);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot parse IFF file: ");
        PutStr((STRPTR)GetErrorString(picture));
        PutStr("\n");
        CloseIFFPicture(picture);
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
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
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Decode image to RGB */
    printf("Calling DecodeToRGB...\n");
    fflush(stdout);
    result = DecodeToRGB(picture, &rgbData, &rgbSize);
    printf("DecodeToRGB returned: %ld, rgbData=%p, size=%lu\n", result, rgbData, rgbSize);
    fflush(stdout);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot decode image: ");
        PutStr((STRPTR)GetErrorString(picture));
        PutStr("\n");
        CloseIFFPicture(picture);
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Get optimal PNG configuration */
    printf("Calling GetOptimalPNGConfig...\n");
    fflush(stdout);
    result = GetOptimalPNGConfig(picture, &config);
    printf("GetOptimalPNGConfig returned: %ld\n", result);
    fflush(stdout);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot analyze image format\n");
        CloseIFFPicture(picture);
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    printf("About to output analysis information...\n");
    fflush(stdout);
    
    /* Output analysis information */
    {
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
        printf("Finished output section, about to call DecodeToRGB...\n");
        fflush(stdout);
    }
    
    /* Write PNG file - args[1] is a STRPTR (pointer to string) from ReadArgs */
    result = PNGEncoder_Write((const char *)args[1], rgbData, &config, picture);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot write PNG file\n");
        PNGEncoder_FreeConfig(&config); /* Free palette/trans if allocated */
        /* Note: rgbData points to picture->pixelData, which is freed by FreeIFFPicture() */
        CloseIFFPicture(picture);
        FreeIFFPicture(picture);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }
    
    /* Cleanup */
    PNGEncoder_FreeConfig(&config); /* Free palette/trans if allocated */
    /* Note: rgbData points to picture->pixelData, which is freed by FreeIFFPicture() */
    CloseIFFPicture(picture);
    FreeIFFPicture(picture);
    FreeArgs(rdargs);
    
    /* Close iffparse.library */
    if (IFFParseBase) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
    }
    
    return (int)RETURN_OK;
}

