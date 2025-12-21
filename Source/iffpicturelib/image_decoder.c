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
    curpos = curline;
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
    UBYTE *paletteOut; /* For storing original palette indices */
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
    
    DEBUG_PRINTF4("DEBUG: DecodeILBM - Starting decode: %ldx%ld, %ld planes, masking=%ld\n",
                  width, height, depth, picture->bmhd->masking);
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
    
    /* Allocate pixel data buffer */
    if (picture->bmhd->masking == mskHasMask) {
        picture->pixelDataSize = (ULONG)width * height * 4; /* RGBA */
        picture->hasAlpha = TRUE;
    } else {
        picture->pixelDataSize = (ULONG)width * height * 3; /* RGB */
        picture->hasAlpha = FALSE;
    }
    
    /* Use public memory (not chip RAM, we're not rendering to display) */
    picture->pixelData = (UBYTE *)AllocMem(picture->pixelDataSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!picture->pixelData) {
        FreeMem(picture->paletteIndices, picture->paletteIndicesSize);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate pixel data buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate buffer for one plane row */
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    /* Allocate alpha buffer if mask plane present */
    alphaValues = NULL;
    if (picture->bmhd->masking == mskHasMask) {
        alphaValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
        if (!alphaValues) {
            FreeMem(planeBuffer, rowBytes);
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate alpha buffer");
            return RETURN_FAIL;
        }
    }
    
    rgbOut = picture->pixelData;
    paletteOut = picture->paletteIndices;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
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
        
        /* Convert pixel indices to RGB using CMAP and store original indices */
        for (col = 0; col < width; col++) {
            pixelIndex = pixelIndices[col];
            
            /* Clamp to valid CMAP range */
            if (pixelIndex >= maxColors) {
                pixelIndex = (UBYTE)(maxColors - 1);
            }
            
            /* Store original palette index */
            *paletteOut++ = pixelIndex;
            
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
    
    FreeMem(planeBuffer, rowBytes);
    if (alphaValues) {
        FreeMem(alphaValues, width);
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel values for this row */
        UBYTE *pixelValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    if (!planeBuffer) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate plane buffer");
        return RETURN_FAIL;
    }
    
    rgbOut = picture->pixelData;
    
    /* Process each row */
    for (row = 0; row < height; row++) {
        /* Clear pixel indices for this row */
        UBYTE *pixelIndices = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
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
    planeBuffer = (UBYTE *)AllocMem(rowBytes, MEMF_PUBLIC | MEMF_CLEAR);
    rValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    gValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    bValues = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
    
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
    rowBuffer = (UBYTE *)AllocMem(width, MEMF_PUBLIC | MEMF_CLEAR);
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

