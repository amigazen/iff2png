/*
** iff2png - Convert IFF bitmap images to PNG format
** Main header file with all includes and function prototypes
**
** All C code must be C89/ANSI C compliant for SAS/C compiler
*/

#ifndef IFF2PNG_MAIN_H
#define IFF2PNG_MAIN_H

/* AmigaOS includes */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/iffparse.h>
#include <utility/tagitem.h>

/* AmigaOS proto includes */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <proto/utility.h>

/* libpng header - pnglib directory is in INCLUDEDIR */
#include <png.h>

/* stdio for printf debug output */
#include <stdio.h>

/* IFFPicture library public interface */
#include "iffpicturelib/iffpicture.h"

/* PNG encoder interface */
#include "png_encoder.h"

/* Function prototypes */
int main(int argc, char **argv);

#endif /* IFF2PNG_MAIN_H */

