#include <gc.h>

#define bstr__alloc(size) GC_MALLOC(size)
#define bstr__realloc(ptr, size) GC_REALLOC(ptr, size)
#define bstr__free(ptr)

