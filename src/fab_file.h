/* fab_file.h - the file helpers of the platform we are building for. */
#ifndef FAB_FILE_H
#define FAB_FILE_H

#include "platform.h"

#ifdef FABLE_WINDOWS
#include "file_win.h"
#else
#include "file_fabgl.h"
#endif

#endif
