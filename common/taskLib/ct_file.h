#ifndef CT_FILE_H
#define CT_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <assert.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C"
{
#endif

//wrapper to read from a ct_file handle. Abstracts the details of compressesion
size_t ct_read(void * ptr, size_t size, FILE* handle);

//wrapper to write to a ct_file handle. Abstracts the details of compression
size_t ct_write(const void * ptr, size_t size, FILE* handle);

#if defined(__cplusplus)
}
#endif


#endif
