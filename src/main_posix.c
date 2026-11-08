/* main_posix.c - Linux/POSIX entry point for Fable. */

#include "platform.h"

#ifdef FABLE_POSIX

extern int fable_main(int argc, char **argv);

int main(int argc, char **argv)
{
    return fable_main(argc, argv);
}

#else
typedef int main_posix_is_posix_only;   /* keeps this file from being empty */
#endif
