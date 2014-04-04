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

//Class to abstract file handles, compressed or uncompressed. 
typedef struct _ct_file
{
    //A handle for a compressed file
    gzFile compressedHandle;
    //and a handle for an uncompressed file
    FILE* uncompressedHandle;
} ct_file;
    
//default constructor, returns a pointer to a ct_file with a clean slate
ct_file* create_ct_file_blank();

//creates a compressed or uncompressed (based on the compressed boolean) filehandle for writing
ct_file* create_ct_file_w(const char* fileName,bool compressed);
//creates a compressed or uncompressed (based on the first two sizeof(int) bytes of the file) filehandle for reading
ct_file* create_ct_file_r(const char* fileName);

//creates an uncompressed wrapper handle from an existing FILE*
ct_file* create_ct_file_from_handle(FILE* existingHandle);

//returns the compressed file handle
gzFile getCompressedHandle(ct_file* handle);

//returns the uncompressedHandle();
FILE* getUncompressedHandle(ct_file* handle);

//returns true if the handle is closed or has never been opened
bool isClosed(ct_file* handle);

//returns true if the Task_file is compressed;
bool isCompressed(ct_file* handle);

//close the handles if open
void close_ct_file(ct_file* handle);

//wrapper to read from a ct_file handle. Abstracts the details of compressesion
size_t ct_read(void * ptr, size_t size, ct_file* handle);

//wrapper to write to a ct_file handle. Abstracts the details of compression
size_t ct_write(void * ptr, size_t size, ct_file* handle);

//returns the value of feof or gzeof depending on whether the handle is compressed or not.
int ct_eof(ct_file* handle);

//fast forward the handle to the specified offset (absolute, from start of file) in raw data stream bytes
int ct_seek( ct_file* handle, unsigned long long offset);

// get the current position in the file
long ct_tell( ct_file* handle);

//rewinds a file to the beginning
void ct_rewind(ct_file* handle);

//Flush out the file handle. Any buffers are written out. Not recommended to use too often as the gzip variety
//can slow this down. This is meant more for finalization.
int ct_flush(ct_file* handle);

#if defined(__cplusplus)
}
#endif


#endif
