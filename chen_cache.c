/*
 * ===========================================================================
 *
 *       Filename:  chen_cache.c
 *
 *    Description:  CHEN CACHE
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

#include "chen_cache.h"

static inline PAGE chen_cache_page_number(BLOCK key)
{
	return (PAGE) (key / MAPPINGS_PER_CACHE_PAGE);
}

static inline OFFSET chen_cache_page_offset(BLOCK key)
{
	return (OFFSET) (key % MAPPINGS_PER_CACHE_PAGE);
}

//CHEN
static inline PAGE chen_cache_page_number_bm(BLOCK key)
{
	return (PAGE) (key / BITMAPS_PER_CACHE_PAGE);
}
//CHEN
static inline OFFSET chen_cache_page_offset_bm(BLOCK key)
{
	return (OFFSET) ((key % BITMAPS_PER_CACHE_PAGE)/BITS_PER_BLOCK);
}
//CHEN
static inline OFFSET chen_cache_block_offset_bm(BLOCK key)
{
	return (OFFSET) ((key % BITMAPS_PER_CACHE_PAGE) % BITS_PER_BLOCK);
}

static PAGE find_replacement_index(DL_CACHE *cache)
{
	cache->hand = (cache->hand + 1) % cache->nr_pages;
	return cache->hand;
}	//Chen: fifo for cache replacement

static void chen_cache_page_mark_clean(DL_CACHE_PAGE *cache_page)
{
	cache_page->is_dirty = false;
}

static void chen_cache_page_mark_dirty(DL_CACHE_PAGE *cache_page)
{
	cache_page->is_dirty = true;
}

static void chen_cache_page_mark_valid(DL_CACHE_PAGE *cache_page)
{
	cache_page->is_valid = true;
}

static void chen_cache_page_mark_invalid(DL_CACHE_PAGE *cache_page)
{
	cache_page->is_valid = false;
}

static DL_CACHE_PAGE *chen_cache_allocate_page(void);
static void chen_cache_free_page(DL_CACHE_PAGE *page);
static int chen_cache_get_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index);

#if 0
static int __chen_cache_init(DL_CACHE *cache)
{
	BLOCK key;

	/* For every key set the value to INVALID_BLOCK */
	key = 0;
	while (key < cache->max_key) {
		/* Remove mapping will set the mapping to INVALID */
		if (!chen_cache_remove_mapping(cache, key))
			return 0;

		/* Increment the key */
		key++;
	}

	return 1;
}
#endif
static int modified_chen_cache_save_page(DL_CACHE *cache, PAGE index,struct bio* main_bio)
{
	DL_CACHE_PAGE *cache_page;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* Write page to disk */
	if (!modified_chen_write_block(cache->dl_dev, (void *) cache_page->mappings->entries,
				  (BLOCK) cache_page->page_number, cache->cache_type,main_bio))
		goto bad;

	/* Mark as clean */
	chen_cache_page_mark_clean(cache_page);

	return 1;

bad:
	return 0;
}

DL_CACHE *chen_cache_init(struct chen_device *dl_dev, BLOCK nr_blocks,
			      int cache_type, int cache_size)	//Chen: initial a cache map with "cache_size" cache pages
													//The map is responding to nr_blocks Block.
{
	DL_CACHE *cache;
	PAGE p;

	/* Sanity check */
	if (!dl_dev || nr_blocks == 0 || cache_size <= 0)
		goto bad;

	/* Create CACHE */
	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		goto bad;

	/* Allocate cache */
	cache->nr_pages = cache_size;
	cache->pages = kzalloc(cache->nr_pages * sizeof(DL_CACHE_PAGE *), GFP_KERNEL);
	if (!cache->pages)
		goto bad_free_cache;

	/* Fill cache with pages */
	for (p = 0; p < cache->nr_pages; ++p) {
		cache->pages[p] = chen_cache_allocate_page();
		if (!cache->pages[p])
			goto bad_free_cache;
	}

	/* Set the properties */
	cache->dl_dev = dl_dev;
	cache->max_key = nr_blocks;
	cache->cache_type = cache_type;
	cache->max_page_number = chen_cache_page_number(nr_blocks);

#if 0
	/* Initialize all the pages in the store */
	if (!__chen_cache_init(cache))
		goto bad_free_cache;
#endif

	/* Return CACHE */
	return cache;

bad_free_cache:
	chen_cache_destroy(cache);
bad:
	return NULL;
}

void chen_cache_destroy(DL_CACHE *cache)
{
	PAGE p;

	if (!cache)
		return;

	if (cache->pages) {
		for (p = 0; p < cache->nr_pages; ++p)
			chen_cache_free_page(cache->pages[p]);
		kfree(cache->pages);
	}
	kfree(cache);
}
static int __modified_chen_cache_set_mapping(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK value,struct bio* main_bio)
{
	PAGE index;

	/* Get the index from the cache holding required page */
	if (!modified_chen_cache_get_cache_index(cache, page_number, &index,main_bio))
		return 0;
	/* Add the mapping */
	cache->pages[index]->mappings->entries[offset] = value;

	/* Mark dirty */
	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}

static int __chen_cache_set_mapping(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK value)
{
	PAGE index;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_cache_index(cache, page_number, &index))
		return 0;
	/* Add the mapping */
	cache->pages[index]->mappings->entries[offset] = value;

	/* Mark dirty */
	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}

static int modified_chen_cache_set_mapping(DL_CACHE *cache, BLOCK key, BLOCK value,struct bio* main_bio)
{
	PAGE page_number;
	OFFSET offset;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number(key);
	offset = chen_cache_page_offset(key);

	return __modified_chen_cache_set_mapping(cache, page_number, offset, value,main_bio);
}

static int chen_cache_set_mapping(DL_CACHE *cache, BLOCK key, BLOCK value)
{
	PAGE page_number;
	OFFSET offset;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number(key);
	offset = chen_cache_page_offset(key);

	return __chen_cache_set_mapping(cache, page_number, offset, value);
}
static int __modified_chen_cache_set_bm(DL_CACHE *cache, PAGE page_number,
					OFFSET offset_block, OFFSET offset_bit, int value,struct bio* main_bio)
{
	PAGE index;
	BLOCK old_value, new_value;

	/* Get the index from the cache holding required page */
	if (!modified_chen_cache_get_cache_index(cache, page_number, &index,main_bio))
		return 0;

	/* Add the mapping */
	old_value = cache->pages[index]->mappings->entries[offset_block];
 	//printk(KERN_ALERT "CHEN: page_number=%d, offset=%d, bm=%llx ", page_number, offset_block, old_value);
	if (value == 0){
		new_value = BIT_CLEAR(old_value, offset_bit);
		cache->pages[index]->mappings->entries[offset_block] = new_value;
 		//printk(KERN_ALERT "CHEN: offset_bit=%d, old bm=%llx, value=%d, new bm=%llx \n", offset_bit, old_value, value, new_value);
	}
	else if (value == 1){
		new_value = BIT_SET(old_value, offset_bit);
		cache->pages[index]->mappings->entries[offset_block] = new_value;
		//printk(KERN_ALERT "CHEN: offset_bit=%d, old bm=%llx, value=%d, new bm=%llx \n", offset_bit, old_value, value, new_value);
	}
	else
		return 0;

	/* Mark dirty */
	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}

//CHEN
static int __chen_cache_set_bm(DL_CACHE *cache, PAGE page_number,
					OFFSET offset_block, OFFSET offset_bit, int value)
{
	PAGE index;
	BLOCK old_value, new_value;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_cache_index(cache, page_number, &index))
		return 0;

	/* Add the mapping */
	old_value = cache->pages[index]->mappings->entries[offset_block];
 	//printk(KERN_ALERT "CHEN: page_number=%d, offset=%d, bm=%llx ", page_number, offset_block, old_value);
	if (value == 0){
		new_value = BIT_CLEAR(old_value, offset_bit);
		cache->pages[index]->mappings->entries[offset_block] = new_value;
 		//printk(KERN_ALERT "CHEN: offset_bit=%d, old bm=%llx, value=%d, new bm=%llx \n", offset_bit, old_value, value, new_value);
	}
	else if (value == 1){
		new_value = BIT_SET(old_value, offset_bit);
		cache->pages[index]->mappings->entries[offset_block] = new_value;
		//printk(KERN_ALERT "CHEN: offset_bit=%d, old bm=%llx, value=%d, new bm=%llx \n", offset_bit, old_value, value, new_value);
	}
	else
		return 0;

	/* Mark dirty */
	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}

int modified_chen_cache_set_bm(DL_CACHE *cache, BLOCK key,struct bio* main_bio)
{
	PAGE page_number;
	OFFSET offset_block;
	OFFSET offset_bit;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number_bm(key);
	offset_block = chen_cache_page_offset_bm(key);
	offset_bit = chen_cache_block_offset_bm(key);

// 	printk(KERN_INFO "CHEN: set_bm %llu\n", key);
	return __modified_chen_cache_set_bm(cache, page_number, offset_block, offset_bit, 1,main_bio);
}
//CHEN
int chen_cache_set_bm(DL_CACHE *cache, BLOCK key)
{
	PAGE page_number;
	OFFSET offset_block;
	OFFSET offset_bit;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number_bm(key);
	offset_block = chen_cache_page_offset_bm(key);
	offset_bit = chen_cache_block_offset_bm(key);

// 	printk(KERN_INFO "CHEN: set_bm %llu\n", key);
	return __chen_cache_set_bm(cache, page_number, offset_block, offset_bit, 1);
}
//CHEN
int chen_cache_clear_bm(DL_CACHE *cache, BLOCK key)
{
	PAGE page_number;
	OFFSET offset_block;
	OFFSET offset_bit;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number_bm(key);
	offset_block = chen_cache_page_offset_bm(key);
	offset_bit = chen_cache_block_offset_bm(key);

// 	printk(KERN_INFO "CHEN: clear_bm %llu\n", key);
	return __chen_cache_set_bm(cache, page_number, offset_block, offset_bit, 0);
}

int modified_chen_cache_clear_bm(DL_CACHE *cache, BLOCK key,struct bio* main_bio)
{
	PAGE page_number;
	OFFSET offset_block;
	OFFSET offset_bit;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number_bm(key);
	offset_block = chen_cache_page_offset_bm(key);
	offset_bit = chen_cache_block_offset_bm(key);

// 	printk(KERN_INFO "CHEN: clear_bm %llu\n", key);
	return __modified_chen_cache_set_bm(cache, page_number, offset_block, offset_bit, 0,main_bio);
}
static int modified_chen_cache_save_ptm_page(DL_CACHE *cache, PAGE index, BLOCK pbn,struct bio* main_bio)
{
//printk("nittin %d\n",__LINE__);	
	DL_CACHE_PAGE *cache_page;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* Write page to disk */
	if (!modified_chen_write_block(cache->dl_dev, (void *) cache_page->mappings->entries,
				  pbn, cache->cache_type,main_bio))
{
//printk("nittin %d\n",__LINE__);	
		goto bad;
}

// 	/* Mark as clean */
// 	chen_cache_page_mark_clean(cache_page);

	return 1;

bad:
//printk("nittin %d\n",__LINE__);	
	return 0;
}

static int chen_cache_save_ptm_page(DL_CACHE *cache, PAGE index, BLOCK pbn)
{
	DL_CACHE_PAGE *cache_page;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* Write page to disk */
	if (!chen_write_block(cache->dl_dev, (void *) cache_page->mappings->entries,
				  pbn, cache->cache_type))
		goto bad;

// 	/* Mark as clean */
// 	chen_cache_page_mark_clean(cache_page);

	return 1;

bad:
	return 0;
}
#if 0
static int modified_chen_cache_load_ptm_page(DL_CACHE *cache, PAGE index, PAGE page_number, BLOCK pbn,struct bio* main_bio)
{
	DL_CACHE_PAGE *cache_page;
	OFFSET i;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* TODO Warn if page is dirty */

	chen_cache_page_mark_invalid(cache_page);
	cache_page->page_number = page_number;

	/* pbn doesn't exist, create a ptm node with invalid mappings*/
	if (pbn == INVALID_BLOCK)
	{
// 		printk(KERN_INFO "CHEN: ptm node not exist in %llu\n", (long long unsigned) pbn);
		for (i = 0; i < MAPPINGS_PER_CACHE_PAGE; ++i)
			cache_page->mappings->entries[i] = INVALID_BLOCK;
	}
	else
	{
		//printk(KERN_INFO "CHEN: ptm node read from disk %llu\n", (long long unsigned) pbn);
		/* Read page from disk */
		if (!modified_chen_read_block(cache->dl_dev, (void *) cache_page->mappings->entries,pbn, cache->cache_type,main_bio))
			goto bad;
	}

	/* Mark as clean and valid */
	chen_cache_page_mark_clean(cache_page);
	chen_cache_page_mark_valid(cache_page);

	return 1;

bad:
	printk(KERN_ALERT "CHEN: fail to read ptm node in %llu\n", (long long unsigned) pbn);
	return 0;
}
#endif
//CHEN
static int chen_cache_load_ptm_page(DL_CACHE *cache, PAGE index, PAGE page_number, BLOCK pbn)
{
	DL_CACHE_PAGE *cache_page;
	OFFSET i;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* TODO Warn if page is dirty */

	chen_cache_page_mark_invalid(cache_page);
	cache_page->page_number = page_number;

	/* pbn doesn't exist, create a ptm node with invalid mappings*/
	if (pbn == INVALID_BLOCK)
	{
// 		printk(KERN_INFO "CHEN: ptm node not exist in %llu\n", (long long unsigned) pbn);
		for (i = 0; i < MAPPINGS_PER_CACHE_PAGE; ++i)
			cache_page->mappings->entries[i] = INVALID_BLOCK;
	}
	else
	{
		//printk(KERN_INFO "CHEN: ptm node read from disk %llu\n", (long long unsigned) pbn);
		/* Read page from disk */
		if (!chen_read_block(cache->dl_dev, (void *) cache_page->mappings->entries,
					pbn, cache->cache_type))
			goto bad;
	}

	/* Mark as clean and valid */
	chen_cache_page_mark_clean(cache_page);
	chen_cache_page_mark_valid(cache_page);

	return 1;

bad:
	printk(KERN_ALERT "CHEN: fail to read ptm node in %llu\n", (long long unsigned) pbn);
	return 0;
}
//CHEN
//CHEN
#if 0
static int modified_chen_cache_get_ptm_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index, BLOCK pbn,struct bio* main_bio)
{
	PAGE i, new_index;

	/* Sanity */
	if (!cache || !index)
		goto bad;

	/* Search for the page in cache */
	for (i = 0; i < cache->nr_pages; ++i)
		if (cache->pages[i]->page_number == page_number) {
			*index = i;
			break;
		}

	/* If not found in cache */
	if (i == cache->nr_pages) {
// 	printk(KERN_INFO "CHEN: node not found in cache\n");
		/* Identify index for replacement */
		new_index = find_replacement_index(cache);

// 		/* Save the page at the index */
// 		if (cache->pages[new_index]->is_dirty)
// 			if (!chen_cache_save_page(cache, new_index))
// 				goto bad;

		/* Load the required page into the index */
		if (!modified_chen_cache_load_ptm_page(cache, new_index, page_number, pbn,main_bio))
			goto bad;

// 		cache->pages[new_index]->page_number = page_number;
		/* Set the index */
		*index = new_index;
	}

	return 1;

bad:
	return 0;
}
#endif
static int chen_cache_get_ptm_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index, BLOCK pbn)
{
	PAGE i, new_index;

	/* Sanity */
	if (!cache || !index)
		goto bad;

	/* Search for the page in cache */
	for (i = 0; i < cache->nr_pages; ++i)
		if (cache->pages[i]->page_number == page_number) {
			*index = i;
			break;
		}

	/* If not found in cache */
	if (i == cache->nr_pages) {
// 	printk(KERN_INFO "CHEN: node not found in cache\n");
		/* Identify index for replacement */
		new_index = find_replacement_index(cache);

// 		/* Save the page at the index */
// 		if (cache->pages[new_index]->is_dirty)
// 			if (!chen_cache_save_page(cache, new_index))
// 				goto bad;

		/* Load the required page into the index */
		if (!chen_cache_load_ptm_page(cache, new_index, page_number, pbn))
			goto bad;

// 		cache->pages[new_index]->page_number = page_number;
		/* Set the index */
		*index = new_index;
	}

	return 1;

bad:
	return 0;
}
//CHEN
#if 0
int modified_chen_cache_get_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK *value,struct bio* main_bio)
{
	PAGE index;

 	//printk(KERN_INFO "CHEN: cache_get_ptm, The map node at pbn=%llu, page_number=%u, offset=%u\n", (long long unsigned int) pbn, page_number, offset);
	/* Sanity */
	if (!cache || !cache->pages || page_number >= cache->max_page_number)
		return 0;

	/* Get the index from the cache holding required page */
	if (!modified_chen_cache_get_ptm_cache_index(cache, page_number, &index, pbn,main_bio))
		return 0;
	*value = cache->pages[index]->mappings->entries[offset];

	return 1;
}
#endif
int chen_cache_get_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK *value)
{
	PAGE index;

 	//printk(KERN_INFO "CHEN: cache_get_ptm, The map node at pbn=%llu, page_number=%u, offset=%u\n", (long long unsigned int) pbn, page_number, offset);
	/* Sanity */
	if (!cache || !cache->pages || page_number >= cache->max_page_number)
		return 0;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_ptm_cache_index(cache, page_number, &index, pbn))
		return 0;
	*value = cache->pages[index]->mappings->entries[offset];

	return 1;
}
int modified_chen_cache_update_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK value, BLOCK new_pbn, BLOCK new_lbn, unsigned depth, BLOCK *child_pbn,struct bio* main_bio)
{
	PAGE index;

	/* Sanity */
	if (!cache || !cache->pages || page_number >= cache->max_page_number)
		return 0;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_ptm_cache_index(cache, page_number, &index, pbn))
		return 0;

	/* Update the mapping */
	*child_pbn = cache->pages[index]->mappings->entries[offset];
	cache->pages[index]->mappings->entries[offset] = value;
	//printk(KERN_INFO "CHEN: Update map node at depth=%u, pbn=%llu to %llu, page_number=%u, offset=%u, old_entry=%llu, new_entry=%llu\n", depth, (long long unsigned int) pbn, new_pbn, page_number, offset, *child_pbn, value);

	if(depth == 1)
		cache->pages[index]->mappings->entries[FAN_OUT-1] = new_lbn;

	/* Write the ptm to the disk */
	if (!modified_chen_cache_save_ptm_page(cache, index, new_pbn,main_bio))
		return 0;

// 	/* Mark dirty */
// 	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}
//CHEN
int chen_cache_update_ptm(DL_CACHE *cache, BLOCK pbn, PAGE page_number, OFFSET offset, BLOCK value, BLOCK new_pbn, BLOCK new_lbn, unsigned depth, BLOCK *child_pbn)
{
	PAGE index;

	/* Sanity */
	if (!cache || !cache->pages || page_number >= cache->max_page_number)
		return 0;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_ptm_cache_index(cache, page_number, &index, pbn))
		return 0;

	/* Update the mapping */
	*child_pbn = cache->pages[index]->mappings->entries[offset];
	cache->pages[index]->mappings->entries[offset] = value;
	//printk(KERN_INFO "CHEN: Update map node at depth=%u, pbn=%llu to %llu, page_number=%u, offset=%u, old_entry=%llu, new_entry=%llu\n", depth, (long long unsigned int) pbn, new_pbn, page_number, offset, *child_pbn, value);

	if(depth == 1)
		cache->pages[index]->mappings->entries[FAN_OUT-1] = new_lbn;

	/* Write the ptm to the disk */
	if (!chen_cache_save_ptm_page(cache, index, new_pbn))
		return 0;

// 	/* Mark dirty */
// 	chen_cache_page_mark_dirty(cache->pages[index]);

	return 1;
}

int modified_chen_cache_add_mapping(DL_CACHE *cache, BLOCK key, BLOCK value,struct bio* main_bio)
{
	return modified_chen_cache_set_mapping(cache, key, value,main_bio);
}

int chen_cache_add_mapping(DL_CACHE *cache, BLOCK key, BLOCK value)
{
	return chen_cache_set_mapping(cache, key, value);
}

int modified_chen_cache_remove_mapping(DL_CACHE *cache, BLOCK key,struct bio* main_bio)
{
	return modified_chen_cache_set_mapping(cache, key, INVALID_BLOCK,main_bio);
}

int chen_cache_remove_mapping(DL_CACHE *cache, BLOCK key)
{
	return chen_cache_set_mapping(cache, key, INVALID_BLOCK);
}

static int chen_cache_set_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					       OFFSET offset, BLOCK value)
{
	/* Sanity */
	if (!cache || !cache->pages)
		goto bad;

	/* Boundary checks */
	if (page_number > cache->max_page_number || offset >= MAPPINGS_PER_CACHE_PAGE)
		goto bad;

	return __chen_cache_set_mapping(cache, page_number, offset, value);

bad:
	return 0;
}

int chen_cache_add_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK value)
{
	return chen_cache_set_mapping_at_coord(cache, page_number, offset, value);
}

int chen_cache_remove_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					   OFFSET offset)
{
	return chen_cache_set_mapping_at_coord(cache, page_number, offset, INVALID_BLOCK);
}

static int __modified_chen_cache_get_mapping(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK *value,struct bio* main_bio)
{
	PAGE index;

	/* Get the index from the cache holding required page */
	if (!modified_chen_cache_get_cache_index(cache, page_number, &index,main_bio))
		return 0;

	*value = cache->pages[index]->mappings->entries[offset];
	return 1;
}

static int __chen_cache_get_mapping(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK *value)
{
	PAGE index;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_cache_index(cache, page_number, &index))
		return 0;

	*value = cache->pages[index]->mappings->entries[offset];
	return 1;
}

int chen_cache_get_mapping(DL_CACHE *cache, BLOCK key, BLOCK *value)
{
	PAGE page_number;
	OFFSET offset;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number(key);
	offset = chen_cache_page_offset(key);

	return __chen_cache_get_mapping(cache, page_number, offset, value);
}

int modified_chen_cache_get_mapping(DL_CACHE *cache, BLOCK key, BLOCK *value,struct bio* main_bio)
{
	PAGE page_number;
	OFFSET offset;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number(key);
	offset = chen_cache_page_offset(key);

	return __modified_chen_cache_get_mapping(cache, page_number, offset, value,main_bio);
}

//CHEN
static int __chen_cache_get_bm(DL_CACHE *cache, PAGE page_number,
					OFFSET offset_block, OFFSET offset_bit, BLOCK *value)
{
	PAGE index;
	BLOCK values;

	/* Get the index from the cache holding required page */
	if (!chen_cache_get_cache_index(cache, page_number, &index))
		return 0;

	values = cache->pages[index]->mappings->entries[offset_block];
// 	printk(KERN_ALERT "BM value=%llx\n", (long long unsigned)values); 
	if (BIT_CHECK(values, offset_bit))
		*value = 1;
	else
		*value = 0;

	return 1;
}
//CHEN
int chen_cache_get_bm(DL_CACHE *cache, BLOCK key, BLOCK *value)
{
	PAGE page_number;
	OFFSET offset_block;
	OFFSET offset_bit;

	/* Sanity */
	if (!cache || !cache->pages || key >= cache->max_key)
		return 0;

	/* Page number and offset */
	page_number = chen_cache_page_number_bm(key);
	offset_block = chen_cache_page_offset_bm(key);
	offset_bit = chen_cache_block_offset_bm(key);

// 	printk(KERN_ALERT "pbn=%u, page_number=%u, os1=%u, os2=%u", key, page_number, offset_block, offset_bit); 
	return __chen_cache_get_bm(cache, page_number, offset_block, offset_bit, value);
}

int chen_cache_get_mapping_at_coord(DL_CACHE *cache, PAGE page_number,
					OFFSET offset, BLOCK *value)
{
	/* Sanity */
	if (!cache || !cache->pages)
		goto bad;

	/* Boundary checks */
	if (page_number > cache->max_page_number || offset >= MAPPINGS_PER_CACHE_PAGE)
		goto bad;

	return __chen_cache_get_mapping(cache, page_number, offset, value);

bad:
	return 0;
}

/*
 *	Helper functions
 */

static DL_CACHE_PAGE *chen_cache_allocate_page(void)
{
	DL_CACHE_PAGE *page;
	OFFSET i;

	page = kmalloc(sizeof(DL_CACHE_PAGE), GFP_KERNEL);
	if (!page)
		goto bad;

	page->mappings = kzalloc(sizeof(DL_CACHE_MAPPINGS), GFP_KERNEL);
	if (!page->mappings)
		goto bad_free_page;

	page->page_number = INVALID_PAGE;
	page->is_valid = false;
	page->is_dirty = false;

	/* Initailize the mappings as invalid
	 * TODO Improve */
	for (i = 0; i < MAPPINGS_PER_CACHE_PAGE; ++i)
		page->mappings->entries[i] = INVALID_BLOCK;	//CHEN: all bits are "1"

	return page;

bad_free_page:
	kfree(page);
bad:
	return NULL;
}

static void chen_cache_free_page(DL_CACHE_PAGE *page)
{
	if (!page)
		return;

	kfree(page->mappings);
	kfree(page);
}


static int chen_cache_save_page(DL_CACHE *cache, PAGE index)
{
	DL_CACHE_PAGE *cache_page;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* Write page to disk */
	if (!chen_write_block(cache->dl_dev, (void *) cache_page->mappings->entries,
				  (BLOCK) cache_page->page_number, cache->cache_type))
		goto bad;

	/* Mark as clean */
	chen_cache_page_mark_clean(cache_page);

	return 1;

bad:
	return 0;
}

static int chen_cache_load_page(DL_CACHE *cache, PAGE index, PAGE page_number)
{
	DL_CACHE_PAGE *cache_page;

	/* Sanity */
	if (!cache || index >= cache->nr_pages)
		goto bad;

	cache_page = cache->pages[index];

	/* TODO Warn if page is dirty */

	chen_cache_page_mark_invalid(cache_page);
	cache_page->page_number = page_number;

	/* Read page from disk */
	if (!chen_read_block(cache->dl_dev, (void *) cache_page->mappings->entries,
				 (BLOCK) cache_page->page_number, cache->cache_type))
		goto bad;

	/* Mark as clean and valid */
	chen_cache_page_mark_clean(cache_page);
	chen_cache_page_mark_valid(cache_page);

	return 1;

bad:
	return 0;
}

static int chen_cache_get_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index)
{
	PAGE i, new_index;

	/* Sanity */
	if (!cache || !index)
		goto bad;

	/* Search for the page in cache */
	for (i = 0; i < cache->nr_pages; ++i)
		if (cache->pages[i]->page_number == page_number) {
			*index = i;
			break;
		}

	/* If not found in cache */
	if (i == cache->nr_pages) {
		/* Identify index for replacement */
		new_index = find_replacement_index(cache);

		/* Save the page at the index */
		if (cache->pages[new_index]->is_dirty)
			if (!chen_cache_save_page(cache, new_index))
				goto bad;

		/* Load the required page into the index */
		if (!chen_cache_load_page(cache, new_index, page_number))
			goto bad;

		/* Set the index */
		*index = new_index;
	}

	return 1;

bad:
	return 0;
}
 int modified_chen_cache_get_cache_index(DL_CACHE *cache, PAGE page_number, PAGE *index,struct bio* main_bio)
{
	PAGE i, new_index;

	/* Sanity */
	if (!cache || !index)
		goto bad;
//printk("nittin %d\n",__LINE__);	

	/* Search for the page in cache */
	for (i = 0; i < cache->nr_pages; ++i)
		if (cache->pages[i]->page_number == page_number) {
			*index = i;
			break;
		}
//printk("nittin %d\n",__LINE__);	
	/* If not found in cache */
	if (i == cache->nr_pages) {
		/* Identify index for replacement */
		new_index = find_replacement_index(cache);

		/* Save the page at the index */
		if (cache->pages[new_index]->is_dirty)
			if (!modified_chen_cache_save_page(cache, new_index,main_bio))
{
//printk("nittin %d\n",__LINE__);	
				goto bad;
}
		/* Load the required page into the index */
		if (!chen_cache_load_page(cache, new_index, page_number))
			goto bad;

		/* Set the index */
		*index = new_index;
	}

	return 1;

bad:
//printk("nittin %d\n",__LINE__);	
	return 0;
}


