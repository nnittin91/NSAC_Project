/*
 * ===========================================================================
 *
 *       Filename:  chen_ptm.c
 *
 *    Description:  CHEN Private Tree Map
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

#include "chen_ptm.h"

/* Helper Functions */
static unsigned ptm_log(BLOCK num)
{
	unsigned i;
	BLOCK prod;

	i = 1;
	prod = FAN_OUT-1;

	while (prod < num) {
		prod = prod * FAN_OUT;
		i++;
	}

	return i;
}

static BLOCK power(unsigned base, unsigned p)
{
	BLOCK x;
	x = 1;
	while (p--)
		x = (BLOCK) x * base;
	return x;
}

static int ptm_get_depth(BLOCK nr_blocks)
{
	return ptm_log(nr_blocks)/ptm_log(FAN_OUT-1);
}

/* ptm_node allocate/free/get/set */
static PTM_NODE *chen_ptm_allocate_ptm_node(void)
{
	PTM_NODE *node;
	unsigned i;

	/* Allocate node */
	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		goto bad;

	/* Initialize all the mappings */
	for (i = 0; i < FAN_OUT; ++i)
		node->entries[i] = INVALID_BLOCK;

	/* Return the node */
	return node;

bad:
	return NULL;
}

static void chen_ptm_free_ptm_node(PTM_NODE *node)
{
	if (!node)
		return;

	kfree(node);
}

/* Read/Write PTM ROOT */
static int read_ptm_root(DL_PTM *ptm)
{
	if (!ptm || !ptm->root)
		return 0;

	return chen_read_block(ptm->dl_dev, ptm->root, 0, PTM_ROOT_BLOCK);
}

static int write_ptm_root(DL_PTM *ptm)
{
	if (!ptm || !ptm->root)
		return 0;

	return chen_write_block(ptm->dl_dev, ptm->root, 0, PTM_ROOT_BLOCK);
}

static int get_ptm_root_map(DL_PTM *ptm, BLOCK lbn, BLOCK *pbn, OFFSET *offset)
{
	BLOCK value, x;
	OFFSET offset_value;

	if (!ptm || !ptm->root)
		return 0;

	if(ptm->depth == 1){
		offset_value = (OFFSET) lbn;
		value = ptm->root->entries[offset_value];
	}
	else{
		x = (FAN_OUT-1) * power(FAN_OUT, (ptm->depth - 2));
		offset_value = lbn / x;
		value = ptm->root->entries[offset_value];
		*offset = (lbn % x);
	}
	//printk(KERN_INFO "CHEN: get_root_map lbn = %llu, entry=%u, pbn=%llu\n", (long long unsigned) lbn, offset_value, value);
	*pbn = value;

// 	if(value == INVALID_BLOCK)
// 		return 0;

	return 1;
}

static int set_ptm_root_map(DL_PTM *ptm, BLOCK lbn, BLOCK pbn)
{
	BLOCK x;
	OFFSET offset;

	if (!ptm || !ptm->root)
		return 0;

	if(ptm->depth == 1)
		offset = (OFFSET) lbn;
	else
	{
		x = (FAN_OUT-1) * power(FAN_OUT, (ptm->depth - 2));
		offset = lbn / x;
		lbn = lbn % x;
	}
// 	printk(KERN_INFO "CHEN: set_root_map lbn = %llu, entry=%u, pbn=%llu\n", (long long unsigned) lbn, offset, pbn);
	ptm->root->entries[offset] = pbn;

	return 1;
}

/* FUNCTION DECLARATIONS */
DL_PTM *chen_ptm_init(struct chen_device *dl_dev, BLOCK nr_blocks)
{
	DL_PTM *ptm;

	/* Sanity check */
	if (!dl_dev || nr_blocks == 0)
		goto bad;

	/* Allocate PTM */
	ptm = kmalloc(sizeof(*ptm), GFP_KERNEL);
	if (!ptm)
		goto bad;

	/* Initialize properties */
	ptm->depth = ptm_get_depth(nr_blocks);
	ptm->max_lbn = nr_blocks;
	ptm->valid_mappings = 0;
	ptm->dl_dev = dl_dev;

	/* Allocate cache */
	ptm->cache = chen_cache_init(dl_dev, ptm->max_lbn,
					 PTM_BLOCK, PTM_CACHE_PAGES);
	if (!ptm->cache)
		goto bad_free_ptm;
	ptm->cache->max_page_number = nr_blocks;	//CHEN???

	/* Allocate root node */
	ptm->root = chen_ptm_allocate_ptm_node();
	if (!ptm->root)
		goto bad_free_ptm_cache;

	/* Write root node to disk */
	if (!write_ptm_root(ptm))
		goto bad_free_ptm_root;

	/* Return PTM */
	return ptm;

bad_free_ptm_root:
	chen_ptm_free_ptm_node(ptm->root);
bad_free_ptm_cache:
	chen_cache_destroy(ptm->cache);
bad_free_ptm:
	kfree(ptm);
bad:
	return NULL;
}

void chen_ptm_destroy(DL_PTM *ptm)
{
	if (!ptm)
		return;

	if (ptm->cache)
		chen_cache_destroy(ptm->cache);

	if (ptm->root)
		chen_ptm_free_ptm_node(ptm->root);

	kfree(ptm);
}

/* Read/Write the ptm for lbn->pbn */

/* Get the ptm index for each level =  depth, index start from the leaf node */
static int chen_lbn_to_ptm_index(DL_PTM *ptm, BLOCK lbn, unsigned depth, PAGE *index)
{
	BLOCK x;
	PAGE _index;
	unsigned root_depth, i;

	root_depth = ptm->depth;

	if(depth > root_depth-1)
		return 0;

	_index = 0;
	for(i=1; i<depth; i++)
		_index += power(FAN_OUT, (root_depth - i));
// 	printk(KERN_INFO "CHEN: lbn=%llu, index1=%u ", (long long unsigned int) lbn, _index);
	x = (FAN_OUT-1) * power(FAN_OUT, (depth - 1));
	_index += (lbn/x + 1);
// 	printk(KERN_INFO "x=%llu, index2=%u\n", (long long unsigned int) x, _index);
	*index = _index;
	/* index start from root node */
// 	for(i=root_depth; i>depth; i--)
// 		_index += power(FAN_OUT, (root_depth - i));
// 
// 	x = (FAN_OUT-1) * power(FAN_OUT, (depth - 1));
// 	_index += (lbn/x + 1);
// 	*index = _index;

	return 1;
}
/* Get the ptm offset for each level =  depth */
static int chen_lbn_to_ptm_offset(DL_PTM *ptm, BLOCK lbn, unsigned depth, OFFSET *offset)
{
	BLOCK x;
	OFFSET _offset;
	unsigned root_depth;

	root_depth = ptm->depth;

	if(depth > root_depth-1)
		return 0;

	x = (FAN_OUT-1) * power(FAN_OUT, (depth - 1));
	_offset = lbn % x;

// 	printk(KERN_INFO "lbn=%llu, x=%llu, os1=%llu", lbn, (long long unsigned int) x, _offset);
	if(depth == 1)
		x = 1;
	else
		x = (FAN_OUT-1) * power(FAN_OUT, (depth - 2));
	_offset = _offset / x;
// 	printk(KERN_INFO "x=%llu, os2=%u\n", (long long unsigned int) x, _offset);

	*offset = _offset;

	return 1;
}
#if 0
int modified_chen_ptm_read(DL_PTM *ptm, BLOCK lbn, BLOCK *pbn)
{
	unsigned depth, i;
	BLOCK _pbn;
	PAGE index;
	OFFSET offset;
	struct bio *bio;
	struct page *p;
	char * data=NULL;
	struct page *read_page=chen_alloc_page(ptm->dl_dev);
	bio = chen_alloc_bio(ptm->dl_dev);
		bio->bi_rw &= ~REQ_WRITE;
	bio->bi_bdev = chen_get_bdev(dl_dev);
	data=kmap(read_page);

	if(!get_ptm_root_map(ptm, lbn, &_pbn, &offset))
		return 0;

	//printk(KERN_INFO "lbn=%llu, child of root=%llu\n", (long long unsigned int) lbn, _pbn);
	if(ptm->depth == 1 || _pbn == INVALID_BLOCK)
	{ 
		*pbn = _pbn;
		return 1;
	}
	
	depth = ptm->depth-1;
	for(i=depth; i>0; i--)
	{
		if(!chen_lbn_to_ptm_index(ptm, lbn, i, &index))
			return 0;
		if(!chen_lbn_to_ptm_offset(ptm, lbn, i, &offset))
			return 0;
		if(!modified_chen_cache_get_ptm(ptm->cache, _pbn, index, offset, pbn,main_bio))
			return 0;
		_pbn = *pbn;
		if(_pbn == INVALID_BLOCK)
		{
			//printk("nittin pbn is INVALID_BLOCK\n");
			return 1;
		}
// 		if(i == 1)
// 			x = 1;
// 		else
// 			x = (FAN_OUT-1) * power(FAN_OUT, (i - 2));
// 
// 		if(!chen_cache_get_ptm(ptm->cache, page_number, offset/x, pbn))
// 		if(!chen_cache_get_ptm(ptm->cache, page_number, index, offset, pbn))
// 			return 0;
// 		page_number = *pbn;
// 		offset = offset % x;
	}

	return 1;
}
#endif

/* Get the map lbn->pbn by reading from the ptm cache, return INVALID_BLOCK if the entry doesn't exist yet */
int chen_ptm_read(DL_PTM *ptm, BLOCK lbn, BLOCK *pbn)
{
	unsigned depth, i;
	BLOCK _pbn;
	PAGE index;
	OFFSET offset;

	if(!get_ptm_root_map(ptm, lbn, &_pbn, &offset))
		return 0;

	//printk(KERN_INFO "lbn=%llu, child of root=%llu\n", (long long unsigned int) lbn, _pbn);
	if(ptm->depth == 1 || _pbn == INVALID_BLOCK)
	{ 
		*pbn = _pbn;
		return 1;
	}
	
	depth = ptm->depth-1;
	for(i=depth; i>0; i--)
	{
		if(!chen_lbn_to_ptm_index(ptm, lbn, i, &index))
			return 0;
		if(!chen_lbn_to_ptm_offset(ptm, lbn, i, &offset))
			return 0;
		if(!chen_cache_get_ptm(ptm->cache, _pbn, index, offset, pbn))
			return 0;
		_pbn = *pbn;
		if(_pbn == INVALID_BLOCK)
		{
			//printk("nittin pbn is INVALID_BLOCK\n");
			return 1;
		}
// 		if(i == 1)
// 			x = 1;
// 		else
// 			x = (FAN_OUT-1) * power(FAN_OUT, (i - 2));
// 
// 		if(!chen_cache_get_ptm(ptm->cache, page_number, offset/x, pbn))
// 		if(!chen_cache_get_ptm(ptm->cache, page_number, index, offset, pbn))
// 			return 0;
// 		page_number = *pbn;
// 		offset = offset % x;
	}

	return 1;
}
int modified_chen_ptm_write(DL_PTM *ptm, BLOCK lbn, BLOCK pbn,struct bio* main_bio)
{
	/* Write the ptm to disk as well as cache, location = pbn : pbn+depth-2 */
	BLOCK child_pbn, _pbn;
	PAGE index;
	OFFSET offset;
	unsigned depth, i;

	if (pbn >= chen_data_limit(ptm->dl_dev))
		goto bad;

	if(!get_ptm_root_map(ptm, lbn, &child_pbn, &offset))	//There is no map for this lbn 
		goto bad;
 		//printk(KERN_INFO "CHEN: root_child_pbn=%llu\n", (long long unsigned int) child_pbn);
	
 	//printk(KERN_INFO "CHEN: lbn=%llu, root_child_pbn=%llu\n", (long long unsigned int) lbn, (long long unsigned int) child_pbn);

	if(ptm->depth == 1)	//Need to write lbn as well
	{
		if(!set_ptm_root_map(ptm, lbn, pbn-1))
			return 0;
		if (!modified_chen_write_block(ptm->dl_dev, (void *) ptm->root->entries, pbn, ptm->cache->cache_type,main_bio))
			return 0;
	}
	else
	{
 		//printk(KERN_INFO "CHEN: set root lbn=%llu to pbn=%llu\n", lbn, pbn);
		if(!set_ptm_root_map(ptm, lbn, pbn-(ptm->depth-1)))
			return 0;
	}

	//printk(KERN_INFO "CHEN: write ptm!\n");
	depth = ptm->depth-1;
	for(i=depth; i>0; i--)
	{
		//printk(KERN_INFO "CHEN: i=%d\n", i);
		if(!chen_lbn_to_ptm_index(ptm, lbn, i, &index))	//get the index of the node
			return 0;
		if(!chen_lbn_to_ptm_offset(ptm, lbn, i, &offset))	//get the offset of the entry inside node
			return 0;
// 		if(!chen_cache_get_ptm(ptm->cache, child_pbn, index, offset, &_pbn))	//read the ptm in child_pbn, the result is _pbn.
// 			return 0;
		if(!modified_chen_cache_update_ptm(ptm->cache, child_pbn, index, offset, pbn-i+1, pbn-i, lbn, i, &_pbn,main_bio))	//node (depth=i) is updated from child_pbn to pbn-i, it is pointed to pbn-i+1
			return 0;
		child_pbn = _pbn;
 		//printk(KERN_INFO "CHEN: end of i=%d\n", i);
	}
	
 		//printk(KERN_INFO "CHEN: end of ptm_write\n");
	return 1;

bad:
	return 0;
}
int chen_ptm_write(DL_PTM *ptm, BLOCK lbn, BLOCK pbn)
{
	/* Write the ptm to disk as well as cache, location = pbn : pbn+depth-2 */
	BLOCK child_pbn, _pbn;
	PAGE index;
	OFFSET offset;
	unsigned depth, i;

	if (pbn >= chen_data_limit(ptm->dl_dev))
		goto bad;

	if(!get_ptm_root_map(ptm, lbn, &child_pbn, &offset))	//There is no map for this lbn 
		goto bad;
// 		printk(KERN_INFO "CHEN: root_child_pbn=%llu\n", (long long unsigned int) child_pbn);
	
// 	printk(KERN_INFO "CHEN: lbn=%llu, root_child_pbn=%llu\n", (long long unsigned int) lbn, (long long unsigned int) child_pbn);

	if(ptm->depth == 1)	//Need to write lbn as well
	{
		if(!set_ptm_root_map(ptm, lbn, pbn-1))
			return 0;
		if (!chen_write_block(ptm->dl_dev, (void *) ptm->root->entries, pbn, ptm->cache->cache_type))
			return 0;
	}
	else
	{
// 		printk(KERN_INFO "CHEN: set root lbn=%llu to pbn=%llu\n", lbn, pbn);
		if(!set_ptm_root_map(ptm, lbn, pbn-(ptm->depth-1)))
			return 0;
	}

	//printk(KERN_INFO "CHEN: write ptm!\n");
	depth = ptm->depth-1;
	for(i=depth; i>0; i--)
	{
// 		printk(KERN_INFO "CHEN: i=%d\n", i);
		if(!chen_lbn_to_ptm_index(ptm, lbn, i, &index))	//get the index of the node
			return 0;
		if(!chen_lbn_to_ptm_offset(ptm, lbn, i, &offset))	//get the offset of the entry inside node
			return 0;
// 		if(!chen_cache_get_ptm(ptm->cache, child_pbn, index, offset, &_pbn))	//read the ptm in child_pbn, the result is _pbn.
// 			return 0;
		if(!chen_cache_update_ptm(ptm->cache, child_pbn, index, offset, pbn-i+1, pbn-i, lbn, i, &_pbn))	//node (depth=i) is updated from child_pbn to pbn-i, it is pointed to pbn-i+1
			return 0;
		child_pbn = _pbn;
// 		printk(KERN_INFO "CHEN: end of i=%d\n", i);
	}
	
// 		printk(KERN_INFO "CHEN: end of ptm_write\n");
	return 1;

bad:
	return 0;
}
#if 0
static int modified_chen_ptm_read_lbn(struct chen_device *dl_dev, BLOCK pbn, BLOCK *lbn,struct bio* main_bio)
{
	BLOCK _lbn;
	PTM_NODE *node;

	/* Sanity */
	if (pbn >= chen_data_limit(dl_dev))
		goto bad;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		goto bad;

	/* Read page from disk */
	if (!modified_chen_read_block(dl_dev, (void *) node->entries, pbn, PTM_BLOCK,main_bio))
		goto bad_free_node;
	_lbn = node->entries[FAN_OUT-1];	//If this is a empty block, then lbn=0, but it doesn't matter
	*lbn = _lbn;
	//printk(KERN_INFO "CHEN: read lbn for pbn=%llu, result is %llu\n", pbn, _lbn);

	kfree(node);
	return 1;

bad_free_node:
	kfree(node);
bad:
	return 0;
}
#endif

/* Read the last level ptm block of current tuple, get the lbn (not through the cache) */
static int chen_ptm_read_lbn(struct chen_device *dl_dev, BLOCK pbn, BLOCK *lbn)
{
	BLOCK _lbn;
	PTM_NODE *node;

	/* Sanity */
	if (pbn >= chen_data_limit(dl_dev))
		goto bad;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		goto bad;

	/* Read page from disk */
	if (!chen_read_block(dl_dev, (void *) node->entries, pbn, PTM_BLOCK))
		goto bad_free_node;
	_lbn = node->entries[FAN_OUT-1];	//If this is a empty block, then lbn=0, but it doesn't matter
	*lbn = _lbn;
	//printk(KERN_INFO "CHEN: read lbn for pbn=%llu, result is %llu\n", pbn, _lbn);

	kfree(node);
	return 1;

bad_free_node:
	kfree(node);
bad:
	return 0;
}
#if 0
int modified_chen_get_one_new_private_block(DL_PTM *ptm, BLOCK current_priv_pbn, BLOCK *new_block,struct bio* main_bio)
{
	BLOCK i, max_pbn, max_lbn, lbn, pbn;
	BLOCK cur;

	max_lbn = ptm->max_lbn;
	max_pbn = chen_data_limit(ptm->dl_dev);
	//printk(KERN_INFO "CHEN: max_pbn=%llu, max_lbn=%llu\n", max_pbn, max_lbn);
	for(i = 0; i < max_pbn; i += TUPLE_SIZE)
	{
		cur = (current_priv_pbn + i) % max_pbn;
		//printk(KERN_INFO "CHEN: read lbn for pbn %llu\n", (cur+TUPLE_SIZE-3));
		if(!modified_chen_ptm_read_lbn(ptm->dl_dev, cur+TUPLE_SIZE-3, &lbn,main_bio))
			goto bad;
// 		printk(KERN_INFO "CHEN: i=%llu, lbn=%llu\n", (long long unsigned int) i, (long long unsigned int) lbn);

// 		if(lbn == INVALID_BLOCK)	//unused block without data
		if(lbn > max_lbn)	//unused block without data
		{
			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu>max_lbn in %llu, Unused block\n", (long long unsigned int) cur, lbn, cur+TUPLE_SIZE-3);
			*new_block = cur;
			return 1;
		}

		//printk(KERN_INFO "CHEN: Check if still valid\n");
		if(!modified_chen_ptm_read(ptm, lbn, &pbn,main_bio))	// the data is deleted
		{
 			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu, cannot read pbn=%llu\n", cur, lbn, (long long unsigned int) pbn);
			*new_block = cur;
			return 1;
		}
		else if(pbn != cur+TUPLE_SIZE-2)	// the data is invalid
		{
 			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu is mapped to pbn=%llu\n", cur, lbn, (long long unsigned int) pbn);
			*new_block = cur;
			return 1;
		}
		else	//date is valid, need re-encryption
		{
 			//printk(KERN_INFO "CHEN: i=%llu, cur=%llu, lbn=%llu is just mapped to pbn=%llu\n", i, cur, lbn, (long long unsigned int) pbn);
		}
	}

	if(i >= max_pbn)
		goto bad;

	return 1;

bad:
	return 0;

// 	*new_block = current_priv_pbn;
// 	return 1;
}
#endif


int chen_get_one_new_private_block(DL_PTM *ptm, BLOCK current_priv_pbn, BLOCK *new_block)
{
	BLOCK i, max_pbn, max_lbn, lbn, pbn;
	BLOCK cur;

	max_lbn = ptm->max_lbn;
	max_pbn = chen_data_limit(ptm->dl_dev);
	//printk(KERN_INFO "CHEN: max_pbn=%llu, max_lbn=%llu\n", max_pbn, max_lbn);
	for(i = 0; i < max_pbn; i += TUPLE_SIZE)
	{
		cur = (current_priv_pbn + i) % max_pbn;
		//printk(KERN_INFO "CHEN: read lbn for pbn %llu\n", (cur+TUPLE_SIZE-3));
		if(!chen_ptm_read_lbn(ptm->dl_dev, cur+TUPLE_SIZE-3, &lbn))
			goto bad;
// 		printk(KERN_INFO "CHEN: i=%llu, lbn=%llu\n", (long long unsigned int) i, (long long unsigned int) lbn);

// 		if(lbn == INVALID_BLOCK)	//unused block without data
		if(lbn > max_lbn)	//unused block without data
		{
			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu>max_lbn in %llu, Unused block\n", (long long unsigned int) cur, lbn, cur+TUPLE_SIZE-3);
			*new_block = cur;
			return 1;
		}

		//printk(KERN_INFO "CHEN: Check if still valid\n");
		if(!chen_ptm_read(ptm, lbn, &pbn))	// the data is deleted
		{
 			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu, cannot read pbn=%llu\n", cur, lbn, (long long unsigned int) pbn);
			*new_block = cur;
			return 1;
		}
		else if(pbn != cur+TUPLE_SIZE-2)	// the data is invalid
		{
 			//printk(KERN_INFO "CHEN: cur=%llu, lbn=%llu is mapped to pbn=%llu\n", cur, lbn, (long long unsigned int) pbn);
			*new_block = cur;
			return 1;
		}
		else	//date is valid, need re-encryption
		{
 			//printk(KERN_INFO "CHEN: i=%llu, cur=%llu, lbn=%llu is just mapped to pbn=%llu\n", i, cur, lbn, (long long unsigned int) pbn);
		}
	}

	if(i >= max_pbn)
		goto bad;

	return 1;

bad:
	return 0;

// 	*new_block = current_priv_pbn;
// 	return 1;
}

