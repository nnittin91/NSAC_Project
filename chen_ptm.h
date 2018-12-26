/*
 * ===========================================================================
 *
 *       Filename:  chen_ptm.h
 *
 *    Description:  Header file for CHEN Private Tree Map
 *
 *        Version:  1.0
 *        Created:  4/14/2016 04:22:39 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#ifndef CHEN_PTM_H 
#define CHEN_PTM_H


#include<linux/types.h>
#include<linux/slab.h>
#include<linux/random.h>
#include<linux/kernel.h>

#include "chen_common.h"
#include "chen_cache.h"

#define MAPPINGS_PER_PTM_PAGE (PAGESIZE/sizeof(BLOCK))
#define PTM_CACHE_PAGES 4

struct bio;
typedef struct {
	BLOCK entries[FAN_OUT];
} PTM_NODE;

typedef struct {
	DL_CACHE *cache;
	PTM_NODE *root;
	unsigned depth;
	BLOCK max_lbn;
	BLOCK valid_mappings;
	struct chen_device *dl_dev;
} DL_PTM;

extern DL_PTM *chen_ptm_init(struct chen_device *dl_dev, BLOCK nr_blocks);
extern void chen_ptm_destroy(DL_PTM *ptm);
extern int chen_ptm_read(DL_PTM *ptm, BLOCK lbn, BLOCK *pbn);
extern int chen_ptm_write(DL_PTM *ptm, BLOCK lbn, BLOCK pbn);
extern int modified_chen_ptm_write(DL_PTM *ptm, BLOCK lbn, BLOCK pbn,struct bio* main_bio);
extern int chen_get_one_new_private_block(DL_PTM *ptm, BLOCK current_priv_pbn, BLOCK *new_block);

#endif
