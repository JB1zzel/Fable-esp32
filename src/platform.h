/* platform.h - which platform are we building for, and platform helpers.
 *
 *   ESP32 (PlatformIO): FABLE_ESP32 is defined by platformio.ini.
 *   Windows:            FABLE_WINDOWS is defined here when the compiler
 *                       defines _WIN32.
 *   Linux and other Unix: FABLE_POSIX is defined here when the compiler
 *                       defines __unix__ or __APPLE__ (tested on Linux).
 *   No -D flag is needed for the last two (passing one still works).
 *   FABLE_DESKTOP is defined for Windows and POSIX: "a PC, not the ESP32".
 *
 * Every source file in src/ is built on both platforms; the files that only
 * make sense on one of them are wrapped in #ifdef and compile to nothing on
 * the other.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdlib.h>

#if !defined(FABLE_ESP32) && !defined(FABLE_WINDOWS) && !defined(FABLE_POSIX)
#  if defined(_WIN32)
#    define FABLE_WINDOWS 1
#  elif defined(__unix__) || defined(__APPLE__)
#    define FABLE_POSIX 1
#  endif
#endif

#if !defined(FABLE_ESP32) && !defined(FABLE_WINDOWS) && !defined(FABLE_POSIX)
#error "Unknown platform: define FABLE_ESP32 (ESP32 build), or build on Windows or Linux"
#endif

#if defined(FABLE_WINDOWS) || defined(FABLE_POSIX)
#define FABLE_DESKTOP 1
#endif

#ifdef FABLE_ESP32
#include "esp_heap_caps.h"
#endif

/* Zeroed memory for the big buffers (document text, spell marks, file
 * list). On the ESP32 they go to PSRAM when there is some, because internal
 * RAM is needed by the VGA driver. Release with free() on both platforms. */
static inline void *fable_big_calloc(size_t count, size_t size)
{
    void *p = NULL;
#ifdef FABLE_ESP32
    p = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM);
#endif
    if (!p) p = calloc(count, size);
    return p;
}

#endif
