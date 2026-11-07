/* main_win.c - Windows entry point for Fable. */

#include "platform.h"

#ifdef FABLE_WINDOWS

extern int fable_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return fable_main(argc, argv);
}

#else
typedef int main_win_is_windows_only;   /* keeps this file from being empty */
#endif
