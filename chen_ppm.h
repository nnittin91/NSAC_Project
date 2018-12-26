/*
 * ===========================================================================
 *
 *       Filename:  chen_ppm.h
 *
 *    Description:  Header file for DATALAIR PPM
 *
 *        Version:  1.0
 *        Created:  10/12/2015 04:22:39 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Vinay Jain (VJ), vinay.g.jain@gmail.com
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#ifndef CHEN_PPM_H /* chen_fbis.h */
#define CHEN_PPM_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "chen_common.h"
#include "chen_cache.h"

#define MAPPINGS_PER_PPM_PAGE (PAGESIZE/sizeof(BLOCK))
#define PPM_CACHE_PAGES 4

typedef struct {
	DL_CACHE *cache;
	BLOCK max_lbn;
	BLOCK valid_mappings;
	struct chen_device *dl_dev;
} DL_PPM;

extern DL_PPM *chen_ppm_init(struct chen_device *, BLOCK nr_blocks);
extern void chen_ppm_destroy(DL_PPM *ppm);
extern int chen_ppm_add_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK pbn);
extern int modified_chen_ppm_add_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK pbn,struct bio*main_bio);
extern int chen_ppm_remove_mapping(DL_PPM *ppm, BLOCK lbn);
extern int chen_ppm_get_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK *pbn);
extern int modified_chen_ppm_get_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK *pbn,struct bio* main_bio);

#endif
