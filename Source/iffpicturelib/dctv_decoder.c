/*
** dctv_decoder.c - DCTV (Digital Component Television Video) ILBM decoder
**
** DCTV stores YCbCr in standard FORM ILBM (3 or 4 bitplanes, ByteRun1).
** Palette indices are not display colours; they encode composite Y/C samples
** that are reconstructed with the pipeline from dctv_codec / original DCTV
** software (LFSR signature, join/chroma/luma, YUV tables, edge blanking).
*/

#include "iffpicture_private.h"
#include <proto/exec.h>
#include <proto/iffparse.h>

#define DCTV_MAX_PLANES  4
#define DCTV_MIN_WIDTH   256

extern const UWORD dctv_yuv_tables[];

#define RowBytesDCTV(w) ((((w) + 15) >> 4) << 1)

struct IFFDCTVState {
    UWORD Width;
    UWORD Height;
    UWORD BytesPerRow;
    UBYTE Depth;
    UBYTE NColors;
    UWORD Lace;
    UWORD LineNum;
    UBYTE Palette[16];
    UBYTE *Red;
    UBYTE *Green;
    UBYTE *Blue;
    UBYTE *Chunky;
    UBYTE *FBuf1[4];
    UBYTE *FBuf2[4];
    UBYTE *Planes[DCTV_MAX_PLANES];
};

static WORD dctv_minmax(WORD pix, WORD minv, WORD maxv)
{
    if (pix < minv) {
        pix = minv;
    } else if (pix > maxv) {
        pix = maxv;
    }
    return pix;
}

static VOID dctv_pal2direct(struct IFFDCTVState *st)
{
    UWORD width;
    UBYTE *pixelPtr;

    width = st->Width;
    pixelPtr = st->Chunky;
    while (width > 0) {
        *pixelPtr = st->Palette[*pixelPtr];
        pixelPtr++;
        width--;
    }
}

static VOID dctv_join(UBYTE *chunky, UBYTE *compositeRow, UWORD width, UWORD zzflag)
{
    UWORD pairCount;
    UBYTE composite;

    pairCount = (width >> 1) - 1 - zzflag;
    chunky = chunky + zzflag + 1;
    compositeRow = compositeRow + 2 * (zzflag + 1);

    while (pairCount > 0) {
        composite = *chunky++;
        composite <<= 1;
        composite |= *chunky++;
        if (!composite) {
            composite = 0x40;
        }
        *compositeRow++ = composite;
        *compositeRow++ = composite;
        pairCount--;
    }
}

static VOID dctv_filter(UBYTE *channelBuf, UWORD width)
{
    WORD remaining;
    UBYTE tapOld;
    UBYTE tapMid;
    UBYTE tapNew;
    UBYTE *readPtr;

    remaining = (WORD)width - 2;
    readPtr = channelBuf + 3;
    tapMid = channelBuf[1];
    tapNew = channelBuf[2];

    while (remaining > 0) {
        tapOld = tapMid;
        tapMid = tapNew;
        tapNew = *readPtr++;
        *channelBuf++ = (UBYTE)((tapOld + tapMid + tapMid + tapNew) >> 2);
        remaining--;
    }
    *channelBuf++ = (UBYTE)((tapMid + tapNew + tapNew) >> 2);
    *channelBuf = (UBYTE)(tapNew >> 1);
}

static VOID dctv_yuv2rgb(UBYTE *lumaRow, BYTE *chroma1Buf, BYTE *chroma2Buf,
    struct IFFDCTVState *st)
{
    WORD countdown;
    WORD channelAccum;
    WORD prevChroma1;
    WORD prevChroma2;
    WORD lumaScaled;
    WORD interpCr;
    WORD interpCb;
    UBYTE *redOut;
    UBYTE *greenOut;
    UBYTE *blueOut;

    countdown = (WORD)st->Width - 1;
    redOut = st->Red;
    greenOut = st->Green;
    blueOut = st->Blue;
    prevChroma1 = (WORD)(*chroma1Buf++);
    prevChroma2 = 0;

    while (countdown >= 0) {
        if (countdown & 1) {
            channelAccum = (WORD)(*chroma2Buf++);
            interpCr = prevChroma1;
            interpCb = (channelAccum + prevChroma2) >> 1;
            prevChroma2 = channelAccum;
        } else {
            if (countdown) {
                channelAccum = (WORD)(*chroma1Buf++);
            } else {
                channelAccum = 0;
            }
            interpCb = prevChroma2;
            interpCr = (channelAccum + prevChroma1) >> 1;
            prevChroma1 = channelAccum;
        }

        lumaScaled = (WORD)dctv_yuv_tables[*lumaRow++];
        channelAccum = (WORD)dctv_yuv_tables[interpCr + 0x180] + lumaScaled;
        *redOut++ = (UBYTE)dctv_minmax(channelAccum >> 4, 0, 255);
        channelAccum = (WORD)dctv_yuv_tables[interpCr + 0x380]
            + (WORD)dctv_yuv_tables[interpCb + 0x480] + lumaScaled;
        *greenOut++ = (UBYTE)dctv_minmax(channelAccum >> 4, 0, 255);
        channelAccum = (WORD)dctv_yuv_tables[interpCb + 0x280] + lumaScaled;
        *blueOut++ = (UBYTE)dctv_minmax(channelAccum >> 4, 0, 255);
        countdown--;
    }
}

static VOID dctv_chroma(UBYTE *compositeRow, UWORD width, UWORD zzflag)
{
    WORD remaining;
    WORD chromaEst;
    WORD sampleOld;
    WORD sampleMid;
    WORD sampleNew;
    BYTE *chromaOut;
    BYTE polarity;

    polarity = 0;
    chromaOut = (BYTE *)(compositeRow + width + 1);
    compositeRow = compositeRow + zzflag + 1;
    remaining = (WORD)((width >> 1) - 2 + zzflag);
    sampleNew = *compositeRow;
    sampleMid = (sampleNew + 64) >> 1;

    while (remaining > 0) {
        sampleOld = sampleMid;
        sampleMid = sampleNew;
        compositeRow += 2;
        sampleNew = *compositeRow;
        chromaEst = sampleMid + sampleMid - sampleOld - sampleNew + 2;
        if (chromaEst < 0) {
            chromaEst += 3;
        }
        chromaEst >>= 2;
        if (polarity ^= 1) {
            chromaEst = -chromaEst;
        }
        *chromaOut++ = (BYTE)dctv_minmax(chromaEst, -127, 127);
        remaining--;
    }
}

static VOID dctv_luma(UBYTE *compositeRow, UWORD width, UWORD zzflag)
{
    WORD remaining;
    WORD lumaCorr;
    WORD recoveredLuma;
    WORD windowSum;
    WORD ctap1;
    WORD ctap2;
    WORD ctap3;
    WORD ctap4;
    WORD ctap5;
    WORD ctap6;
    WORD ctap7;
    BYTE *chromaIn;
    BYTE *chromaBase;
    BYTE corrSign;

    chromaBase = (BYTE *)(compositeRow + width);
    chromaIn = (BYTE *)(compositeRow + width + 1);
    compositeRow = compositeRow + zzflag + 1;
    remaining = (WORD)((width >> 1) - 2 + zzflag - 1);
    corrSign = 1;
    ctap5 = (WORD)(*chromaIn++);
    ctap6 = (WORD)(*chromaIn++);
    ctap7 = (WORD)(*chromaIn++);
    windowSum = ctap5 + ctap6 + ctap7 + 4;
    ctap1 = ctap2 = ctap3 = ctap4 = 0;

    if (!zzflag) {
        corrSign = 0;
        ctap4 = ctap5;
        ctap5 = ctap6;
        ctap6 = ctap7;
        ctap7 = (WORD)(*chromaIn++);
        windowSum = windowSum + ctap7 + ctap4;
        lumaCorr = (WORD)(*compositeRow) + (windowSum >> 3);
        lumaCorr = lumaCorr + lumaCorr - 64;
        recoveredLuma = dctv_minmax(lumaCorr, 64, 224);
        lumaCorr -= recoveredLuma;
        if (lumaCorr) {
            lumaCorr <<= 2;
            lumaCorr += ctap4;
            lumaCorr = dctv_minmax(lumaCorr, -127, 127);
            windowSum = windowSum - ctap4 - ctap4 + lumaCorr + lumaCorr;
            chromaIn[-4] = (BYTE)lumaCorr;
            ctap4 = lumaCorr;
        }
        *compositeRow++ = 64;
        *compositeRow++ = (UBYTE)recoveredLuma;
    }

    while (remaining > 0) {
        lumaCorr = windowSum - ctap1 - ctap4;
        ctap1 = ctap2;
        ctap2 = ctap3;
        ctap3 = ctap4;
        ctap4 = ctap5;
        ctap5 = ctap6;
        ctap6 = ctap7;
        ctap7 = 0;
        if (remaining >= (WORD)(zzflag + 3)) {
            ctap7 = (WORD)(*chromaIn++);
        }
        windowSum = lumaCorr + ctap7 + ctap4;
        lumaCorr = windowSum >> 3;
        if (!(corrSign ^= 1)) {
            lumaCorr = -lumaCorr;
        }
        recoveredLuma = dctv_minmax((WORD)(*compositeRow) - lumaCorr, 64, 224);
        *compositeRow++ = (UBYTE)recoveredLuma;
        *compositeRow++ = (UBYTE)recoveredLuma;
        remaining--;
    }

    if (zzflag) {
        compositeRow[0] = 64;
        compositeRow[1] = 64;
    } else {
        recoveredLuma = (windowSum - ctap1 - ctap4 + ctap5) >> 3;
        if (corrSign) {
            recoveredLuma = -recoveredLuma;
        }
        recoveredLuma = (WORD)(*compositeRow) - recoveredLuma;
        lumaCorr = dctv_minmax(recoveredLuma, 64, 224);
        recoveredLuma -= lumaCorr;
        if (recoveredLuma) {
            recoveredLuma <<= 2;
            chromaIn[-1] = (BYTE)((WORD)ctap5 - recoveredLuma);
        }
        *compositeRow++ = (UBYTE)lumaCorr;
        *compositeRow++ = 64;
    }

    chromaIn = chromaBase;
    lumaCorr = (WORD)(chromaIn - (BYTE *)compositeRow);
    if (lumaCorr > 0) {
        lumaCorr -= 1;
        while (lumaCorr >= 0) {
            *compositeRow++ = 64;
            lumaCorr--;
        }
    }
    *chromaIn++ = 0;
    if (zzflag) {
        chromaIn[0] = chromaIn[1];
    }
    chromaIn = chromaBase + (width >> 1) - 1;
    lumaCorr = (WORD)(*chromaIn);
    if (lumaCorr < 0) {
        lumaCorr += 1;
    }
    lumaCorr >>= 1;
    *chromaIn = (BYTE)lumaCorr;
}

static VOID dctv_p2c(UBYTE *chunky, struct IFFDCTVState *st, UWORD line)
{
    UWORD bpr;
    UWORD width;
    UBYTE depth;
    ULONG off;
    UWORD x;
    UWORD bit;
    UBYTE pixel;
    UBYTE p;
    UBYTE planeByte;

    bpr = st->BytesPerRow;
    width = st->Width;
    depth = st->Depth;
    off = (ULONG)line * (ULONG)bpr;

    for (x = 0; x < width; x++) {
        bit = (UWORD)(7 - (x & 7));
        pixel = 0;
        for (p = 0; p < depth; p++) {
            planeByte = st->Planes[p][off + (x >> 3)];
            if ((planeByte >> bit) & 1) {
                pixel |= (UBYTE)(1 << p);
            }
        }
        chunky[x] = pixel;
    }
}

static VOID dctv_dc2rgb(struct IFFDCTVState *st, UWORD linenr)
{
    UBYTE *curLineBuf;
    UBYTE *prevLineBuf;
    BYTE *chromaBuf1;
    BYTE *chromaBuf2;
    UWORD width;
    ULONG fieldIdx;
    UWORD zzflag;
    UWORD lineIdx;

    dctv_p2c(st->Chunky, st, linenr);
    dctv_pal2direct(st);
    width = st->Width;
    lineIdx = linenr;

    if (st->Lace) {
        fieldIdx = ((ULONG)lineIdx + 1UL) & 1UL;
        lineIdx >>= 1;
    } else {
        fieldIdx = 0;
    }

    if (!fieldIdx) {
        curLineBuf = st->FBuf1[lineIdx & 3];
        prevLineBuf = st->FBuf1[(lineIdx - 1) & 3];
    } else {
        curLineBuf = st->FBuf2[lineIdx & 3];
        prevLineBuf = st->FBuf2[(lineIdx - 1) & 3];
    }

    if (st->Chunky[1] == 0x54) {
        zzflag = 1;
    } else {
        zzflag = 0;
    }

    dctv_join(st->Chunky, curLineBuf, width, zzflag);
    dctv_chroma(curLineBuf, width, zzflag);
    dctv_luma(curLineBuf, width, zzflag);
    chromaBuf1 = (BYTE *)(prevLineBuf + width);
    chromaBuf2 = (BYTE *)(curLineBuf + width);
    if (zzflag) {
        dctv_yuv2rgb(curLineBuf, chromaBuf2, chromaBuf1, st);
    } else {
        dctv_yuv2rgb(curLineBuf, chromaBuf1, chromaBuf2, st);
    }
    dctv_filter(st->Red, width);
    dctv_filter(st->Green, width);
    dctv_filter(st->Blue, width);
}

static VOID dctv_blank(struct IFFDCTVState *st, UWORD startPixel, UWORD count)
{
    BYTE *redPtr;
    BYTE *greenPtr;
    BYTE *bluePtr;

    redPtr = (BYTE *)(st->Red + startPixel);
    greenPtr = (BYTE *)(st->Green + startPixel);
    bluePtr = (BYTE *)(st->Blue + startPixel);
    while (count > 0) {
        *redPtr++ = 0;
        *greenPtr++ = 0;
        *bluePtr++ = 0;
        count--;
    }
}

static VOID dctv_convert_line(struct IFFDCTVState *st)
{
    UWORD lineIdx;

    lineIdx = st->LineNum;
    if ((lineIdx > st->Lace) && (lineIdx < st->Height)) {
        dctv_dc2rgb(st, lineIdx);
        dctv_blank(st, 0, 4);
        dctv_blank(st, (UWORD)(st->Width - 2), 2);
    } else {
        dctv_blank(st, 0, st->Width);
    }
    st->LineNum++;
}

/*
** CheckDCTV signature in highest bitplane (from dctv_codec).
** Minimum row width 32 bytes (256 pixels).
*/
static BOOL dctv_check_signature(UBYTE * const *planes, UWORD bytesPerRow, UBYTE depth)
{
    static const UBYTE lfsr[8] = { 0xD5, 0xAB, 0x57, 0xAF, 0x5F, 0xBF, 0x7F, 0xFF };
    UBYTE lfsrBuf[32];
    UBYTE *planePtr;
    WORD planeDepth;
    WORD byteIdx;
    WORD bitIdx;
    WORD shiftCount;
    UBYTE currentByte;
    UBYTE lfsrByte;
    UBYTE signByte;

    planeDepth = (WORD)depth;
    if (bytesPerRow < 32 || planeDepth > DCTV_MAX_PLANES) {
        return FALSE;
    }

    while (planeDepth > 0) {
        planeDepth--;
        planePtr = planes[planeDepth];
        shiftCount = (WORD)(bytesPerRow - 31);
        while ((*planePtr == 0) && (shiftCount != 0)) {
            planePtr++;
            shiftCount--;
        }
        if (shiftCount == 0) {
            continue;
        }
        currentByte = *planePtr;
        shiftCount = 7;
        while ((currentByte & 0x80) == 0) {
            currentByte <<= 1;
            shiftCount--;
        }
        lfsrByte = lfsr[shiftCount];
        for (byteIdx = 0; byteIdx < 32; byteIdx++) {
            signByte = 0;
            for (bitIdx = 0; bitIdx < 8; bitIdx++) {
                currentByte = (UBYTE)((lfsrByte & 0xC3) + 0x41);
                currentByte = (UBYTE)((currentByte & 0x82) + 0x7E);
                currentByte = (UBYTE)((currentByte >> 7) & 1);
                lfsrByte = (UBYTE)(lfsrByte + lfsrByte + currentByte);
                signByte = (UBYTE)(signByte + signByte + 1 - currentByte);
            }
            lfsrBuf[byteIdx] = signByte;
        }
        if (shiftCount == 7) {
            lfsrBuf[31] &= 0xFE;
        }
        for (byteIdx = 0; byteIdx < 31; byteIdx++) {
            currentByte = *planePtr++;
            lfsrByte = lfsrBuf[byteIdx];
            if (currentByte != lfsrByte) {
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

static VOID dctv_setmap(struct IFFDCTVState *st, struct IFFColorMap *cmap)
{
    UBYTE nColors;
    UBYTE directVal;
    UBYTE *palOut;
    ULONG i;
    UBYTE r8;
    UBYTE g8;
    UBYTE b8;
    UWORD paletteColor;

    nColors = st->NColors;
    palOut = st->Palette;
    for (i = 0; i < (ULONG)nColors && i < cmap->numcolors; i++) {
        r8 = cmap->data[i * 3];
        g8 = cmap->data[i * 3 + 1];
        b8 = cmap->data[i * 3 + 2];
        paletteColor = (UWORD)(((ULONG)(r8 & 0xF0) << 4) | (ULONG)(g8 & 0xF0) | ((ULONG)b8 >> 4));
        directVal = 0;
        if (paletteColor & 1) {
            directVal = 64;
        }
        if (paletteColor & 0x800) {
            directVal |= 16;
        }
        if (paletteColor & 8) {
            directVal |= 4;
        }
        if (paletteColor & 0x80) {
            directVal |= 1;
        }
        *palOut++ = directVal;
    }
}

static struct IFFDCTVState *dctv_alloc_state(UBYTE *planes[DCTV_MAX_PLANES],
    UWORD width, UWORD height, UBYTE depth, UWORD lace)
{
    struct IFFDCTVState *st;
    ULONG allocSize;
    UBYTE *field1Buf;
    UBYTE *field2Buf;
    WORD rowIdx;
    WORD stride15;
    UBYTE p;

    if (width < DCTV_MIN_WIDTH || height < (UWORD)(2 * (lace + 1)) || depth > DCTV_MAX_PLANES) {
        return NULL;
    }

    allocSize = sizeof(struct IFFDCTVState) + ((ULONG)width << 4);
    st = (struct IFFDCTVState *)AllocMem(allocSize, MEMF_PUBLIC | MEMF_CLEAR);
    if (!st) {
        return NULL;
    }

    st->Width = width;
    st->Height = height;
    st->BytesPerRow = (UWORD)(width >> 3);
    st->Depth = depth;
    st->NColors = (UBYTE)(1U << depth);
    st->Lace = lace & 1;
    st->LineNum = 0;
    for (p = 0; p < depth; p++) {
        st->Planes[p] = planes[p];
    }

    st->Red = (UBYTE *)st + sizeof(struct IFFDCTVState);
    st->Green = st->Red + width;
    st->Blue = st->Green + width;
    st->Chunky = st->Blue + width;
    field1Buf = st->Chunky + width;
    field2Buf = field1Buf;

    for (rowIdx = 0; rowIdx < (WORD)width; rowIdx++) {
        *field2Buf++ = 0x40;
    }

    stride15 = (WORD)(width >> 1);
    stride15 = (WORD)(stride15 + (WORD)width);
    field2Buf = field1Buf + ((ULONG)stride15 << 2);

    CopyMem(field1Buf + stride15, field1Buf, (ULONG)width);
    CopyMem(field1Buf + ((ULONG)stride15 << 1), field1Buf, (ULONG)stride15 << 1);
    CopyMem(field2Buf, field1Buf, (ULONG)stride15 << 2);

    for (rowIdx = 0; rowIdx < 4; rowIdx++) {
        st->FBuf1[rowIdx] = field1Buf + (ULONG)stride15 * (ULONG)rowIdx;
        st->FBuf2[rowIdx] = field2Buf + (ULONG)stride15 * (ULONG)rowIdx;
    }

    return st;
}

static LONG dctv_unpack_row_exact(const UBYTE *src, ULONG srcLen, UBYTE *dest,
    ULONG destLen, ULONG *consumed)
{
    ULONG inPos;
    ULONG outPos;
    UBYTE n;
    ULONG count;
    UBYTE val;

    inPos = 0;
    outPos = 0;
    while (outPos < destLen && inPos < srcLen) {
        n = src[inPos++];
        if (n <= 127) {
            count = (ULONG)n + 1UL;
            if (inPos + count > srcLen || outPos + count > destLen) {
                return RETURN_FAIL;
            }
            CopyMem((APTR)(src + inPos), dest + outPos, count);
            inPos += count;
            outPos += count;
        } else if (n == 128) {
            count = 129UL;
            if (inPos >= srcLen || outPos + count > destLen) {
                return RETURN_FAIL;
            }
            val = src[inPos++];
            while (count-- > 0UL) {
                dest[outPos++] = val;
            }
        } else {
            count = 257UL - (ULONG)n;
            if (inPos >= srcLen || outPos + count > destLen) {
                return RETURN_FAIL;
            }
            val = src[inPos++];
            while (count-- > 0UL) {
                dest[outPos++] = val;
            }
        }
    }
    if (outPos != destLen) {
        return RETURN_FAIL;
    }
    *consumed = inPos;
    return RETURN_OK;
}

static LONG dctv_decompress_body(struct IFFPicture *picture, const UBYTE *bodyBuf,
    ULONG bodySize, UBYTE *rawBody, ULONG rawLen)
{
    UWORD height;
    UBYTE depth;
    UWORD rowBytes;
    UWORD y;
    UBYTE p;
    ULONG destOff;
    ULONG inPos;
    ULONG consumed;
    LONG result;

    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytesDCTV(picture->bmhd->w);

    if (picture->bmhd->compression == cmpNone) {
        if (bodySize != rawLen) {
            return RETURN_FAIL;
        }
        CopyMem((APTR)bodyBuf, rawBody, rawLen);
        return RETURN_OK;
    }
    if (picture->bmhd->compression != cmpByteRun1) {
        return RETURN_FAIL;
    }

    inPos = 0;
    for (y = 0; y < height; y++) {
        for (p = 0; p < depth; p++) {
            destOff = ((ULONG)y * (ULONG)depth + (ULONG)p) * (ULONG)rowBytes;
            result = dctv_unpack_row_exact(bodyBuf + inPos, bodySize - inPos,
                rawBody + destOff, (ULONG)rowBytes, &consumed);
            if (result != RETURN_OK) {
                return RETURN_FAIL;
            }
            inPos += consumed;
        }
    }
    if (inPos != bodySize) {
        return RETURN_FAIL;
    }
    return RETURN_OK;
}

static VOID dctv_planes_for_row(UBYTE *rawBody, UBYTE *planePtrs[DCTV_MAX_PLANES],
    UWORD row, UBYTE depth, UWORD rowBytes)
{
    UBYTE p;
    ULONG rowBase;

    for (p = 0; p < depth; p++) {
        rowBase = ((ULONG)row * (ULONG)depth + (ULONG)p) * (ULONG)rowBytes;
        planePtrs[p] = rawBody + rowBase;
    }
}

BOOL IsDCTVCandidate(struct IFFPicture *picture)
{
    UWORD depth;

    if (!picture || picture->formtype != ID_ILBM) {
        return FALSE;
    }
    if (!picture->bmhd || !picture->cmap || !picture->cmap->data) {
        return FALSE;
    }
    if (picture->isFramestore || picture->isHAM || picture->isEHB || picture->isDigiViewRgb) {
        return FALSE;
    }
    if (IFFMultipalette_Active(picture)) {
        return FALSE;
    }
    depth = picture->bmhd->nPlanes;
    if (depth < 3U || depth > DCTV_MAX_PLANES) {
        return FALSE;
    }
    if (picture->bmhd->w < DCTV_MIN_WIDTH) {
        return FALSE;
    }
    if (picture->bmhd->masking == mskHasMask || picture->bmhd->masking == mskHasAlpha) {
        return FALSE;
    }
    if (picture->bmhd->compression != cmpNone && picture->bmhd->compression != cmpByteRun1) {
        return FALSE;
    }
    return TRUE;
}

/*
** Read BODY chunk once into picture->bodyReadCache (shared with ILBM decode fallback).
*/
LONG CacheBODY(struct IFFPicture *picture)
{
    struct ContextNode *cn;
    UBYTE *buf;
    LONG bytesRead;

    if (!picture || !picture->iff) {
        return RETURN_FAIL;
    }
    if (picture->bodyReadCache) {
        return RETURN_OK;
    }
    cn = CurrentChunk(picture->iff);
    if (!cn || cn->cn_ID != ID_BODY) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "BODY chunk not current for cache read");
        return RETURN_FAIL;
    }
    if (cn->cn_Size == 0) {
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Empty BODY chunk");
        return RETURN_FAIL;
    }
    buf = (UBYTE *)AllocMem((ULONG)cn->cn_Size, MEMF_PUBLIC);
    if (!buf) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate BODY cache");
        return RETURN_FAIL;
    }
    bytesRead = ReadChunkBytes(picture->iff, buf, cn->cn_Size);
    if (bytesRead != cn->cn_Size) {
        FreeMem(buf, (ULONG)cn->cn_Size);
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "Failed to read BODY into cache");
        return RETURN_FAIL;
    }
    picture->bodyReadCache = buf;
    picture->bodyReadCacheSize = (ULONG)cn->cn_Size;
    picture->bodyReadRawOffset = 0;
    return RETURN_OK;
}

/*
** DecodeDCTV - decode DCTV ILBM to RGB in picture->pixelData.
** Returns DCTV_NOT_DCTV when BODY is loaded but LFSR signature does not match
** (caller should fall back to standard ILBM decode).
*/
LONG DecodeDCTV(struct IFFPicture *picture)
{
    UWORD width;
    UWORD height;
    UWORD rowBytes;
    UWORD pixWidth;
    UBYTE depth;
    ULONG rawLen;
    UBYTE *rawBody;
    UBYTE *planePtrs[DCTV_MAX_PLANES];
    UWORD lace;
    UWORD y;
    UWORD outWidth;
    UWORD col;
    struct IFFDCTVState *st;
    UBYTE p;
    LONG result;
    UBYTE *rgbOut;

    if (!picture || !picture->bmhd || !picture->cmap) {
        SetIFFPictureError(picture, IFFPICTURE_INVALID, "Missing BMHD or CMAP for DCTV decoding");
        return RETURN_FAIL;
    }

    width = picture->bmhd->w;
    height = picture->bmhd->h;
    depth = picture->bmhd->nPlanes;
    rowBytes = RowBytesDCTV(width);
    pixWidth = (UWORD)(rowBytes << 3);
    rawLen = (ULONG)height * (ULONG)depth * (ULONG)rowBytes;

    if (CacheBODY(picture) != RETURN_OK) {
        return RETURN_FAIL;
    }

    rawBody = (UBYTE *)AllocMem(rawLen, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rawBody) {
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DCTV plane buffer");
        return RETURN_FAIL;
    }

    result = dctv_decompress_body(picture, picture->bodyReadCache, picture->bodyReadCacheSize,
        rawBody, rawLen);
    if (result != RETURN_OK) {
        FreeMem(rawBody, rawLen);
        SetIFFPictureError(picture, IFFPICTURE_BADFILE, "DCTV BODY decompression failed");
        return RETURN_FAIL;
    }

    for (p = 0; p < depth; p++) {
        planePtrs[p] = rawBody + (ULONG)p * (ULONG)rowBytes;
    }
    if (!dctv_check_signature(planePtrs, rowBytes, depth)) {
        FreeMem(rawBody, rawLen);
        picture->isDCTV = FALSE;
        return IFFPICTURE_NOT_DCTV;
    }

    lace = 0;
    if ((picture->viewportmodes & vmLACE) != 0) {
        lace = 1;
    } else if (picture->bmhd->pageHeight >= 400) {
        lace = 1;
    }

    st = dctv_alloc_state(planePtrs, pixWidth, height, depth, lace);
    if (!st) {
        FreeMem(rawBody, rawLen);
        SetIFFPictureError(picture, IFFPICTURE_NOMEM, "Failed to allocate DCTV decode state");
        return RETURN_FAIL;
    }
    dctv_setmap(st, picture->cmap);

    outWidth = width;
    if (pixWidth < width) {
        outWidth = pixWidth;
    }

    rgbOut = picture->pixelData;
    st->LineNum = 0;
    for (y = 0; y < height; y++) {
        dctv_planes_for_row(rawBody, st->Planes, y, depth, rowBytes);
        dctv_convert_line(st);
        for (col = 0; col < outWidth; col++) {
            rgbOut[0] = st->Red[col];
            rgbOut[1] = st->Green[col];
            rgbOut[2] = st->Blue[col];
            rgbOut += 3;
        }
    }

    FreeMem(st, sizeof(struct IFFDCTVState) + ((ULONG)pixWidth << 4));
    FreeMem(rawBody, rawLen);
    picture->isDCTV = TRUE;
    picture->isIndexed = FALSE;
    picture->hasAlpha = FALSE;
    return RETURN_OK;
}
