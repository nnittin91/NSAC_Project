/*
 * ===========================================================================
 *
 *       Filename:  chen_pbm.h
 *
 *    Description:  Header file for CHEN PBM
 *
 *        Version:  1.0
 *        Created:  4/6/2016 04:22:39 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#ifndef CHEN_PBM_H 
#define CHEN_PBM_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "chen_common.h"
#include "chen_cache.h"

#define BITMAPS_PER_PBM_PAGE (PAGESIZE/sizeof(long)*BITS_PER_LONG)
#define PBM_CACHE_PAGES 4

typedef struct {
	DL_CACHE *cache;
	BLOCK max_pbn;
	BLOCK valid_blocks;
	struct chen_device *dl_dev;
} DL_PBM;

extern DL_PBM *chen_pbm_init(struct chen_device *dl_dev, BLOCK nr_blocks);
extern void chen_pbm_destroy(DL_PBM *pbm);
extern int chen_pbm_set_bm(DL_PBM *pbm, BLOCK pbn);
extern int modified_chen_pbm_set_bm(DL_PBM *pbm, BLOCK pbn,struct bio* main_bio);
extern int chen_pbm_clear_bm(DL_PBM *pbm, BLOCK pbn);
extern int modified_chen_pbm_clear_bm(DL_PBM *pbm, BLOCK pbn,struct bio* main_bio);
extern int chen_pbm_get_bm(DL_PBM *pbm, BLOCK pbn, BLOCK *bm);
extern int chen_get_one_new_public_block(DL_PBM *pbm, BLOCK current_pub_pbn, BLOCK *new_block);
#endif
