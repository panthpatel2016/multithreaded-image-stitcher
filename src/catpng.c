#include <stdio.h>
#include <stdlib.h>
#include "../inc/lab_png.h"
#include "../inc/zutil.h"

#define BUF_LEN (256 * 32)

void catpng (int argc, char *argv[])
{
    // helper variables
    U32 height = 0;
    U32 width = 0;
    U8 *full_inflated_data = NULL;
    U64 total_inflated_size = 0;

    // for loop through the other arguments
    for (int i = 1; i < argc; i++)
    {
        // open all the files to read the data
        FILE *fp = fopen(argv[i], "r");
        if (fp == NULL)
        {
            perror("fopen error\n");
            exit(2);
        }

        // fread from file, the signature of the file onto a buffer
        U8 signature_buffer[PNG_SIG_SIZE];
        int status_read = fread(signature_buffer, 1, PNG_SIG_SIZE, fp);
        if (status_read != PNG_SIG_SIZE)
        {
            perror("signature read fail\n");
            fclose(fp);
            exit(3);
        }

        // check for is_png using signature
        int png_check = is_png(signature_buffer, PNG_SIG_SIZE);
        if (png_check != 1)
        {
            perror("not a valid png\n");
            fclose(fp);
            exit(4);
        }

        // reading the IHDR chunk from the file
        struct data_IHDR IHDR_data;
        if (get_png_data_IHDR(&IHDR_data, fp, 16, SEEK_SET) != 0)
        {
            fclose(fp);
            perror("IHDR data retrieval failed\n");
            exit(5);
        } // offset by 16 to get to the data field of IHDR

        // check widths, the first file width is the reference
        if (i == 1)
        {
            width = IHDR_data.width;
        }
        else if (IHDR_data.width != width)
        {
            fclose(fp);
            perror("Different widths, png is not aligned\n");
            exit(6);
        }
        height += IHDR_data.height; // increment height

        // allocate the cur_png
        simple_PNG_p cur_png = mallocPNG();
        if (cur_png == NULL)
        {
            fclose(fp);
            perror("current png allocate error\n");
            exit(7);
        }

        // store the info of the current png
        if (get_png_chunks(cur_png, fp, PNG_SIG_SIZE, SEEK_SET) != 0)
        {
            fclose(fp);
            free_png(cur_png);
            perror("couldnt retrieve png chunks\n");
            exit(8);
        }

        // initialisations for IDAT
        U8 *deflated_data = cur_png->p_IDAT->p_data;
        U64 deflated_len = (U64)cur_png->p_IDAT->length;
        U64 inflated_len = 0;
        U64 expected_inflated_len = (U64)(IHDR_data.height * (4 * IHDR_data.width + 1));
        U8 *buf_inf = (U8 *)malloc(expected_inflated_len * 2); // ×2 for safety

        // inflate the data
        int result_inf = mem_inf(buf_inf, &inflated_len, deflated_data, deflated_len);
        if (result_inf != 0)
        {
            free(buf_inf);
            fclose(fp);
            free_png(cur_png);
            perror("inflation error\n");
            exit(9);
        }

        // append into global buffer
        U64 cur_position = total_inflated_size;
        total_inflated_size += inflated_len;
        full_inflated_data = (U8 *)realloc(full_inflated_data, total_inflated_size);
        memcpy(full_inflated_data + cur_position, buf_inf, inflated_len);

        free(buf_inf);
        fclose(fp);
        free_png(cur_png);
    }

    U8 *final_deflated_data = (U8 *)malloc(total_inflated_size * 2);
    U64 final_deflated_len = 0;

    // deflate all data back
    int result_def = mem_def(final_deflated_data, &final_deflated_len,
                             full_inflated_data, total_inflated_size,
                             Z_DEFAULT_COMPRESSION);
    if (result_def != 0)
    {
        free(full_inflated_data);
        free(final_deflated_data);
        perror("deflation error\n");
        exit(10);
    }

    free(full_inflated_data);

    // allocate the output PNG
    simple_PNG_p all_png = mallocPNG();
    if (all_png == NULL)
    {
        free(final_deflated_data);
        perror("no png allocated\n");
        exit(11);
    }

    // ---- IHDR ----
    all_png->p_IHDR = (chunk_p)malloc(sizeof(struct chunk));
    all_png->p_IHDR->length = DATA_IHDR_SIZE;
    memcpy(all_png->p_IHDR->type, "IHDR", 4);

    all_png->p_IHDR->p_data = (U8 *)malloc(DATA_IHDR_SIZE);
    all_png->p_IHDR->p_data[0] = (width >> 24) & 0xFF;
    all_png->p_IHDR->p_data[1] = (width >> 16) & 0xFF;
    all_png->p_IHDR->p_data[2] = (width >> 8) & 0xFF;
    all_png->p_IHDR->p_data[3] = (width) & 0xFF;
    all_png->p_IHDR->p_data[4] = (height >> 24) & 0xFF;
    all_png->p_IHDR->p_data[5] = (height >> 16) & 0xFF;
    all_png->p_IHDR->p_data[6] = (height >> 8) & 0xFF;
    all_png->p_IHDR->p_data[7] = (height) & 0xFF;
    all_png->p_IHDR->p_data[8]  = 8;  // bit depth
    all_png->p_IHDR->p_data[9]  = 6;  // color type
    all_png->p_IHDR->p_data[10] = 0;  // compression
    all_png->p_IHDR->p_data[11] = 0;  // filter
    all_png->p_IHDR->p_data[12] = 0;  // interlace

    all_png->p_IHDR->crc = calculate_chunk_crc(all_png->p_IHDR);

    // ---- IDAT ----
    all_png->p_IDAT = (chunk_p)malloc(sizeof(struct chunk));
    all_png->p_IDAT->length = (U32)final_deflated_len;
    memcpy(all_png->p_IDAT->type, "IDAT", 4);
    all_png->p_IDAT->p_data = final_deflated_data;
    all_png->p_IDAT->crc = calculate_chunk_crc(all_png->p_IDAT);

    // ---- IEND ----
    all_png->p_IEND = (chunk_p)malloc(sizeof(struct chunk));
    all_png->p_IEND->length = 0;
    memcpy(all_png->p_IEND->type, "IEND", 4);
    all_png->p_IEND->p_data = NULL;
    all_png->p_IEND->crc = calculate_chunk_crc(all_png->p_IEND);

    // write final png
    int write_status = write_PNG("all.png", all_png);
    if (write_status != 0)
    {
        free_png(all_png);
        perror("error writing all.png file\n");
        exit(12);
    }

    free_png(all_png);
}


// void catpng (int argc, char *argv[])
// {

//     // helper variables
//     U32 height = 0;
//     U32 width = 0;
//     U8 *full_inflated_data = NULL;
//     U64 total_inflated_size = 0;

//     // for loop through the other arguments
//     for (int i = 1; i < argc; i++)
//     {

//         // open all the files to read the data
//         FILE *fp = fopen(argv[i], "r");
//         if (fp == NULL)
//         {
//             perror("fopen error\n");
//             exit(2);
//         }

//         // fread from file, the signature of the file onto a buffer
//         U8 signature_buffer[PNG_SIG_SIZE];
//         int status_read = fread(signature_buffer, 1, PNG_SIG_SIZE, fp);
//         if (status_read != PNG_SIG_SIZE)
//         {
//             perror("signature read fail\n");
//             fclose(fp);
//             exit(3);
//         }

//         // check for is_png using signuatre
//         int png_check = is_png(signature_buffer, PNG_SIG_SIZE);
//         if (png_check != 1)
//         {
//             perror("not a valid png\n");
//             fclose(fp);
//             exit(4);
//         }

//         // reading the IHDR chunk from the file
//         struct data_IHDR IHDR_data;
//         if (get_png_data_IHDR(&IHDR_data, fp, 16, SEEK_SET) != 0)
//         {
//             fclose(fp);
//             perror("IHDR data retrieval failed\n");
//             exit(5);
//         }; // offset by 16 to get to the data field of ihdr

//         // check widths, the starting file width will be the reference for the rest of the widths
//         if (i == 1)
//         {
//             width = IHDR_data.width;
//         }
//         else if (IHDR_data.width != width)
//         {
//             fclose(fp);
//             perror("Different widths, png is not aligned\n");
//             exit(6);
//         }
//         height += IHDR_data.height; // increment height

//         // allocate the cur_png
//         simple_PNG_p cur_png = mallocPNG();
//         if (cur_png == NULL)
//         {
//             fclose(fp);
//             perror("current png allocate error\n");
//             exit(7);
//         }

//         // store the info of the current png
//         if (get_png_chunks(cur_png, fp, PNG_SIG_SIZE, SEEK_SET) != 0)
//         {
//             fclose(fp);
//             free_png(cur_png);
//             perror("couldnt retrieve png chunks\n");
//             exit(8);
//         };

//         // initialisations for IDAT
//         U8 *deflated_data = cur_png->p_IDAT->p_data;
//         U64 deflated_len = (U64)cur_png->p_IDAT->length;
//         U64 inflated_len = 0;
//         U64 expected_inflated_len = (U64)(IHDR_data.height * (4 * IHDR_data.width + 1));
//         U8 *buf_inf = (U8 *)malloc(expected_inflated_len * 2); // times two to make sure enough space

//         // inlfate the data from the current IDAT chunk
//         int result_inf = mem_inf(buf_inf, &inflated_len, deflated_data, deflated_len);
//         if (result_inf != 0)
//         {
//             free(buf_inf);
//             fclose(fp);
//             free_png(cur_png);
//             perror("inflation error\n");
//             exit(9);
//         }

//         // keep track of current position in the raw data field
//         U64 cur_position = total_inflated_size;
//         total_inflated_size += inflated_len; // increment the total size of inflated data

//         // reallocate the fully inflated data every iteration and update its size
//         full_inflated_data = (U8 *)realloc(full_inflated_data, total_inflated_size);

//         //"full_inflated_data + cur_position" means start copying at the END of the previous'y inputted data
//         // copy over the buffer data onto full_inflated_data
//         memcpy(full_inflated_data + cur_position, buf_inf, inflated_len);

//         free(buf_inf);
//         fclose(fp);
//         free_png(cur_png);
//     }

//     U8 *final_deflated_data = (U8 *)malloc(total_inflated_size * 2); // again two for safety
//     U64 final_deflated_len = 0;                                      // initialising deflated len

//     // deflating the data from the source "full_inflated_data" of size "total_inflated size"
//     int result_def = mem_def(final_deflated_data, &final_deflated_len, full_inflated_data, total_inflated_size, Z_DEFAULT_COMPRESSION);
//     if (result_def != 0)
//     {
//         free(full_inflated_data);
//         free(final_deflated_data);
//         perror("deflation error\n");
//         exit(10);
//     }

//     // free the inlfated data as we dont need it anymore
//     free(full_inflated_data);

//     // allocate the output of the file -> all_png
//     simple_PNG_p all_png = mallocPNG();
//     if (all_png == NULL)
//     {
//         free(final_deflated_data);
//         perror("no png allocated\n");
//         exit(11);
//     }

//     // populating the 1. IHDR chunk
//     all_png->p_IHDR = (chunk_p)malloc(sizeof(struct chunk));

//     all_png->p_IHDR->length = DATA_IHDR_SIZE;

//     all_png->p_IHDR->type[0] = 'I';
//     all_png->p_IHDR->type[1] = 'H';
//     all_png->p_IHDR->type[2] = 'D';
//     all_png->p_IHDR->type[3] = 'R';

//     all_png->p_IHDR->p_data = (U8 *)malloc(DATA_IHDR_SIZE);
//     all_png->p_IHDR->p_data[0] = (width >> 24) & 0xFF;
//     all_png->p_IHDR->p_data[1] = (width >> 16) & 0xFF;
//     all_png->p_IHDR->p_data[2] = (width >> 8) & 0xFF;
//     all_png->p_IHDR->p_data[3] = (width) & 0xFF;
//     all_png->p_IHDR->p_data[4] = (height >> 24) & 0xFF;
//     all_png->p_IHDR->p_data[5] = (height >> 16) & 0xFF;
//     all_png->p_IHDR->p_data[6] = (height >> 8) & 0xFF;
//     all_png->p_IHDR->p_data[7] = (height) & 0xFF;
//     all_png->p_IHDR->p_data[8] = 8;  // bit depth
//     all_png->p_IHDR->p_data[9] = 6;  // color type
//     all_png->p_IHDR->p_data[10] = 0; // compression
//     all_png->p_IHDR->p_data[11] = 0; // filter
//     all_png->p_IHDR->p_data[12] = 0; // interlace

//     all_png->p_IHDR->crc = calculate_chunk_crc(all_png->p_IHDR);

//     // populating the 2. IDAT chunk
//     all_png->p_IDAT = (chunk_p)malloc(sizeof(struct chunk));

//     all_png->p_IDAT->length = (U32)final_deflated_len;

//     all_png->p_IDAT->type[0] = 'I';
//     all_png->p_IDAT->type[1] = 'D';
//     all_png->p_IDAT->type[2] = 'A';
//     all_png->p_IDAT->type[3] = 'T';

//     all_png->p_IDAT->p_data = final_deflated_data;

//     all_png->p_IDAT->crc = calculate_chunk_crc(all_png->p_IDAT);

//     // populating the 3. IEND chunk
//     all_png->p_IEND = (chunk_p)malloc(sizeof(struct chunk));

//     all_png->p_IEND->length = 0; // IEND length = 0

//     all_png->p_IEND->type[0] = 'I';
//     all_png->p_IEND->type[1] = 'E';
//     all_png->p_IEND->type[2] = 'N';
//     all_png->p_IEND->type[3] = 'D';

//     all_png->p_IEND->p_data = NULL;

//     all_png->p_IEND->crc = calculate_chunk_crc(all_png->p_IEND);

//     // writing all_png to the file "all.png"
//     int write_status = write_PNG("all.png", all_png);
//     if (write_status != 0)
//     {
//         free_png(all_png);
//         perror("error writing all.png file\n");
//         exit(12);
//     }

//     free_png(all_png); // freeing the png after writing it

// }

// int main(int argc, char *argv[])
// {
//     if (argc < 2)
//     {
//         fprintf(stderr, "Usage: %s <filename1> <filename2> ...\n", argv[0]);
//         return 1;
//     }
//     else {
//         catpng(argc, argv);
//     }
//     return 0;
// }