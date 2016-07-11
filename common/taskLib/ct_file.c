#include "ct_file.h"
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>

size_t ct_read(void * ptr, size_t size, FILE* handle)
{
    size_t read = 0;
    
    do {
        size_t r = fread((char*)ptr + read, 1, size - read, handle);
        read += r * 1; // fread returns the number of items read
        if ((r == 0) && (feof(handle) != 0)) break;
    } while (read < size);
    
    return read;
}


size_t ct_write(const void * ptr, size_t size, FILE* handle)
{
    size_t written = 0;
    
    do {
        size_t w = fwrite((char*)ptr + written, 1, size - written, handle);
        written += w * 1;
    } while (written < size);
    return written;
}
