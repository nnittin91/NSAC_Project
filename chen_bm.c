/*
 * ===========================================================================
 *
 *       Filename:  chen_bm.c
 *
 *    Description:  CHEN BITMAP
 *
 *        Version:  1.0
 *        Created:  10/20/2015 11:55:22 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#include "chen_bm.h"

BITMAP *chen_bm_init(BLOCK size)
{
	BITMAP *bm;
	unsigned int bitmap_size;

	/* Sanity */
	if (size <= 0)
		goto bad;

	/* Create BITMAP */
	bm = kzalloc(sizeof(BITMAP), GFP_KERNEL);
	if (!bm)
		goto bad;

	/* Create bitmap */
	bitmap_size  = (size / BITS_PER_LONG) * sizeof(long);
	bm->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!bm->bitmap)
		goto free_bitmap;

	/* Initial BITMAP */
	bm->size = size;
	bm->count = 0;

	return bm;

free_bitmap:
	kfree(bm);
bad:
	return NULL;
}

void chen_bm_destroy(BITMAP *bm)
{
	if (!bm)
		return;

	kfree(bm->bitmap);
	kfree(bm);
}

static int __chen_bm_get_free_block(BITMAP *bm, BLOCK *new_block,
					BLOCK start)
{
	*new_block = find_next_zero_bit(bm->bitmap, bm->size, start);
	if (*new_block < bm->size)
		return 1;
	return 0;
}

int chen_bm_get_free_block(BITMAP *bm, BLOCK *new_block)
{
	BLOCK rand, start;
	int retry = DATALAIR_BITMAP_RETRY_COUNT;

	if (bm->count >= bm->size)
		return 0;

	do {
		get_random_bytes(&rand, sizeof(BLOCK));
		start = rand % bm->size;
		if (__chen_bm_get_free_block(bm, new_block, start))
			return 1;
	} while (--retry > 0);

	return __chen_bm_get_free_block(bm, new_block, 0);
}

int chen_bm_set_mapping(BITMAP *bm, BLOCK pbn)
{
	/* Sanity */
	if (!bm)
		goto bad;

	/* Check bound */
	if (pbn >= bm->size)
		goto bad;

	/* Set the bit if unset */
	if (!test_bit(pbn, bm->bitmap)) {
		set_bit(pbn, bm->bitmap);
		bm->count++;
		return 1;
	}

bad:
	return 0;
}
