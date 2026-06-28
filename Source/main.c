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
static const char *verstag = "$VER: iff2png 1.8 (17/5/2026)";
static const char *stack_cookie = "$STACK: 4096";
long oslibversion  = 40L; 

/*
** Raw IFF scan: list top-level chunks inside FORM (file order), sizes, and short summaries.
** Uses the source path (re-opened) so it works after CloseIFFPicture; does not fail conversion.
*/
static ULONG cat_rdbe32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | (ULONG)p[3];
}

static UWORD cat_rdbe16(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

static VOID cat_fmt_id(ULONG id, char *o)
{
    ULONG i;
    UBYTE c;
    for (i = 0; i < 4; i++) {
        c = (UBYTE)(id >> (24 - (i * 8)));
        if (c < 32 || c > 126) {
            o[i] = '.';
        } else {
            o[i] = (char)c;
        }
    }
    o[4] = '\0';
}

static VOID cat_summarize_chunk(ULONG cid, ULONG csize, const UBYTE *d, ULONG dlen,
    struct BitMapHeader *bm, char *sum, ULONG sumsiz)
{
    UWORD uw;
    UWORD uh;
    ULONG cm;
    ULONG hdrcomp;
    ULONG flags;
    LONG startl;
    UWORD rawsl;
    ULONG lc;
    ULONG chg;
    ULONG totalc;
    ULONG ncol;
    ULONG i;
    UBYTE c;
    char tmp[132];
    ULONG n;
    ULONG w;

    sum[0] = '\0';
    if (cid == MAKE_ID('B','M','H','D')) {
        /* Prefer the iffparse-loaded BMHD (same as decoder); raw disk slice can disagree
         * if the read offset/length ever diverges from chunk boundaries on some media. */
        if (bm != NULL) {
            SNPrintf((STRPTR)sum, sumsiz,
                "bitmap header w=%lu h=%lu planes=%u mask=%u comp=%u (loaded)",
                (ULONG)bm->w, (ULONG)bm->h,
                (unsigned int)bm->nPlanes, (unsigned int)bm->masking, (unsigned int)bm->compression);
            if (csize >= 20UL && dlen >= 20UL) {
                if ((ULONG)bm->w != (ULONG)cat_rdbe16(d) || (ULONG)bm->h != (ULONG)cat_rdbe16(d + 2) ||
                    (ULONG)bm->nPlanes != (ULONG)d[8] || (ULONG)bm->masking != (ULONG)d[9] ||
                    (ULONG)bm->compression != (ULONG)d[10]) {
                    SNPrintf((STRPTR)sum, sumsiz,
                        "bitmap header w=%lu h=%lu planes=%u mask=%u comp=%u (loaded; raw chunk differs)",
                        (ULONG)bm->w, (ULONG)bm->h,
                        (unsigned int)bm->nPlanes, (unsigned int)bm->masking, (unsigned int)bm->compression);
                }
            }
        } else if (csize >= 20UL && dlen >= 20UL) {
            uw = cat_rdbe16(d);
            uh = cat_rdbe16(d + 2);
            SNPrintf((STRPTR)sum, sumsiz,
                "bitmap header w=%lu h=%lu planes=%u mask=%u comp=%u (raw file)",
                (ULONG)uw, (ULONG)uh, (unsigned int)d[8], (unsigned int)d[9], (unsigned int)d[10]);
        } else {
            SNPrintf((STRPTR)sum, sumsiz, "BMHD truncated (%lu bytes)", csize);
        }
        return;
    }
    if (cid == MAKE_ID('C','A','M','G')) {
        if (csize >= 4UL && dlen >= 4UL) {
            cm = cat_rdbe32(d);
            SNPrintf((STRPTR)sum, sumsiz,
                "viewport modes 0x%08lx %s%s%s%s",
                cm,
                (cm & 0x8000UL) ? "HIRES " : "",
                (cm & 0x0800UL) ? "HAM " : "",
                (cm & 0x0080UL) ? "EHB " : "",
                (cm & 0x0004UL) ? "LACE " : "");
        }
        return;
    }
    if (cid == MAKE_ID('C','M','A','P')) {
        ncol = csize / 3UL;
        SNPrintf((STRPTR)sum, sumsiz, "palette %lu RGB triples", ncol);
        return;
    }
    if (cid == MAKE_ID('P','C','H','G')) {
        if (csize >= 20UL && dlen >= 20UL) {
            hdrcomp = (ULONG)cat_rdbe16(d);
            flags = (ULONG)cat_rdbe16(d + 2);
            rawsl = cat_rdbe16(d + 4);
            startl = (LONG)(WORD)rawsl;
            lc = (ULONG)cat_rdbe16(d + 6);
            chg = (ULONG)cat_rdbe16(d + 8);
            totalc = cat_rdbe32(d + 16);
            SNPrintf((STRPTR)sum, sumsiz,
                "line palette comp=%lu flags=0x%04lx startLine=%ld lineCount=%lu changedLines=%lu totalChanges=%lu",
                hdrcomp, flags, startl, lc, chg, totalc);
        } else {
            SNPrintf((STRPTR)sum, sumsiz, "PCHG truncated (%lu bytes)", csize);
        }
        return;
    }
    if (cid == MAKE_ID('B','O','D','Y')) {
        if (bm) {
            if (bm->compression == 1) {
                SNPrintf((STRPTR)sum, sumsiz, "plane data (BMHD comp=1 ByteRun1 per row)");
            } else if (bm->compression == 2) {
                SNPrintf((STRPTR)sum, sumsiz, "plane data (BMHD comp=2 ByteRun1 per column)");
            } else if (bm->compression == 0) {
                SNPrintf((STRPTR)sum, sumsiz, "plane data (BMHD comp=0 uncompressed)");
            } else {
                SNPrintf((STRPTR)sum, sumsiz, "plane data (BMHD comp=%u)", (unsigned int)bm->compression);
            }
        } else {
            SNPrintf((STRPTR)sum, sumsiz, "bitmap/plane payload (see BMHD for layout)");
        }
        return;
    }
    if (cid == MAKE_ID('S','H','A','M')) {
        SNPrintf((STRPTR)sum, sumsiz, "SHAM half-brite style line palette");
        return;
    }
    if (cid == MAKE_ID('C','T','B','L')) {
        SNPrintf((STRPTR)sum, sumsiz, "CTBL %lu RGB444 register pairs", csize / 2UL);
        return;
    }
    if (cid == MAKE_ID('P','L','T','P')) {
        SNPrintf((STRPTR)sum, sumsiz, "Video Toaster PLTP plane routing");
        return;
    }
    if (cid == MAKE_ID('J','U','N','K')) {
        SNPrintf((STRPTR)sum, sumsiz, "padding / unused");
        return;
    }
    if (cid == MAKE_ID('G','R','A','B')) {
        if (csize >= 4UL && dlen >= 4UL) {
            SNPrintf((STRPTR)sum, sumsiz, "hotspot x=%ld y=%ld",
                (LONG)(WORD)cat_rdbe16(d), (LONG)(WORD)cat_rdbe16(d + 2));
        }
        return;
    }
    if (cid == MAKE_ID('D','E','S','T')) {
        if (csize >= 8UL && dlen >= 8UL) {
            SNPrintf((STRPTR)sum, sumsiz, "merge depth=%lu picks=0x%04lx",
                (ULONG)cat_rdbe16(d), (ULONG)cat_rdbe16(d + 2));
        }
        return;
    }
    if (cid == MAKE_ID('S','P','R','T')) {
        if (csize >= 2UL && dlen >= 2UL) {
            SNPrintf((STRPTR)sum, sumsiz, "sprite precedence %lu", (ULONG)cat_rdbe16(d));
        }
        return;
    }
    if (cid == MAKE_ID('C','R','N','G')) {
        SNPrintf((STRPTR)sum, sumsiz, "color cycle / range data");
        return;
    }
    if (cid == MAKE_ID('F','V','E','R')) {
        if (dlen > 0UL) {
            n = dlen;
            if (n > 120UL) {
                n = 120UL;
            }
            w = 0;
            for (i = 0; i < n && w + 1 < sizeof(tmp); i++) {
                c = d[i];
                if (c < 32 || c > 126) {
                    c = (UBYTE)'.';
                }
                tmp[w] = (char)c;
                w++;
            }
            tmp[w] = '\0';
            SNPrintf((STRPTR)sum, sumsiz, "version string: %s", tmp);
        }
        return;
    }
    if (cid == MAKE_ID('A','N','N','O') || cid == MAKE_ID('T','E','X','T') ||
        cid == MAKE_ID('A','U','T','H') || cid == MAKE_ID('(','c',')',' ')) {
        if (dlen > 0UL) {
            n = dlen;
            if (n > 120UL) {
                n = 120UL;
            }
            w = 0;
            for (i = 0; i < n && w + 1 < sizeof(tmp); i++) {
                c = d[i];
                if (c < 32 || c > 126) {
                    c = (UBYTE)'.';
                }
                tmp[w] = (char)c;
                w++;
            }
            tmp[w] = '\0';
            SNPrintf((STRPTR)sum, sumsiz, "text: %s", tmp);
        }
        return;
    }
    if (cid == MAKE_ID('E','X','I','F') || cid == MAKE_ID('I','P','T','C') ||
        cid == MAKE_ID('X','M','P','0') || cid == MAKE_ID('X','M','P','1') ||
        cid == MAKE_ID('I','C','C','P') || cid == MAKE_ID('G','E','O','T') ||
        cid == MAKE_ID('G','E','O','F')) {
        SNPrintf((STRPTR)sum, sumsiz, "extended metadata blob");
        return;
    }
    SNPrintf((STRPTR)sum, sumsiz, "binary payload");
}

static VOID PrintIFFChunkCatalog(const char *path, ULONG fileSize, struct IFFPicture *picture,
    char *obuf, ULONG obufsz)
{
    BPTR fh;
    UBYTE hdr[12];
    UBYTE payload[512];
    LONG nr;
    ULONG formLen;
    ULONG formType;
    ULONG end;
    ULONG pos;
    ULONG nextPos;
    ULONG chunkIdx;
    ULONG chunkId;
    ULONG chunkSize;
    ULONG toRead;
    ULONG prevPos;
    ULONG zi;
    struct BitMapHeader *bm;
    char id4[8];
    char sum[400];

    bm = NULL;
    if (picture) {
        bm = GetBMHD(picture);
    }
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        PutStr("  Chunk list: (could not re-open source file)\n");
        return;
    }
    nr = Read(fh, hdr, 12L);
    if (nr != 12L || hdr[0] != 'F' || hdr[1] != 'O' || hdr[2] != 'R' || hdr[3] != 'M') {
        PutStr("  Chunk list: (not a top-level IFF FORM)\n");
        Close(fh);
        return;
    }
    formLen = cat_rdbe32(hdr + 4);
    formType = cat_rdbe32(hdr + 8);
    end = 8UL + formLen;
    if (end < 12UL) {
        PutStr("  Chunk list: (invalid FORM chunk size)\n");
        Close(fh);
        return;
    }
    if (end > fileSize) {
        end = fileSize;
    }
    cat_fmt_id(formType, id4);
    SNPrintf((STRPTR)obuf, obufsz, "  IFF chunks (FORM %.4s, %lu bytes in FORM):\n", id4, formLen);
    PutStr((STRPTR)obuf);
    chunkIdx = 0;
    pos = 12UL;
    prevPos = 0xFFFFFFFFUL;
    while (pos + 8UL <= end && pos != prevPos) {
        prevPos = pos;
        if (Seek(fh, (LONG)pos, OFFSET_BEGINNING) == -1L) {
            break;
        }
        nr = Read(fh, hdr, 8L);
        if (nr != 8L) {
            break;
        }
        chunkId = cat_rdbe32(hdr);
        chunkSize = cat_rdbe32(hdr + 4);
        chunkIdx++;
        cat_fmt_id(chunkId, id4);
        toRead = chunkSize;
        if (toRead > (ULONG)sizeof(payload)) {
            toRead = (ULONG)sizeof(payload);
        }
        for (zi = 0; zi < (ULONG)sizeof(payload); zi++) {
            payload[zi] = 0;
        }
        if (chunkSize > 0UL) {
            if (Seek(fh, (LONG)(pos + 8UL), OFFSET_BEGINNING) == -1L) {
                break;
            }
            nr = Read(fh, payload, (LONG)toRead);
            if (nr < 0) {
                nr = 0;
            }
            if ((ULONG)nr < toRead) {
                toRead = (ULONG)nr;
            }
        } else {
            toRead = 0;
        }
        cat_summarize_chunk(chunkId, chunkSize, payload, toRead, bm, sum, (ULONG)sizeof(sum));
        SNPrintf((STRPTR)obuf, obufsz, "    %lu. %.4s  %lu bytes  %s\n",
            chunkIdx, id4, chunkSize, sum);
        PutStr((STRPTR)obuf);
        nextPos = pos + 8UL + chunkSize + (chunkSize & 1UL);
        if (nextPos <= pos) {
            break;
        }
        pos = nextPos;
        if (chunkIdx > 4096UL) {
            PutStr("    ... (truncated)\n");
            break;
        }
    }
    Close(fh);
    if (picture) {
        ULONG mpid;
        char mids[8];

        mpid = GetMultipaletteChunkId(picture);
        if (mpid != 0UL) {
            cat_fmt_id(mpid, mids);
            SNPrintf((STRPTR)obuf, obufsz,
                "  Library multipalette: %.4s\n", mids);
        } else {
            SNPrintf((STRPTR)obuf, obufsz,
                "  Library multipalette: (none)\n");
        }
        PutStr((STRPTR)obuf);
    }
}

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
        PrintFault(IoErr(), "iff2png");
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
        PrintFault(IoErr(), "iff2png");
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
            PutStr("iff2png: Input path is a directory, not a file\n");
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
                PutStr("iff2png: Output path is a directory\n");
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
            PrintFault(IoErr(), "iff2png");
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
            PutStr((STRPTR)IFFPictureErrorString(picture));
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
            PutStr((STRPTR)IFFPictureErrorString(picture));
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
            PutStr((STRPTR)IFFPictureErrorString(picture));
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
            PutStr((STRPTR)IFFPictureErrorString(picture));
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
            case ID_YUVN: formName = "YUVN"; break;
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
        
        /* Sub-format / variant identification */
        {
            const char *subFormat = NULL;
            if (formType == ID_ILBM) {
                if (IsFramestore(picture)) {
                    subFormat = "Video Toaster Framestore (16-plane quadrature YCbCr)";
                } else if (IsDCTV(picture)) {
                    subFormat = "DCTV (Digital Component Television Video)";
                } else if (bmhd->nPlanes == 24 && !IsHAM(picture) && !IsEHB(picture)) {
                    subFormat = "24-bit true color";
                } else if (IsHAM(picture)) {
                    subFormat = "HAM (Hold And Modify)";
                } else if (IsEHB(picture)) {
                    subFormat = "EHB (Extra Half-Brite)";
                }
            } else if (formType == ID_DEEP) {
                subFormat = "Chunky pixels (DGBL/DPEL/DBOD)";
            } else if (formType == ID_FAXX) {
                subFormat = "Facsimile (ITU-T T.4)";
            } else if (formType == ID_YUVN) {
                subFormat = "YUV (MacroSystem VLab)";
            } else if (formType == ID_ACBM) {
                subFormat = "Alpha channel (ABIT)";
            }
            if (subFormat) {
                SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  Sub-format: %s\n", subFormat);
                PutStr((STRPTR)outputBuffer);
            }
        }
        
        PrintIFFChunkCatalog((const char *)sourceFile, sourceFileSize, picture,
            (STRPTR)outputBuffer, (ULONG)sizeof(outputBuffer));
        
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
            (void)SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, "\n");
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
        
        /* Metadata from input file (all known chunks) */
        {
            STRPTR s;
            struct Point2D *grab;
            struct DestMerge *dest;
            UWORD *sprt;
            struct CRangeList *crngList;
            struct TextList *annoList;
            struct TextList *textList;
            ULONG i;
            int hasMeta;
            hasMeta = 0;
            s = ReadCopyright(picture);
            if (s && s[0]) { hasMeta = 1; }
            if (!hasMeta) { s = ReadAuthor(picture); if (s && s[0]) hasMeta = 1; }
            if (!hasMeta) { s = ReadAnnotation(picture); if (s && s[0]) hasMeta = 1; }
            if (!hasMeta) { s = ReadText(picture); if (s && s[0]) hasMeta = 1; }
            if (!hasMeta) { grab = ReadGRAB(picture); if (grab) hasMeta = 1; }
            if (!hasMeta) { dest = ReadDEST(picture); if (dest) hasMeta = 1; }
            if (!hasMeta) { sprt = ReadSPRT(picture); if (sprt) hasMeta = 1; }
            if (!hasMeta) { if (ReadCRNG(picture)) hasMeta = 1; }
            if (!hasMeta) { s = ReadFVER(picture); if (s && s[0]) hasMeta = 1; }
            if (!hasMeta) { if (ReadEXIF(picture, NULL)) hasMeta = 1; }
            if (!hasMeta) { if (ReadIPTC(picture, NULL)) hasMeta = 1; }
            if (!hasMeta) { if (ReadXMP0(picture, NULL)) hasMeta = 1; }
            if (!hasMeta) { if (ReadXMP1(picture, NULL)) hasMeta = 1; }
            if (!hasMeta) { if (ReadICCP(picture, NULL)) hasMeta = 1; }
            if (!hasMeta) { s = ReadICCN(picture); if (s && s[0]) hasMeta = 1; }
            if (!hasMeta) { if (ReadGEOT(picture, NULL)) hasMeta = 1; }
            if (hasMeta) {
                PutStr("  Metadata:\n");
                s = ReadCopyright(picture);
                if (s && s[0]) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Copyright: %s\n", s);
                    PutStr((STRPTR)outputBuffer);
                }
                s = ReadAuthor(picture);
                if (s && s[0]) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Author: %s\n", s);
                    PutStr((STRPTR)outputBuffer);
                }
                annoList = ReadAllAnnotations(picture);
                if (annoList && annoList->count > 0) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Annotations: %lu\n", (ULONG)annoList->count);
                    PutStr((STRPTR)outputBuffer);
                    for (i = 0; i < annoList->count && i < 5; i++) {
                        if (annoList->texts[i] && annoList->texts[i][0]) {
                            SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "      [%lu] %.400s\n", (ULONG)(i + 1), annoList->texts[i]);
                            PutStr((STRPTR)outputBuffer);
                        }
                    }
                    if (annoList->count > 5) {
                        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "      ... and %lu more\n", (ULONG)(annoList->count - 5));
                        PutStr((STRPTR)outputBuffer);
                    }
                }
                textList = ReadAllTexts(picture);
                if (textList && textList->count > 0) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Text chunks: %lu\n", (ULONG)textList->count);
                    PutStr((STRPTR)outputBuffer);
                }
                grab = ReadGRAB(picture);
                if (grab) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Hotspot (GRAB): %ld, %ld\n", (LONG)grab->x, (LONG)grab->y);
                    PutStr((STRPTR)outputBuffer);
                }
                dest = ReadDEST(picture);
                if (dest) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    DEST: depth=%lu planePick=%lu planeOnOff=%lu planeMask=%lu\n",
                             (ULONG)dest->depth, (ULONG)dest->planePick, (ULONG)dest->planeOnOff, (ULONG)dest->planeMask);
                    PutStr((STRPTR)outputBuffer);
                }
                sprt = ReadSPRT(picture);
                if (sprt) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Sprite precedence (SPRT): %lu\n", (ULONG)*sprt);
                    PutStr((STRPTR)outputBuffer);
                }
                crngList = ReadAllCRNG(picture);
                if (crngList && crngList->count > 0) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    Color ranges (CRNG): %lu\n", (ULONG)crngList->count);
                    PutStr((STRPTR)outputBuffer);
                }
                s = ReadFVER(picture);
                if (s && s[0]) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    FVER: %s\n", s);
                    PutStr((STRPTR)outputBuffer);
                }
                if (ReadEXIF(picture, NULL)) {
                    PutStr("    EXIF: present\n");
                }
                if (ReadIPTC(picture, NULL)) {
                    PutStr("    IPTC: present\n");
                }
                if (ReadXMP0(picture, NULL)) {
                    PutStr("    XMP (XMP0): present\n");
                }
                if (ReadXMP1(picture, NULL)) {
                    PutStr("    XMP (XMP1): present\n");
                }
                if (ReadICCP(picture, NULL)) {
                    PutStr("    ICC profile: present\n");
                }
                s = ReadICCN(picture);
                if (s && s[0]) {
                    SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "    ICC profile name (ICCN): %s\n", s);
                    PutStr((STRPTR)outputBuffer);
                }
                if (ReadGEOT(picture, NULL)) {
                    PutStr("    GeoTIFF: present\n");
                }
            }
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
        PrintFault(IoErr(), "iff2png");
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
                (void)SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1,
                               " (ratio: %lu%%)\n", ratio);
            } else {
                (void)SNPrintf((STRPTR)outputBuffer + len - 1, sizeof(outputBuffer) - len + 1, "\n");
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

