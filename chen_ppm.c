/*
 * ===========================================================================
 *
 *       Filename:  chen_ppm.c
 *
 *    Description:  CHEN PPM
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

#include "chen_ppm.h"

static int __chen_ppm_init(DL_PPM *ppm)
{
	BLOCK i;
	i = 0;
	while (i < ppm->max_lbn)
		if (!chen_ppm_remove_mapping(ppm, i++))
			break;
		
	ppm->valid_mappings = 0;

	if (i == ppm->max_lbn)
		return 1;
	else
		return 0;
}

DL_PPM *chen_ppm_init(struct chen_device *dl_dev, BLOCK nr_blocks)
{
	DL_PPM *ppm;

	/* Sanity check */
	if (!dl_dev || nr_blocks == 0)
		goto bad;

	/* Create PPM */
	ppm = kzalloc(sizeof(*ppm), GFP_KERNEL);
	if (!ppm)
		goto bad;

	/* Initialize */
	ppm->dl_dev = dl_dev;
	ppm->max_lbn = nr_blocks;
	ppm->valid_mappings = 0;

	/* Allocate cache */
	ppm->cache = chen_cache_init(dl_dev, ppm->max_lbn,
					 PPM_BLOCK, PPM_CACHE_PAGES);
	if (!ppm->cache)
		goto bad_free_ppm;

	if (!__chen_ppm_init(ppm))
		goto bad_free_ppm;
		
	return ppm;

bad_free_ppm:
	chen_ppm_destroy(ppm);
bad:
	return NULL;
}

void chen_ppm_destroy(DL_PPM *ppm)
{
	if (!ppm)
		return;

	if (ppm->cache)
		chen_cache_destroy(ppm->cache);

	kfree(ppm);
}
int modified_chen_ppm_add_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK pbn,struct bio* main_bio)
{
	if (modified_chen_cache_add_mapping(ppm->cache, lbn, pbn,main_bio)) {
		ppm->valid_mappings++;
		return 1;
	} else
		return 0;
}

int chen_ppm_add_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK pbn)
{
	if (chen_cache_add_mapping(ppm->cache, lbn, pbn)) {
		ppm->valid_mappings++;
		return 1;
	} else
		return 0;
}
int modified_chen_ppm_remove_mapping(DL_PPM *ppm, BLOCK lbn,struct bio* main_bio)
{
	if (modified_chen_cache_remove_mapping(ppm->cache, lbn,main_bio)) {
		ppm->valid_mappings--;
		return 1;
	} else
		return 0;
}

int chen_ppm_remove_mapping(DL_PPM *ppm, BLOCK lbn)
{
	if (chen_cache_remove_mapping(ppm->cache, lbn)) {
		ppm->valid_mappings--;
		return 1;
	} else
		return 0;
}

int modified_chen_ppm_get_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK *pbn,struct bio*main_bio)
{
	BLOCK _pbn;

	if (!modified_chen_cache_get_mapping(ppm->cache, lbn, &_pbn,main_bio))
		goto bad;

	if (_pbn >= ppm->max_lbn)
		goto bad;

	*pbn = _pbn;
	return 1;

bad:
	return 0;
}

int chen_ppm_get_mapping(DL_PPM *ppm, BLOCK lbn, BLOCK *pbn)
{
	BLOCK _pbn;

	if (!chen_cache_get_mapping(ppm->cache, lbn, &_pbn))
		goto bad;

	if (_pbn >= ppm->max_lbn)
		goto bad;

	*pbn = _pbn;
	return 1;

bad:
	return 0;
}
