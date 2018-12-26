/*
 * ===========================================================================
 *
 *       Filename:  chen_bm.h
 *
 *    Description: Header file for the BITMAP
 *
 *        Version:  1.0
 *        Created:  10/20/2015 11:55:10 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Vinay Jain (VJ), vinay.g.jain@gmail.com
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#include <linux/slab.h>
#include <linux/random.h>
#include "chen_common.h"

#define DATALAIR_BITMAP_RETRY_COUNT 3

typedef struct {
	unsigned long *bitmap;
	BLOCK count;
	BLOCK size;
} BITMAP;

extern BITMAP *chen_bm_init(BLOCK size);
extern void chen_bm_destroy(BITMAP *bm);
extern int chen_bm_get_free_block(BITMAP *bm, BLOCK *new_block);
extern int chen_bm_set_mapping(BITMAP *bm, BLOCK pbn);
