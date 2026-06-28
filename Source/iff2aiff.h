/*
** iff2aiff - Convert IFF audio to AIFF format
** Main header with includes and prototypes
**
** All C code must be C89 compliant for SAS/C compiler
*/

#ifndef IFF2AIFF_H
#define IFF2AIFF_H

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/iffparse.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <proto/utility.h>

#include "iffsound.h"
#include "aiff_encoder.h"

int main(int argc, char **argv);

#endif /* IFF2AIFF_H */
