#include "../inc/lab_png.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../inc/crc.h"

int is_png(U8 *buf, size_t n)
{ // check if PNG signature is present
    // takes in pointer to least 8 bytes of binary data
    if (n < PNG_SIG_SIZE)
        return 0; // less than 8 bytes

    static const U8 png_signature[PNG_SIG_SIZE] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return (memcmp(buf, png_signature, PNG_SIG_SIZE) == 0); // returns 1 when true, 0 otherwise
}

int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence)
{

    // parameter input checking
    if (out == NULL || fp == NULL)
    {
        return 1;
    };

    // buffer for the IHDR data
    U8 data[13];

    // use fseek to set the file position indicator for the stream pointed to by fp
    // the new position is obtained by adding offset bytes to the position specified
    //  by whence
    int seek_status = fseek(fp, offset, whence);
    // the file pointer is now at the data field, the rest of the code assumes that
    // to get to that offset == 16 and whence == SEEK_SET

    if (seek_status != 0)
    {
        return 2;
    }

    // read the data onto the data buffer
    int read_status_data = fread(data, 1, DATA_IHDR_SIZE, fp);
    if (read_status_data != 13)
    {
        return 3;
    }

    // populate the output struct -> use bitwise manipulators to convert from big endian to little endian
    out->width = (U32)data[0] << 24 | (U32)data[1] << 16 | (U32)data[2] << 8 | (U32)data[3];
    out->height = (U32)data[4] << 24 | (U32)data[5] << 16 | (U32)data[6] << 8 | (U32)data[7];
    out->bit_depth = data[8];
    out->color_type = data[9];
    out->compression = data[10];
    out->filter = data[11];
    out->interlace = data[12];

    return 0;
}

// read out image height from a struct data_IHDR
int get_png_height(struct data_IHDR *buf)
{
    if (buf != NULL)
    {
        return (int)buf->height;
    }
    return -1;
}

// read out image width from a struct data_IHDR
int get_png_width(struct data_IHDR *buf)
{
    if (buf != NULL)
    {
        return (int)buf->width;
    }
    return -1;
}

// helper function created for easy access to get a certain chunk
chunk_p get_chunk(FILE *fp)
{
    // param check
    if (fp == NULL)
    {
        return NULL;
    }

    U8 length[CHUNK_LEN_SIZE], type[CHUNK_TYPE_SIZE], crc[CHUNK_CRC_SIZE]; // stack allocated buffers to store the data for each field

    // reading the length of the chunk onto the buffer
    int status_read_length = fread(length, 1, CHUNK_LEN_SIZE, fp);
    if (status_read_length != CHUNK_LEN_SIZE)
    {
        return NULL;
    }

    // big endian to little endian conversion for length
    U32 len = (U32)length[0] << 24 | (U32)length[1] << 16 | (U32)length[2] << 8 | (U32)length[3];

    // reading the type of the chunk onto the buffer
    int status_read_type = fread(type, 1, CHUNK_TYPE_SIZE, fp);
    if (status_read_type != CHUNK_TYPE_SIZE)
    {
        return NULL;
    }

    // dynamically allocating the chunk's data onto the heap buffer
    // because otherwise we would have a dangling pointer when we tried to return chunk_p
    // this is because p_data in chunk_p is a pointer to the data
    // to account for variable size we use the heap
    U8 *data = (U8 *)malloc(len);
    if (data == NULL)
    {
        return NULL;
    }

    // read the data onto the heap buffer
    int status_read_data = fread(data, 1, len, fp);
    if (status_read_data != len)
    {
        free(data);
        return NULL;
    }

    // read the crc of the chunk onto the buffer
    int status_read_crc = fread(crc, 1, CHUNK_CRC_SIZE, fp);
    if (status_read_crc != CHUNK_CRC_SIZE)
    {
        free(data);
        return NULL;
    }

    // convert from big endian to little endian
    U32 crc_n = (U32)crc[0] << 24 | (U32)crc[1] << 16 | (U32)crc[2] << 8 | (U32)crc[3];

    // malloc the new chunk that we are going to return for this function
    chunk_p new_chunk = (chunk_p)malloc(sizeof(struct chunk)); // needs to be the size of the struct -> but returns a chunk_p pointer
    if (new_chunk == NULL)
    {
        free(data);
        return NULL;
    }

    // populate the newly allocated chunk (new_chunk)
    new_chunk->length = len;
    memcpy(new_chunk->type, type, CHUNK_TYPE_SIZE);
    new_chunk->p_data = data;
    new_chunk->crc = crc_n;

    return new_chunk; // return the newly populated chunk
}

// read out expected crc from a struct chunk
U32 get_chunk_crc(chunk_p in)
{
    if (in == NULL)
    {
        return 0;
    }

    return (in->crc);
}

// calculate crc using chunk type and chunk data

U32 calculate_chunk_crc(chunk_p in)
{
    // param check
    if (in == NULL)
    {
        return 0;
    }

    size_t total_bytes = CHUNK_TYPE_SIZE + (size_t)in->length; // find the total bytes of TYPE(4) + DATA(Length)
    U8 *buf = (U8 *)malloc(total_bytes);                 // dynamically allocated buffer

    if (buf == NULL)
    {
        return 0;
    }

    // now copy over the TYPE into the buffer
    // and then copy over the DATA into the buffer after leaving the space for the TYPE to get copied over
    memcpy(buf, in->type, CHUNK_TYPE_SIZE);
    memcpy(buf + CHUNK_TYPE_SIZE, in->p_data, in->length);

    // pass in the completed buffer to the crc function created in crc.h
    U32 crc_value = (U32)crc(buf, total_bytes);

    // sucessful implementation
    free(buf);
    return crc_value;
}

// allocate memory for a struct simple_PNG
simple_PNG_p mallocPNG()
{
    simple_PNG_p PNG_ptr = (simple_PNG_p)malloc(sizeof(struct simple_PNG));
    return PNG_ptr;
}

// free the memory of a struct chunk and inner data buffers
void free_chunk(chunk_p in)
{
    if (in == NULL)
    {
        return;
    }
    if (in->p_data != NULL)
    {
        free(in->p_data);
    }
    free(in);
    return;
}

// free the memory of a struct simple_PNG
void free_png(simple_PNG_p in)
{
    if (in == NULL)
    {
        return;
    }

    free_chunk(in->p_IHDR);
    free_chunk(in->p_IDAT);
    free_chunk(in->p_IEND);

    free(in);

    return;
}

// extract from file all chunks in a png, to populate a struct simple_PNG
// takes in file pointer and how to reach the IHDR chunk (see fseek parameters)
// offset must be PNG_SIG_SIZE and whence must be SEEK_SET
int get_png_chunks(simple_PNG_p out, FILE *fp, long offset, int whence)
{
    // param check
    if (out == NULL || fp == NULL)
    {
        return 1;
    }
    // makes sure that we are at the start of the IHDR chunk (after skipping the signature)
    // offset must be PNG_SIG_SIZE and whence must be SEEK_SET
    int status_seek = fseek(fp, offset, whence);
    if (status_seek != 0)
    {
        return 2;
    }

    // initialise new chunks by calling get_chunk helper function
    // the file pointer moves according to the read calls made by the get_chunk function
    chunk_p ihdr = get_chunk(fp);
    chunk_p idat = get_chunk(fp); // use a loop in case there are more files in between ihdr and iend
    chunk_p iend = get_chunk(fp);

    // param check
    if (ihdr == NULL || idat == NULL || iend == NULL)
    {
        free_chunk(ihdr);
        free_chunk(idat);
        free_chunk(iend);

        return 3;
    }

    // populate the output struct with the chunk structs
    out->p_IHDR = ihdr;
    out->p_IDAT = idat;
    out->p_IEND = iend;

    return 0;
}

void check_crc(const char *name, chunk_p c)
{
    if (get_chunk_crc(c) != calculate_chunk_crc(c))
    {
        printf("%s chunk CRC error: computed %08x, expected %08x\n",
               name,
               calculate_chunk_crc(c),
               get_chunk_crc(c));
    }
}

// write a struct chunk to file
int write_chunk(FILE *fp, chunk_p in)
{
    if (fp == NULL || in == NULL)
    {
        return 1;
    }

    // writing the length (need to change from little endian to big endian)
    // also change from U32 to U8
    U8 length[4] = {
        (U8)((in->length >> 24) & 0xFF),
        (U8)((in->length >> 16) & 0xFF),
        (U8)((in->length >> 8) & 0xFF),
        (U8)((in->length) & 0xFF)};

    // writing the length
    int write_status_len = fwrite(length, 1, CHUNK_LEN_SIZE, fp);
    if (write_status_len != CHUNK_LEN_SIZE)
    {
        return 2;
    }

    // writing the type
    int write_status_type = fwrite(in->type, 1, CHUNK_TYPE_SIZE, fp);
    if (write_status_type != CHUNK_TYPE_SIZE)
    {
        return 3;
    }

    // writing the data
    int write_status_data = fwrite(in->p_data, 1, in->length, fp);
    if (write_status_data != in->length)
    {
        return 4;
    }

    // writing the crc (need to change from little endian to big endian)
    // also change from U32 to U8
    U8 crc[4] = {
        (U8)((in->crc >> 24) & 0xFF),
        (U8)((in->crc >> 16) & 0xFF),
        (U8)((in->crc >> 8) & 0xFF),
        (U8)((in->crc) & 0xFF)};

    // writing the crc
    int write_status_crc = fwrite(crc, 1, CHUNK_CRC_SIZE, fp);
    if (write_status_crc != CHUNK_CRC_SIZE)
    {
        return 5;
    }

    return 0; // writing sucessful
}

// write a struct simple_PNG to file
int write_PNG(char *filepath, simple_PNG_p in)
{
    if (filepath == NULL || in == NULL)
    {
        return 1;
    }

    FILE *fp = fopen(filepath, "w");
    if (fp == NULL)
    {
        return 2;
    }

    // writing the PNG signature to a file
    static const U8 png_signature[PNG_SIG_SIZE] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    int status_write = fwrite(png_signature, 1, PNG_SIG_SIZE, fp);
    if (status_write != PNG_SIG_SIZE)
    {
        return 3;
    }

    // writing IHDR chunk
    int status_idhr = write_chunk(fp, in->p_IHDR);
    if (status_idhr != 0)
    {
        fclose(fp);
        return 5;
    }

    // writing the IDAT chunk
    int status_idat = write_chunk(fp, in->p_IDAT);
    if (status_idat != 0)
    {
        fclose(fp);
        return 6;
    }

    // writing the IEND chunk
    int status_iend = write_chunk(fp, in->p_IEND);
    if (status_iend != 0)
    {
        fclose(fp);
        return 7;
    }

    // sucessful implementation
    fclose(fp);
    return 0;
}