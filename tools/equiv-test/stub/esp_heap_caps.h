#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
static inline void*heap_caps_calloc(size_t a,size_t b,int c){(void)c;return calloc(a,b);}
static inline void*heap_caps_malloc(size_t a,int c){(void)c;return malloc(a);}
static inline void heap_caps_free(void*p){free(p);}
