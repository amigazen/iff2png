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

/*
** ExtractBitsFromPlane - Optimized bitplane extraction
** Extracts bits from a plane buffer and sets corresponding bits in pixel array
** Processes 8 pixels at a time for better performance
** 
** planeBuffer: Source plane data (rowBytes bytes)
** pixelArray: Destination pixel array (width elements)
** width: Image width in pixels
** rowBytes: Number of bytes per row in plane buffer
** planeBit: Bit position to set (0-31)
** 
** Returns: Nothing (void function)
*/
static VOID ExtractBitsFromPlane(const UBYTE *planeBuffer, UBYTE *pixelArray, 
                                 UWORD width, UWORD rowBytes, UBYTE planeBit)
{
    UWORD byteIdx;
    UWORD col;
    UBYTE byte;
    UBYTE bitMask;
    
    /* Process whole bytes (8 pixels at a time) */
    for (byteIdx = 0; byteIdx < rowBytes; byteIdx++) {
        byte = planeBuffer[byteIdx];
        col = byteIdx * 8;
        
        /* Process up to 8 pixels from this byte */
        bitMask = 0x80; /* Start with MSB (bit 7) */
        while (bitMask != 0 && col < width) {
            if (byte & bitMask) {
                pixelArray[col] |= (1 << planeBit);
            }
            bitMask >>= 1; /* Shift to next bit (MSB to LSB) */
            col++;
        }
    }
}

/*
** ExtractAlphaFromPlane - Optimized alpha channel extraction from mask plane
** Extracts bits from a plane buffer and sets alpha values (0xFF or 0x00)
** Processes 8 pixels at a time for better performance
** 
** planeBuffer: Source plane data (rowBytes bytes)
** alphaArray: Destination alpha array (width elements)
** width: Image width in pixels
** rowBytes: Number of bytes per row in plane buffer
** 
** Returns: Nothing (void function)
*/
static VOID ExtractAlphaFromPlane(const UBYTE *planeBuffer, UBYTE *alphaArray,
                                  UWORD width, UWORD rowBytes)
{
    UWORD byteIdx;
    UWORD col;
    UBYTE byte;
    UBYTE bitMask;
    
    /* Process whole bytes (8 pixels at a time) */
    for (byteIdx = 0; byteIdx < rowBytes; byteIdx++) {
        byte = planeBuffer[byteIdx];
        col = byteIdx * 8;
        
        /* Process up to 8 pixels from this byte */
        bitMask = 0x80; /* Start with MSB (bit 7) */
        while (bitMask != 0 && col < width) {
            alphaArray[col] = (byte & bitMask) ? 0xFF : 0x00;
            bitMask >>= 1; /* Shift to next bit (MSB to LSB) */
            col++;
        }
    }
}

/* FAXX compression constants */
#define FXCMPNONE   0
#define FXCMPMH     1
#define FXCMPMR     2
#define FXCMPMMR    4

/* MR (Modified READ) opcodes */
#define OP_P    -5
#define OP_H    -6
#define OP_VR3  -7
#define OP_VR2  -8
#define OP_VR1  -9
#define OP_V   -10
#define OP_VL1 -11
#define OP_VL2 -12
#define OP_VL3 -13
#define OP_EXT -14

#define DECODE_OPCODE_BITS 4

/* Opcode lookup table removed - not used in current implementation */

/* Bitstream reader for FAXX compressed data */
typedef struct {
    struct IFFHandle *iff;
    UBYTE currentByte;
    ULONG bitPos;  /* Bit position within current byte (0-7, MSB first) */
    BOOL eof;
    LONG bytesRemaining;
} FaxBitstream;

/*
** InitFaxBitstream - Initialize bitstream reader
*/
static VOID InitFaxBitstream(FaxBitstream *bs, struct IFFHandle *iff)
{
    bs->iff = iff;
    bs->currentByte = 0;
    bs->bitPos = 8; /* Force read on first bit */
    bs->eof = FALSE;
    bs->bytesRemaining = -1; /* Unknown */
}

/*
** ReadFaxBit - Read a single bit from FAXX compressed stream
** Returns: 0, 1, or -1 on error/EOF
*/
static LONG ReadFaxBit(FaxBitstream *bs)
{
    LONG bytesRead;
    
    if (bs->eof) {
        return -1;
    }
    
    /* Need to read new byte? */
    if (bs->bitPos >= 8) {
        bytesRead = ReadChunkBytes(bs->iff, &bs->currentByte, 1);
        if (bytesRead != 1) {
            bs->eof = TRUE;
            return -1;
        }
        bs->bitPos = 0;
    }
    
    /* Extract bit (MSB first) */
    {
        LONG bit;
        bit = (bs->currentByte >> (7 - bs->bitPos)) & 1;
        bs->bitPos++;
        return bit;
    }
}

/*
** SkipToEOL - Skip to End of Line marker (0x0001) in FAXX stream
** FAXX data starts with EOL and ends with RTC (6 consecutive EOLs)
** EOL is 11 zeros followed by 1 (000000000001)
** 
** According to ITU-T T.4, EOL markers may be preceded by fill bits (0s)
** to ensure byte alignment. We need to handle this correctly.
*/
static LONG SkipToEOL(FaxBitstream *bs)
{
    LONG bit;
    ULONG consecutiveZeros;
    ULONG maxZeros;
    
    consecutiveZeros = 0;
    maxZeros = 0;
    
    while (!bs->eof) {
        bit = ReadFaxBit(bs);
        if (bit < 0) {
            return -1;
        }
        
        if (bit == 0) {
            consecutiveZeros++;
            /* Track maximum consecutive zeros seen */
            if (consecutiveZeros > maxZeros) {
                maxZeros = consecutiveZeros;
            }
            
            /* Check for EOL: exactly 11 zeros followed by 1 */
            if (consecutiveZeros == 11) {
                bit = ReadFaxBit(bs);
                if (bit < 0) {
                    return -1;
                }
                if (bit == 1) {
                    /* Found EOL - align to byte boundary if needed */
                    /* Fill bits may follow EOL to reach byte boundary */
                    while ((bs->bitPos % 8) != 0 && !bs->eof) {
                        bit = ReadFaxBit(bs);
                        if (bit < 0) {
                            break;
                        }
                        /* Consume fill bits (should be 0) */
                    }
                    return 0; /* Found EOL */
                }
                /* Not an EOL - reset counter but keep the 1 bit we just read */
                consecutiveZeros = 0;
            } else if (consecutiveZeros > 11) {
                /* Too many zeros - reset counter */
                consecutiveZeros = 0;
            }
        } else {
            /* Non-zero bit resets counter */
            consecutiveZeros = 0;
        }
    }
    return -1;
}

/* ITU-T T.4 Code Tables for Modified Huffman (MH) */
/* 
** Correct ITU-T T.4 tables extracted from netpbm source code.
** Source: https://gitlab.apertis.org/pkg/netpbm-free
** 
** The tables are in ITU-T Recommendation T.4, Table 1 (white runs)
** and Table 2 (black runs), plus makeup codes for runs >= 64.
*/

/* White run length codes (terminating codes for runs 0-63) */
/* From ITU-T T.4 Table 1 - extracted from netpbm whitehuff encoding table */
static const struct {
    UWORD code;
    UBYTE bits;
    UWORD run;
} mh_white_codes[] = {
    {0x35, 8, 0}, {0x07, 6, 1}, {0x07, 4, 2}, {0x08, 4, 3}, {0x0b, 4, 4}, {0x0c, 4, 5},
    {0x0e, 4, 6}, {0x0f, 4, 7}, {0x13, 5, 8}, {0x14, 5, 9}, {0x07, 5, 10}, {0x08, 5, 11},
    {0x08, 6, 12}, {0x03, 6, 13}, {0x34, 6, 14}, {0x35, 6, 15}, {0x2a, 6, 16}, {0x2b, 6, 17},
    {0x27, 7, 18}, {0x0c, 7, 19}, {0x08, 7, 20}, {0x17, 7, 21}, {0x03, 7, 22}, {0x04, 7, 23},
    {0x28, 7, 24}, {0x2b, 7, 25}, {0x13, 7, 26}, {0x24, 7, 27}, {0x18, 7, 28}, {0x02, 8, 29},
    {0x03, 8, 30}, {0x1a, 8, 31}, {0x1b, 8, 32}, {0x12, 8, 33}, {0x13, 8, 34}, {0x14, 8, 35},
    {0x15, 8, 36}, {0x16, 8, 37}, {0x17, 8, 38}, {0x28, 8, 39}, {0x29, 8, 40}, {0x2a, 8, 41},
    {0x2b, 8, 42}, {0x2c, 8, 43}, {0x2d, 8, 44}, {0x04, 8, 45}, {0x05, 8, 46}, {0x0a, 8, 47},
    {0x0b, 8, 48}, {0x52, 8, 49}, {0x53, 8, 50}, {0x54, 8, 51}, {0x55, 8, 52}, {0x24, 8, 53},
    {0x25, 8, 54}, {0x58, 8, 55}, {0x59, 8, 56}, {0x5a, 8, 57}, {0x5b, 8, 58}, {0x4a, 8, 59},
    {0x4b, 8, 60}, {0x32, 8, 61}, {0x33, 8, 62}, {0x34, 8, 63}
};

/* Black run length codes (terminating codes for runs 0-63) */
/* From ITU-T T.4 Table 2 - extracted from netpbm blackhuff encoding table */
static const struct {
    UWORD code;
    UBYTE bits;
    UWORD run;
} mh_black_codes[] = {
    {0x037, 10, 0}, {0x002, 3, 1}, {0x003, 2, 2}, {0x002, 2, 3}, {0x003, 3, 4}, {0x003, 4, 5},
    {0x002, 4, 6}, {0x003, 5, 7}, {0x005, 6, 8}, {0x004, 6, 9}, {0x004, 7, 10}, {0x005, 7, 11},
    {0x007, 7, 12}, {0x004, 8, 13}, {0x007, 8, 14}, {0x018, 9, 15}, {0x017, 10, 16}, {0x018, 10, 17},
    {0x008, 10, 18}, {0x067, 11, 19}, {0x068, 11, 20}, {0x06c, 11, 21}, {0x037, 11, 22}, {0x028, 11, 23},
    {0x017, 11, 24}, {0x018, 11, 25}, {0x0ca, 12, 26}, {0x0cb, 12, 27}, {0x0cc, 12, 28}, {0x0cd, 12, 29},
    {0x068, 12, 30}, {0x069, 12, 31}, {0x06a, 12, 32}, {0x06b, 12, 33}, {0x0d2, 12, 34}, {0x0d3, 12, 35},
    {0x0d4, 12, 36}, {0x0d5, 12, 37}, {0x0d6, 12, 38}, {0x0d7, 12, 39}, {0x06c, 12, 40}, {0x06d, 12, 41},
    {0x0da, 12, 42}, {0x0db, 12, 43}, {0x054, 12, 44}, {0x055, 12, 45}, {0x056, 12, 46}, {0x057, 12, 47},
    {0x064, 12, 48}, {0x065, 12, 49}, {0x052, 12, 50}, {0x053, 12, 51}, {0x024, 12, 52}, {0x037, 12, 53},
    {0x038, 12, 54}, {0x027, 12, 55}, {0x028, 12, 56}, {0x058, 12, 57}, {0x059, 12, 58}, {0x02b, 12, 59},
    {0x02c, 12, 60}, {0x05a, 12, 61}, {0x066, 12, 62}, {0x067, 12, 63}
};

/* Make-up codes for runs >= 64 (shared by white and black) */
/* From ITU-T T.4 - extracted from netpbm whitehuff/blackhuff encoding tables */
/* Note: White and black share the same makeup codes for runs >= 64 */
static const struct {
    UWORD code;
    UBYTE bits;
    UWORD run;
} mh_makeup_codes[] = {
    {0x01b, 5, 64}, {0x012, 5, 128}, {0x017, 6, 192}, {0x037, 7, 256}, {0x036, 8, 320},
    {0x037, 8, 384}, {0x064, 8, 448}, {0x065, 8, 512}, {0x068, 8, 576}, {0x067, 8, 640},
    {0x0cc, 9, 704}, {0x0cd, 9, 768}, {0x0d2, 9, 832}, {0x0d3, 9, 896}, {0x0d4, 9, 960},
    {0x0d5, 9, 1024}, {0x0d6, 9, 1088}, {0x0d7, 9, 1152}, {0x0d8, 9, 1216}, {0x0d9, 9, 1280},
    {0x0da, 9, 1344}, {0x0db, 9, 1408}, {0x098, 9, 1472}, {0x099, 9, 1536}, {0x09a, 9, 1600},
    {0x018, 6, 1664}, {0x09b, 9, 1728}, {0x008, 11, 1792}, {0x00c, 11, 1856}, {0x00d, 11, 1920},
    {0x012, 12, 1984}, {0x013, 12, 2048}, {0x014, 12, 2112}, {0x015, 12, 2176}, {0x016, 12, 2240},
    {0x017, 12, 2304}, {0x01c, 12, 2368}, {0x01d, 12, 2432}, {0x01e, 12, 2496}, {0x01f, 12, 2560}
};

/*
** DecodeMHRun - Decode a single run length using Modified Huffman codes
** Returns: Run length, or -1 on error
** 
** MH uses prefix codes (Huffman). Read bits one at a time and match incrementally.
** Makeup codes (runs >= 64) can be followed by terminal codes.
*/
static LONG DecodeMHRun(FaxBitstream *bs, BOOL isWhite)
{
    LONG code;
    ULONG i;
    const struct { UWORD code; UBYTE bits; UWORD run; } *table;
    ULONG tableSize;
    LONG totalRun;
    LONG bit;
    UBYTE bitsRead;
    BOOL found;
    UWORD mask;
    
    /* Select appropriate code table */
    if (isWhite) {
        table = mh_white_codes;
        tableSize = sizeof(mh_white_codes) / sizeof(mh_white_codes[0]);
    } else {
        table = mh_black_codes;
        tableSize = sizeof(mh_black_codes) / sizeof(mh_black_codes[0]);
    }
    
    totalRun = 0;
    
    /* Read makeup codes first (for runs >= 64) */
    while (1) {
        code = 0;
        bitsRead = 0;
        found = FALSE;
        
        /* Build code by reading bits one at a time and matching incrementally */
        while (bitsRead < 13 && !found) {
            bit = ReadFaxBit(bs);
            if (bit < 0) {
                if (totalRun > 0) {
                    return totalRun; /* Return what we have */
                }
                return -1;
            }
            code = (code << 1) | bit;
            bitsRead++;
            
            /* Create mask for current bit length */
            mask = (1 << bitsRead) - 1;
            
            /* Check against makeup table first (shared for white/black) */
            for (i = 0; i < sizeof(mh_makeup_codes) / sizeof(mh_makeup_codes[0]); i++) {
                if (mh_makeup_codes[i].bits == bitsRead) {
                    /* Mask code to current bit length for comparison */
                    if ((mh_makeup_codes[i].code & mask) == ((UWORD)code & mask)) {
                        totalRun += mh_makeup_codes[i].run;
                        found = TRUE;
                        break; /* Continue to read terminal code */
                    }
                }
            }
            
            /* If not makeup, check terminal code table */
            if (!found) {
                for (i = 0; i < tableSize; i++) {
                    if (table[i].bits == bitsRead) {
                        /* Mask code to current bit length for comparison */
                        if ((table[i].code & mask) == ((UWORD)code & mask)) {
                            totalRun += table[i].run;
                            return totalRun;
                        }
                    }
                }
            }
        }
        
        /* If we didn't find anything, error */
        if (!found && bitsRead >= 13) {
            if (totalRun > 0) {
                return totalRun; /* Return what we have */
            }
            return -1;
        }
        
        /* If we found makeup, continue to read terminal code */
        if (!found) {
            break;
        }
    }
    
    return totalRun;
}

/*
** DecodeMHLine - Decode a single line using Modified Huffman
** Returns: RETURN_OK on success, RETURN_FAIL on error
*/
static LONG DecodeMHLine(FaxBitstream *bs, UBYTE *output, UWORD width)
{
    UWORD pos;
    BOOL isWhite;
    LONG runLength;
    UWORD maxRuns;
    UWORD runCount;
    
    pos = 0;
    isWhite = TRUE; /* Lines start with white */
    maxRuns = width * 2; /* Maximum possible runs (alternating) */
    runCount = 0;
    
    /* Clear output buffer first */
    {
        UWORD i;
        for (i = 0; i < width; i++) {
            output[i] = 0; /* White */
        }
    }
    
    while (pos < width && runCount < maxRuns) {
        runLength = DecodeMHRun(bs, isWhite);
        if (runLength < 0) {
            /* Error or EOF - fill rest with current color */
            {
                UBYTE color;
                color = isWhite ? 0 : 1;
                while (pos < width) {
                    output[pos++] = color;
                }
            }
            break;
        }
        
        /* Bounds check */
        if (runLength > (width - pos)) {
            runLength = width - pos;
        }
        
        /* Fill output with current color */
        {
            UWORD i;
            UBYTE color;
            color = isWhite ? 0 : 1;
            for (i = 0; i < runLength && pos < width; i++) {
                output[pos++] = color;
            }
        }
        
        /* Alternate color */
        isWhite = !isWhite;
        runCount++;
    }
    
    /* Pad to width if needed */
    if (pos < width) {
        UBYTE color;
        color = isWhite ? 0 : 1;
        while (pos < width) {
            output[pos++] = color;
        }
    }
    
    return RETURN_OK;
}

/*
** FindNextChangingElement - Find next color change on a line
** Returns: Position of next changing element, or width if none found
** a0: Starting position
** color: Current color (0=white, 1=black)
*/
static UWORD FindNextChangingElement(UBYTE *line, UWORD width, UWORD a0, UBYTE color)
{
    UWORD pos;
    for (pos = a0; pos < width; pos++) {
        if (line[pos] != color) {
            return pos;
        }
    }
    return width; /* No change found */
}

/*
** FindNextChangingElementAny - Find next color change on a line (any color)
** Returns: Position of next changing element, or width if none found
** a0: Starting position
*/
static UWORD FindNextChangingElementAny(UBYTE *line, UWORD width, UWORD a0)
{
    UWORD pos;
    UBYTE startColor;
    
    if (a0 >= width) return width;
    startColor = line[a0];
    
    for (pos = a0 + 1; pos < width; pos++) {
        if (line[pos] != startColor) {
            return pos;
        }
    }
    return width; /* No change found */
}

/*
** DecodeMROpcode - Decode a single MR opcode
** Returns: Opcode value (OP_P, OP_H, OP_V, etc.) or -1 on error
** 
** Opcodes from ITU-T T.4 (extracted from netpbm):
** - OP_V: 1 bit = 1
** - OP_H: 3 bits = 001  
** - OP_VR1: 3 bits = 011
** - OP_VL1: 3 bits = 010
** - OP_P: 4 bits = 0001
** - OP_VR2: 6 bits = 000011
** - OP_VL2: 6 bits = 000010
** - OP_VR3: 7 bits = 0000011
** - OP_VL3: 7 bits = 0000010
*/
static LONG DecodeMROpcode(FaxBitstream *bs)
{
    LONG code;
    LONG bit;
    
    /* Read first bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = bit;
    
    /* Check 1-bit opcode */
    if (code == 1) {
        return OP_V;  /* 1 = OP_V */
    }
    
    /* Read second bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Read third bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Check 3-bit opcodes */
    if (code == 0x01) return OP_H;   /* 001 = OP_H */
    if (code == 0x03) return OP_VR1;  /* 011 = OP_VR1 */
    if (code == 0x02) return OP_VL1; /* 010 = OP_VL1 */
    
    /* Read fourth bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Check 4-bit opcodes */
    if (code == 0x01) return OP_P;  /* 0001 = OP_P */
    
    /* Read fifth bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Read sixth bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Check 6-bit opcodes */
    if (code == 0x03) return OP_VR2;  /* 000011 = OP_VR2 */
    if (code == 0x02) return OP_VL2;  /* 000010 = OP_VL2 */
    
    /* Read seventh bit */
    bit = ReadFaxBit(bs);
    if (bit < 0) return -1;
    code = (code << 1) | bit;
    
    /* Check 7-bit opcodes */
    if (code == 0x03) return OP_VR3;  /* 0000011 = OP_VR3 */
    if (code == 0x02) return OP_VL3;  /* 0000010 = OP_VL3 */
    
    return -1; /* Unknown opcode */
}

/*
** DecodeMRLine - Decode a single line using Modified READ (2D)
** Returns: RETURN_OK on success, RETURN_FAIL on error
** 
** MR uses 2D compression with opcodes that reference the previous line.
** - OP_P (Pass): Skip b2 on reference line (a0 = b2)
** - OP_H (Horizontal): Two runs (white then black), store positions
** - OP_V (Vertical): a0 = b1, color changes
** - OP_VR1/VR2/VR3: a0 = b1 + offset, color changes
** - OP_VL1/VL2/VL3: a0 = b1 - offset, color changes
*/
static LONG DecodeMRLine(FaxBitstream *bs, UBYTE *output, UBYTE *refLine, UWORD width)
{
    UWORD *curline;   /* Changing element positions on current line */
    UWORD *curpos;    /* Pointer to current position in curline */
    UWORD curposIndex; /* Index into curline array */
    UWORD a0;         /* Current decoding position */
    BOOL isWhite;     /* Current color (TRUE=white, FALSE=black) */
    LONG opcode;
    LONG runLength;
    UWORD maxPositions;
    
    /* Allocate array for changing element positions on current line */
    maxPositions = width + 2; /* Worst case: every pixel changes + sentinel */
    curline = (UWORD *)AllocMem(maxPositions * sizeof(UWORD), MEMF_PUBLIC | MEMF_CLEAR);
    if (!curline) {
        return RETURN_FAIL;
    }
    
    /* Initialize */
    a0 = 0;
    isWhite = TRUE; /* Lines start with white */
    curposIndex = 0;
    
    /* Helper function to find b1 and b2 on reference line */
    /* b1: first changing element to the right of a0 with OPPOSITE color to isWhite */
    /* b2: next changing element after b1 */
    
    /* Decode line using MR algorithm */
    do {
        UWORD b1, b2;
        
        /* Find b1: first transition on refLine to the right of a0 with opposite color */
        b1 = FindNextChangingElement(refLine, width, a0, isWhite ? 1 : 0);
        if (b1 >= width) {
            b1 = width; /* No transition found */
        }
        
        /* Find b2: next transition after b1 */
        if (b1 < width) {
            b2 = FindNextChangingElementAny(refLine, width, b1 + 1);
            if (b2 >= width) {
                b2 = width;
            }
        } else {
            b2 = width;
        }
        
        opcode = DecodeMROpcode(bs);
        if (opcode < 0) {
            /* Error - free and return */
            FreeMem(curline, maxPositions * sizeof(UWORD));
            return RETURN_FAIL;
        }
        
        if (opcode == OP_P) {
            /* Pass mode: a0 = b2 */
            a0 = b2;
            if (a0 >= width) {
                break;
            }
        } else if (opcode == OP_H) {
            /* Horizontal mode: two runs */
            runLength = DecodeMHRun(bs, isWhite);
            if (runLength < 0) {
                FreeMem(curline, maxPositions * sizeof(UWORD));
                return RETURN_FAIL;
            }
            a0 += runLength;
            if (a0 > width) a0 = width;
            curline[curposIndex++] = a0;
            isWhite = !isWhite;
            
            runLength = DecodeMHRun(bs, isWhite);
            if (runLength < 0) {
                FreeMem(curline, maxPositions * sizeof(UWORD));
                return RETURN_FAIL;
            }
            a0 += runLength;
            if (a0 > width) a0 = width;
            curline[curposIndex++] = a0;
            isWhite = !isWhite;
        } else if ((opcode >= OP_VL3) && (opcode <= OP_VR3)) {
            /* Vertical modes: a0 = b1 + (opcode - OP_V) */
            if (b1 >= width) {
                /* No b1 available - fill rest with current color and break */
                while (a0 < width) {
                    curline[curposIndex++] = width;
                    a0 = width;
                }
                break;
            }
            a0 = (UWORD)((LONG)b1 + (opcode - OP_V));
            if (a0 > width) {
                a0 = width;
            }
            if ((LONG)b1 + (opcode - OP_V) < 0) {
                /* Underflow - shouldn't happen, but handle gracefully */
                a0 = 0;
            }
            curline[curposIndex++] = a0;
            isWhite = !isWhite;
        } else {
            /* Unknown opcode */
            FreeMem(curline, maxPositions * sizeof(UWORD));
            return RETURN_FAIL;
        }
    } while (a0 < width);
    
    /* Add sentinel */
    curline[curposIndex] = width + 1;
    
    /* Convert changing element positions to pixel data */
    {
        UWORD pos;
        UBYTE currentColor;
        
        pos = 0;
        currentColor = 0; /* Always start with white (0 = white, 1 = black) */
        
        for (curpos = curline; *curpos <= width; curpos++) {
            /* Fill from pos to *curpos with currentColor */
            /* This represents one complete run */
            while (pos < *curpos && pos < width) {
                output[pos] = currentColor;
                pos++;
            }
            /* Toggle color for next run */
            currentColor = (currentColor == 0) ? 1 : 0;
        }
        
        /* Fill any remaining pixels with last color */
        while (pos < width) {
            output[pos] = currentColor;
            pos++;
        }
    }
    
    FreeMem(curline, maxPositions * sizeof(UWORD));
    return RETURN_OK;
}

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
        } else if (code == 128) {
            /* Photoshop writes ILBM PackBits with 0x80 as repeat-129 (EA IFF appendix); treat as repeat. */
            count = 129;
            if (count > bytesLeft) {
                return -1;
            }
            bytesRead = ReadChunkBytes(iff, &value, 1);
            if (bytesRead != 1) {
                return -1;
            }
            while (count > 0) {
                *out++ = value;
                bytesLeft--;
                count--;
            }
        } else {
            /* Repeat run: next byte repeated (256-code)+1 times */
            count = 256 - code;
            if ((count + 1) > bytesLeft) {
                return -1; /* Would overflow */
            }
            bytesRead = ReadChunkBytes(iff, &value, 1);
            if (bytesRead != 1) {
                return -1; /* Error reading */
            }
            while (count >= 0) {
                *out++ = value;
                bytesLeft--;
                count--;
            }
        }
    }
    
    return destBytes - bytesLeft;
}

/*
** UnpackByteRun1Buffer - Decompress ByteRun1 (PackBits) from memory buffer.
** Returns: RETURN_OK on success, RETURN_FAIL on error.
*/
static LONG UnpackByteRun1Buffer(const UBYTE *src, ULONG srcLen, UBYTE *dest, ULONG destLen)
{
    ULONG inPos = 0;
    ULONG outPos = 0;
    UBYTE n;
    ULONG count;
    UBYTE val;

    while (inPos < srcLen && outPos < destLen) {
        n = src[inPos++];
        if (n <= 127) {
            count = (ULONG)(n + 1);
            if (inPos + count > srcLen || outPos + count > destLen) return RETURN_FAIL;
            CopyMem((APTR)(src + inPos), dest + outPos, count);
            inPos += count;
            outPos += count;
        } else if (n == 128) {
            count = 129;
            if (inPos >= srcLen || outPos + count > destLen) return RETURN_FAIL;
            val = src[inPos++];
            while (count-- > 0) dest[outPos++] = val;
        } else {
            count = 257 - (ULONG)n;
            if (inPos >= srcLen || outPos + count > destLen) return RETURN_FAIL;
            val = src[inPos++];
            while (count-- > 0) dest[outPos++] = val;
        }
    }
    return (outPos == destLen) ? RETURN_OK : RETURN_FAIL;
}

/*
** UnpackByteRun1Exact - Decompress exactly destLen bytes from buffer; return source bytes consumed.
** Returns: number of source bytes consumed, or -1 on error.
** Used for ILBM compression 2 (column-wise ByteRun1).
*/
static LONG UnpackByteRun1Exact(const UBYTE *src, ULONG srcLen, UBYTE *dest, ULONG destLen)
{
    ULONG inPos = 0;
    ULONG outPos = 0;
    UBYTE n;
    ULONG count;
    UBYTE val;

    while (inPos < srcLen && outPos < destLen) {
        n = src[inPos++];
        if (n <= 127) {
            count = (ULONG)(n + 1);
            if (inPos + count > srcLen || outPos + count > destLen) return -1;
            CopyMem((APTR)(src + inPos), dest + outPos, count);
            inPos += count;
            outPos += count;
        } else if (n == 128) {
            count = 129;
            if (inPos >= srcLen || outPos + count > destLen) return -1;
            val = src[inPos++];
            while (count-- > 0) dest[outPos++] = val;
        } else {
            count = 257 - (ULONG)n;
            if (inPos >= srcLen || outPos + count > destLen) return -1;
            val = src[inPos++];
            while (count-- > 0) dest[outPos++] = val;
        }
    }
    return (outPos == destLen) ? (LONG)inPos : -1;
}

/*
** PrepareBodyDecodeBufferColumnWise - For ILBM compression 2 (Column-wise ByteRun1),
** read entire BODY, decompress column-by-column into row-major buffer, set picture->bodyDecodeBuffer.
** Returns: RETURN_OK on success, RETURN_FAIL on error.
*/
static LONG PrepareBodyDecodeBufferColumnWise(struct IFFPicture *picture)
{
    UWORD width, height, depth, rowBytes;
    ULONG nPlanes;
    ULONG rawSize;
    UBYTE *bodyBuf;
    UBYTE *rowMajorBuf;
    UBYTE *colBuf;
    ULONG inPos;
    LONG consumed;
    UWORD x, p, y;
    struct ContextNode *cn;

    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    nPlanes = (ULONG)depth + (picture->bmhd->masking == mskHasMask ? 1UL : 0UL);
    rawSize = (ULONG)rowBytes * nPlanes * (ULONG)height;

    cn = CurrentChunk(picture->iff);
    if (!cn || cn->cn_ID != ID_BODY || cn->cn_Size == 0) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Expected BODY for compression 2");
        return RETURN_FAIL;
    }

    bodyBuf = (UBYTE *)AllocMem(cn->cn_Size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!bodyBuf) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BODY buffer for compression 2");
        return RETURN_FAIL;
    }
    if (ReadChunkBytes(picture->iff, bodyBuf, cn->cn_Size) != cn->cn_Size) {
        FreeMem(bodyBuf, cn->cn_Size);
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read BODY for compression 2");
        return RETURN_FAIL;
    }

    rowMajorBuf = (UBYTE *)AllocMem(rawSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rowMajorBuf) {
        FreeMem(bodyBuf, cn->cn_Size);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate decode buffer for compression 2");
        return RETURN_FAIL;
    }

    colBuf = (UBYTE *)AllocMem(height, MEMF_PUBLIC | MEMF_CLEAR);
    if (!colBuf) {
        FreeMem(rowMajorBuf, rawSize);
        FreeMem(bodyBuf, cn->cn_Size);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate column buffer for compression 2");
        return RETURN_FAIL;
    }

    inPos = 0;
    for (x = 0; x < rowBytes; x++) {
        for (p = 0; p < (UWORD)nPlanes; p++) {
            if (inPos >= (ULONG)cn->cn_Size) {
                FreeMem(colBuf, height);
                FreeMem(rowMajorBuf, rawSize);
                FreeMem(bodyBuf, cn->cn_Size);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Compression 2 BODY truncated");
                return RETURN_FAIL;
            }
            consumed = UnpackByteRun1Exact(bodyBuf + inPos, cn->cn_Size - inPos, colBuf, (ULONG)height);
            if (consumed < 0) {
                FreeMem(colBuf, height);
                FreeMem(rowMajorBuf, rawSize);
                FreeMem(bodyBuf, cn->cn_Size);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Compression 2 ByteRun1 decode failed");
                return RETURN_FAIL;
            }
            inPos += (ULONG)consumed;
            for (y = 0; y < height; y++) {
                rowMajorBuf[y * nPlanes * rowBytes + p * rowBytes + x] = colBuf[y];
            }
        }
    }

    FreeMem(colBuf, height);
    FreeMem(bodyBuf, cn->cn_Size);
    picture->bodyDecodeBuffer = rowMajorBuf;
    picture->bodyDecodeOffset = 0;
    picture->bodyDecodeSize = rawSize;
    return RETURN_OK;
}

/*
** PrepareBodyDecodeBufferRowMajor - For ILBM compression 1 (standard ByteRun1): read the full BODY
** chunk into memory, then expand PackBits with UnpackByteRun1Exact for each (row, plane) rowBytes
** slice into one row-major buffer (same layout as sequential ReadBodyRow). This avoids relying on
** the IFF handle's incremental read position across many small reads, which can diverge from a
** single-buffer unpack on some iffparse/DOS stream combinations; reference decoders read the chunk
** once then decompress (matches iff_png_compare.py and picture.datatype-style behaviour).
*/
static LONG PrepareBodyDecodeBufferRowMajor(struct IFFPicture *picture)
{
    UWORD width, height, depth, rowBytes;
    ULONG nPlanes;
    ULONG rawSize;
    UBYTE *bodyBuf;
    UBYTE *rowMajorBuf;
    ULONG inPos;
    LONG consumed;
    UWORD y, p;
    struct ContextNode *cn;
    ULONG remaining;
    ULONG cnSize;
    UBYTE *rowDest;
    BOOL bodyBufOwned;

    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    nPlanes = (ULONG)depth;
    if (picture->bmhd->masking == mskHasMask) {
        nPlanes++;
    }
    rawSize = (ULONG)rowBytes * nPlanes * (ULONG)height;

    cn = CurrentChunk(picture->iff);
    if (!cn || cn->cn_ID != ID_BODY || cn->cn_Size == 0) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Expected BODY for row-major ByteRun1 preload");
        return RETURN_FAIL;
    }
    cnSize = (ULONG)cn->cn_Size;

    if (picture->bodyDecodeBuffer) {
        FreeMem(picture->bodyDecodeBuffer, picture->bodyDecodeSize);
        picture->bodyDecodeBuffer = NULL;
        picture->bodyDecodeOffset = 0;
        picture->bodyDecodeSize = 0;
    }

    bodyBufOwned = TRUE;
    if (picture->bodyReadCache && picture->bodyReadCacheSize > 0) {
        bodyBuf = picture->bodyReadCache;
        cnSize = picture->bodyReadCacheSize;
        bodyBufOwned = FALSE;
    } else {
        bodyBuf = (UBYTE *)AllocMem(cnSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (!bodyBuf) {
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BODY buffer for ByteRun1 preload");
            return RETURN_FAIL;
        }
        if (ReadChunkBytes(picture->iff, bodyBuf, (LONG)cnSize) != (LONG)cnSize) {
            FreeMem(bodyBuf, cnSize);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read BODY for ByteRun1 preload");
            return RETURN_FAIL;
        }
        picture->bodyReadCache = bodyBuf;
        picture->bodyReadCacheSize = cnSize;
        picture->bodyReadRawOffset = 0;
        bodyBufOwned = FALSE;
    }

    rowMajorBuf = (UBYTE *)AllocMem(rawSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rowMajorBuf) {
        if (bodyBufOwned) {
            FreeMem(bodyBuf, cnSize);
        }
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate decode buffer for ByteRun1 preload");
        return RETURN_FAIL;
    }

    inPos = 0;
    for (y = 0; y < height; y++) {
        for (p = 0; p < (UWORD)nPlanes; p++) {
            if (inPos > cnSize) {
                FreeMem(rowMajorBuf, rawSize);
                if (bodyBufOwned) {
                    FreeMem(bodyBuf, cnSize);
                }
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 preload overran BODY");
                return RETURN_FAIL;
            }
            remaining = cnSize - inPos;
            rowDest = rowMajorBuf + (((ULONG)y * nPlanes) + (ULONG)p) * (ULONG)rowBytes;
            consumed = UnpackByteRun1Exact(bodyBuf + inPos, remaining, rowDest, (ULONG)rowBytes);
            if (consumed < 0) {
                FreeMem(rowMajorBuf, rawSize);
                if (bodyBufOwned) {
                    FreeMem(bodyBuf, cnSize);
                }
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 plane row decode failed in preload");
                return RETURN_FAIL;
            }
            inPos += (ULONG)consumed;
        }
    }

    if (bodyBufOwned) {
        FreeMem(bodyBuf, cnSize);
    }

    if (inPos != cnSize) {
        FreeMem(rowMajorBuf, rawSize);
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 preload size mismatch");
        return RETURN_FAIL;
    }

    picture->bodyDecodeBuffer = rowMajorBuf;
    picture->bodyDecodeOffset = 0;
    picture->bodyDecodeSize = rawSize;
    return RETURN_OK;
}

/*
** ReadBodyRow - Read one row of BODY data (for ILBM). Uses bodyDecodeBuffer when set
** (compression 2 column-wise preload or compression 1 row-major preload), else streaming
** ByteRun1 from the IFF handle or raw chunk read. Returns: bytes read, or -1 on error.
*/
static LONG ReadBodyRow(struct IFFPicture *picture, UBYTE *dest, ULONG rowBytes)
{
    if (picture->bodyDecodeBuffer) {
        if (picture->bodyDecodeOffset + rowBytes > picture->bodyDecodeSize) return -1;
        CopyMem(picture->bodyDecodeBuffer + picture->bodyDecodeOffset, dest, rowBytes);
        picture->bodyDecodeOffset += rowBytes;
        return (LONG)rowBytes;
    }
    if (picture->bmhd->compression == cmpByteRun1) {
        return DecompressByteRun1(picture->iff, dest, (LONG)rowBytes);
    }
    if (picture->bmhd->compression != cmpNone) {
        return -1;  /* Unsupported compression (e.g. > 2) */
    }
    if (picture->bodyReadCache && picture->bodyReadRawOffset + rowBytes <= picture->bodyReadCacheSize) {
        CopyMem(picture->bodyReadCache + picture->bodyReadRawOffset, dest, rowBytes);
        picture->bodyReadRawOffset += rowBytes;
        return (LONG)rowBytes;
    }
    return (LONG)ReadChunkBytes(picture->iff, dest, rowBytes);
}

/*
** ilbm_want_dest - TRUE if DEST chunk changes how source planes map to pixel indices.
*/
static BOOL ilbm_want_dest(struct DestMerge *dest, UBYTE nPlanes)
{
    UWORD full;
    if (!dest) {
        return FALSE;
    }
    if (nPlanes == 0 || nPlanes > 16) {
        return TRUE;
    }
    full = (UWORD)((1UL << nPlanes) - 1UL);
    if (dest->depth != nPlanes) {
        return TRUE;
    }
    if (dest->planeMask != full || dest->planePick != full) {
        return TRUE;
    }
    if (dest->planeOnOff != 0) {
        return TRUE;
    }
    return FALSE;
}

/*
** ilbm_dest_remap_pixel - Scatter nPlanes source bits into dest depth per DEST (EA ILBM).
*/
static UWORD ilbm_dest_remap_pixel(struct DestMerge *dest, UWORD srcBits, UBYTE nPlanes)
{
    UWORD outv;
    UWORD j;
    UWORD bit;
    UBYTE si;
    UWORD dlim;
    UWORD pick;
    UWORD onoff;
    UWORD mask;
    
    if (!dest || dest->depth == 0) {
        return srcBits;
    }
    pick = dest->planePick;
    onoff = dest->planeOnOff;
    mask = dest->planeMask;
    outv = 0;
    si = 0;
    dlim = dest->depth;
    if (dlim > 16) {
        dlim = 16;
    }
    for (j = 0; j < dlim; j++) {
        bit = (UWORD)(1U << j);
        if ((mask & bit) == 0) {
            continue;
        }
        if (pick & bit) {
            if (si >= nPlanes) {
                return srcBits;
            }
            if (srcBits & ((UWORD)1U << si)) {
                outv |= bit;
            }
            si++;
        } else {
            if (onoff & bit) {
                outv |= bit;
            }
        }
    }
    return outv;
}

/*
** ilbm_lasso_alpha - Flood from image border through transparentIdx; matched region -> alpha 0 (ILBM lasso).
*/
static VOID ilbm_lasso_alpha(UBYTE *alpha, const UBYTE *indices, UWORD w, UWORD h, UWORD transparentIdx)
{
    ULONG *queue;
    UBYTE *visited;
    ULONG qs;
    ULONG qe;
    ULONG qcap;
    ULONG x;
    ULONG y;
    ULONG idx;
    ULONG n;
    ULONG nx;
    ULONG ny;
    ULONG i;
    UBYTE t;
    
    n = (ULONG)w * (ULONG)h;
    t = (UBYTE)(transparentIdx & 0xFFU);
    for (i = 0; i < n; i++) {
        alpha[i] = 255;
    }
    if (n == 0) {
        return;
    }
    qcap = n;
    queue = (ULONG *)AllocMem(qcap * sizeof(ULONG), MEMF_PUBLIC | MEMF_CLEAR);
    visited = (UBYTE *)AllocMem(n * sizeof(UBYTE), MEMF_PUBLIC | MEMF_CLEAR);
    if (!queue || !visited) {
        if (queue) {
            FreeMem(queue, qcap * sizeof(ULONG));
        }
        if (visited) {
            FreeMem(visited, n * sizeof(UBYTE));
        }
        return;
    }
    qs = 0;
    qe = 0;
    for (x = 0; x < (ULONG)w; x++) {
        idx = x;
        if (indices[idx] == t) {
            visited[idx] = 1;
            queue[qe++] = idx;
        }
        idx = (ULONG)(h - 1U) * (ULONG)w + x;
        if (indices[idx] == t) {
            visited[idx] = 1;
            queue[qe++] = idx;
        }
    }
    for (y = 0; y < (ULONG)h; y++) {
        idx = y * (ULONG)w;
        if (indices[idx] == t && !visited[idx]) {
            visited[idx] = 1;
            queue[qe++] = idx;
        }
        idx = y * (ULONG)w + (ULONG)(w - 1U);
        if (indices[idx] == t && !visited[idx]) {
            visited[idx] = 1;
            queue[qe++] = idx;
        }
    }
    while (qs < qe) {
        idx = queue[qs++];
        alpha[idx] = 0;
        x = idx % (ULONG)w;
        y = idx / (ULONG)w;
        if (x > 0) {
            nx = idx - 1;
            if (!visited[nx] && indices[nx] == t) {
                visited[nx] = 1;
                queue[qe++] = nx;
            }
        }
        if (x + 1 < (ULONG)w) {
            nx = idx + 1;
            if (!visited[nx] && indices[nx] == t) {
                visited[nx] = 1;
                queue[qe++] = nx;
            }
        }
        if (y > 0) {
            ny = idx - (ULONG)w;
            if (!visited[ny] && indices[ny] == t) {
                visited[ny] = 1;
                queue[qe++] = ny;
            }
        }
        if (y + 1 < (ULONG)h) {
            ny = idx + (ULONG)w;
            if (!visited[ny] && indices[ny] == t) {
                visited[ny] = 1;
                queue[qe++] = ny;
            }
        }
    }
    FreeMem(queue, qcap * sizeof(ULONG));
    FreeMem(visited, n * sizeof(UBYTE));
}

/* Video Toaster Framestore constants (quadrature YCbCr decode) */
#define VT_Y_SCALE   1.8351
#define VT_Y_OFFSET  123.25
#define VT_CB_COS    (-0.5776)
#define VT_CB_SIN    0.8752
#define VT_CR_COS    0.6313
#define VT_CR_SIN    0.5039
#define VT_R_CR      1.402
#define VT_G_CB      (-0.344136)
#define VT_G_CR      (-0.714136)
#define VT_B_CB      1.772

/*
** decode_framestore_row - Decode one scanline of 16 bitplanes into comp6 and comp7.
*/
static VOID decode_framestore_row(const UBYTE *raw_body, UWORD width, UWORD height,
    UWORD row_bytes, UWORD y, UBYTE *comp6_row, UBYTE *comp7_row)
{
    ULONG row_start;
    UWORD x;
    ULONG byte_idx;
    UWORD bit_shift;
    UWORD p;
    UBYTE comp6;
    UBYTE comp7;

    row_start = (ULONG)y * row_bytes * VT_FRAMESTORE_NPLANES;
    for (x = 0; x < width; x++) {
        byte_idx = x >> 3;
        bit_shift = 7 - (x & 7);
        comp6 = 0;
        for (p = 0; p <= 7; p++) {
            if ((raw_body[row_start + p * row_bytes + byte_idx] >> bit_shift) & 1)
                comp6 |= (UBYTE)(1 << (7 - p));
        }
        comp7 = 0;
        for (p = 8; p <= 15; p++) {
            if ((raw_body[row_start + p * row_bytes + byte_idx] >> bit_shift) & 1)
                comp7 |= (UBYTE)(1 << (15 - p));
        }
        comp6_row[x] = comp6;
        comp7_row[x] = comp7;
    }
}

/*
** framestore_row_to_rgb - Convert one row comp6/comp7 to RGB (quadrature chroma, BT.601).
*/
static VOID framestore_row_to_rgb(UWORD width, UWORD y,
    const UBYTE *comp6_row, const UBYTE *comp7_row, UBYTE *rgb_row)
{
    double row_sign;
    UWORD x;
    UWORD x0;
    double diff_signed[4];
    double cos_comp;
    double sin_comp;
    double Cb;
    double Cr;
    double Y_raw;
    double Y_disp;
    double R, G, B;
    LONG r, g, b;

    row_sign = ((y % 4) == 0 || (y % 4) == 3) ? -1.0 : 1.0;
    for (x0 = 0; x0 < width; x0 += 4) {
        if (x0 + 4 <= width) {
            diff_signed[0] = row_sign * (double)((LONG)comp6_row[x0+0] - (LONG)comp7_row[x0+0]);
            diff_signed[1] = row_sign * (double)((LONG)comp6_row[x0+1] - (LONG)comp7_row[x0+1]);
            diff_signed[2] = row_sign * (double)((LONG)comp6_row[x0+2] - (LONG)comp7_row[x0+2]);
            diff_signed[3] = row_sign * (double)((LONG)comp6_row[x0+3] - (LONG)comp7_row[x0+3]);
            cos_comp = (diff_signed[0] - diff_signed[2]) * 0.5;
            sin_comp = (diff_signed[1] - diff_signed[3]) * 0.5;
        } else {
            cos_comp = 0.0;
            sin_comp = 0.0;
            if (x0 + 1 <= width)
                cos_comp = row_sign * (double)((LONG)comp6_row[x0] - (LONG)comp7_row[x0]) * 0.5;
            if (x0 + 2 <= width)
                sin_comp = row_sign * (double)((LONG)comp6_row[x0+1] - (LONG)comp7_row[x0+1]) * 0.5;
        }
        Cb = VT_CB_COS * cos_comp + VT_CB_SIN * sin_comp;
        Cr = VT_CR_COS * cos_comp + VT_CR_SIN * sin_comp;
        for (x = x0; x < x0 + 4 && x < width; x++) {
            Y_raw = ((double)comp6_row[x] + (double)comp7_row[x]) * 0.5;
            Y_disp = VT_Y_SCALE * Y_raw - VT_Y_OFFSET;
            if (Y_disp < 0.0) Y_disp = 0.0;
            if (Y_disp > 255.0) Y_disp = 255.0;
            R = Y_disp + VT_R_CR * Cr;
            G = Y_disp + VT_G_CB * Cb + VT_G_CR * Cr;
            B = Y_disp + VT_B_CB * Cb;
            if (R < 0.0) R = 0.0; if (R > 255.0) R = 255.0;
            if (G < 0.0) G = 0.0; if (G > 255.0) G = 255.0;
            if (B < 0.0) B = 0.0; if (B > 255.0) B = 255.0;
            r = (LONG)R; g = (LONG)G; b = (LONG)B;
            rgb_row[x * 3 + 0] = (UBYTE)r;
            rgb_row[x * 3 + 1] = (UBYTE)g;
            rgb_row[x * 3 + 2] = (UBYTE)b;
        }
    }
}

/*
** DecodeFramestore - Decode NewTek Video Toaster framestore (16-plane ILBM + PLTP) to RGB.
** Returns: RETURN_OK on success, RETURN_FAIL on error.
** Format: BMHD nPlanes==16, PLTP maps planes 0-7 to component 6, 8-15 to component 7.
** Luma from (comp6+comp7)/2, chroma from quadrature (comp6-comp7); BT.601 RGB.
*/
LONG DecodeFramestore(struct IFFPicture *picture)
{
    UWORD width, height;
    UWORD rowBytes;
    ULONG rawLen;
    UBYTE *bodyBuf;
    UBYTE *rawBody;
    UBYTE *comp6_row;
    UBYTE *comp7_row;
    UBYTE *rgb_row;
    UWORD y;
    LONG bytesRead;
    LONG result;

    if (!picture || !picture->bmhd || !picture->pltp) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD or PLTP for Framestore decoding");
        return RETURN_FAIL;
    }
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    if (picture->bmhd->nPlanes != VT_FRAMESTORE_NPLANES) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Framestore requires 16 bitplanes");
        return RETURN_FAIL;
    }
    rowBytes = RowBytes(width);
    rawLen = (ULONG)height * VT_FRAMESTORE_NPLANES * rowBytes;

    bodyBuf = (UBYTE *)AllocMem(picture->bodyChunkSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!bodyBuf) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BODY buffer for Framestore");
        return RETURN_FAIL;
    }
    bytesRead = ReadChunkBytes(picture->iff, bodyBuf, picture->bodyChunkSize);
    if (bytesRead != (LONG)picture->bodyChunkSize) {
        FreeMem(bodyBuf, picture->bodyChunkSize);
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read BODY for Framestore");
        return RETURN_FAIL;
    }

    rawBody = (UBYTE *)AllocMem(rawLen, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rawBody) {
        FreeMem(bodyBuf, picture->bodyChunkSize);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate decompression buffer for Framestore");
        return RETURN_FAIL;
    }
    if (picture->bmhd->compression == cmpByteRun1) {
        result = UnpackByteRun1Buffer(bodyBuf, picture->bodyChunkSize, rawBody, rawLen);
        FreeMem(bodyBuf, picture->bodyChunkSize);
        if (result != RETURN_OK) {
            FreeMem(rawBody, rawLen);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed for Framestore");
            return RETURN_FAIL;
        }
    } else {
        if (picture->bodyChunkSize != rawLen) {
            FreeMem(rawBody, rawLen);
            FreeMem(bodyBuf, picture->bodyChunkSize);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Uncompressed BODY size mismatch for Framestore");
            return RETURN_FAIL;
        }
        FreeMem(rawBody, rawLen);
        rawBody = bodyBuf;  /* Use BODY buffer as raw data; free at end */
    }

    comp6_row = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    comp7_row = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    rgb_row = (UBYTE *)AllocMem((ULONG)width * 3, MEMF_PUBLIC | MEMF_CLEAR);
    if (!comp6_row || !comp7_row || !rgb_row) {
        if (comp6_row) FreeMem(comp6_row, width);
        if (comp7_row) FreeMem(comp7_row, width);
        if (rgb_row) FreeMem(rgb_row, (ULONG)width * 3);
        FreeMem(rawBody, rawLen);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate row buffers for Framestore");
        return RETURN_FAIL;
    }

    for (y = 0; y < height; y++) {
        decode_framestore_row(rawBody, width, height, rowBytes, y, comp6_row, comp7_row);
        framestore_row_to_rgb(width, y, comp6_row, comp7_row, rgb_row);
        CopyMem(rgb_row, picture->pixelData + (ULONG)y * width * 3, (ULONG)width * 3);
    }

    FreeMem(comp6_row, width);
    FreeMem(comp7_row, width);
    FreeMem(rgb_row, (ULONG)width * 3);
    FreeMem(rawBody, rawLen);
    picture->hasAlpha = FALSE;
    return RETURN_OK;
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
**
** 24-bit ILBM (deep ILBM):
** - nPlanes = 24 (8 bits per color component)
** - Planes 0-7: Red component (R0-R7)
** - Planes 8-15: Green component (G0-G7)
** - Planes 16-23: Blue component (B0-B7)
** - No CMAP required (bits represent absolute RGB values)
*/
LONG DecodeILBM(struct IFFPicture *picture)
{
    UWORD width, height, depth;
    UWORD rowBytes;
    UBYTE *planeBuffer;
    UBYTE *rgbOut;
    UBYTE *paletteOut;
    UWORD row, plane, col;
    UBYTE pixelIndex;
    UWORD pixW;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE *alphaValues;
    BOOL is24Bit;
    BOOL is32Bit;
    BOOL isGrey8;
    BOOL digiviewRgb21;
    ULONG bpp;
    BOOL wantAlpha;
    BOOL useDest;
    struct DestMerge *destM;
    UBYTE *workPal;
    BOOL mpalOn;
    struct IFFMultipaletteState mpst;
    const UBYTE *palSrc;
    UBYTE transIdx;
    UBYTE aByte;
    ULONG pixCount;
    ULONG pi;
    UBYTE rv;
    UBYTE gv;
    UBYTE bv;
    UBYTE bitv;
    UWORD p;
    ULONG greyV;
    
    workPal = NULL;
    mpalOn = FALSE;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for ILBM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    
    is24Bit = (depth == 24U);
    is32Bit = (depth == 32U);
    isGrey8 = (depth == 8U && (!picture->cmap || !picture->cmap->data));
    digiviewRgb21 = (depth == 21U);
    
    if (!is24Bit && !is32Bit && !isGrey8 && !digiviewRgb21) {
        if (!picture->cmap || !picture->cmap->data) {
            SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing CMAP for ILBM decoding");
            return RETURN_FAIL;
        }
        cmapData = picture->cmap->data;
        maxColors = picture->cmap->numcolors;
    } else {
        cmapData = NULL;
        maxColors = 0;
    }
    
    destM = ReadDEST(picture);
    useDest = FALSE;
    if (destM && depth <= 16U && !is24Bit && !is32Bit && !isGrey8 && !digiviewRgb21) {
        useDest = ilbm_want_dest(destM, (UBYTE)depth);
    }
    
    wantAlpha = FALSE;
    bpp = 3;
    if (picture->bmhd->masking == mskHasMask
        || picture->bmhd->masking == mskHasTransparentColor
        || picture->bmhd->masking == mskLasso
        || is32Bit) {
        wantAlpha = TRUE;
        bpp = 4;
        picture->hasAlpha = TRUE;
    } else {
        picture->hasAlpha = FALSE;
    }
    
    transIdx = 0;
    if (picture->bmhd->masking == mskHasTransparentColor
        || picture->bmhd->masking == mskLasso) {
        transIdx = (UBYTE)(picture->bmhd->transparentColor & 0xFFU);
    }
    
    DEBUG_PRINTF4("DEBUG: DecodeILBM - Starting decode: %ldx%ld, %ld planes, masking=%ld\n",
                  width, height, depth, picture->bmhd->masking);
    rowBytes = RowBytes(width);
    
    /* Decode() always pre-allocates w*h*3 RGB; ILBM re-allocates here (RGB or RGBA by masking).
     * Drop the parent buffer so we do not leak and so pixelDataSize matches this decode. */
    if (picture->paletteIndices) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
    }
    if (picture->pixelData) {
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelData = NULL;
        picture->pixelDataSize = 0;
    }
    
    picture->pixelDataSize = (ULONG)width * height * bpp;
    
    /* Use public memory (not chip RAM, we're not rendering to display) */
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->pixelData) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel data buffer");
        return RETURN_FAIL;
    }
    
    /* Indexed ILBM: keep palette indices for PNG tRNS and lasso mask */
    if (!is24Bit && !is32Bit && !isGrey8 && !digiviewRgb21) {
        picture->paletteIndicesSize = (ULONG)width * height;
        picture->paletteIndices = (UBYTE *)AllocMem(picture->paletteIndicesSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (!picture->paletteIndices) {
            FreeMem(picture->pixelData, picture->pixelDataSize);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate palette indices buffer");
            return RETURN_FAIL;
        }
        paletteOut = picture->paletteIndices;
    } else {
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
        paletteOut = NULL;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        if (picture->paletteIndices) {
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        }
        FreeMem(picture->pixelData, picture->pixelDataSize);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate alpha buffer if mask plane present */
    alphaValues = NULL;
    if (picture->bmhd->masking == mskHasMask) {
        alphaValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!alphaValues) {
            FreeMem(planeBuffer, rowBytes);
            if (picture->paletteIndices) {
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            }
            FreeMem(picture->pixelData, picture->pixelDataSize);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate alpha buffer");
            return RETURN_FAIL;
        }
    }
    
    /* ILBM compression 2 (Column-wise ByteRun1): pre-decode entire BODY into row-major buffer */
    if (picture->bmhd->compression == cmpByteRun1Column) {
        if (PrepareBodyDecodeBufferColumnWise(picture) != RETURN_OK) {
            if (alphaValues) FreeMem(alphaValues, width);
            FreeMem(planeBuffer, rowBytes);
            if (picture->paletteIndices) {
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            }
            FreeMem(picture->pixelData, picture->pixelDataSize);
            return RETURN_FAIL;
        }
    }
    
    /* ILBM compression 1 (ByteRun1): pre-decode BODY from one buffer (see PrepareBodyDecodeBufferRowMajor). */
    if (picture->bmhd->compression == cmpByteRun1) {
        if (PrepareBodyDecodeBufferRowMajor(picture) != RETURN_OK) {
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            if (picture->paletteIndices) {
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            }
            FreeMem(picture->pixelData, picture->pixelDataSize);
            return RETURN_FAIL;
        }
    }
    
    if (!is24Bit && !is32Bit && !isGrey8 && !digiviewRgb21) {
        mpalOn = IFFMultipalette_Active(picture);
        if (mpalOn) {
            workPal = (UBYTE *)AllocMem(768, MEMF_PUBLIC | MEMF_CLEAR);
            if (!workPal) {
                if (alphaValues) {
                    FreeMem(alphaValues, width);
                }
                FreeMem(planeBuffer, rowBytes);
                if (picture->paletteIndices) {
                    FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                    picture->paletteIndices = NULL;
                    picture->paletteIndicesSize = 0;
                }
                FreeMem(picture->pixelData, picture->pixelDataSize);
                picture->pixelData = NULL;
                picture->pixelDataSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate multipalette buffer");
                return RETURN_FAIL;
            }
            IFFMultipalette_Init(picture, workPal, &mpst);
        }
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        if (is32Bit) {
            /* 32-bit RGBA ILBM: 24 RGB planes + 8 alpha planes (EA IFF supplement) */
            UBYTE *rValues, *gValues, *bValues, *aValues;
            
            rValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            gValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            bValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            aValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            if (!rValues || !gValues || !bValues || !aValues) {
                if (rValues) FreeMem(rValues, width);
                if (gValues) FreeMem(gValues, width);
                if (bValues) FreeMem(bValues, width);
                if (aValues) FreeMem(aValues, width);
                FreeMem(planeBuffer, rowBytes);
                if (alphaValues) FreeMem(alphaValues, width);
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate 32-bit ILBM buffers");
                return RETURN_FAIL;
            }
            for (plane = 0; plane < 8; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(aValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                ExtractBitsFromPlane(planeBuffer, rValues, width, rowBytes, plane);
            }
            for (plane = 8; plane < 16; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(aValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                ExtractBitsFromPlane(planeBuffer, gValues, width, rowBytes, plane - 8);
            }
            for (plane = 16; plane < 24; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(aValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                ExtractBitsFromPlane(planeBuffer, bValues, width, rowBytes, plane - 16);
            }
            for (plane = 24; plane < 32; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(aValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read alpha plane data");
                    return RETURN_FAIL;
                }
                ExtractBitsFromPlane(planeBuffer, aValues, width, rowBytes, plane - 24);
            }
            for (col = 0; col < width; col++) {
                rgbOut[0] = rValues[col];
                rgbOut[1] = gValues[col];
                rgbOut[2] = bValues[col];
                rgbOut[3] = aValues[col];
                rgbOut += 4;
            }
            FreeMem(rValues, width);
            FreeMem(gValues, width);
            FreeMem(bValues, width);
            FreeMem(aValues, width);
        } else if (is24Bit) {
            /* 24-bit ILBM: Decode bitplanes directly as RGB */
            /* Planes 0-7: Red, Planes 8-15: Green, Planes 16-23: Blue */
            UBYTE *rValues, *gValues, *bValues;
            
            rValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            gValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            bValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            
            if (!rValues || !gValues || !bValues) {
                if (rValues) FreeMem(rValues, width);
                if (gValues) FreeMem(gValues, width);
                if (bValues) FreeMem(bValues, width);
                FreeMem(planeBuffer, rowBytes);
                if (alphaValues) FreeMem(alphaValues, width);
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate 24-bit component buffers");
                return RETURN_FAIL;
            }
            
            /* Decode Red component (planes 0-7) */
            for (plane = 0; plane < 8; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                /* Extract bits from this plane (optimized) */
                ExtractBitsFromPlane(planeBuffer, rValues, width, rowBytes, plane);
            }
            
            /* Decode Green component (planes 8-15) */
            for (plane = 8; plane < 16; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                /* Extract bits from this plane (optimized) */
                ExtractBitsFromPlane(planeBuffer, gValues, width, rowBytes, plane - 8);
            }
            
            /* Decode Blue component (planes 16-23) */
            for (plane = 16; plane < 24; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                /* Extract bits from this plane (optimized) */
                ExtractBitsFromPlane(planeBuffer, bValues, width, rowBytes, plane - 16);
            }
            
            /* Read mask plane if present (comes after all data planes) */
            if (picture->bmhd->masking == mskHasMask) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rValues, width);
                    FreeMem(gValues, width);
                    FreeMem(bValues, width);
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read mask plane");
                    return RETURN_FAIL;
                }
                /* Extract mask bits to alpha channel (optimized) */
                ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
            }
            
            /* Write RGB output */
            for (col = 0; col < width; col++) {
                rgbOut[0] = rValues[col];
                rgbOut[1] = gValues[col];
                rgbOut[2] = bValues[col];
                
                if (picture->bmhd->masking == mskHasMask) {
                    rgbOut[3] = alphaValues[col];
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            }
            
            FreeMem(rValues, width);
            FreeMem(gValues, width);
            FreeMem(bValues, width);
        } else if (isGrey8) {
            UBYTE *pixelIndices;
            pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            if (!pixelIndices) {
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                FreeMem(planeBuffer, rowBytes);
                if (alphaValues) FreeMem(alphaValues, width);
                if (picture->paletteIndices) {
                    FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                    picture->paletteIndices = NULL;
                    picture->paletteIndicesSize = 0;
                }
                FreeMem(picture->pixelData, picture->pixelDataSize);
                picture->pixelData = NULL;
                picture->pixelDataSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate grey row buffer");
                return RETURN_FAIL;
            }
            for (plane = 0; plane < 8; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(pixelIndices, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read grey plane data");
                    return RETURN_FAIL;
                }
                ExtractBitsFromPlane(planeBuffer, pixelIndices, width, rowBytes, plane);
            }
            if (picture->bmhd->masking == mskHasMask) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(pixelIndices, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read mask plane");
                    return RETURN_FAIL;
                }
                ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
            }
            for (col = 0; col < width; col++) {
                greyV = (ULONG)pixelIndices[col];
                rgbOut[0] = (UBYTE)greyV;
                rgbOut[1] = (UBYTE)greyV;
                rgbOut[2] = (UBYTE)greyV;
                aByte = 255;
                if (picture->bmhd->masking == mskHasMask) {
                    aByte = alphaValues[col];
                } else if (picture->bmhd->masking == mskHasTransparentColor
                    && (UBYTE)greyV == transIdx) {
                    aByte = 0;
                }
                if (wantAlpha) {
                    rgbOut[3] = aByte;
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            }
            FreeMem(pixelIndices, width);
        } else if (digiviewRgb21) {
            UBYTE *rAcc;
            UBYTE *gAcc;
            UBYTE *bAcc;
            rAcc = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            gAcc = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            bAcc = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            if (!rAcc || !gAcc || !bAcc) {
                if (rAcc) FreeMem(rAcc, width);
                if (gAcc) FreeMem(gAcc, width);
                if (bAcc) FreeMem(bAcc, width);
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                FreeMem(planeBuffer, rowBytes);
                if (alphaValues) FreeMem(alphaValues, width);
                if (picture->paletteIndices) {
                    FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                    picture->paletteIndices = NULL;
                    picture->paletteIndicesSize = 0;
                }
                FreeMem(picture->pixelData, picture->pixelDataSize);
                picture->pixelData = NULL;
                picture->pixelDataSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate Digi-View RGB row buffers");
                return RETURN_FAIL;
            }
            for (p = 0; p < 21; p++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rAcc, width);
                    FreeMem(gAcc, width);
                    FreeMem(bAcc, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read Digi-View RGB plane data");
                    return RETURN_FAIL;
                }
                for (col = 0; col < width; col++) {
                    bitv = (UBYTE)((planeBuffer[col >> 3] >> (7 - (col & 7))) & 1);
                    if ((p % 3U) == 0U) {
                        rAcc[col] = (UBYTE)(rAcc[col] | (UBYTE)(bitv << (6U - (p / 3U))));
                    } else if ((p % 3U) == 1U) {
                        gAcc[col] = (UBYTE)(gAcc[col] | (UBYTE)(bitv << (6U - (p / 3U))));
                    } else {
                        bAcc[col] = (UBYTE)(bAcc[col] | (UBYTE)(bitv << (6U - (p / 3U))));
                    }
                }
            }
            if (picture->bmhd->masking == mskHasMask) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(rAcc, width);
                    FreeMem(gAcc, width);
                    FreeMem(bAcc, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read mask plane");
                    return RETURN_FAIL;
                }
                ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
            }
            for (col = 0; col < width; col++) {
                rv = (UBYTE)((rAcc[col] * 255U) / 127U);
                gv = (UBYTE)((gAcc[col] * 255U) / 127U);
                bv = (UBYTE)((bAcc[col] * 255U) / 127U);
                rgbOut[0] = rv;
                rgbOut[1] = gv;
                rgbOut[2] = bv;
                if (picture->bmhd->masking == mskHasMask) {
                    rgbOut[3] = alphaValues[col];
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            }
            FreeMem(rAcc, width);
            FreeMem(gAcc, width);
            FreeMem(bAcc, width);
        } else {
            /* Standard ILBM: Build pixel indices and look up in CMAP */
            /* Clear pixel indices for this row */
            UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
            if (!pixelIndices) {
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                FreeMem(planeBuffer, rowBytes);
                if (alphaValues) FreeMem(alphaValues, width);
                if (picture->paletteIndices) {
                    FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                    picture->paletteIndices = NULL;
                    picture->paletteIndicesSize = 0;
                }
                FreeMem(picture->pixelData, picture->pixelDataSize);
                picture->pixelData = NULL;
                picture->pixelDataSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel indices");
                return RETURN_FAIL;
            }
            
            /* PCHG/SHAM/CTBL: apply this scanline's palette updates before reading BODY bits
             * for the same row (matches picture.datatype / iff_png_compare.py; applying after
             * planes would use the previous line's colours and breaks PCHGLib14-style ILBMs). */
            if (mpalOn) {
                IFFMultipalette_ApplyScanline(&mpst, row, workPal);
                palSrc = workPal;
            } else {
                palSrc = cmapData;
            }
            
            /* Read all data planes for this row (planes 0 through nPlanes-1) */
            for (plane = 0; plane < depth; plane++) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(pixelIndices, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    if (alphaValues) FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                    return RETURN_FAIL;
                }
                /* Extract bits from this plane to build pixel indices (optimized) */
                ExtractBitsFromPlane(planeBuffer, pixelIndices, width, rowBytes, plane);
            }
            
            /* Read mask plane if present (comes after all data planes) */
            if (picture->bmhd->masking == mskHasMask) {
                bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
                if (bytesRead != (LONG)rowBytes) {
                    FreeMem(pixelIndices, width);
                    if (workPal) {
                        FreeMem(workPal, 768);
                    }
                    FreeMem(planeBuffer, rowBytes);
                    FreeMem(alphaValues, width);
                    if (picture->paletteIndices) {
                        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                        picture->paletteIndices = NULL;
                        picture->paletteIndicesSize = 0;
                    }
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    picture->pixelDataSize = 0;
                    SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read mask plane");
                    return RETURN_FAIL;
                }
                /* Extract mask bits to alpha channel (optimized) */
                ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
            }
            
            /* Convert pixel indices to RGB using CMAP; optional DEST remap; alpha from mask / transparent / lasso */
            for (col = 0; col < width; col++) {
                pixW = (UWORD)pixelIndices[col];
                if (useDest) {
                    pixW = ilbm_dest_remap_pixel(destM, pixW, (UBYTE)depth);
                }
                if (pixW >= maxColors) {
                    pixelIndex = (UBYTE)(maxColors - 1);
                } else {
                    pixelIndex = (UBYTE)(pixW & 0xFFU);
                }
                
                if (paletteOut) {
                    *paletteOut++ = pixelIndex;
                }
                
                rgbOut[0] = palSrc[pixelIndex * 3];
                rgbOut[1] = palSrc[pixelIndex * 3 + 1];
                rgbOut[2] = palSrc[pixelIndex * 3 + 2];
                
                if (picture->cmap && picture->cmap->is4Bit && !mpalOn) {
                    rgbOut[0] |= (rgbOut[0] >> 4);
                    rgbOut[1] |= (rgbOut[1] >> 4);
                    rgbOut[2] |= (rgbOut[2] >> 4);
                }
                
                aByte = 255;
                if (picture->bmhd->masking == mskHasMask) {
                    aByte = alphaValues[col];
                } else if (picture->bmhd->masking == mskHasTransparentColor
                    && pixelIndex == transIdx) {
                    aByte = 0;
                }
                if (wantAlpha) {
                    rgbOut[3] = aByte;
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            }
            
            FreeMem(pixelIndices, width);
        }
    }
    
    if (picture->bmhd->masking == mskLasso && wantAlpha && picture->paletteIndices) {
        UBYTE *lassoBuf;
        lassoBuf = (UBYTE *)AllocMem((ULONG)width * (ULONG)height, MEMF_PUBLIC | MEMF_CLEAR);
        if (lassoBuf) {
            ilbm_lasso_alpha(lassoBuf, picture->paletteIndices, width, height,
                picture->bmhd->transparentColor);
            rgbOut = picture->pixelData;
            pixCount = (ULONG)width * (ULONG)height;
            for (pi = 0; pi < pixCount; pi++) {
                rgbOut[pi * 4U + 3U] = lassoBuf[pi];
            }
            FreeMem(lassoBuf, (ULONG)width * (ULONG)height);
        }
    }
    
    FreeMem(planeBuffer, rowBytes);
    if (alphaValues) {
        FreeMem(alphaValues, width);
    }
    if (workPal) {
        FreeMem(workPal, 768);
    }
    
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
    UBYTE *workPal;
    BOOL mpalOn;
    struct IFFMultipaletteState mpst;
    const UBYTE *hamPal;
    UBYTE *alphaValues;
    BOOL wantAlpha;
    BOOL hamSingleBit;
    UBYTE aByte;
    UBYTE transHAM;
    UBYTE *lassoIdxBuf;
    ULONG lassoPos;
    ULONG pixCount;
    ULONG pi;
    
    workPal = NULL;
    mpalOn = FALSE;
    alphaValues = NULL;
    lassoIdxBuf = NULL;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for HAM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytes(width);
    
    hamSingleBit = FALSE;
    if (depth == 5 || depth == 7) {
        hamSingleBit = TRUE;
    } else if (depth < 6) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "HAM requires 5, 6, 7, or more planes");
        return RETURN_FAIL;
    }
    
    if (hamSingleBit) {
        hambits = (UBYTE)(depth - 1U);
    } else {
        hambits = (UBYTE)(depth - 2U);
    }
    if (hambits > 7) {
        hambits = 7;
    }
    hammask = (UBYTE)((1U << hambits) - 1U);
    hamshift = (UBYTE)(8U - hambits);
    hammask2 = (UBYTE)((1U << hamshift) - 1U);
    
    wantAlpha = FALSE;
    if (picture->bmhd->masking == mskHasMask
        || picture->bmhd->masking == mskHasTransparentColor
        || picture->bmhd->masking == mskLasso) {
        wantAlpha = TRUE;
        picture->hasAlpha = TRUE;
    } else {
        picture->hasAlpha = FALSE;
    }
    transHAM = (UBYTE)(picture->bmhd->transparentColor & 0xFFU);
    
    if (picture->cmap && picture->cmap->data) {
        cmapData = picture->cmap->data;
        maxColors = picture->cmap->numcolors;
    } else {
        cmapData = NULL;
        maxColors = 0;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    if (picture->bmhd->masking == mskHasMask) {
        alphaValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!alphaValues) {
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate HAM alpha buffer");
            return RETURN_FAIL;
        }
    }
    
    if (picture->bmhd->masking == mskLasso) {
        lassoIdxBuf = (UBYTE *)AllocMem((ULONG)width * (ULONG)height, MEMF_PUBLIC | MEMF_CLEAR);
        if (!lassoIdxBuf) {
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate HAM lasso buffer");
            return RETURN_FAIL;
        }
    }
    
    /* ILBM compression 2 (Column-wise ByteRun1): pre-decode entire BODY */
    if (picture->bmhd->compression == cmpByteRun1Column) {
        if (PrepareBodyDecodeBufferColumnWise(picture) != RETURN_OK) {
            if (lassoIdxBuf) {
                FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
            }
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            return RETURN_FAIL;
        }
    }
    
    if (picture->bmhd->compression == cmpByteRun1) {
        if (PrepareBodyDecodeBufferRowMajor(picture) != RETURN_OK) {
            if (lassoIdxBuf) {
                FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
            }
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            return RETURN_FAIL;
        }
    }
    
    if (IFFMultipalette_Active(picture)) {
        workPal = (UBYTE *)AllocMem(768, MEMF_PUBLIC | MEMF_CLEAR);
        if (!workPal) {
            if (lassoIdxBuf) {
                FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
            }
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate multipalette buffer");
            return RETURN_FAIL;
        }
        IFFMultipalette_Init(picture, workPal, &mpst);
        mpalOn = TRUE;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel values for this row */
        UBYTE *pixelValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!pixelValues) {
            if (workPal) {
                FreeMem(workPal, 768);
            }
            if (lassoIdxBuf) {
                FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
            }
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel values");
            return RETURN_FAIL;
        }
        
        /* Multipalette: update working palette for this row before reading its bitplanes. */
        if (mpalOn) {
            IFFMultipalette_ApplyScanline(&mpst, row, workPal);
            hamPal = workPal;
        } else {
            hamPal = cmapData;
        }
        
        /* Read all HAM color planes for this row */
        for (plane = 0; plane < depth; plane++) {
            bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
            if (bytesRead != (LONG)rowBytes) {
                FreeMem(pixelValues, width);
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                if (lassoIdxBuf) {
                    FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
                }
                if (alphaValues) {
                    FreeMem(alphaValues, width);
                }
                FreeMem(planeBuffer, rowBytes);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                return RETURN_FAIL;
            }
            ExtractBitsFromPlane(planeBuffer, pixelValues, width, rowBytes, plane);
        }
        
        if (picture->bmhd->masking == mskHasMask) {
            bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
            if (bytesRead != (LONG)rowBytes) {
                FreeMem(pixelValues, width);
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                if (lassoIdxBuf) {
                    FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
                }
                FreeMem(alphaValues, width);
                FreeMem(planeBuffer, rowBytes);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read HAM mask plane");
                return RETURN_FAIL;
            }
            ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
        }
        
        /* Decode HAM pixels */
        r = g = b = 0;
        for (col = 0; col < width; col++) {
            pixelValue = pixelValues[col];
            if (hamSingleBit) {
                hamCode = (UBYTE)((pixelValue >> hambits) & 1U);
                hamIndex = (UBYTE)(pixelValue & hammask);
            } else {
                hamCode = (UBYTE)((pixelValue >> hambits) & 3U);
                hamIndex = (UBYTE)(pixelValue & hammask);
            }
            
            if (hamSingleBit) {
                if (hamCode == 0) {
                    if (hamPal && hamIndex < maxColors) {
                        r = hamPal[hamIndex * 3];
                        g = hamPal[hamIndex * 3 + 1];
                        b = hamPal[hamIndex * 3 + 2];
                        if (picture->cmap && picture->cmap->is4Bit && !mpalOn) {
                            r |= (r >> 4);
                            g |= (g >> 4);
                            b |= (b >> 4);
                        }
                    } else {
                        r = g = b = (UBYTE)((hamIndex << hamshift) | ((hamIndex << hamshift) >> hambits));
                    }
                } else {
                    b = (UBYTE)((b & hammask2) | (hamIndex << hamshift));
                }
            } else {
                switch (hamCode) {
                    case HAMCODE_CMAP:
                        if (hamPal && hamIndex < maxColors) {
                            r = hamPal[hamIndex * 3];
                            g = hamPal[hamIndex * 3 + 1];
                            b = hamPal[hamIndex * 3 + 2];
                            if (picture->cmap && picture->cmap->is4Bit && !mpalOn) {
                                r |= (r >> 4);
                                g |= (g >> 4);
                                b |= (b >> 4);
                            }
                        } else {
                            r = g = b = (UBYTE)((hamIndex << hamshift) | ((hamIndex << hamshift) >> hambits));
                        }
                        break;
                    case HAMCODE_BLUE:
                        b = (UBYTE)((b & hammask2) | (hamIndex << hamshift));
                        break;
                    case HAMCODE_RED:
                        r = (UBYTE)((r & hammask2) | (hamIndex << hamshift));
                        break;
                    case HAMCODE_GREEN:
                        g = (UBYTE)((g & hammask2) | (hamIndex << hamshift));
                        break;
                }
            }
            
            if (lassoIdxBuf) {
                lassoPos = (ULONG)row * (ULONG)width + (ULONG)col;
                if (!hamSingleBit) {
                    if (hamCode == HAMCODE_CMAP) {
                        lassoIdxBuf[lassoPos] = hamIndex;
                    } else {
                        lassoIdxBuf[lassoPos] = 0xFFU;
                    }
                } else {
                    if (hamCode == 0) {
                        lassoIdxBuf[lassoPos] = hamIndex;
                    } else {
                        lassoIdxBuf[lassoPos] = 0xFFU;
                    }
                }
            }
            
            rgbOut[0] = r;
            rgbOut[1] = g;
            rgbOut[2] = b;
            aByte = 255;
            if (picture->bmhd->masking == mskHasMask) {
                aByte = alphaValues[col];
            } else if (picture->bmhd->masking == mskHasTransparentColor) {
                if ((!hamSingleBit && hamCode == HAMCODE_CMAP && hamIndex == transHAM)
                    || (hamSingleBit && hamCode == 0 && hamIndex == transHAM)) {
                    aByte = 0;
                }
            }
            if (wantAlpha) {
                rgbOut[3] = aByte;
                rgbOut += 4;
            } else {
                rgbOut += 3;
            }
        }
        
        FreeMem(pixelValues, width);
    }
    
    if (picture->bmhd->masking == mskLasso && wantAlpha && lassoIdxBuf) {
        UBYTE *lassoTmp;
        lassoTmp = (UBYTE *)AllocMem((ULONG)width * (ULONG)height, MEMF_PUBLIC | MEMF_CLEAR);
        if (lassoTmp) {
            ilbm_lasso_alpha(lassoTmp, lassoIdxBuf, width, height, picture->bmhd->transparentColor);
            rgbOut = picture->pixelData;
            pixCount = (ULONG)width * (ULONG)height;
            for (pi = 0; pi < pixCount; pi++) {
                rgbOut[pi * 4U + 3U] = lassoTmp[pi];
            }
            FreeMem(lassoTmp, (ULONG)width * (ULONG)height);
        }
    }
    
    if (lassoIdxBuf) {
        FreeMem(lassoIdxBuf, (ULONG)width * (ULONG)height);
    }
    if (alphaValues) {
        FreeMem(alphaValues, width);
    }
    FreeMem(planeBuffer, rowBytes);
    if (workPal) {
        FreeMem(workPal, 768);
    }
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
    UBYTE *paletteOut;
    UWORD row, plane, col;
    UBYTE pixelIndex;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE *workPal;
    BOOL mpalOn;
    struct IFFMultipaletteState mpst;
    const UBYTE *palSrc;
    UBYTE *alphaValues;
    BOOL wantAlpha;
    UBYTE aByte;
    UBYTE transIdx;
    ULONG pixCount;
    ULONG pi;
    UBYTE *lassoTmp;
    
    workPal = NULL;
    mpalOn = FALSE;
    alphaValues = NULL;
    
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
    
    wantAlpha = FALSE;
    if (picture->bmhd->masking == mskHasMask
        || picture->bmhd->masking == mskHasTransparentColor
        || picture->bmhd->masking == mskLasso) {
        wantAlpha = TRUE;
        picture->hasAlpha = TRUE;
    } else {
        picture->hasAlpha = FALSE;
    }
    transIdx = (UBYTE)(picture->bmhd->transparentColor & 0xFFU);
    
    if (picture->paletteIndices) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
    }
    picture->paletteIndicesSize = (ULONG)width * (ULONG)height;
    picture->paletteIndices = (UBYTE *)AllocMem(picture->paletteIndicesSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->paletteIndices) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate EHB palette indices");
        return RETURN_FAIL;
    }
    paletteOut = picture->paletteIndices;
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    if (picture->bmhd->masking == mskHasMask) {
        alphaValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!alphaValues) {
            FreeMem(planeBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate EHB alpha buffer");
            return RETURN_FAIL;
        }
    }
    
    /* ILBM compression 2 (Column-wise ByteRun1): pre-decode entire BODY */
    if (picture->bmhd->compression == cmpByteRun1Column) {
        if (PrepareBodyDecodeBufferColumnWise(picture) != RETURN_OK) {
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            return RETURN_FAIL;
        }
    }
    
    if (picture->bmhd->compression == cmpByteRun1) {
        if (PrepareBodyDecodeBufferRowMajor(picture) != RETURN_OK) {
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            return RETURN_FAIL;
        }
    }
    
    if (IFFMultipalette_Active(picture)) {
        workPal = (UBYTE *)AllocMem(768, MEMF_PUBLIC | MEMF_CLEAR);
        if (!workPal) {
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate multipalette buffer");
            return RETURN_FAIL;
        }
        IFFMultipalette_Init(picture, workPal, &mpst);
        mpalOn = TRUE;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!pixelIndices) {
            if (workPal) {
                FreeMem(workPal, 768);
            }
            if (alphaValues) {
                FreeMem(alphaValues, width);
            }
            FreeMem(planeBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel indices");
            return RETURN_FAIL;
        }
        
        if (mpalOn) {
            IFFMultipalette_ApplyScanline(&mpst, row, workPal);
            palSrc = workPal;
        } else {
            palSrc = cmapData;
        }
        
        for (plane = 0; plane < depth; plane++) {
            bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
            if (bytesRead != (LONG)rowBytes) {
                FreeMem(pixelIndices, width);
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                if (alphaValues) {
                    FreeMem(alphaValues, width);
                }
                FreeMem(planeBuffer, rowBytes);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                picture->paletteIndices = NULL;
                picture->paletteIndicesSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read plane data");
                return RETURN_FAIL;
            }
            ExtractBitsFromPlane(planeBuffer, pixelIndices, width, rowBytes, plane);
        }
        
        if (picture->bmhd->masking == mskHasMask) {
            bytesRead = ReadBodyRow(picture, planeBuffer, rowBytes);
            if (bytesRead != (LONG)rowBytes) {
                FreeMem(pixelIndices, width);
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                FreeMem(alphaValues, width);
                FreeMem(planeBuffer, rowBytes);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                picture->paletteIndices = NULL;
                picture->paletteIndicesSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read EHB mask plane");
                return RETURN_FAIL;
            }
            ExtractAlphaFromPlane(planeBuffer, alphaValues, width, rowBytes);
        }
        
        for (col = 0; col < width; col++) {
            pixelIndex = pixelIndices[col];
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            *paletteOut++ = pixelIndex;
            
            rgbOut[0] = palSrc[pixelIndex * 3];
            rgbOut[1] = palSrc[pixelIndex * 3 + 1];
            rgbOut[2] = palSrc[pixelIndex * 3 + 2];
            
            if (picture->cmap->is4Bit && !mpalOn) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            if (pixelIndex >= 32) {
                rgbOut[0] = rgbOut[0] >> 1;
                rgbOut[1] = rgbOut[1] >> 1;
                rgbOut[2] = rgbOut[2] >> 1;
            }
            
            aByte = 255;
            if (picture->bmhd->masking == mskHasMask) {
                aByte = alphaValues[col];
            } else if (picture->bmhd->masking == mskHasTransparentColor
                && pixelIndex == transIdx) {
                aByte = 0;
            }
            if (wantAlpha) {
                rgbOut[3] = aByte;
                rgbOut += 4;
            } else {
                rgbOut += 3;
            }
        }
        
        FreeMem(pixelIndices, width);
    }
    
    if (picture->bmhd->masking == mskLasso && wantAlpha && picture->paletteIndices) {
        lassoTmp = (UBYTE *)AllocMem((ULONG)width * (ULONG)height, MEMF_PUBLIC | MEMF_CLEAR);
        if (lassoTmp) {
            ilbm_lasso_alpha(lassoTmp, picture->paletteIndices, width, height,
                picture->bmhd->transparentColor);
            rgbOut = picture->pixelData;
            pixCount = (ULONG)width * (ULONG)height;
            for (pi = 0; pi < pixCount; pi++) {
                rgbOut[pi * 4U + 3U] = lassoTmp[pi];
            }
            FreeMem(lassoTmp, (ULONG)width * (ULONG)height);
        }
    }
    
    if (alphaValues) {
        FreeMem(alphaValues, width);
    }
    FreeMem(planeBuffer, rowBytes);
    if (workPal) {
        FreeMem(workPal, 768);
    }
    return RETURN_OK;
}

/*
** DecompressDEEPRunLength - Decompress RUNLENGTH compressed DEEP data
** Returns: Number of bytes decompressed, or -1 on error
*/
static LONG DecompressDEEPRunLength(struct IFFHandle *iff, UBYTE *dest, LONG destBytes)
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
        } else {
            /* Repeat run: next byte repeated (256-code)+1 times */
            count = 256 - code;
            if ((count + 1) > bytesLeft) {
                return -1; /* Would overflow */
            }
            bytesRead = ReadChunkBytes(iff, &value, 1);
            if (bytesRead != 1) {
                return -1; /* Error reading */
            }
            /* Write count+1 bytes */
            while (count >= 0) {
                *out++ = value;
                bytesLeft--;
                count--;
            }
        }
    }
    
    return destBytes;
}

/*
** DecompressDEEPTVDC - Decompress TVDC (TVPaint Deep Compression) data
** Returns: Number of bytes decompressed, or -1 on error
** 
** TVDC is a modified delta compression using a 16-word lookup table
** and incorporates Run Length Limiting compression for short runs.
** Compression is made line by line for each element of DPEL.
*/
static LONG DecompressDEEPTVDC(struct IFFHandle *iff, UBYTE *dest, LONG destBytes, WORD *table)
{
    LONG i;
    LONG d;
    LONG pos = 0;
    UBYTE v = 0;
    UBYTE *source;
    UBYTE *sourceBuf;
    LONG sourceSize;
    LONG bytesRead;
    
    /* Estimate source size - TVDC typically compresses well, but worst case is same size */
    /* We'll read in chunks as needed */
    sourceSize = (destBytes + 1) / 2; /* Worst case: 2 source bytes per dest byte */
    sourceBuf = (UBYTE *)AllocMem(sourceSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!sourceBuf) {
        return -1; /* Out of memory */
    }
    
    /* Read compressed data */
    bytesRead = ReadChunkBytes(iff, sourceBuf, sourceSize);
    if (bytesRead < 0) {
        FreeMem(sourceBuf, sourceSize);
        return -1;
    }
    
    source = sourceBuf;
    
    /* Decompress using TVDC algorithm from spec */
    for (i = 0; i < destBytes; i++) {
        d = source[pos >> 1];
        if (pos++ & 1) {
            d &= 0xf;
        } else {
            d >>= 4;
        }
        v += table[d];
        dest[i] = v;
        if (!table[d]) {
            /* Run length encoding */
            if (pos >= sourceSize * 2) {
                /* Out of source data */
                break;
            }
            d = source[pos >> 1];
            if (pos++ & 1) {
                d &= 0xf;
            } else {
                d >>= 4;
            }
            while (d-- && i < destBytes - 1) {
                dest[++i] = v;
            }
        }
    }
    
    FreeMem(sourceBuf, sourceSize);
    return (pos + 1) / 2; /* Return source bytes consumed */
}

/*
** DecodeDEEP - Decode DEEP format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** DEEP format stores chunky pixels (consecutive memory locations) with
** pixel structure defined by DPEL chunk. Supports various compression types
** and pixel component types (RGB, RGBA, YCM, etc.).
*/
LONG DecodeDEEP(struct IFFPicture *picture)
{
    UWORD width, height;
    UWORD compression;
    ULONG nElements;
    ULONG pixelSizeBytes;
    ULONG rowSizeBytes;
    UBYTE *rowBuffer;
    UBYTE *rgbOut;
    UWORD row, col;
    ULONG elem;
    LONG bytesRead;
    ULONG totalBits;
    ULONG bytesPerPixel;
    ULONG i;
    BOOL hasRed, hasGreen, hasBlue, hasAlpha;
    UBYTE redIdx, greenIdx, blueIdx, alphaIdx;
    UBYTE *elementData;
    ULONG elementOffset;
    ULONG value;
    
    if (!picture || !picture->dgbl || !picture->dpel) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing DGBL or DPEL for DEEP decoding");
        return RETURN_FAIL;
    }
    
    /* Get dimensions from DLOC if present, otherwise from DGBL */
    if (picture->dloc) {
        width = picture->dloc->w;
        height = picture->dloc->h;
    } else {
        width = picture->dgbl->DisplayWidth;
        height = picture->dgbl->DisplayHeight;
    }
    
    compression = picture->dgbl->Compression;
    nElements = picture->dpel->nElements;
    
    if (nElements == 0) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "DPEL has zero elements");
        return RETURN_FAIL;
    }
    
    /* Calculate total bits per pixel and bytes per pixel (padded to byte boundary) */
    totalBits = 0;
    for (i = 0; i < nElements; i++) {
        totalBits += picture->dpel->typedepth[i].cBitDepth;
    }
    bytesPerPixel = (totalBits + 7) / 8; /* Round up to byte boundary */
    pixelSizeBytes = bytesPerPixel;
    rowSizeBytes = (ULONG)width * pixelSizeBytes;
    
    /* Find RGB/Alpha component indices */
    hasRed = hasGreen = hasBlue = hasAlpha = FALSE;
    redIdx = greenIdx = blueIdx = alphaIdx = 0;
    
    for (i = 0; i < nElements; i++) {
        switch (picture->dpel->typedepth[i].cType) {
            case DEEP_TYPE_RED:
                hasRed = TRUE;
                redIdx = (UBYTE)i;
                break;
            case DEEP_TYPE_GREEN:
                hasGreen = TRUE;
                greenIdx = (UBYTE)i;
                break;
            case DEEP_TYPE_BLUE:
                hasBlue = TRUE;
                blueIdx = (UBYTE)i;
                break;
            case DEEP_TYPE_ALPHA:
                hasAlpha = TRUE;
                alphaIdx = (UBYTE)i;
                break;
        }
    }
    
    /* Decode() pre-allocates RGB; DEEP may choose RGBA from DPEL. */
    if (picture->paletteIndices) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
    }
    if (picture->pixelData) {
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelData = NULL;
        picture->pixelDataSize = 0;
    }
    
    /* Allocate pixel data buffer (RGB or RGBA) */
    if (hasAlpha) {
        picture->pixelDataSize = (ULONG)width * height * 4;
        picture->hasAlpha = TRUE;
    } else {
        picture->pixelDataSize = (ULONG)width * height * 3;
        picture->hasAlpha = FALSE;
    }
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->pixelData) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DEEP pixel data buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate row buffer for compressed/uncompressed data */
    rowBuffer = (UBYTE *)AllocMem(rowSizeBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rowBuffer) {
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelData = NULL;
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DEEP row buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row - DEEP stores data line by line for each element */
    for (row = 0; row < height; row++) {
        /* Allocate buffer for element data for this row */
        elementData = (UBYTE *)AllocMem(rowSizeBytes, MEMF_PUBLIC | MEMF_CLEAR);
        if (!elementData) {
            FreeMem(rowBuffer, rowSizeBytes);
            FreeMem(picture->pixelData, picture->pixelDataSize);
            picture->pixelData = NULL;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate element data buffer");
            return RETURN_FAIL;
        }
        
        /* Read/decompress each element for this row */
        elementOffset = 0;
        for (elem = 0; elem < nElements; elem++) {
            UWORD elementBits = picture->dpel->typedepth[elem].cBitDepth;
            ULONG elementBytesPerPixel = (elementBits + 7) / 8;
            ULONG elementRowBytes = (ULONG)width * elementBytesPerPixel;
            
            /* Read/decompress element data */
            switch (compression) {
                case DEEP_COMPRESS_NONE:
                    bytesRead = ReadChunkBytes(picture->iff, rowBuffer, elementRowBytes);
                    if (bytesRead != elementRowBytes) {
                        FreeMem(elementData, rowSizeBytes);
                        FreeMem(rowBuffer, rowSizeBytes);
                        FreeMem(picture->pixelData, picture->pixelDataSize);
                        picture->pixelData = NULL;
                        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read DEEP element data");
                        return RETURN_FAIL;
                    }
                    break;
                case DEEP_COMPRESS_RUNLENGTH:
                    bytesRead = DecompressDEEPRunLength(picture->iff, rowBuffer, elementRowBytes);
                    if (bytesRead != elementRowBytes) {
                        FreeMem(elementData, rowSizeBytes);
                        FreeMem(rowBuffer, rowSizeBytes);
                        FreeMem(picture->pixelData, picture->pixelDataSize);
                        picture->pixelData = NULL;
                        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DEEP RUNLENGTH decompression failed");
                        return RETURN_FAIL;
                    }
                    break;
                case DEEP_COMPRESS_TVDC:
                    if (!picture->tvdc) {
                        FreeMem(elementData, rowSizeBytes);
                        FreeMem(rowBuffer, rowSizeBytes);
                        FreeMem(picture->pixelData, picture->pixelDataSize);
                        picture->pixelData = NULL;
                        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "TVDC compression requires TVDC chunk");
                        return RETURN_FAIL;
                    }
                    bytesRead = DecompressDEEPTVDC(picture->iff, rowBuffer, elementRowBytes, picture->tvdc->table);
                    if (bytesRead < 0) {
                        FreeMem(elementData, rowSizeBytes);
                        FreeMem(rowBuffer, rowSizeBytes);
                        FreeMem(picture->pixelData, picture->pixelDataSize);
                        picture->pixelData = NULL;
                        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DEEP TVDC decompression failed");
                        return RETURN_FAIL;
                    }
                    break;
                case DEEP_COMPRESS_HUFFMAN:
                case DEEP_COMPRESS_DYNAMICHUFF:
                case DEEP_COMPRESS_JPEG:
                default:
                    FreeMem(elementData, rowSizeBytes);
                    FreeMem(rowBuffer, rowSizeBytes);
                    FreeMem(picture->pixelData, picture->pixelDataSize);
                    picture->pixelData = NULL;
                    SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "DEEP compression type not supported");
                    return RETURN_FAIL;
            }
            
            /* Copy element data to element buffer (interleaved by pixel) */
            for (col = 0; col < width; col++) {
                CopyMem(rowBuffer + col * elementBytesPerPixel,
                       elementData + col * pixelSizeBytes + elementOffset,
                       elementBytesPerPixel);
            }
            elementOffset += elementBytesPerPixel;
        }
        
        /* Convert element data to RGB/RGBA output */
            for (col = 0; col < width; col++) {
            UBYTE *pixelData = elementData + col * pixelSizeBytes;
            UBYTE r = 0, g = 0, b = 0, a = 255;
            ULONG byteOffset = 0;
            
            /* Extract component values from pixel data (elements stored consecutively) */
            for (elem = 0; elem < nElements; elem++) {
                UWORD elementBits = picture->dpel->typedepth[elem].cBitDepth;
                ULONG elementBytes = (elementBits + 7) / 8;
                value = 0;
                
                /* Extract value from pixel data (big-endian, MSB first) */
                for (i = 0; i < elementBytes; i++) {
                    value = (value << 8) | pixelData[byteOffset + i];
                }
                
                /* Scale value to 8-bit if needed */
                if (elementBits < 8) {
                    value = (value * 255) / ((1UL << elementBits) - 1);
                } else if (elementBits > 8) {
                    value = value >> (elementBits - 8);
                }
                
                /* Store in RGB/A components */
                if (elem == redIdx && hasRed) {
                    r = (UBYTE)value;
                } else if (elem == greenIdx && hasGreen) {
                    g = (UBYTE)value;
                } else if (elem == blueIdx && hasBlue) {
                    b = (UBYTE)value;
                } else if (elem == alphaIdx && hasAlpha) {
                    a = (UBYTE)value;
                }
                
                byteOffset += elementBytes;
            }
            
            /* Write RGB/RGBA output */
            rgbOut[0] = r;
            rgbOut[1] = g;
            rgbOut[2] = b;
            if (hasAlpha) {
                rgbOut[3] = a;
                rgbOut += 4;
            } else {
            rgbOut += 3;
        }
    }
    
        FreeMem(elementData, rowSizeBytes);
    }
    
    FreeMem(rowBuffer, rowSizeBytes);
    return RETURN_OK;
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
    UBYTE *maskRow;
    UBYTE *rgbOut;
    UBYTE *paletteOut;
    UWORD row, col;
    UBYTE pixelIndex;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE *workPal;
    BOOL mpalOn;
    struct IFFMultipaletteState mpst;
    const UBYTE *palSrc;
    BOOL wantAlpha;
    UBYTE aByte;
    UBYTE transIdx;
    ULONG pixCount;
    ULONG pi;
    UBYTE *lassoTmp;
    
    workPal = NULL;
    mpalOn = FALSE;
    maskRow = NULL;
    
    if (!picture || !picture->bmhd) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD for PBM decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    
    if (!picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing CMAP for PBM decoding");
        return RETURN_FAIL;
    }
    
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    wantAlpha = FALSE;
    if (picture->bmhd->masking == mskHasMask
        || picture->bmhd->masking == mskHasTransparentColor
        || picture->bmhd->masking == mskLasso) {
        wantAlpha = TRUE;
        picture->hasAlpha = TRUE;
    } else {
        picture->hasAlpha = FALSE;
    }
    transIdx = (UBYTE)(picture->bmhd->transparentColor & 0xFFU);
    
    if (picture->paletteIndices) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
    }
    picture->paletteIndicesSize = (ULONG)width * (ULONG)height;
    picture->paletteIndices = (UBYTE *)AllocMem(picture->paletteIndicesSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->paletteIndices) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate PBM palette indices");
        return RETURN_FAIL;
    }
    paletteOut = picture->paletteIndices;
    
    rowBuffer = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rowBuffer) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        picture->paletteIndicesSize = 0;
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate row buffer");
        return RETURN_FAIL;
    }
    
    if (picture->bmhd->masking == mskHasMask) {
        maskRow = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!maskRow) {
            FreeMem(rowBuffer, width);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate PBM mask row");
            return RETURN_FAIL;
        }
    }
    
    if (IFFMultipalette_Active(picture)) {
        workPal = (UBYTE *)AllocMem(768, MEMF_PUBLIC | MEMF_CLEAR);
        if (!workPal) {
            if (maskRow) {
                FreeMem(maskRow, width);
            }
            FreeMem(rowBuffer, width);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            picture->paletteIndices = NULL;
            picture->paletteIndicesSize = 0;
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate multipalette buffer");
            return RETURN_FAIL;
        }
        IFFMultipalette_Init(picture, workPal, &mpst);
        mpalOn = TRUE;
    }
    
    rgbOut = picture->pixelData;
    
    for (row = 0; row < height; row++) {
        if (mpalOn) {
            IFFMultipalette_ApplyScanline(&mpst, row, workPal);
            palSrc = workPal;
        } else {
            palSrc = cmapData;
        }
        
        if (picture->bmhd->compression == cmpByteRun1) {
            bytesRead = DecompressByteRun1(picture->iff, rowBuffer, width);
            if (bytesRead != width) {
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                if (maskRow) {
                    FreeMem(maskRow, width);
                }
                FreeMem(rowBuffer, width);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                picture->paletteIndices = NULL;
                picture->paletteIndicesSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "ByteRun1 decompression failed");
                return RETURN_FAIL;
            }
        } else {
            bytesRead = ReadChunkBytes(picture->iff, rowBuffer, width);
            if (bytesRead != width) {
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                if (maskRow) {
                    FreeMem(maskRow, width);
                }
                FreeMem(rowBuffer, width);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                picture->paletteIndices = NULL;
                picture->paletteIndicesSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read row data");
                return RETURN_FAIL;
            }
        }
        
        if (picture->bmhd->masking == mskHasMask) {
            if (picture->bmhd->compression == cmpByteRun1) {
                bytesRead = DecompressByteRun1(picture->iff, maskRow, width);
            } else {
                bytesRead = ReadChunkBytes(picture->iff, maskRow, width);
            }
            if (bytesRead != width) {
                if (workPal) {
                    FreeMem(workPal, 768);
                }
                FreeMem(maskRow, width);
                FreeMem(rowBuffer, width);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                picture->paletteIndices = NULL;
                picture->paletteIndicesSize = 0;
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read PBM mask row");
                return RETURN_FAIL;
            }
        }
        
        for (col = 0; col < width; col++) {
            pixelIndex = rowBuffer[col];
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            *paletteOut++ = pixelIndex;
            
            rgbOut[0] = palSrc[pixelIndex * 3];
            rgbOut[1] = palSrc[pixelIndex * 3 + 1];
            rgbOut[2] = palSrc[pixelIndex * 3 + 2];
            
            if (picture->cmap->is4Bit && !mpalOn) {
                rgbOut[0] |= (rgbOut[0] >> 4);
                rgbOut[1] |= (rgbOut[1] >> 4);
                rgbOut[2] |= (rgbOut[2] >> 4);
            }
            
            aByte = 255;
            if (picture->bmhd->masking == mskHasMask) {
                aByte = maskRow[col];
            } else if (picture->bmhd->masking == mskHasTransparentColor
                && pixelIndex == transIdx) {
                aByte = 0;
            }
            if (wantAlpha) {
                rgbOut[3] = aByte;
                rgbOut += 4;
            } else {
                rgbOut += 3;
            }
        }
    }
    
    if (picture->bmhd->masking == mskLasso && wantAlpha && picture->paletteIndices) {
        lassoTmp = (UBYTE *)AllocMem((ULONG)width * (ULONG)height, MEMF_PUBLIC | MEMF_CLEAR);
        if (lassoTmp) {
            ilbm_lasso_alpha(lassoTmp, picture->paletteIndices, width, height,
                picture->bmhd->transparentColor);
            rgbOut = picture->pixelData;
            pixCount = (ULONG)width * (ULONG)height;
            for (pi = 0; pi < pixCount; pi++) {
                rgbOut[pi * 4U + 3U] = lassoTmp[pi];
            }
            FreeMem(lassoTmp, (ULONG)width * (ULONG)height);
        }
    }
    
    if (maskRow) {
        FreeMem(maskRow, width);
    }
    FreeMem(rowBuffer, width);
    if (workPal) {
        FreeMem(workPal, 768);
    }
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, rValues, width, rowBytes, plane);
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, gValues, width, rowBytes, plane);
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, bValues, width, rowBytes, plane);
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, rValues, width, rowBytes, plane);
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, gValues, width, rowBytes, plane);
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
            
            /* Extract bits from this plane (optimized) */
            ExtractBitsFromPlane(planeBuffer, bValues, width, rowBytes, plane);
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
    planeData = (UBYTE *)AllocMem(planeDataSize, MEMF_PUBLIC | MEMF_CLEAR);
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        FreeMem(planeData, planeDataSize);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row - extract interleaved plane data from contiguous storage */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
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
            
            /* Extract bits from this plane to build pixel indices (optimized) */
            ExtractBitsFromPlane(planeBuffer, pixelIndices, width, rowBytes, plane);
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

/*
** DecodeFAXX - Decode FAXX format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** FAXX format stores fax images:
** - Always 1-bit (black and white)
** - Uses FXHD chunk (FaxHeader) instead of BMHD
** - Uses PAGE chunk instead of BODY
** - Compression: FXCMPNONE=0 (uncompressed), FXCMPMH=1, FXCMPMR=2, FXCMPMMR=4
** - For now, only uncompressed FAXX is supported
*/
LONG DecodeFAXX(struct IFFPicture *picture)
{
    UWORD width, height;
    UWORD rowBytes;
    UBYTE *rowBuffer;
    UBYTE *rgbOut;
    UBYTE *paletteOut;
    UWORD row, col;
    UBYTE pixelValue;
    LONG bytesRead;
    UBYTE *cmapData;
    ULONG maxColors;
    UBYTE bit_mask[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    
    if (!picture || !picture->bmhd || !picture->cmap || !picture->cmap->data) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD or CMAP for FAXX decoding");
        return RETURN_FAIL;
    }
    
    width = picture->bmhd->w;
    height = picture->bmhd->h;
    
    printf("DecodeFAXX: Starting decode %ldx%ld\n", width, height);
    fflush(stdout);
    
    DEBUG_PRINTF2("DEBUG: DecodeFAXX - Starting decode: %ldx%ld\n", width, height);
    
    /* Get FAXX compression type */
    {
        UBYTE faxxComp;
        faxxComp = picture->faxxCompression;
        
        /* Check compression type - we support all standard types */
        if (faxxComp != FXCMPNONE && faxxComp != FXCMPMH && 
            faxxComp != FXCMPMR && faxxComp != FXCMPMMR) {
            SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Unknown FAXX compression type");
            return RETURN_FAIL;
        }
    }
    
    /* Check that pixelData is allocated */
    if (!picture->pixelData) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Pixel data buffer not allocated");
        return RETURN_FAIL;
    }
    
    /* Check that IFF handle is valid and positioned at PAGE chunk */
    if (!picture->iff) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "IFF handle not available");
        return RETURN_FAIL;
    }
    
    rowBytes = RowBytes(width);
    cmapData = picture->cmap->data;
    maxColors = picture->cmap->numcolors;
    
    /* For indexed images, also store original palette indices */
    picture->paletteIndicesSize = (ULONG)width * height;
    picture->paletteIndices = (UBYTE *)AllocMem(picture->paletteIndicesSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->paletteIndices) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate palette indices buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate row buffer for reading bit-packed data */
    rowBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rowBuffer) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        picture->paletteIndices = NULL;
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate row buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    paletteOut = picture->paletteIndices;
    
    /* Check that IFF handle is valid and positioned at PAGE chunk */
    if (!picture->iff) {
        FreeMem(rowBuffer, rowBytes);
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "IFF handle not available");
        return RETURN_FAIL;
    }
    
    /* Process each row based on compression type */
    if (picture->faxxCompression == FXCMPNONE) {
        /* Uncompressed - read directly */
        for (row = 0; row < height; row++) {
            /* Read row data (bit-packed, MSB first) */
            bytesRead = ReadChunkBytes(picture->iff, rowBuffer, rowBytes);
            if (bytesRead != rowBytes) {
                FreeMem(rowBuffer, rowBytes);
                FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read FAXX row data");
                return RETURN_FAIL;
            }
            
            /* Extract pixels from bit-packed data */
            for (col = 0; col < width; col++) {
                UBYTE byteIndex;
                UBYTE bitIndex;
                UBYTE bit;
                
                byteIndex = col / 8;
                bitIndex = 7 - (col % 8); /* MSB first */
                bit = (rowBuffer[byteIndex] & bit_mask[bitIndex]) ? 1 : 0;
                
                /* Store palette index (0 = black, 1 = white) */
                pixelValue = bit;
                *paletteOut++ = pixelValue;
                
                /* Clamp to valid CMAP range */
                if (pixelValue >= maxColors) {
                    pixelValue = (UBYTE)(maxColors - 1);
                }
                
                /* Look up RGB from CMAP */
                rgbOut[0] = cmapData[pixelValue * 3];     /* R */
                rgbOut[1] = cmapData[pixelValue * 3 + 1]; /* G */
                rgbOut[2] = cmapData[pixelValue * 3 + 2]; /* B */
                
                rgbOut += 3;
            }
        }
    } else if (picture->faxxCompression == FXCMPMH) {
        /* Modified Huffman (MH) compression - full ITU-T T.4 implementation */
        FaxBitstream bs;
        UBYTE *lineBuffer;
        
        InitFaxBitstream(&bs, picture->iff);
        
        /* Allocate buffer for decoded line */
        lineBuffer = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!lineBuffer) {
            FreeMem(rowBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate line buffer for MH");
            return RETURN_FAIL;
        }
        
        /* Skip initial EOL */
        if (SkipToEOL(&bs) < 0) {
            FreeMem(lineBuffer, width);
            FreeMem(rowBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FAXX: Failed to find initial EOL");
            return RETURN_FAIL;
        }
        
        for (row = 0; row < height; row++) {
            /* Skip EOL at start of each line (except first) */
            if (row > 0) {
                if (SkipToEOL(&bs) < 0) {
                    /* End of data - pad remaining rows with white */
                    while (row < height) {
                        for (col = 0; col < width; col++) {
                            pixelValue = 0; /* White */
                            *paletteOut++ = pixelValue;
                            rgbOut[0] = cmapData[0];
                            rgbOut[1] = cmapData[1];
                            rgbOut[2] = cmapData[2];
                            rgbOut += 3;
                        }
                        row++;
                    }
                    break;
                }
            }
            
            /* Decode line using MH */
            if (DecodeMHLine(&bs, lineBuffer, width) != RETURN_OK) {
                /* Decode failed - pad remaining rows */
                while (row < height) {
                    for (col = 0; col < width; col++) {
                        pixelValue = 0; /* White */
                        *paletteOut++ = pixelValue;
                        rgbOut[0] = cmapData[0];
                        rgbOut[1] = cmapData[1];
                        rgbOut[2] = cmapData[2];
                        rgbOut += 3;
                    }
                    row++;
                }
                break;
            }
            
            /* Convert decoded line to RGB */
            for (col = 0; col < width; col++) {
                pixelValue = lineBuffer[col];
                *paletteOut++ = pixelValue;
                
                if (pixelValue >= maxColors) {
                    pixelValue = (UBYTE)(maxColors - 1);
                }
                
                rgbOut[0] = cmapData[pixelValue * 3];
                rgbOut[1] = cmapData[pixelValue * 3 + 1];
                rgbOut[2] = cmapData[pixelValue * 3 + 2];
                rgbOut += 3;
            }
        }
        
        FreeMem(lineBuffer, width);
    } else if (picture->faxxCompression == FXCMPMR) {
        /* Modified READ (MR) - 2D compression using reference line */
        FaxBitstream bs;
        UBYTE *lineBuffer;
        UBYTE *refLine;
        
        InitFaxBitstream(&bs, picture->iff);
        
        /* Allocate buffers for current and reference lines */
        lineBuffer = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        refLine = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!lineBuffer || !refLine) {
            if (lineBuffer) FreeMem(lineBuffer, width);
            if (refLine) FreeMem(refLine, width);
            FreeMem(rowBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate line buffers for MR");
            return RETURN_FAIL;
        }
        
        /* Skip initial EOL */
        if (SkipToEOL(&bs) < 0) {
            FreeMem(lineBuffer, width);
            FreeMem(refLine, width);
            FreeMem(rowBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FAXX: Failed to find initial EOL");
            return RETURN_FAIL;
        }
        
        /* First line is always MH (1D) */
        if (DecodeMHLine(&bs, refLine, width) != RETURN_OK) {
            FreeMem(lineBuffer, width);
            FreeMem(refLine, width);
            FreeMem(rowBuffer, rowBytes);
            FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "FAXX: MR first line decode failed");
            return RETURN_FAIL;
        }
        
        /* Convert first line to RGB */
        for (col = 0; col < width; col++) {
            pixelValue = refLine[col];
            *paletteOut++ = pixelValue;
            if (pixelValue >= maxColors) pixelValue = (UBYTE)(maxColors - 1);
            rgbOut[0] = cmapData[pixelValue * 3];
            rgbOut[1] = cmapData[pixelValue * 3 + 1];
            rgbOut[2] = cmapData[pixelValue * 3 + 2];
            rgbOut += 3;
        }
        
        /* Decode remaining lines using MR (2D) */
        for (row = 1; row < height; row++) {
            LONG bit;  /* Tag bit for line encoding type */
            
            /* Skip EOL */
            if (SkipToEOL(&bs) < 0) {
                /* End of data - pad remaining rows with white */
                while (row < height) {
                    for (col = 0; col < width; col++) {
                        pixelValue = 0; /* White */
                        *paletteOut++ = pixelValue;
                        rgbOut[0] = cmapData[0];
                        rgbOut[1] = cmapData[1];
                        rgbOut[2] = cmapData[2];
                        rgbOut += 3;
                    }
                    row++;
                }
                break;
            }
            
            /* Read tag bit - 0 = 1D (MH), 1 = 2D (MR) */
            bit = ReadFaxBit(&bs);
            if (bit < 0) {
                /* Error - pad remaining rows */
                while (row < height) {
                    for (col = 0; col < width; col++) {
                        pixelValue = 0; /* White */
                        *paletteOut++ = pixelValue;
                        rgbOut[0] = cmapData[0];
                        rgbOut[1] = cmapData[1];
                        rgbOut[2] = cmapData[2];
                        rgbOut += 3;
                    }
                    row++;
                }
                break;
            }
            
            if (bit == 0) {
                /* 1D line - use MH */
                if (DecodeMHLine(&bs, lineBuffer, width) != RETURN_OK) {
                    /* Decode failed - pad remaining rows */
                    while (row < height) {
                        for (col = 0; col < width; col++) {
                            pixelValue = 0; /* White */
                            *paletteOut++ = pixelValue;
                            rgbOut[0] = cmapData[0];
                            rgbOut[1] = cmapData[1];
                            rgbOut[2] = cmapData[2];
                            rgbOut += 3;
                        }
                        row++;
                    }
                    break;
                }
            } else {
                /* 2D line - use MR with reference line */
                if (DecodeMRLine(&bs, lineBuffer, refLine, width) != RETURN_OK) {
                    /* Decode failed - pad remaining rows */
                    while (row < height) {
                        for (col = 0; col < width; col++) {
                            pixelValue = 0; /* White */
                            *paletteOut++ = pixelValue;
                            rgbOut[0] = cmapData[0];
                            rgbOut[1] = cmapData[1];
                            rgbOut[2] = cmapData[2];
                            rgbOut += 3;
                        }
                        row++;
                    }
                    break;
                }
            }
            
            /* Convert decoded line to RGB */
            for (col = 0; col < width; col++) {
                pixelValue = lineBuffer[col];
                *paletteOut++ = pixelValue;
                if (pixelValue >= maxColors) pixelValue = (UBYTE)(maxColors - 1);
                rgbOut[0] = cmapData[pixelValue * 3];
                rgbOut[1] = cmapData[pixelValue * 3 + 1];
                rgbOut[2] = cmapData[pixelValue * 3 + 2];
                rgbOut += 3;
            }
            
            /* Swap buffers - current becomes reference */
            {
                UBYTE *tmp;
                tmp = refLine;
                refLine = lineBuffer;
                lineBuffer = tmp;
            }
        }
        
        FreeMem(lineBuffer, width);
        FreeMem(refLine, width);
    } else if (picture->faxxCompression == FXCMPMMR) {
        /* Modified Modified READ (MMR) - similar to MR but no EOL codes */
        /* For now, treat as MR */
        FreeMem(rowBuffer, rowBytes);
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "MMR compression not yet fully implemented");
        return RETURN_FAIL;
    } else {
        /* Should not reach here due to earlier check */
        FreeMem(rowBuffer, rowBytes);
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Unsupported FAXX compression type");
        return RETURN_FAIL;
    }
    
    FreeMem(rowBuffer, rowBytes);
    return RETURN_OK;
}

/*
** DecodeYUVN - Decode YUVN format to RGB (internal)
** Returns: RETURN_OK on success, RETURN_FAIL on error
**
** YUVN format stores Y (luminance) and optional U, V (color difference) channels.
** The format supports various subsampling modes:
** - YCHD_MODE_400: Grayscale (Y only, no U/V)
** - YCHD_MODE_411: YUV-411 (U and V subsampled 4:1 horizontally)
** - YCHD_MODE_422: YUV-422 (U and V subsampled 2:1 horizontally)
** - YCHD_MODE_444: YUV-444 (U and V at full resolution)
** - YCHD_MODE_200, YCHD_MODE_211, YCHD_MODE_222: Lores versions
**
** Y values range from 16 (black) to 235 (white) per CCIR standard.
** U and V values range from 16 to 240 (128 means 0, subtract 128 for calculations).
**
** The chunks must appear in order: DATY, DATU, DATV, DATA (optional alpha).
*/
LONG DecodeYUVN(struct IFFPicture *picture)
{
    struct YCHDHeader *ychd;
    UWORD width, height;
    UWORD row, col;
    UBYTE *yBuf;
    UBYTE *uBuf;
    UBYTE *vBuf;
    UBYTE *yData;
    UBYTE *uData;
    UBYTE *vData;
    UBYTE *rgbOut;
    LONG bytesRead;
    ULONG uBytes, vBytes;
    ULONG uStep, vStep;
    UBYTE y, u, v;
    LONG r, g, b;
    BOOL isColor;
    UBYTE *alphaData;
    BOOL hasAlpha;
    UBYTE *alphaBuf;
    ULONG alphaSize;
    UBYTE *yRow;
    UBYTE *uRow;
    UBYTE *vRow;
    UBYTE *alphaRow;
    
    if (!picture || !picture->ychd || !picture->iff) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing YCHD or IFF handle for YUVN decoding");
        return RETURN_FAIL;
    }
    
    ychd = picture->ychd;
    width = ychd->ychd_Width;
    height = ychd->ychd_Height;
    
    /* Determine if color or grayscale */
    isColor = (ychd->ychd_Mode != YCHD_MODE_400 && ychd->ychd_Mode != YCHD_MODE_200);
    
    /* Calculate U and V buffer sizes and step values based on mode */
    switch (ychd->ychd_Mode) {
        case YCHD_MODE_400:
        case YCHD_MODE_200:
            /* Grayscale - no U/V data */
            uBytes = 0;
            vBytes = 0;
            uStep = 0;
            vStep = 0;
            break;
        case YCHD_MODE_411:
            /* U and V subsampled 4:1 horizontally */
            uBytes = width / 4;
            vBytes = width / 4;
            uStep = 4;
            vStep = 4;
            break;
        case YCHD_MODE_422:
        case YCHD_MODE_211:
            /* U and V subsampled 2:1 horizontally */
            uBytes = width / 2;
            vBytes = width / 2;
            uStep = 2;
            vStep = 2;
            break;
        case YCHD_MODE_444:
        case YCHD_MODE_222:
            /* U and V at full resolution */
            uBytes = width;
            vBytes = width;
            uStep = 1;
            vStep = 1;
            break;
        default:
            SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "Unsupported YUVN mode");
            return RETURN_FAIL;
    }
    
    /* Allocate buffers for Y, U, V data */
    yBuf = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    if (!yBuf) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate Y buffer");
        return RETURN_FAIL;
    }
    
    if (isColor && uBytes > 0) {
        uBuf = (UBYTE *)AllocMem(uBytes, MEMF_PUBLIC | MEMF_CLEAR);
        if (!uBuf) {
            FreeMem(yBuf, width);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate U buffer");
            return RETURN_FAIL;
        }
    } else {
        uBuf = NULL;
    }
    
    if (isColor && vBytes > 0) {
        vBuf = (UBYTE *)AllocMem(vBytes, MEMF_PUBLIC | MEMF_CLEAR);
        if (!vBuf) {
            FreeMem(yBuf, width);
            if (uBuf) FreeMem(uBuf, uBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate V buffer");
            return RETURN_FAIL;
        }
    } else {
        vBuf = NULL;
    }
    
    /* Allocate buffers for storing all Y, U, V data */
    yData = (UBYTE *)AllocMem((ULONG)width * height, MEMF_PUBLIC | MEMF_CLEAR);
    if (!yData) {
        FreeMem(yBuf, width);
        if (uBuf) FreeMem(uBuf, uBytes);
        if (vBuf) FreeMem(vBuf, vBytes);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate Y data buffer");
        return RETURN_FAIL;
    }
    
    if (isColor && uBytes > 0) {
        uData = (UBYTE *)AllocMem((ULONG)uBytes * height, MEMF_PUBLIC | MEMF_CLEAR);
        if (!uData) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uBuf) FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate U data buffer");
            return RETURN_FAIL;
        }
    } else {
        uData = NULL;
    }
    
    if (isColor && vBytes > 0) {
        vData = (UBYTE *)AllocMem((ULONG)vBytes * height, MEMF_PUBLIC | MEMF_CLEAR);
        if (!vData) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uData) FreeMem(uData, (ULONG)uBytes * height);
            if (uBuf) FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate V data buffer");
            return RETURN_FAIL;
        }
    } else {
        vData = NULL;
    }
    
    /* Read all DATY rows */
    for (row = 0; row < height; row++) {
        bytesRead = ReadChunkBytes(picture->iff, yBuf, width);
        if (bytesRead != width) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uData) FreeMem(uData, (ULONG)uBytes * height);
            if (vData) FreeMem(vData, (ULONG)vBytes * height);
            if (uBuf) FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read DATY data");
            return RETURN_FAIL;
        }
        /* Copy row to Y data buffer */
        CopyMem(yBuf, yData + (ULONG)row * width, width);
    }
    
    /* Parse to DATU chunk if color image */
    if (isColor && uBytes > 0) {
        if (ParseIFF(picture->iff, IFFPARSE_STEP) != 0) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uData) FreeMem(uData, (ULONG)uBytes * height);
            if (vData) FreeMem(vData, (ULONG)vBytes * height);
            if (uBuf) FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to find DATU chunk");
            return RETURN_FAIL;
        }
        
        /* Read all DATU rows */
        for (row = 0; row < height; row++) {
            bytesRead = ReadChunkBytes(picture->iff, uBuf, uBytes);
            if (bytesRead != uBytes) {
                FreeMem(yBuf, width);
                FreeMem(yData, (ULONG)width * height);
                FreeMem(uData, (ULONG)uBytes * height);
                if (vData) FreeMem(vData, (ULONG)vBytes * height);
                FreeMem(uBuf, uBytes);
                if (vBuf) FreeMem(vBuf, vBytes);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read DATU data");
                return RETURN_FAIL;
            }
            /* Copy row to U data buffer */
            CopyMem(uBuf, uData + (ULONG)row * uBytes, uBytes);
        }
    }
    
    /* Parse to DATV chunk if color image */
    if (isColor && vBytes > 0) {
        if (ParseIFF(picture->iff, IFFPARSE_STEP) != 0) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uData) FreeMem(uData, (ULONG)uBytes * height);
            if (vData) FreeMem(vData, (ULONG)vBytes * height);
            FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to find DATV chunk");
            return RETURN_FAIL;
        }
        
        /* Read all DATV rows */
        for (row = 0; row < height; row++) {
            bytesRead = ReadChunkBytes(picture->iff, vBuf, vBytes);
            if (bytesRead != vBytes) {
                FreeMem(yBuf, width);
                FreeMem(yData, (ULONG)width * height);
                if (uData) FreeMem(uData, (ULONG)uBytes * height);
                FreeMem(vData, (ULONG)vBytes * height);
                FreeMem(uBuf, uBytes);
                FreeMem(vBuf, vBytes);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read DATV data");
                return RETURN_FAIL;
            }
            /* Copy row to V data buffer */
            CopyMem(vBuf, vData + (ULONG)row * vBytes, vBytes);
        }
    }
    
    /* Check for DATA chunk (alpha channel) after DATV (color) or DATY (grayscale) */
    hasAlpha = FALSE;
    alphaData = NULL;
    
    /* Try to parse to DATA chunk (optional alpha) */
    /* For color images, DATA comes after DATV; for grayscale, after DATY */
    if (ParseIFF(picture->iff, IFFPARSE_STEP) == 0) {
        struct ContextNode *cn;

        cn = CurrentChunk(picture->iff);
        if (cn && cn->cn_ID == ID_DATA) {
            /* DATA chunk found - read alpha channel */
            alphaSize = (ULONG)width * height;
            alphaData = (UBYTE *)AllocMem(alphaSize, MEMF_PUBLIC | MEMF_CLEAR);
            if (alphaData) {
                alphaBuf = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
                if (alphaBuf) {
                    hasAlpha = TRUE;
                    /* Read all alpha rows */
                    for (row = 0; row < height; row++) {
                        bytesRead = ReadChunkBytes(picture->iff, alphaBuf, width);
                        if (bytesRead != width) {
                            /* Error reading alpha - treat as no alpha */
                            FreeMem(alphaBuf, width);
                            FreeMem(alphaData, alphaSize);
                            alphaData = NULL;
                            hasAlpha = FALSE;
                            break;
                        }
                        /* Copy row to alpha data buffer */
                        CopyMem(alphaBuf, alphaData + (ULONG)row * width, width);
                    }
                    FreeMem(alphaBuf, width);
                } else {
                    /* Failed to allocate alpha buffer - no alpha */
                    FreeMem(alphaData, alphaSize);
                    alphaData = NULL;
                }
            }
        }
    }
    
    /* Reallocate pixel data buffer to include alpha if needed */
    if (hasAlpha && alphaData) {
        /* Free RGB-only buffer and allocate RGBA buffer */
        FreeMem(picture->pixelData, picture->pixelDataSize);
        picture->pixelDataSize = (ULONG)width * height * 4;
        picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (!picture->pixelData) {
            FreeMem(yBuf, width);
            FreeMem(yData, (ULONG)width * height);
            if (uData) FreeMem(uData, (ULONG)uBytes * height);
            if (vData) FreeMem(vData, (ULONG)vBytes * height);
            if (uBuf) FreeMem(uBuf, uBytes);
            if (vBuf) FreeMem(vBuf, vBytes);
            FreeMem(alphaData, (ULONG)width * height);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate RGBA pixel data buffer");
            return RETURN_FAIL;
        }
    }
    
    /* Get RGB/RGBA output buffer */
    rgbOut = picture->pixelData;
    
    /* Convert YUV to RGB/RGBA for all rows */
    for (row = 0; row < height; row++) {
        yRow = yData + (ULONG)row * width;
        uRow = (uData) ? (uData + (ULONG)row * uBytes) : NULL;
        vRow = (vData) ? (vData + (ULONG)row * vBytes) : NULL;
        alphaRow = (alphaData) ? (alphaData + (ULONG)row * width) : NULL;
        
        for (col = 0; col < width; col++) {
            y = yRow[col];
            
            if (isColor) {
                /* Get U and V values based on subsampling */
                LONG uSigned, vSigned;
                
                if (uRow) {
                    u = uRow[col / uStep];
                    /* Convert from CCIR range (16-240, 128=0) to signed (-112 to 112) */
                    uSigned = (LONG)u - 128;
                } else {
                    uSigned = 0;
                }
                
                if (vRow) {
                    v = vRow[col / vStep];
                    /* Convert from CCIR range (16-240, 128=0) to signed (-112 to 112) */
                    vSigned = (LONG)v - 128;
                } else {
                    vSigned = 0;
                }
                
                /* Convert YUV to RGB using CCIR-601 coefficients */
                /* R = Y + 1.140 * V */
                /* G = Y - 0.396 * U - 0.581 * V */
                /* B = Y + 2.029 * U */
                /* Using integer math: multiply by 1000, then divide */
                r = 1000L * (LONG)y + 1140L * vSigned;
                g = 1000L * (LONG)y - 396L * uSigned - 581L * vSigned;
                b = 1000L * (LONG)y + 2029L * uSigned;
                
                r /= 1000;
                g /= 1000;
                b /= 1000;
                
                /* Clamp to 0-255 range */
                if (r > 255) r = 255;
                if (r < 0) r = 0;
                if (g > 255) g = 255;
                if (g < 0) g = 0;
                if (b > 255) b = 255;
                if (b < 0) b = 0;
                
                rgbOut[0] = (UBYTE)r;
                rgbOut[1] = (UBYTE)g;
                rgbOut[2] = (UBYTE)b;
                if (hasAlpha && alphaRow) {
                    rgbOut[3] = alphaRow[col];
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            } else {
                /* Grayscale - Y only, convert to RGB */
                /* Y is in range 16-235, scale to 0-255 */
                if (y < 16) {
                    y = 0;
                } else if (y > 235) {
                    y = 255;
                } else {
                    /* Scale from 16-235 to 0-255 */
                    y = (UBYTE)(((LONG)y - 16) * 255 / (235 - 16));
                }
                
                rgbOut[0] = y;
                rgbOut[1] = y;
                rgbOut[2] = y;
                if (hasAlpha && alphaRow) {
                    rgbOut[3] = alphaRow[col];
                    rgbOut += 4;
                } else {
                    rgbOut += 3;
                }
            }
        }
    }
    
    /* Free all buffers */
    FreeMem(yBuf, width);
    FreeMem(yData, (ULONG)width * height);
    if (uData) FreeMem(uData, (ULONG)uBytes * height);
    if (vData) FreeMem(vData, (ULONG)vBytes * height);
    if (uBuf) FreeMem(uBuf, uBytes);
    if (vBuf) FreeMem(vBuf, vBytes);
    if (alphaData) FreeMem(alphaData, (ULONG)width * height);
    
    /* Set format flags */
    picture->isIndexed = FALSE;
    picture->isGrayscale = !isColor;
    picture->isCompressed = FALSE;
    picture->hasAlpha = hasAlpha;
    
    return RETURN_OK;
}

