/*
 * ===========================================================================
 *
 *       Filename:  chen_cache.h
 *
 *    Description:  Header file for CHEN CACHE
 *
 *        Version:  1.0
 *        Created:  4/9/2015 04:22:39 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#ifndef CHEN_CACHE_H /* chen_cache.h */
#define CHEN_CACHE_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "chen_common.h"
#include<linux/bio.h>

#define MAPPINGS_PER_CACHE_PAGE (PAGESIZE/sizeof(BLOCK))
#define BITMAPS_PER_CACHE_PAGE (PAGESIZE/sizeof(BLOCK)*8*sizeof(BLOCK))
#define BITS_PER_BLOCK (8*sizeof(BLOCK))

#define BIT_SET(a, b) ((a) | ((BLOCK)1<<(b)))	// set the bit to be 1???
#define BIT_CLEAR(a, b) ((a) & ~((BLOCK)1<<(b)))	//set the bit to be 0???
#define BIT_FLIP(a, b) ((a) ^ ((BLOCK)1<<(b)))
#define BIT_CHECK(a, b) ((a) & ((BLOCK)1<<(b)))

typedef struct {
	BLOCK entries[MAPPINGS_PER_CACHE_PAGE];
} DL_CACHE_MAPPINGS;	//Chen: a mapping page?

typedef struct {
	DL_CACHE_MAPPINGS *mappings;
	PAGE page_number;
	bool is_valid;
	bool is_dirty;
} DL_CACHE_PAGE;	//Chen: a mapping page with properties

typedef struct {
	/* Pages */
	DL_CACHE_PAGE **pages;
	PAGE nr_pages;

	/* For replacement algorithm */
	PAGE hand;

	/* Cache */
	int cache_type;
	BLOCK max_key;
	PAGE max_page_number;

	/* Store */
	struct chen_device *dl_dev;
} DL_CACHE;		//Chen: a set of mapping pages in chen_device

extern DL_CACHE *chen_cache_init(struct chen_device *dl_dev,
				     BLOCK nr_blocks,
				     int cache_type, int cache_size);
extern void chen_cache_destroy(DL_CACHE *cache);
extern int chen_cache_add_mapping(DL_CACHE *cache, BLOCK key, BLOCK value);
extern int modified_chen_cache_add_mapping(DL_CACHE *cache, BLOCK key, BLOCK value,struct bio*main_bio);
extern int chen_cache_add_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					       OFFSET offset, BLOCK value);

extern int chen_cache_remove_mapping(DL_CACHE *cache, BLOCK key);
extern int modified_chen_cache_remove_mapping(DL_CACHE *cache, BLOCK key,struct bio*main_bio);
extern int chen_cache_remove_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
						  OFFSET offset);

extern int chen_cache_get_mapping(DL_CACHE *cache, BLOCK key, BLOCK *value);
extern int modified_chen_cache_get_mapping(DL_CACHE *cache, BLOCK key, BLOCK *value,struct bio* main_bio);
extern int chen_cache_get_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					       OFFSET offset, BLOCK *value);

extern int chen_cache_set_bm(DL_CACHE *cache, BLOCK key);
extern int modified_chen_cache_set_bm(DL_CACHE *cache, BLOCK key,struct bio* main_bio);
extern int chen_cache_clear_bm(DL_CACHE *cache, BLOCK key);
extern int modified_chen_cache_clear_bm(DL_CACHE *cache, BLOCK key,struct bio* main_bio);
extern int chen_cache_get_bm(DL_CACHE *cache, BLOCK key, BLOCK *value);

extern int chen_cache_get_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK *value);
extern int chen_cache_update_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK value, BLOCK new_pbn, BLOCK new_lbn, unsigned depth, BLOCK *child_pbn);
extern int modified_chen_cache_update_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK value, BLOCK new_pbn, BLOCK new_lbn, unsigned depth, BLOCK *child_pbn,struct bio* main_bio);

extern int modified_chen_cache_get_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index,struct bio* main_bio);
#endif
