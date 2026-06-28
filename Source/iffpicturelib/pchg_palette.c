/*
** pchg_palette.c - SHAM / PCHG / CTBL multipalette for ILBM-style indexed decode
**
** Line mask and change parsing follow ILBM picture.datatype-style consumption (byte
** stream, MSB-first within each mask byte).
*/

#include "iffpicture_private.h"
#include <proto/exec.h>
#include <proto/iffparse.h>

#define PCHG_COMP_NONE     0
#define PCHG_COMP_HUFFMANN 1
#define IFF_PCHGF_12BIT    1
#define IFF_PCHGF_32BIT    2

static ULONG mp_rdbe32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | (ULONG)p[3];
}

static UWORD mp_rdbe16(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

static UWORD mp_mask_bytes(UWORD lineCount)
{
    return (UWORD)(4U * ((ULONG)lineCount + 31UL) / 32UL);
}

/*
** PCHG_Decompress - expand Huffman-packed PCHG payload.
*/
static void PCHG_Decompress(const ULONG *Source, UBYTE *Dest, WORD *Tree, ULONG OriginalSize)
{
    ULONG i;
    ULONG bits;
    ULONG CurLongword;
    WORD *p;

    i = 0;
    bits = 0;
    CurLongword = 0;
    p = Tree;

    while (i < OriginalSize) {
        if (bits == 0) {
            CurLongword = *Source;
            Source++;
            bits = 32;
        }
        if (CurLongword & 0x80000000UL) {
            if (*p >= 0) {
                *Dest = (UBYTE)((unsigned char)*p);
                Dest++;
                i++;
                p = Tree;
            } else {
                p += (*p / 2);
            }
        } else {
            p--;
            if (*p > 0 && (*p & 0x100)) {
                *Dest = (UBYTE)((unsigned char)*p);
                Dest++;
                i++;
                p = Tree;
            }
        }
        CurLongword <<= 1;
        bits--;
    }
}

/*
** IFFMultipalette_SourceChunkId - Internal: IFF chunk ID of multipalette source for decode
** (ID_SHAM, ID_PCHG, or ID_CTBL), or 0. SHAM wins over PCHG when both were in file.
*/
ULONG IFFMultipalette_SourceChunkId(const struct IFFPicture *picture)
{
    if (!picture) {
        return 0UL;
    }
    if (picture->mpalSham) {
        return ID_SHAM;
    }
    if (picture->mpalPchgPayload != NULL && picture->mpalPchgPayloadSize >= 4U) {
        return ID_PCHG;
    }
    if (picture->mpalPchg) {
        return ID_PCHG;
    }
    if (picture->mpalCtbl && picture->mpalCtblData && picture->mpalCtblSize >= 2) {
        return ID_CTBL;
    }
    return 0UL;
}

BOOL IFFMultipalette_Active(const struct IFFPicture *picture)
{
    if (IFFMultipalette_SourceChunkId(picture) != 0UL) {
        return TRUE;
    }
    return FALSE;
}

/*
** ReadILBMMultipalette - Load PCHG, SHAM, CTBL from stored properties (ILBM/PBM/ACBM).
** SHAM takes precedence over PCHG when both exist (matches common Amiga loader order).
*/
LONG ReadILBMMultipalette(struct IFFPicture *picture)
{
    struct StoredProperty *sp;
    UBYTE *raw;
    ULONG rawSize;
    UBYTE *decomp;
    ULONG compInfo;
    ULONG origSize;
    ULONG hdrComp;
    WORD *treePtr;
    const ULONG *srcUL;

    picture->mpalSham = FALSE;
    picture->mpalPchg = FALSE;
    picture->mpalCtbl = FALSE;
    picture->mpalShamAlloc = NULL;
    picture->mpalShamAllocSize = 0;
    picture->mpalPchgAlloc = NULL;
    picture->mpalPchgAllocSize = 0;
    picture->mpalPchgPayload = NULL;
    picture->mpalPchgPayloadSize = 0;
    picture->mpalCtblData = NULL;
    picture->mpalCtblSize = 0;

    if (!picture || !picture->iff) {
        return RETURN_OK;
    }

    sp = FindProp(picture->iff, picture->formtype, ID_CTBL);
    if (sp && sp->sp_Size >= 2 && (sp->sp_Size % 2) == 0) {
        picture->mpalCtblData = (UBYTE *)AllocMem(sp->sp_Size, MEMF_PUBLIC | MEMF_CLEAR);
        if (!picture->mpalCtblData) {
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "CTBL allocation failed");
            return RETURN_FAIL;
        }
        CopyMem(sp->sp_Data, picture->mpalCtblData, sp->sp_Size);
        picture->mpalCtblSize = sp->sp_Size;
        picture->mpalCtbl = TRUE;
    }

    sp = FindProp(picture->iff, picture->formtype, ID_PCHG);
    if (sp && sp->sp_Size >= 20) {
        rawSize = sp->sp_Size;
        raw = (UBYTE *)AllocMem(rawSize, MEMF_PUBLIC | MEMF_CLEAR);
        if (!raw) {
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "PCHG allocation failed");
            goto mp_fail;
        }
        CopyMem(sp->sp_Data, raw, rawSize);

        picture->mpalPchgCompression = mp_rdbe16(raw);
        picture->mpalPchgFlags = mp_rdbe16(raw + 2);
        picture->mpalPchgStartLine = (WORD)mp_rdbe16(raw + 4);
        picture->mpalPchgLineCount = mp_rdbe16(raw + 6);
        picture->mpalPchgChangedLines = mp_rdbe16(raw + 8);
        picture->mpalPchgMinReg = mp_rdbe16(raw + 10);
        picture->mpalPchgMaxReg = mp_rdbe16(raw + 12);
        picture->mpalPchgMaxChanges = mp_rdbe16(raw + 14);
        picture->mpalPchgTotalChanges = mp_rdbe32(raw + 16);

        hdrComp = (ULONG)picture->mpalPchgCompression;
        if (hdrComp == PCHG_COMP_NONE) {
            if (rawSize < 21) {
                FreeMem(raw, rawSize);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "PCHG chunk too small");
                goto mp_fail;
            }
            picture->mpalPchgAlloc = raw;
            picture->mpalPchgAllocSize = rawSize;
            picture->mpalPchgPayload = raw + 20;
            picture->mpalPchgPayloadSize = rawSize - 20;
            picture->mpalPchg = TRUE;
        } else if (hdrComp == PCHG_COMP_HUFFMANN) {
            if (rawSize < 28) {
                FreeMem(raw, rawSize);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "PCHG compressed chunk too small");
                goto mp_fail;
            }
            compInfo = mp_rdbe32(raw + 20);
            origSize = mp_rdbe32(raw + 24);
            if (compInfo < 2 || origSize == 0 || rawSize < 28 + compInfo) {
                FreeMem(raw, rawSize);
                SetIFFPictureError(picture, IFFPICTURE_BADFILE, "PCHG compressed header invalid");
                goto mp_fail;
            }
            decomp = (UBYTE *)AllocMem(origSize, MEMF_PUBLIC | MEMF_CLEAR);
            if (!decomp) {
                FreeMem(raw, rawSize);
                SetIFFPictureError(picture, IFFPICTURE_NOMEM, "PCHG decompress buffer failed");
                goto mp_fail;
            }
            treePtr = (WORD *)(void *)(raw + 28 + compInfo - 2);
            srcUL = (const ULONG *)(void *)(raw + 28 + compInfo);
            PCHG_Decompress(srcUL, decomp, treePtr, origSize);
            FreeMem(raw, rawSize);
            picture->mpalPchgAlloc = decomp;
            picture->mpalPchgAllocSize = origSize;
            picture->mpalPchgPayload = decomp;
            picture->mpalPchgPayloadSize = origSize;
            picture->mpalPchg = TRUE;
        } else {
            FreeMem(raw, rawSize);
            SetIFFPictureError(picture, IFFPICTURE_UNSUPPORTED, "PCHG compression not supported");
            goto mp_fail;
        }
    }

    sp = FindProp(picture->iff, picture->formtype, ID_SHAM);
    if (sp && sp->sp_Size >= 34) {
        if (mp_rdbe16((UBYTE *)sp->sp_Data) != 0) {
            SetIFFPictureError(picture, IFFPICTURE_BADFILE, "SHAM first word must be 0");
            goto mp_fail;
        }
        picture->mpalShamAlloc = (UBYTE *)AllocMem(sp->sp_Size, MEMF_PUBLIC | MEMF_CLEAR);
        if (!picture->mpalShamAlloc) {
            SetIFFPictureError(picture, IFFPICTURE_NOMEM, "SHAM allocation failed");
            goto mp_fail;
        }
        CopyMem(sp->sp_Data, picture->mpalShamAlloc, sp->sp_Size);
        picture->mpalShamAllocSize = sp->sp_Size;
        picture->mpalSham = TRUE;
    }

    if (picture->mpalSham) {
        picture->mpalPchg = FALSE;
    }

    return RETURN_OK;

mp_fail:
    if (picture->mpalShamAlloc) {
        FreeMem(picture->mpalShamAlloc, picture->mpalShamAllocSize);
        picture->mpalShamAlloc = NULL;
        picture->mpalShamAllocSize = 0;
    }
    picture->mpalSham = FALSE;
    if (picture->mpalPchgAlloc) {
        FreeMem(picture->mpalPchgAlloc, picture->mpalPchgAllocSize);
        picture->mpalPchgAlloc = NULL;
        picture->mpalPchgAllocSize = 0;
        picture->mpalPchgPayload = NULL;
        picture->mpalPchgPayloadSize = 0;
    }
    picture->mpalPchg = FALSE;
    if (picture->mpalCtblData) {
        FreeMem(picture->mpalCtblData, picture->mpalCtblSize);
        picture->mpalCtblData = NULL;
        picture->mpalCtblSize = 0;
    }
    picture->mpalCtbl = FALSE;
    return RETURN_FAIL;
}

static void mp_expand_cmap_to_8bit(UBYTE *pal768, ULONG numcolors)
{
    ULONG i;
    UBYTE r;
    UBYTE g;
    UBYTE b;

    for (i = 0; i < numcolors; i++) {
        r = pal768[i * 3];
        g = pal768[i * 3 + 1];
        b = pal768[i * 3 + 2];
        pal768[i * 3]     = (UBYTE)(r | (r >> 4));
        pal768[i * 3 + 1] = (UBYTE)(g | (g >> 4));
        pal768[i * 3 + 2] = (UBYTE)(b | (b >> 4));
    }
}

static void mp_apply_ctbl(struct IFFPicture *picture, UBYTE *pal768)
{
    ULONG nent;
    ULONG i;
    UWORD w;
    UBYTE r;
    UBYTE g;
    UBYTE b;
    const UBYTE *d;

    if (!picture->mpalCtbl || !picture->mpalCtblData) {
        return;
    }
    nent = picture->mpalCtblSize / 2;
    d = picture->mpalCtblData;
    for (i = 0; i < nent && i < 256; i++) {
        w = mp_rdbe16(d + i * 2);
        r = (UBYTE)(((w >> 8) & 0x0F) * 17);
        g = (UBYTE)(((w >> 4) & 0x0F) * 17);
        b = (UBYTE)((w & 0x0F) * 17);
        pal768[i * 3]     = r;
        pal768[i * 3 + 1] = g;
        pal768[i * 3 + 2] = b;
    }
}

static void mp_sham_line(struct IFFMultipaletteState *st, LONG line, UBYTE *pal768)
{
    ULONG i;
    ULONG reg;
    UBYTE t;
    const UBYTE *data;

    if (st->ms_mode != IFF_MPAL_MODE_SHAM) {
        return;
    }
    if (st->ms_lace) {
        if ((line & 1) != 0) {
            return;
        }
    }
    if (st->ms_shamRemain < 32) {
        return;
    }
    data = st->ms_sham;
    for (i = 0; i < 16; i++) {
        reg = i;
        t = (UBYTE)(data[0] & 0x0F);
        pal768[reg * 3]     = (UBYTE)(t * 17);
        t = (UBYTE)(data[1] >> 4);
        pal768[reg * 3 + 1] = (UBYTE)(t * 17);
        t = (UBYTE)(data[1] & 0x0F);
        pal768[reg * 3 + 2] = (UBYTE)(t * 17);
        data += 2;
        st->ms_shamRemain -= 2;
    }
    st->ms_sham = data;
}

static void mp_pchg_start(struct IFFMultipaletteState *st, UBYTE *pal768)
{
    SHORT row;
    SHORT changedLines;
    SHORT changes;
    SHORT changes2;
    SHORT i;
    ULONG bits;
    UWORD masklen;
    ULONG dataremaining;
    ULONG totalchanges;
    ULONG reg;
    ULONG off;
    UBYTE t;
    UBYTE *mask;
    UBYTE *data;
    UBYTE thismask;
    UWORD lineCount;
    WORD startLine;
    WORD origStartLine;
    UWORD uw;
    UWORD colrgb;

    mask = (UBYTE *)st->ms_maskPtr;
    dataremaining = st->ms_dataRemain;
    changedLines = (SHORT)st->ms_changedLines;
    lineCount = st->ms_lineCount;
    startLine = st->ms_startLine;
    origStartLine = startLine;
    masklen = mp_mask_bytes(lineCount);
    totalchanges = 0;

    if ((ULONG)masklen > dataremaining) {
        return;
    }
    data = mask + masklen;
    dataremaining -= masklen;

    for (row = startLine, thismask = 0, bits = 0; changedLines != 0 && row < 0; row++) {
        if (bits == 0) {
            thismask = *mask;
            mask++;
            bits = 8;
        }
        if ((thismask & 0x80U) != 0) {
            if (dataremaining < 2) {
                break;
            }
            if ((st->ms_flags & IFF_PCHGF_32BIT) != 0) {
                changes = (SHORT)(((UWORD)data[0] << 8) | (UWORD)data[1]);
                data += 2;
                dataremaining -= 2;
                for (i = 0; i < changes; i++) {
                    if ((ULONG)totalchanges >= st->ms_totalChanges) {
                        break;
                    }
                    if (dataremaining < 6) {
                        break;
                    }
                    reg = ((ULONG)data[0] << 8) | (ULONG)data[1];
                    data += 2;
                    dataremaining -= 2;
                    data++;
                    dataremaining--;
                    pal768[reg * 3]     = *data;
                    data++;
                    pal768[reg * 3 + 2] = *data;
                    data++;
                    pal768[reg * 3 + 1] = *data;
                    data++;
                    dataremaining -= 3;
                    totalchanges++;
                }
                changedLines--;
            } else if ((st->ms_flags & IFF_PCHGF_12BIT) != 0) {
                /* UWORD-packed lines use same header counts as small nibbles in ILBMdt path */
                changes = (SHORT)data[0];
                changes2 = (SHORT)data[1];
                data += 2;
                dataremaining -= 2;
                off = 0;
                do {
                    for (i = 0; i < changes; i++) {
                        if ((ULONG)totalchanges >= st->ms_totalChanges) {
                            break;
                        }
                        if (dataremaining < 2) {
                            break;
                        }
                        uw = mp_rdbe16(data);
                        data += 2;
                        dataremaining -= 2;
                        reg = (uw >> 12) & 0x0F;
                        reg += off;
                        colrgb = (UWORD)(uw & 0x0FFF);
                        pal768[reg * 3]     = (UBYTE)(((colrgb >> 8) & 0x0F) * 17);
                        pal768[reg * 3 + 1] = (UBYTE)(((colrgb >> 4) & 0x0F) * 17);
                        pal768[reg * 3 + 2] = (UBYTE)((colrgb & 0x0F) * 17);
                        totalchanges++;
                    }
                    changes = changes2;
                    off = 16;
                    changes2 = 0;
                } while (changes > 0);
                changedLines--;
            } else {
                /* Small-nibble line header (same 2-byte preamble as 12-bit path). */
                changes = (SHORT)data[0];
                changes2 = (SHORT)data[1];
                data += 2;
                dataremaining -= 2;
                off = 0;
                do {
                    for (i = 0; i < changes; i++) {
                        if ((ULONG)totalchanges >= st->ms_totalChanges) {
                            break;
                        }
                        if (dataremaining < 2) {
                            break;
                        }
                        reg = (ULONG)((data[0] >> 4) + off);
                        t = (UBYTE)(data[0] & 0x0F);
                        pal768[reg * 3]     = (UBYTE)(t * 17);
                        t = (UBYTE)(data[1] >> 4);
                        pal768[reg * 3 + 1] = (UBYTE)(t * 17);
                        t = (UBYTE)(data[1] & 0x0F);
                        pal768[reg * 3 + 2] = (UBYTE)(t * 17);
                        data += 2;
                        dataremaining -= 2;
                        totalchanges++;
                    }
                    changes = changes2;
                    off = 16;
                    changes2 = 0;
                } while (changes > 0);
                changedLines--;
            }
        }
        thismask = (UBYTE)(thismask << 1);
        bits--;
        if (st->ms_maskRowsLeft > 0) {
            st->ms_maskRowsLeft--;
        }
    }

    if (origStartLine < 0) {
        st->ms_startLine = 0;
    }
    st->ms_bits = (UBYTE)bits;
    st->ms_thismask = thismask;
    st->ms_maskPtr = mask;
    st->ms_totalChanges -= totalchanges;
    st->ms_changedLines = (UWORD)changedLines;
    st->ms_data = data;
    st->ms_dataRemain = dataremaining;
}

static void mp_pchg_line(struct IFFMultipaletteState *st, LONG line, UBYTE *pal768)
{
    SHORT changes;
    SHORT changes2;
    SHORT i;
    ULONG bits;
    ULONG dataremaining;
    ULONG totalchanges;
    ULONG reg;
    ULONG off;
    UBYTE t;
    UBYTE *mask;
    UBYTE *data;
    UBYTE thismask;
    UWORD uw;
    UWORD colrgb;

    (void)line;

    if (st->ms_mode != IFF_MPAL_MODE_PCHG) {
        return;
    }

    data = st->ms_data;
    mask = (UBYTE *)st->ms_maskPtr;
    dataremaining = st->ms_dataRemain;
    totalchanges = 0;
    bits = st->ms_bits;
    thismask = st->ms_thismask;

    if (st->ms_startLine > 0) {
        st->ms_startLine--;
        return;
    }
    /* One PCHG mask bit per image row after StartLine, for exactly LineCount rows.
     * trailing zero bits must still be shifted after ChangedLines reaches zero. */
    if (st->ms_maskRowsLeft == 0) {
        return;
    }

    if (bits == 0) {
        thismask = *mask;
        mask++;
        bits = 8;
    }
    if ((thismask & 0x80U) != 0) {
        if (dataremaining > 2 && (SHORT)st->ms_changedLines > 0 && st->ms_totalChanges > 0) {
            st->ms_changedLines--;
            if ((st->ms_flags & IFF_PCHGF_32BIT) != 0) {
                changes = (SHORT)(((UWORD)data[0] << 8) | (UWORD)data[1]);
                data += 2;
                dataremaining -= 2;
                for (i = 0; i < changes; i++) {
                    if (totalchanges >= st->ms_totalChanges) {
                        break;
                    }
                    if (dataremaining < 6) {
                        break;
                    }
                    reg = ((ULONG)data[0] << 8) | (ULONG)data[1];
                    data += 2;
                    dataremaining -= 2;
                    data++;
                    dataremaining--;
                    pal768[reg * 3]     = *data;
                    data++;
                    pal768[reg * 3 + 2] = *data;
                    data++;
                    pal768[reg * 3 + 1] = *data;
                    data++;
                    dataremaining -= 3;
                    totalchanges++;
                }
            } else if ((st->ms_flags & IFF_PCHGF_12BIT) != 0) {
                changes = (SHORT)data[0];
                changes2 = (SHORT)data[1];
                data += 2;
                dataremaining -= 2;
                off = 0;
                do {
                    for (i = 0; i < changes; i++) {
                        if (totalchanges >= st->ms_totalChanges) {
                            break;
                        }
                        if (dataremaining < 2) {
                            break;
                        }
                        uw = mp_rdbe16(data);
                        data += 2;
                        dataremaining -= 2;
                        reg = (uw >> 12) & 0x0F;
                        reg += off;
                        colrgb = (UWORD)(uw & 0x0FFF);
                        pal768[reg * 3]     = (UBYTE)(((colrgb >> 8) & 0x0F) * 17);
                        pal768[reg * 3 + 1] = (UBYTE)(((colrgb >> 4) & 0x0F) * 17);
                        pal768[reg * 3 + 2] = (UBYTE)((colrgb & 0x0F) * 17);
                        totalchanges++;
                    }
                    changes = changes2;
                    off = 16;
                    changes2 = 0;
                } while (changes > 0);
            } else {
                changes = (SHORT)data[0];
                changes2 = (SHORT)data[1];
                data += 2;
                dataremaining -= 2;
                off = 0;
                do {
                    for (i = 0; i < changes; i++) {
                        if (totalchanges >= st->ms_totalChanges) {
                            break;
                        }
                        if (dataremaining < 2) {
                            break;
                        }
                        reg = (ULONG)((data[0] >> 4) + off);
                        t = (UBYTE)(data[0] & 0x0F);
                        pal768[reg * 3]     = (UBYTE)(t * 17);
                        t = (UBYTE)(data[1] >> 4);
                        pal768[reg * 3 + 1] = (UBYTE)(t * 17);
                        t = (UBYTE)(data[1] & 0x0F);
                        pal768[reg * 3 + 2] = (UBYTE)(t * 17);
                        data += 2;
                        dataremaining -= 2;
                        totalchanges++;
                    }
                    changes = changes2;
                    off = 16;
                    changes2 = 0;
                } while (changes > 0);
            }
        }
    }
    thismask = (UBYTE)(thismask << 1);
    bits--;

    if (st->ms_maskRowsLeft > 0) {
        st->ms_maskRowsLeft--;
    }

    st->ms_bits = (UBYTE)bits;
    st->ms_thismask = thismask;
    st->ms_maskPtr = mask;
    st->ms_totalChanges -= totalchanges;
    st->ms_data = data;
    st->ms_dataRemain = dataremaining;
}

LONG IFFMultipalette_Init(struct IFFPicture *picture, UBYTE *pal768,
    struct IFFMultipaletteState *st)
{
    ULONG i;
    ULONG nc;
    UBYTE *dst;

    if (!picture || !pal768 || !st) {
        return RETURN_FAIL;
    }

    for (i = 0; i < 768; i++) {
        pal768[i] = 0;
    }

    if (picture->cmap && picture->cmap->data && picture->cmap->numcolors > 0) {
        nc = picture->cmap->numcolors;
        if (nc > 256) {
            nc = 256;
        }
        dst = pal768;
        CopyMem(picture->cmap->data, dst, nc * 3);
        if (picture->cmap->is4Bit) {
            mp_expand_cmap_to_8bit(pal768, nc);
        }
    }

    mp_apply_ctbl(picture, pal768);

    st->ms_mode = IFF_MPAL_MODE_NONE;
    st->ms_lace = FALSE;
    st->ms_sham = NULL;
    st->ms_shamRemain = 0;
    st->ms_flags = 0;
    st->ms_startLine = 0;
    st->ms_lineCount = 0;
    st->ms_maskRowsLeft = 0;
    st->ms_changedLines = 0;
    st->ms_totalChanges = 0;
    st->ms_data = NULL;
    st->ms_dataRemain = 0;
    st->ms_bits = 0;
    st->ms_thismask = 0;
    st->ms_maskPtr = NULL;

    if ((picture->viewportmodes & vmLACE) != 0) {
        st->ms_lace = TRUE;
    }

    if (picture->mpalSham && picture->mpalShamAlloc && picture->mpalShamAllocSize >= 34) {
        st->ms_mode = IFF_MPAL_MODE_SHAM;
        st->ms_sham = picture->mpalShamAlloc + 2;
        st->ms_shamRemain = picture->mpalShamAllocSize - 2;
        return RETURN_OK;
    }

    if (picture->mpalPchgPayload != NULL && picture->mpalPchgPayloadSize > 0U) {
        st->ms_mode = IFF_MPAL_MODE_PCHG;
        st->ms_flags = picture->mpalPchgFlags;
        st->ms_startLine = picture->mpalPchgStartLine;
        st->ms_lineCount = picture->mpalPchgLineCount;
        st->ms_maskRowsLeft = picture->mpalPchgLineCount;
        st->ms_changedLines = picture->mpalPchgChangedLines;
        st->ms_totalChanges = picture->mpalPchgTotalChanges;
        st->ms_maskPtr = picture->mpalPchgPayload;
        st->ms_dataRemain = picture->mpalPchgPayloadSize;
        st->ms_data = picture->mpalPchgPayload;
        st->ms_bits = 0;
        st->ms_thismask = 0;
        mp_pchg_start(st, pal768);
        return RETURN_OK;
    }

    return RETURN_OK;
}

LONG IFFMultipalette_ApplyScanline(struct IFFMultipaletteState *st, UWORD row,
    UBYTE *pal768)
{
    if (!st || !pal768) {
        return RETURN_FAIL;
    }
    if (st->ms_mode == IFF_MPAL_MODE_SHAM) {
        mp_sham_line(st, (LONG)row, pal768);
        return RETURN_OK;
    }
    if (st->ms_mode == IFF_MPAL_MODE_PCHG) {
        mp_pchg_line(st, (LONG)row, pal768);
        return RETURN_OK;
    }
    return RETURN_OK;
}
