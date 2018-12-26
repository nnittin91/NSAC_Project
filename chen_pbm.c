/*
 * ===========================================================================
 *
 *       Filename:  chen_pbm.c
 *
 *    Description:  CHEN PBM
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

#include "chen_pbm.h"

//CHEN
static int __chen_pbm_init(DL_PBM *pbm)
{
	BLOCK i;
	i = 0;
	while (i < pbm->max_pbn)
		if (!chen_pbm_set_bm(pbm, i++))	//CHEN: set_bm means the block is invalid
			break;
		
	pbm->valid_blocks = 0;

	if (i == pbm->max_pbn)
		return 1;
	else
		return 0;
}
//CHEN
DL_PBM *chen_pbm_init(struct chen_device *dl_dev, BLOCK nr_blocks)
{
	DL_PBM *pbm;

	printk(KERN_ALERT "CHEN: pbm_init(), pbm->max_pbn=%llu", (long long unsigned)nr_blocks);
	/* Sanity check */
	if (!dl_dev || nr_blocks == 0)
		goto bad;

	/* Create PBM */
	pbm = kzalloc(sizeof(*pbm), GFP_KERNEL);
	if (!pbm)
		goto bad;

	/* Initialize */
	pbm->dl_dev = dl_dev;
	pbm->max_pbn = nr_blocks;
	pbm->valid_blocks = 0;

	/* Allocate cache */
	pbm->cache = chen_cache_init(dl_dev, pbm->max_pbn,
					 PBM_BLOCK, PBM_CACHE_PAGES);
	if (!pbm->cache)
		goto bad_free_pbm;

	if (!__chen_pbm_init(pbm))
		goto bad_free_pbm;
		
	return pbm;

bad_free_pbm:
	chen_pbm_destroy(pbm);
bad:
	return NULL;
}

void chen_pbm_destroy(DL_PBM *pbm)
{
	if (!pbm)
		return;

	if (pbm->cache)
		chen_cache_destroy(pbm->cache);

	kfree(pbm);
}

//CHEN
int chen_pbm_set_bm(DL_PBM *pbm, BLOCK pbn)
{
	if (chen_cache_set_bm(pbm->cache, pbn)) {
		pbm->valid_blocks--;
		return 1;
	}else
		return 0;
}
int modified_chen_pbm_set_bm(DL_PBM *pbm, BLOCK pbn,struct bio* main_bio)
{
	if (modified_chen_cache_set_bm(pbm->cache, pbn,main_bio)) {
		pbm->valid_blocks--;
		return 1;
	}else
		return 0;
}
//CHEN
int chen_pbm_clear_bm(DL_PBM *pbm, BLOCK pbn)
{
	if (chen_cache_clear_bm(pbm->cache, pbn)) {
		pbm->valid_blocks++;
		return 1;
	}else
		return 0;
}

int modified_chen_pbm_clear_bm(DL_PBM *pbm, BLOCK pbn,struct bio* main_bio)
{
	if (modified_chen_cache_clear_bm(pbm->cache, pbn,main_bio)) {
		pbm->valid_blocks++;
		return 1;
	}else
		return 0;
}
//CHEN
int chen_pbm_get_bm(DL_PBM *pbm, BLOCK pbn, BLOCK *bm)
{
	BLOCK _bm;

	if (!chen_cache_get_bm(pbm->cache, pbn, &_bm))
		goto bad;

	if (_bm > 1)
		goto bad;

	*bm = _bm;
// 	printk(KERN_ALERT "CHEN: pbn=%llu, bm=%x", pbn, _bm);
	return 1;

bad:
	return 0;
}
//CHEN
int chen_get_one_new_public_block(DL_PBM *pbm, BLOCK current_pub_pbn, BLOCK *new_block)
{
	BLOCK i;
	BLOCK cur;
	BLOCK bm;

// 	printk(KERN_ALERT "CHEN: Get_new: cur=%llu, pbm->max_pbn=%llu\n", current_pub_pbn, pbm->max_pbn);
	bm = 0;
	for(i = 0; i < pbm->max_pbn; i += TUPLE_SIZE)
	{
		cur = (current_pub_pbn + i) % pbm->max_pbn;
		if(!chen_pbm_get_bm(pbm, cur, &bm))
			goto bad;
		if(bm)
		{
			*new_block = cur;
			break;
		}
	}
	if(!bm)
		goto bad;

	return 1;

bad:
	return 0;
}


