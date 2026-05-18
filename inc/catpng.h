#ifndef CATPNG_H
#define CATPNG_H
#pragma once

/* Public interface for catpng.c */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUF_LEN
#define BUF_LEN (256u * 32u)
#endif

/* 
 * Concatenate multiple PNGs (stacked vertically) and write "all.png".
 * Expects argv[1..argc-1] to be paths to valid PNG files.
 * Returns nothing; prints errors to stderr and exits on failure.
 */
void catpng(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* CATPNG_H */
