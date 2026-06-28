/*
** iff2aiff.c - Convert IFF audio to AIFF
**
** Command-line tool to convert any IFF audio (8SVX, 16SV, AIFF, MAUD) to
** uncompressed AIFF with optimal profile matching source properties.
*/

#include "iff2aiff.h"
#include "debug.h"

static const char *verstag = "$VER: iff2aiff 1.1 (17.5.2026)";
static const char *stack_cookie = "$STACK: 4096";
long oslibversion = 40L;

static const char TEMPLATE[] = "SOURCE/A,TARGET/A,FORCE/S,QUIET/S,COMPRESS=AIFC/S";
static const char USAGE[] = "Usage: iff2aiff SOURCE/A TARGET/A [FORCE/S] [QUIET/S] [COMPRESS=AIFC/S]\n"
                            "  SOURCE/A   - Input IFF audio file (8SVX, 16SV, AIFF, AIFC, MAUD)\n"
                            "  TARGET/A   - Output AIFF file (or AIFC if AIFC/S is set)\n"
                            "  FORCE/S    - Overwrite existing output file\n"
                            "  QUIET/S    - Suppress normal output messages\n"
                            "  AIFC/S     - Write AIFC format instead of AIFF (default)\n"
                            "  COMPRESS/S - Equivalent to AIFC/S\n";

struct Library *IFFParseBase;

int main(int argc, char **argv)
{
    struct RDArgs *rdargs;
    LONG args[5];
    char sourceFile[256];
    char targetFile[256];
    struct IFFSound *sound;
    BPTR filehandle;
    LONG result;
    BOOL forceOverwrite;
    BOOL quiet;
    BOOL useAIFC;
    BPTR lock;
    BPTR targetLock;
    struct FileInfoBlock fib;
    ULONG targetFileSize;
    UBYTE outputBuffer[512];
    struct AIFFEncoderProfile profile;
    UBYTE *pcmData8;
    UWORD *pcmData16;
    ULONG dataSize;
    ULONG bitDepth;
    ULONG channels;
    ULONG numFrames;
    ULONG sampleRate;

    args[0] = 0;
    args[1] = 0;
    args[2] = 0;
    args[3] = 0;
    args[4] = 0;

    IFFParseBase = OpenLibrary("iffparse.library", 0);
    if (!IFFParseBase) {
        PutStr("Error: Cannot open iffparse.library\n");
        return (int)RETURN_FAIL;
    }

    rdargs = ReadArgs((STRPTR)TEMPLATE, args, NULL);
    if (!rdargs) {
        PutStr((STRPTR)USAGE);
        PrintFault(IoErr(), "iff2aiff");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    if (!args[0] || !args[1]) {
        PutStr("Error: Missing required arguments\n");
        PutStr((STRPTR)USAGE);
        FreeArgs(rdargs);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    Strncpy(sourceFile, (STRPTR)args[0], sizeof(sourceFile) - 1);
    sourceFile[sizeof(sourceFile) - 1] = '\0';
    Strncpy(targetFile, (STRPTR)args[1], sizeof(targetFile) - 1);
    targetFile[sizeof(targetFile) - 1] = '\0';
    forceOverwrite = (args[2] != 0);
    quiet = (args[3] != 0);
    useAIFC = (args[4] != 0);
    FreeArgs(rdargs);

    lock = Lock((STRPTR)sourceFile, ACCESS_READ);
    if (!lock) {
        PrintFault(IoErr(), "iff2aiff");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    if (Examine(lock, &fib)) {
        if (fib.fib_DirEntryType > 0) {
            UnLock(lock);
            PutStr("iff2aiff: Input path is a directory, not a file\n");
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
    }
    UnLock(lock);

    lock = Lock((STRPTR)targetFile, ACCESS_READ);
    if (lock) {
        if (Examine(lock, &fib)) {
            if (fib.fib_DirEntryType > 0) {
                UnLock(lock);
                PutStr("iff2aiff: Output path is a directory\n");
                CloseLibrary(IFFParseBase);
                IFFParseBase = NULL;
                return (int)RETURN_FAIL;
            }
        }
        UnLock(lock);
        if (!forceOverwrite) {
            PutStr("Error: Output file already exists: ");
            PutStr((STRPTR)targetFile);
            PutStr("\nUse FORCE to overwrite existing file\n");
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
    }

    sound = AllocIFFSound();
    if (!sound) {
        PutStr("Error: Cannot create sound object\n");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    filehandle = Open((STRPTR)sourceFile, MODE_OLDFILE);
    if (!filehandle) {
        PrintFault(IoErr(), "iff2aiff");
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    InitIFFSoundasDOS(sound);
    {
        struct IFFHandle *iff;
        iff = GetIFFHandle(sound);
        if (!iff) {
            PutStr("Error: Cannot initialize IFFSound\n");
            Close(filehandle);
            FreeIFFSound(sound);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        iff->iff_Stream = (ULONG)filehandle;
    }

    result = OpenIFFSound(sound, IFFF_READ);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot open IFF stream: ");
        PutStr((STRPTR)sourceFile);
        PutStr("\n  ");
        PutStr((STRPTR)IFFSoundErrorString(sound));
        PutStr("\n");
        Close(filehandle);
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    result = ParseIFFSound(sound);
    if (result != RETURN_OK) {
        PutStr("Error: Invalid or unsupported IFF audio: ");
        PutStr((STRPTR)sourceFile);
        PutStr("\n  ");
        PutStr((STRPTR)IFFSoundErrorString(sound));
        PutStr("\n");
        CloseIFFSound(sound);
        Close(filehandle);
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    result = AnalyzeFormat(sound);
    if (result != RETURN_OK) {
        PutStr("Error: Cannot analyze format: ");
        PutStr((STRPTR)IFFSoundErrorString(sound));
        PutStr("\n");
        CloseIFFSound(sound);
        Close(filehandle);
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    sampleRate = GetSampleRate(sound);
    channels = GetChannels(sound);
    numFrames = GetSampleCount(sound);
    bitDepth = GetBitDepth(sound);

    if (sampleRate == 0 || channels == 0 || numFrames == 0) {
        PutStr("Error: Invalid audio parameters (sample rate, channels, or frame count)\n");
        CloseIFFSound(sound);
        Close(filehandle);
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    profile.sampleRate = sampleRate;
    profile.channels = channels;
    profile.numFrames = numFrames;

    /* Optimal profile: match source bit depth for 8 or 16; otherwise output 16-bit */
    if (bitDepth == 8) {
        profile.bitDepth = 8;
        result = DecodeTo8Bit(sound, &pcmData8, &dataSize);
        if (result != RETURN_OK) {
            PutStr("Error: Decode failed: ");
            PutStr((STRPTR)IFFSoundErrorString(sound));
            PutStr("\n");
            CloseIFFSound(sound);
            Close(filehandle);
            FreeIFFSound(sound);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        result = AIFFEncoder_Write((const char *)targetFile, pcmData8, dataSize, &profile, useAIFC);
    } else {
        profile.bitDepth = 16;
        result = DecodeTo16Bit(sound, &pcmData16, &dataSize);
        if (result != RETURN_OK) {
            PutStr("Error: Decode failed: ");
            PutStr((STRPTR)IFFSoundErrorString(sound));
            PutStr("\n");
            CloseIFFSound(sound);
            Close(filehandle);
            FreeIFFSound(sound);
            CloseLibrary(IFFParseBase);
            IFFParseBase = NULL;
            return (int)RETURN_FAIL;
        }
        result = AIFFEncoder_Write((const char *)targetFile, (UBYTE *)pcmData16, dataSize, &profile, useAIFC);
    }

    CloseIFFSound(sound);
    Close(filehandle);

    if (result != RETURN_OK) {
        PrintFault(IoErr(), "iff2aiff");
        FreeIFFSound(sound);
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return (int)RETURN_FAIL;
    }

    if (!quiet) {
        const char *formName;
        ULONG formType;

        formType = GetFormType(sound);
        switch (formType) {
            case ID_8SVX: formName = "8SVX"; break;
            case ID_16SV: formName = "16SV"; break;
            case ID_AIFF: formName = "AIFF"; break;
            case ID_AIFC: formName = "AIFC"; break;
            case ID_MAUD: formName = "MAUD"; break;
            default: formName = "IFF"; break;
        }

        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "iff2aiff %s %s\n", sourceFile, targetFile);
        PutStr((STRPTR)outputBuffer);
        PutStr("Source: ");
        PutStr((STRPTR)formName);
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer),
                 "  %lu Hz, %lu channel(s), %lu bit  ->  %s %lu Hz, %lu channel(s), %lu bit\n",
                 (unsigned long)sampleRate, (unsigned long)channels, (unsigned long)bitDepth,
                 useAIFC ? (STRPTR)"AIFC" : (STRPTR)"AIFF",
                 (unsigned long)profile.sampleRate, (unsigned long)profile.channels, (unsigned long)profile.bitDepth);
        PutStr((STRPTR)outputBuffer);
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "  %lu sample frames, %lu bytes\n",
                 (unsigned long)numFrames, (unsigned long)dataSize);
        PutStr((STRPTR)outputBuffer);

        targetFileSize = 0;
        targetLock = Lock((STRPTR)targetFile, ACCESS_READ);
        if (targetLock) {
            if (Examine(targetLock, &fib)) {
                targetFileSize = fib.fib_Size;
            }
            UnLock(targetLock);
        }
        SNPrintf((STRPTR)outputBuffer, sizeof(outputBuffer), "Conversion complete. Output: %lu bytes\n", (unsigned long)targetFileSize);
        PutStr((STRPTR)outputBuffer);
    }

    FreeIFFSound(sound);
    if (IFFParseBase) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
    }
    return (int)RETURN_OK;
}
