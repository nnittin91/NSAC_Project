/*
 * ===========================================================================
 *
 *       Filename:  dm-chen-target.c
 *
 *    Description:  Device Mapper Target Kernel Module for SEQ
 *
 *        Version:  1.0
 *        Created:  03/28/2016
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Chen 
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

/* TODO Remove unwanted includes */
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/workqueue.h>
#include <linux/backing-dev.h>
#include <linux/percpu.h>
#include <linux/atomic.h>
#include <linux/scatterlist.h>
#include <asm/page.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/md5.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/device-mapper.h>
#include <linux/semaphore.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "chen_pbm.h"
#include "chen_ppm.h"
#include "chen_ptm.h"
// #include "chen_pfl.h"
// #include "chen_fbis.h"
// #include "chen_oram.h"
#include "chen_common.h"

/* General */
#define DATALAIR_PROC_FILE "chentab"
#define DM_MSG_PREFIX "chen"
#define DATALAIR_PUBLIC_VOLUME 0
#define DATALAIR_PRIVATE_VOLUME 1
#define DATALAIR_ONLY_PUBLIC 0
#define DATALAIR_PUBLIC_PRIVATE 1
#define DATALAIR_KEY_LEN 32 /* in bytes */
#define DATALAIR_IV_LEN 16 /* in bytes */
#define DATALAIR_ENCRYPT 0
#define DATALAIR_DECRYPT 1
#define DATALAIR_DO_CRYPT 1
#define DATALAIR_DO_LINEAR 0
#define DATALAIR_SECTOR 4096 
#define SEQ_MAP_HEIGHT 3 
#define SEQ_SPACE_OVERHEAD 1 

/* Device */
#define DATALAIR_DEVICE_NAME_LEN 16

/* Volume */
#define DATALAIR_MAX_VOLUMES 4

/* Memory pools */
#define DATALAIR_BIOSET_SIZE 64
#define DATALAIR_PAGE_POOL_SIZE 128
#define DATALAIR_IO_POOL_SIZE 16

static inline BLOCK sector2block(SECTOR sector)
{
	return (BLOCK)(sector >> LOG_SECTORS_PER_BLOCK);
}
static inline SECTOR block2sector(BLOCK block)
{
	return (SECTOR)(block << LOG_SECTORS_PER_BLOCK);
}
static inline SECTOR offset_in_block(SECTOR sector)
{
	return (SECTOR)(sector % SECTORS_PER_BLOCK);
}

struct chen_io;
struct chen_volume;
static void kchend_queue_crypt(struct chen_io *);
static void chen_io_read(struct chen_io *);
static void chen_io_write(struct chen_io *);
static void chen_io_init(struct chen_io *io, struct bio *bio_orig);
static void kchend_crypt_read_convert(struct chen_io *);
static void kchend_crypt_write_convert(struct chen_io *);
static struct chen_crypto_context *chen_crypto_context_init(void);
int __modified_chen_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw,struct bio* main_bio);
 int __chen_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw);
 int __chen_multi_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw,int num);

/************************************ Chen : Data struct def **************************************/
struct chen_crypto_context {
	struct crypto_blkcipher *tfm;
	u8 *key;
	u8 *iv;
	struct semaphore crypto_lock;
};

struct chen_device {
	dev_t dev;
	char device_name[DATALAIR_DEVICE_NAME_LEN];

	/* SEQ */
	BLOCK current_pub_pbn;
	BLOCK end_pub_pbn;
	BLOCK current_priv_pbn;
	BLOCK end_priv_pbn;

	/* DATALAIR */
	DL_PBM *pbm;
	DL_PPM *ppm;
	DL_PTM *ptm;
// 	DL_PFL *pfl;
// 	DL_FBIS *fbis;
// 	DL_ORAM *oram;

	/* mempools */
	struct bio_set *bioset;
	mempool_t *page_pool;

	/* Crypto */
	struct chen_crypto_context *dl_cc;

	/* Volumes */
	u32 nr_volumes;
	struct chen_volume *dl_vol[DATALAIR_MAX_VOLUMES];
	unsigned device_type;

	/* List head */
	struct list_head list;

	/* Space management */
	BLOCK device_total_capacity;	/* #blocks on physical device */
	BLOCK device_first_block;	/* Usually 1 */

	BLOCK device_header_start_block;	/* device_first_block */
	BLOCK device_header_block_count;	/* TODO Constant #blocks */

	//CHEN
	BLOCK pbm_start_block;
	BLOCK pbm_block_count;

	BLOCK ppm_start_block;
	BLOCK ppm_block_count;

	BLOCK ptm_root_start_block;
	BLOCK ptm_root_block_count;	/* TODO Constant #blocks */

// 	BLOCK pfl_start_block;
// 	BLOCK pfl_block_count;
// 	BLOCK pfl_block_count_per_ma;	/* #blocks for one mapping array */
// 
// 	BLOCK fbis_meta_start_block;
// 	BLOCK fbis_meta_block_count;
// 
// 	BLOCK fbis_start_block;
// 	BLOCK fbis_block_count;
// 
// 	BLOCK oram_header_start_block;
// 	BLOCK oram_header_block_count;	/* TODO Constant #blocks */

	BLOCK data_total_blocks;	/* N = M + D */
	BLOCK map_block_count;		/* M */
	BLOCK data_block_count;		/* D */
	BLOCK allocated_data_blocks;	/* # blocks allocated to volumes */
	BLOCK data_start_block;		/* First logical data block */
};

struct chen_volume {
	struct chen_device *dl_dev;
	struct dm_dev *dev;

	/* Memory pools */
	struct kmem_cache *_io_pool;
	mempool_t *io_pool;

	/* Workqueue */
	struct workqueue_struct *io_queue;
	struct workqueue_struct *crypt_queue;

	/* Crypto */
	struct chen_crypto_context *dl_cc;

	/* Locks. The crypto_lock is in dl_cc */
	struct semaphore io_lock;

	/* Volume information */
	char *volume_name;
	unsigned volume_index;
	unsigned volume_type;
	BLOCK start_block;
	BLOCK volume_size;
};

static DEFINE_SEMAPHORE(chen_device_lock);
static LIST_HEAD(chen_device_list);

BLOCK chen_data_limit(struct chen_device *dl_dev)
{
	return dl_dev->data_total_blocks;
}

static inline void chen_lbn_to_pbn_linear(struct chen_device *dl_dev, BLOCK lbn, BLOCK *pbn)
{
	*pbn = dl_dev->data_start_block + lbn;
}

static int modified_chen_lbn_to_pbn_public(struct chen_device *dl_dev, BLOCK lbn, BLOCK *pbn, bool is_write, struct bio* main_bio)
{
	BLOCK old_pbn;
	BLOCK new_pbn;
	BLOCK bm;

	/* Sanity */
	if (!dl_dev || !dl_dev->pbm || !dl_dev->ppm)
		return 0;

	/* Check if there is lbn_pbn mapping */
		if(is_write)
	{
	if (!modified_chen_ppm_get_mapping(dl_dev->ppm, lbn, &old_pbn,main_bio))	//No old mapping, no need to invalid pbn
		goto notfound;
	}
	else
	{
	if (!chen_ppm_get_mapping(dl_dev->ppm, lbn, &old_pbn))	//No old mapping, no need to invalid pbn
		goto notfound;
	
	}

	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and old_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) old_pbn);
	if(!is_write)
	{
		new_pbn = old_pbn;
		goto found_read;
	}

	/* Invalid the pbn according to the old mapping */
	if(is_write)
	{
	if (!modified_chen_pbm_set_bm(dl_dev->pbm, old_pbn,main_bio))
		return 0;
	}
		else
	{
	if (!chen_pbm_set_bm(dl_dev->pbm, old_pbn))
		return 0;
	}
notfound:
	/* Get a new block for public */
	if (DEBUG_ON)
	DMERR("CHEN: 257 current_pub_pbn = %llu", (long long unsigned) dl_dev->current_pub_pbn);
	if (!chen_get_one_new_public_block(dl_dev->pbm, dl_dev->current_pub_pbn, &new_pbn))
		return 0;
	/* Add the mapping to ppm */
	if(is_write)
	{	
	if (!modified_chen_ppm_add_mapping(dl_dev->ppm, lbn, new_pbn,main_bio))
		return 0;
	}
	else
	{
	if (!chen_ppm_add_mapping(dl_dev->ppm, lbn, new_pbn))
		return 0;
	
	}
// 		goto bad_add_mapping;

	/* Mark the block as used in PBM */
	if(is_write)
	{		
	if (!modified_chen_pbm_clear_bm(dl_dev->pbm, new_pbn,main_bio))
//		if (FATAL_ON)
{
			DMERR("Unable to mark pbn = %llu as used in pbm",
				(long long unsigned int) new_pbn);
}
}
else
{
	if (!chen_pbm_clear_bm(dl_dev->pbm, new_pbn))
//		if (FATAL_ON)
{
			DMERR("Unable to mark pbn = %llu as used in pbm",
				(long long unsigned int) new_pbn);
}
}

	dl_dev->current_pub_pbn = (new_pbn + TUPLE_SIZE) % (dl_dev->data_total_blocks);

found_read:
	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and new_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) new_pbn);
	*pbn = dl_dev->data_start_block + new_pbn;
// 	DMERR("pbn = %llu returned", (long long unsigned) *pbn);
// 	DMERR("CHEN: current_pub_pbn = %llu after", (long long unsigned) dl_dev->current_pub_pbn);
	return 1;
}

//CHEN
static int chen_lbn_to_pbn_public(struct chen_device *dl_dev, BLOCK lbn, BLOCK *pbn, bool is_write)
{
	BLOCK old_pbn;
	BLOCK new_pbn;
	BLOCK bm;
	/* Sanity */
	if (!dl_dev || !dl_dev->pbm || !dl_dev->ppm)
		return 0;

	/* Check if there is lbn_pbn mapping */
	if (!chen_ppm_get_mapping(dl_dev->ppm, lbn, &old_pbn))	//No old mapping, no need to invalid pbn
		goto notfound;

	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and old_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) old_pbn);
	if(!is_write)
	{
		new_pbn = old_pbn;
		goto found_read;
	}

	/* Invalid the pbn according to the old mapping */
	if (!chen_pbm_set_bm(dl_dev->pbm, old_pbn))
		return 0;

notfound:
	/* Get a new block for public */
	if (DEBUG_ON)
	DMERR("CHEN: current_pub_pbn = %llu", (long long unsigned) dl_dev->current_pub_pbn);
	if (!chen_get_one_new_public_block(dl_dev->pbm, dl_dev->current_pub_pbn, &new_pbn))
		return 0;

	/* Add the mapping to ppm */
	if (!chen_ppm_add_mapping(dl_dev->ppm, lbn, new_pbn))
		return 0;
// 		goto bad_add_mapping;

	/* Mark the block as used in PBM */
	if (!chen_pbm_clear_bm(dl_dev->pbm, new_pbn))
//		if (FATAL_ON)
{
			DMERR("Unable to mark pbn = %llu as used in pbm",
				(long long unsigned int) new_pbn);
}

	dl_dev->current_pub_pbn = (new_pbn + TUPLE_SIZE) % (dl_dev->data_total_blocks);

found_read:
	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and new_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) new_pbn);
	*pbn = dl_dev->data_start_block + new_pbn;
 	DMERR("pbn = %llu returned", (long long unsigned) *pbn);
 	DMERR("CHEN: current_pub_pbn = %llu after", (long long unsigned) dl_dev->current_pub_pbn);
	return 1;
}

static void modified_chen_lbn_to_pbn_private(struct chen_device *dl_dev, BLOCK lbn, BLOCK *pbn, bool is_write,struct bio* main_bio)
{
	BLOCK new_pbn;

	/* Sanity */
	if (!dl_dev || !dl_dev->ptm)
	{
		goto bad;
	}

	if(is_write)
		goto write_to_new_block;

	/* Look into the ptm for the mapping */
	if (DEBUG_ON)
		DMERR("CHEN: op=%s, Read the old version for lbn = %llu", (is_write?"W":"R"), lbn);
//	if(is_write)
//	{
	if (chen_ptm_read(dl_dev->ptm, lbn, &new_pbn))
	{
		if (DEBUG_ON)
			DMERR("Mapping lbn = %llu and old_pbn = %llu",
					(long long unsigned int) lbn,
					(long long unsigned int) new_pbn);

		if(new_pbn != INVALID_BLOCK)
			goto found;
	}
//	}
//	else
//	{
//	if (modified_chen_ptm_read(dl_dev->ptm, lbn, &new_pbn))
//	{
//		if (DEBUG_ON)
//			DMERR("Mapping lbn = %llu and old_pbn = %llu",
//					(long long unsigned int) lbn,
//					(long long unsigned int) new_pbn);

//		if(new_pbn != INVALID_BLOCK)
//			goto found;
//	}
	
//	}

write_to_new_block:
	/* Get a new block */
		if (DEBUG_ON)
		DMERR("CHEN: get_new_block, current_priv_pbn = %llu", (long long unsigned) dl_dev->current_priv_pbn);
//	if(is_write)
//	{
	if (!chen_get_one_new_private_block(dl_dev->ptm, dl_dev->current_priv_pbn, &new_pbn))
	{
		DMERR("Cannot get a new private block.");
		goto bad;
	}
//	}
//	else
//	{
//	if (!modified_chen_get_one_new_private_block(dl_dev->ptm, dl_dev->current_priv_pbn, &new_pbn))
//	{
//		DMERR("Cannot get a new private block.");
//		goto bad;
//	}
	
//	}
	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and new_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) new_pbn);

	new_pbn = new_pbn + TUPLE_SIZE - 2;
	if (DEBUG_ON)
		DMERR("CHEN: Write the PTM for lbn = %llu", lbn);
	/* Set the mapping in ptm */
	if(is_write)
	{	
	if (!modified_chen_ptm_write(dl_dev->ptm, lbn, new_pbn,main_bio))
		{
		goto bad_free_new_block;
		}
	}
	else
	{
	if (!chen_ptm_write(dl_dev->ptm, lbn, new_pbn))
		{
		goto bad_free_new_block;
		}
	}
	dl_dev->current_priv_pbn = (new_pbn + 2) % (dl_dev->data_total_blocks);

found:
	*pbn = dl_dev->data_start_block + new_pbn;
// 	DMERR("pbn = %llu returned", new_pbn+TUPLE_SIZE-1 );
// 	DMERR("CHEN: current_priv_pbn = %llu after", (long long unsigned) dl_dev->current_priv_pbn);
	return;

bad_free_new_block:
	/* TODO Return the new block to fbis */
bad:
	if (FATAL_ON)
		DMERR("Unable to get pbn for lbn = %llu",
			(long long unsigned int) lbn);
	chen_lbn_to_pbn_linear(dl_dev, lbn, pbn);

	return;
// 	*pbn = dl_dev->data_start_block + lbn;
// 	return;
}
static void chen_lbn_to_pbn_private(struct chen_device *dl_dev, BLOCK lbn, BLOCK *pbn, bool is_write)
{
	BLOCK new_pbn;

	/* Sanity */
	if (!dl_dev || !dl_dev->ptm)
		goto bad;

	if(is_write)
		goto write_to_new_block;

	/* Look into the ptm for the mapping */
//	if (DEBUG_ON)
		DMERR("CHEN: op=%s, Read the old version for lbn = %llu", (is_write?"W":"R"), lbn);
	if (chen_ptm_read(dl_dev->ptm, lbn, &new_pbn))
	{
	//	if (DEBUG_ON)
			DMERR("Mapping lbn = %llu and old_pbn = %llu",
					(long long unsigned int) lbn,
					(long long unsigned int) new_pbn);

		if(new_pbn != INVALID_BLOCK)
			goto found;
	}

write_to_new_block:
	/* Get a new block */
		if (DEBUG_ON)
 	DMERR("CHEN: get_new_block, current_priv_pbn = %llu", (long long unsigned) dl_dev->current_priv_pbn);
	if (!chen_get_one_new_private_block(dl_dev->ptm, dl_dev->current_priv_pbn, &new_pbn))
	{
		DMERR("Cannot get a new private block.");
		goto bad;
	}

	if (DEBUG_ON)
		DMERR("Mapping lbn = %llu and new_pbn = %llu",
				(long long unsigned int) lbn,
				(long long unsigned int) new_pbn);

	new_pbn = new_pbn + TUPLE_SIZE - 2;
//	if (DEBUG_ON)
		DMERR("CHEN: Write the PTM for lbn = %llu", lbn);
	/* Set the mapping in ptm */
	if (!chen_ptm_write(dl_dev->ptm, lbn, new_pbn))
		goto bad_free_new_block;

	dl_dev->current_priv_pbn = (new_pbn + 2) % (dl_dev->data_total_blocks);

found:
	*pbn = dl_dev->data_start_block + new_pbn;
 	DMERR("pbn = %llu returned", new_pbn+TUPLE_SIZE-1 );
 	DMERR("CHEN: current_priv_pbn = %llu after", (long long unsigned) dl_dev->current_priv_pbn);
	return;

bad_free_new_block:
	/* TODO Return the new block to fbis */
bad:
//	if (FATAL_ON)
		DMERR("Unable to get pbn for lbn = %llu",
			(long long unsigned int) lbn);
	chen_lbn_to_pbn_linear(dl_dev, lbn, pbn);

	return;
// 	*pbn = dl_dev->data_start_block + lbn;
// 	return;
}
static inline struct page *chen_alloc_page(struct chen_device *dl_dev)
{
	return mempool_alloc(dl_dev->page_pool, GFP_KERNEL);
}
static SECTOR modified_chen_lsec_to_psec(struct chen_volume *dl_vol, SECTOR lsec, bool is_write, struct bio* main_bio)
{
	BLOCK lbn, pbn;
	/* Get lbn */
	lbn = dl_vol->start_block + sector2block(lsec);

	/* Get pbn */
	pbn = INVALID_BLOCK;
#if DATALAIR_DO_LINEAR
	chen_lbn_to_pbn_linear(dl_vol->dl_dev, lbn, &pbn);
#else
	if (dl_vol->volume_type == DATALAIR_PUBLIC_VOLUME)
{
		char * dl_data=NULL;
	if(is_write)
{
	int ret=0;
		modified_chen_lbn_to_pbn_public(dl_vol->dl_dev, lbn, &pbn, is_write,main_bio);
	ret = __chen_multi_read_write_block(dl_vol->dl_dev, (void *) dl_data, pbn+1, DATALAIR_READ,2);
	if(ret)
	{
	 ret = __modified_chen_read_write_block(dl_vol->dl_dev, (void *) dl_data, pbn+1, DATALAIR_WRITE,main_bio);
//	 ret = __chen_read_write_block(dl_vol->dl_dev, (void *) dl_data, pbn+2, DATALAIR_READ);
	 ret = __modified_chen_read_write_block(dl_vol->dl_dev, (void *) (dl_data+4096), pbn+2, DATALAIR_WRITE,main_bio);
	}
}
else
{
		modified_chen_lbn_to_pbn_public(dl_vol->dl_dev, lbn, &pbn, is_write,main_bio);

}			
}	
else
{
		modified_chen_lbn_to_pbn_private(dl_vol->dl_dev, lbn, &pbn, is_write,main_bio);
}

#endif

	/* Return the sector */
	return block2sector(pbn) + offset_in_block(lsec);
}
static SECTOR chen_lsec_to_psec(struct chen_volume *dl_vol, SECTOR lsec, bool is_write)
{
	BLOCK lbn, pbn;

	/* Get lbn */
	lbn = dl_vol->start_block + sector2block(lsec);
	printk(KERN_ERR"lbn is %lu\n",lbn);
	/* Get pbn */
	pbn = INVALID_BLOCK;
#if DATALAIR_DO_LINEAR
	chen_lbn_to_pbn_linear(dl_vol->dl_dev, lbn, &pbn);
#else
	if (dl_vol->volume_type == DATALAIR_PUBLIC_VOLUME)
		chen_lbn_to_pbn_public(dl_vol->dl_dev, lbn, &pbn, is_write);
	else
		chen_lbn_to_pbn_private(dl_vol->dl_dev, lbn, &pbn, is_write);
#endif

	/* Return the sector */
	return block2sector(pbn) + offset_in_block(lsec);
}

static inline struct block_device *chen_get_bdev(struct chen_device *dl_dev)
{
	return dl_dev->dl_vol[0]->dev->bdev;
}

static inline struct bio *chen_alloc_bio(struct chen_device *dl_dev)
{
	return bio_alloc_bioset(GFP_KERNEL, 2, dl_dev->bioset);
}


static inline void chen_free_page(struct chen_device *dl_dev, struct page *p)
{
	mempool_free(p, dl_dev->page_pool);
}


static int page_number_to_pbn(struct chen_device *dl_dev, unsigned int block_type,
			      BLOCK page_number, BLOCK *pbn)
{
	switch(block_type) {
		case DEVICE_HEADER_BLOCK:
			*pbn = dl_dev->device_header_start_block + page_number;
			break;
		case PBM_BLOCK:	//CHEN
			*pbn = dl_dev->pbm_start_block + page_number;
			break;
		case PPM_BLOCK:
			*pbn = dl_dev->ppm_start_block + page_number;
			break;
// 		case PFL_FWD_MA_BLOCK:
// 			*pbn = dl_dev->pfl_start_block + page_number;
// 			break;
// 		case PFL_REV_MA_BLOCK:
// 			*pbn = dl_dev->pfl_start_block + dl_dev->pfl_block_count_per_ma + page_number;
// 			break;
// 		case FBIS_META_BLOCK:
// 			*pbn = dl_dev->fbis_meta_start_block + page_number;
// 			break;
// 		case FBIS_BLOCK:
// 			*pbn = dl_dev->fbis_start_block + page_number;
// 			break;
		case PTM_ROOT_BLOCK:
			*pbn = dl_dev->ptm_root_start_block + page_number;
			break;
		case PTM_BLOCK:
			*pbn = dl_dev->data_start_block + page_number;
			break;
		default:
			goto bad;
	}

	if (DEBUG_ON)
		DMERR("RW Disk: page_number=%llu, pbn = %llu block_type=%u", page_number, *pbn, block_type);
	return 1;

bad:
	return 0;
}
 int __chen_multi_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw,int num)
{
	struct bio *bio;
	struct page *p;
	int i=0;
	/* Sanity */
	if (!dl_dev )
		goto bad;
	struct page *read_page=chen_alloc_page(dl_dev);
	
	bio = chen_alloc_bio(dl_dev);

	if (rw == DATALAIR_READ)
		bio->bi_rw &= ~REQ_WRITE;
	else if (rw == DATALAIR_WRITE)
		bio->bi_rw |= REQ_WRITE;
	else
		goto bad_free_bio;

	bio->bi_bdev = chen_get_bdev(dl_dev);
	data=kmap(read_page);
	char* local_data=(char*)data;
	bio->bi_sector = (sector_t) block2sector(pbn+i);
	for(;i<num;i++)
	{
	if (DEBUG_ON)
		DMERR("RW Disk: pbn=%llu, sector = %llu", pbn+i, bio->bi_sector);

	p = virt_to_page(data);
	bio_add_page(bio, p, PAGE_SIZE, i);
	data+=4096;
	read_page=chen_alloc_page(dl_dev);
	data=kmap(read_page);
	}

	submit_bio_wait(0, bio);

	if (!bio_flagged(bio, BIO_UPTODATE))
		goto bad_free_bio;

	bio_put(bio);	//Need to be done after a chen_alloc_bio()
	data=local_data;
	return 1;

bad_free_bio:
	bio_put(bio);
bad:
	DMCRIT("ERROR: read_write_block pbn = %llu rw = %d",
		(unsigned long long) pbn, rw);
	return 0;
}
 int __chen_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw)
{
	struct bio *bio;
	struct page *p;
	/* Sanity */
	if (!dl_dev || !data)
		goto bad;

	bio = chen_alloc_bio(dl_dev);

	if (rw == DATALAIR_READ)
		bio->bi_rw &= ~REQ_WRITE;
	else if (rw == DATALAIR_WRITE)
		bio->bi_rw |= REQ_WRITE;
	else
		goto bad_free_bio;

	bio->bi_bdev = chen_get_bdev(dl_dev);
	bio->bi_sector = (sector_t) block2sector(pbn);
	if (DEBUG_ON)
		DMERR("RW Disk: pbn=%llu, sector = %llu", pbn, bio->bi_sector);

	p = virt_to_page(data);
	bio_add_page(bio, p, PAGE_SIZE, 0);

	submit_bio_wait(0, bio);

	if (!bio_flagged(bio, BIO_UPTODATE))
		goto bad_free_bio;

	bio_put(bio);	//Need to be done after a chen_alloc_bio()
	return 1;

bad_free_bio:
	bio_put(bio);
bad:
	DMCRIT("ERROR: read_write_block pbn = %llu rw = %d",
		(unsigned long long) pbn, rw);
	return 0;
}
 int __modified_chen_read_write_block(struct chen_device *dl_dev, void *data, BLOCK pbn, int rw,struct bio* main_bio)
{
	struct bio *bio;
	struct page *p;

	/* Sanity */
	if (!dl_dev || !data)
		goto bad;

	bio = chen_alloc_bio(dl_dev);

	if (rw == DATALAIR_READ)
		bio->bi_rw &= ~REQ_WRITE;
	else if (rw == DATALAIR_WRITE)
		bio->bi_rw |= REQ_WRITE;
	else
		goto bad_free_bio;

	bio->bi_bdev = chen_get_bdev(dl_dev);
	bio->bi_sector = (sector_t) block2sector(pbn);
	if (DEBUG_ON)
		DMERR("RW Disk: pbn=%llu, sector = %llu", pbn, bio->bi_sector);

	p = virt_to_page(data);
	
	if(rw!=DATALAIR_READ)
{
									bio_add_page(main_bio,p,PAGE_SIZE,0);
return 1;
}
	else
								bio_add_page(bio, p, PAGE_SIZE, 0);


	if(rw==DATALAIR_READ)
	{
	submit_bio_wait(0, bio);

	if (!bio_flagged(bio, BIO_UPTODATE))
		goto bad_free_bio;

	bio_put(bio);	//Need to be done after a chen_alloc_bio()
	return 1;

bad_free_bio:
	bio_put(bio);
bad:
	DMCRIT("ERROR: read_write_block pbn = %llu rw = %d",
		(unsigned long long) pbn, rw);
	}
	return 0;
}
int modified_chen_write_block(struct chen_device *dl_dev, void *data,
			 BLOCK page_number, unsigned int block_type,struct bio* main_bio)
{
	BLOCK pbn;
	struct page *p;
	char *disk_data;
	char *dl_data;
	int ret;

	if (!dl_dev)
		goto bad;

	if (!page_number_to_pbn(dl_dev, block_type, page_number, &pbn))
		goto bad;

	/* Allocate page */
	p = chen_alloc_page(dl_dev);
	disk_data = kmap(p);

	/* Copy data to page */
	dl_data = (char *) data;
	memcpy(disk_data, dl_data, PAGE_SIZE);

	ret = __modified_chen_read_write_block(dl_dev, (void *) disk_data, pbn, DATALAIR_WRITE,main_bio);

	/* Free the page */
	kunmap(p);
	chen_free_page(dl_dev, p);

	return ret;

bad:
	return 0;
}

int chen_write_block(struct chen_device *dl_dev, void *data,
			 BLOCK page_number, unsigned int block_type)
{
	BLOCK pbn;
	struct page *p;
	char *disk_data;
	char *dl_data;
	int ret;

	if (!dl_dev)
		goto bad;

	if (!page_number_to_pbn(dl_dev, block_type, page_number, &pbn))
		goto bad;

	/* Allocate page */
	p = chen_alloc_page(dl_dev);
	disk_data = kmap(p);

	/* Copy data to page */
	dl_data = (char *) data;
	memcpy(disk_data, dl_data, PAGE_SIZE);

	ret = __chen_read_write_block(dl_dev, (void *) disk_data, pbn, DATALAIR_WRITE);

	/* Free the page */
	kunmap(p);
	chen_free_page(dl_dev, p);

	return ret;

bad:
	return 0;
}
#if 0
int modified_chen_read_block(struct chen_device *dl_dev, void *data,
			BLOCK page_number, unsigned int block_type,struct bio* main_bio)
{
	BLOCK pbn;
	struct page *p;
	char *disk_data;
	char *dl_data;
	int ret;

	/* Sanity */
	if (!dl_dev || !data)
		goto bad;

	if (!page_number_to_pbn(dl_dev, block_type, page_number, &pbn))
		goto bad;

	/* Allocate page */
	p = chen_alloc_page(dl_dev);
	disk_data = kmap(p);
	//printk("nittin %d\n",__LINE__);

	ret = __modified_chen_read_write_block(dl_dev, (void *) disk_data, pbn, DATALAIR_READ,main_bio);

	/* Copy from page to data */
	dl_data = (char *) data;
	memcpy(dl_data, disk_data, PAGE_SIZE);

	/* Free the page */
	kunmap(p);
	chen_free_page(dl_dev, p);

	return ret;

bad:
	return 0;
}
#endif

int chen_read_block(struct chen_device *dl_dev, void *data,
			BLOCK page_number, unsigned int block_type)
{
	BLOCK pbn;
	struct page *p;
	char *disk_data;
	char *dl_data;
	int ret;

	/* Sanity */
	if (!dl_dev || !data)
		goto bad;

	if (!page_number_to_pbn(dl_dev, block_type, page_number, &pbn))
		goto bad;

	/* Allocate page */
	p = chen_alloc_page(dl_dev);
	disk_data = kmap(p);

	ret = __chen_read_write_block(dl_dev, (void *) disk_data, pbn, DATALAIR_READ);

	/* Copy from page to data */
	dl_data = (char *) data;
	memcpy(dl_data, disk_data, PAGE_SIZE);

	/* Free the page */
	kunmap(p);
	chen_free_page(dl_dev, p);

	return ret;

bad:
	return 0;
}

/*
 * DATALAIR IO and related functions
 */

struct chen_io {
	struct chen_volume *dl_vol;
	struct bio *base_bio;
	struct work_struct work;

	/* Original bio end function */
	void *orig_bi_private;
	bio_end_io_t *orig_end_io;

	atomic_t io_pending;
	int error;
	SECTOR sector;
	struct chen_io *base_io;
};

static struct chen_io *chen_io_alloc(struct chen_volume *dl_vol,
					     struct bio *bio, SECTOR lsec)
{
	struct chen_io *io;
	//unsigned i;

	io = mempool_alloc(dl_vol->io_pool, GFP_NOIO);
	io->dl_vol = dl_vol;
	io->base_bio = bio;
	io->sector = lsec;
	io->error = 0;
	io->base_io = NULL;
	atomic_set(&io->io_pending, 0);

#if 0
	/* Print */
	if (bio->bi_vcnt == 0)
		goto end;

	if (bio->bi_vcnt == 1 && bio->bi_io_vec[0].bv_len == DATALAIR_SECTOR)
		goto end;

	for (i = 0; i < bio->bi_vcnt; ++i)
		DMCRIT("Out-of-shape bio: sector = %llu vec_index = %u length = %u", bio->bi_sector, i, bio->bi_io_vec[i].bv_len);
#endif

// end:
	return io;
}

static void chen_dec_pending(struct chen_io *io)
{
	struct chen_volume *dl_vol = io->dl_vol;
	struct bio *base_bio = io->base_bio;
	struct chen_io *base_io = io->base_io;
	//unsigned rw = bio_data_dir(base_bio);
	//unsigned rw;
	int error = io->error;

	if (!atomic_dec_and_test(&io->io_pending))
		return;

	/* Free the io object */
	mempool_free(io, dl_vol->io_pool);

	if (!base_io) {
		/* Only applicable for READ operation
		 * For write operation not required as the
		 * make_generic_request is directly called on the bio
		 */
		//rw = bio_data_dir(base_bio);
		//if (rw == READ)
			bio_endio(base_bio, error);
	} else {
		/*
		 * TODO We will be having an base_io when we split the io
		 * per bi_vector. Till then having base_io is an error
		 */
		base_io->error = error;
		chen_dec_pending(base_io);
	}
}

static void chen_inc_pending(struct chen_io *io)
{
	atomic_inc(&io->io_pending);
}

static void chen_endio(struct bio *clone, int error)
{
	struct chen_io *io = clone->bi_private;
	struct bio *bio_orig = io->base_bio;
	unsigned rw = bio_data_dir(clone);

	if (unlikely(!bio_flagged(clone, BIO_UPTODATE) && !error))
		error = -EIO;

	/* If the base_io is null then this is user's bio
	 * Restore the bio to original state */
	if (!io->base_io) {
		bio_orig->bi_private = io->orig_bi_private;
		bio_orig->bi_end_io = io->orig_end_io;
	} else {
		if (rw == WRITE) {
			chen_free_page(io->dl_vol->dl_dev, bio_orig->bi_io_vec[0].bv_page);
			bio_put(bio_orig);
		} else {
			DMCRIT("Got a read operation on clone with base_io");
			error = -EIO;
		}
	}

	if (rw == READ && !error) {
		kchend_queue_crypt(io);
		return;
	}

	if (unlikely(error))
		io->error = error;

	chen_dec_pending(io);
}

/*
 * Workqueue related functions
 */

static void kchend_io(struct work_struct *work)
{
	struct chen_io *io = container_of(work, struct chen_io, work);

	if (bio_data_dir(io->base_bio) == READ)
		{
		chen_io_read(io);
		}
	else
		{
		chen_io_write(io);
		}
}

static void kchend_queue_io(struct chen_io *io)
{
	INIT_WORK(&io->work, kchend_io);
	queue_work(io->dl_vol->io_queue, &io->work);
}

static void kchend_crypt(struct work_struct *work)
{
	struct chen_io *io = container_of(work, struct chen_io, work);

	if (bio_data_dir(io->base_bio) == READ)
		kchend_crypt_read_convert(io);
	else
		kchend_crypt_write_convert(io);
}

static void kchend_queue_crypt(struct chen_io *io)
{
	INIT_WORK(&io->work, kchend_crypt);
	queue_work(io->dl_vol->crypt_queue, &io->work);
}

/*
 * Crypto functions
 */

static void chen_do_crypto(struct chen_crypto_context *dl_cc,
			       struct scatterlist *src, struct scatterlist *dst,
			       u64 len, unsigned crypt_op_type)
{
	struct blkcipher_desc desc;

	/* Lock */
	down(&dl_cc->crypto_lock);

	/* Set IV */
	crypto_blkcipher_set_iv(dl_cc->tfm, dl_cc->iv, DATALAIR_IV_LEN);

	/* Crypt */
	desc.tfm = dl_cc->tfm;
	desc.flags = 0;

	if (crypt_op_type == DATALAIR_ENCRYPT) {
		if (crypto_blkcipher_encrypt(&desc, dst, src, len))
			DMCRIT("Error doing encryption");
	} else {
		if (crypto_blkcipher_decrypt(&desc, dst, src, len))
			DMCRIT("Error doing decryption");
	}

	/* Unlock */
	up(&dl_cc->crypto_lock);
}

static void chen_do_crypto_from_buffer(struct chen_crypto_context *dl_cc,
					   char *in_buf, char *out_buf,
					   u64 len, unsigned crypt_op_type)
{
	struct scatterlist src;
	struct scatterlist dst;

	sg_init_table(&src, 1);
	sg_init_table(&dst, 1);
	sg_set_buf(&src, in_buf, len);
	sg_set_buf(&dst, out_buf, len);

	chen_do_crypto(dl_cc, &src, &dst, len, crypt_op_type);
}

static void chen_do_crypto_from_page_inline(struct chen_crypto_context *dl_cc,
					 struct page *p, unsigned offset,
					 u64 len, unsigned crypt_op_type)
{
	char *buf;

	/* Get buffer */
	buf = ((char *) kmap(p)) + offset;

	/* Apply crypto */
	chen_do_crypto_from_buffer(dl_cc, buf, buf, len, crypt_op_type);

	/* Return buffer */
	kunmap(p);
}

static void chen_do_crypto_from_page(struct chen_crypto_context *dl_cc,
					 struct page *in_p, unsigned in_offset,
					 struct page *out_p, unsigned out_offset,
					 u64 len, unsigned crypt_op_type)
{
	char *in_buf;
	char *out_buf;

	/* Get buffer */
	in_buf = ((char *) kmap(in_p)) + in_offset;
	out_buf = ((char *) kmap(out_p)) + out_offset;

	/* Apply crypto */
	chen_do_crypto_from_buffer(dl_cc, in_buf, out_buf, len, crypt_op_type);

	/* Return buffer */
	kunmap(out_p);
	kunmap(in_p);
}

static void kchend_crypt_read_convert(struct chen_io *io)
{
// 	DMCRIT("CHEN: crypt_read");	//CHEN
#if DATALAIR_DO_CRYPT
	unsigned i, j;

	/* Crypt Logic */
	struct bio *bio = io->base_bio;

	for (i = 0; i < bio->bi_vcnt; ++i)
		for (j = 0; j < bio->bi_io_vec[i].bv_len; j += DATALAIR_SECTOR)
			chen_do_crypto_from_page_inline(io->dl_vol->dl_cc,
							    bio->bi_io_vec[i].bv_page,
							    bio->bi_io_vec[i].bv_offset + j,
							    DATALAIR_SECTOR, DATALAIR_DECRYPT);
#endif
	/* read and crypt are done */
	chen_dec_pending(io);
}

static void kchend_crypt_write_convert_old(struct chen_io *io)
{
#if DATALAIR_DO_CRYPT
	unsigned i, j;

	/* Crypt Logic */
	struct bio *bio = io->base_bio;

	for (i = 0; i < bio->bi_vcnt; ++i)
		for (j = 0; j < bio->bi_io_vec[i].bv_len; j += DATALAIR_SECTOR)
			chen_do_crypto_from_page_inline(io->dl_vol->dl_cc,
						     bio->bi_io_vec[i].bv_page,
						     bio->bi_io_vec[i].bv_offset + j,
						     DATALAIR_SECTOR, DATALAIR_ENCRYPT);

#endif

	/* Perform write IO */
	kchend_queue_io(io);
}

static void kchend_crypt_write_convert(struct chen_io *io)
{
// 	DMCRIT("CHEN: crypt_write");	//CHEN
#if DATALAIR_DO_CRYPT
	unsigned i, j;

	/* Crypt Logic */
	struct bio *bio = io->base_bio;
	struct bio *new_bio;
	struct chen_io *new_io;
	struct page *new_page;
	sector_t off = 0;

	for (i = 0; i < bio->bi_vcnt; ++i) {

		/* Set the off */
		if (i == 0)
			off = 0;
		else
			off += bio->bi_io_vec[i-1].bv_len;

		for (j = 0; j < bio->bi_io_vec[i].bv_len; j += DATALAIR_SECTOR) {

			/* Get a new bio */
			new_bio = chen_alloc_bio(io->dl_vol->dl_dev);
			if (!new_bio) {
				io->error = 1;
				goto end;
			}

			/* Fill the details */
			new_bio->bi_rw |= REQ_WRITE;
			new_bio->bi_bdev = bio->bi_bdev;
			new_bio->bi_sector = bio->bi_sector + ((off + j) / 512);	

			/* Add a page */
			new_page = chen_alloc_page(io->dl_vol->dl_dev);
			bio_add_page(new_bio, new_page, DATALAIR_SECTOR, 0);

			/* Encrypt */
			chen_do_crypto_from_page(io->dl_vol->dl_cc,
						     bio->bi_io_vec[i].bv_page,
						     bio->bi_io_vec[i].bv_offset + j,
						     new_bio->bi_io_vec[0].bv_page,
						     new_bio->bi_io_vec[0].bv_offset,
						     DATALAIR_SECTOR, DATALAIR_ENCRYPT);

			/* Make and submit the io */
			new_io = chen_io_alloc(io->dl_vol, new_bio, io->sector + ((off + j) / 512));
			new_io->base_io = io;
			chen_io_init(new_io, new_bio);
			chen_inc_pending(new_io);
			chen_inc_pending(new_io);
			chen_inc_pending(io);
			kchend_queue_io(new_io);
	if (DEBUG_ON)
		DMERR("Write after crypt, new_bio_sector=%llu", new_bio->bi_sector);
		}
	}
#endif

	/* Perform write IO */
	//kchend_queue_io(io);

end:
	chen_dec_pending(io);
}

/*
 * DATALAIR IO functions
 */

static void chen_io_init(struct chen_io *io, struct bio *bio_orig)
{
	io->orig_bi_private = bio_orig->bi_private;
	bio_orig->bi_private = io;
	io->orig_end_io = bio_orig->bi_end_io;
	bio_orig->bi_end_io = chen_endio;
}

static void chen_io_write(struct chen_io *io)
{
	struct bio *bio_orig = io->base_bio;
	down(&io->dl_vol->io_lock); 
//	bio_orig->bi_sector = chen_lsec_to_psec(io->dl_vol, io->sector, true);
	bio_orig->bi_sector =modified_chen_lsec_to_psec(io->dl_vol,io->sector,true,bio_orig);
 	DMCRIT("CHEN: io_write, bi_sector=%llu", bio_orig->bi_sector);	//CHEN
	up(&io->dl_vol->io_lock); 
	generic_make_request(bio_orig);

	/* crypt + write done */
	chen_dec_pending(io);

	return;
}

static void chen_io_read(struct chen_io *io)
{
	struct bio *bio_orig = io->base_bio;
	chen_io_init(io, bio_orig);
	down(&io->dl_vol->io_lock); 
	bio_orig->bi_sector = chen_lsec_to_psec(io->dl_vol, io->sector, false);
//	bio_orig->bi_sector =modified_chen_lsec_to_psec(io->dl_vol,io->sector,false,bio_orig);
 	DMCRIT("CHEN: io_read, bi_sector=%llu", bio_orig->bi_sector);	//CHEN
	up(&io->dl_vol->io_lock); 
	generic_make_request(bio_orig);
	return;
}

/*
 * DATALAIR Device - DATALAIR Volume functions
 */

static int chen_get_bdev_capacity(char *dev_path, BLOCK *block_count)
{
	struct block_device *bdev;

	bdev = lookup_bdev(dev_path);
	if (IS_ERR(bdev))
		return 0;

	/*
	 * i_size_read give the capacity in bytes
	 * shift right by 9 converts the capacity to number of sectors
	 * then convert the sectors to blocks
	 */
	*block_count = sector2block((SECTOR)(i_size_read(bdev->bd_inode) >> 9));
	bdput(bdev);

	return 1;
}

static struct chen_crypto_context *chen_crypto_context_init(void) 
{
	struct chen_crypto_context *dl_cc;

	/* Allocate crypto_context */
	dl_cc = kmalloc(sizeof(*dl_cc), GFP_KERNEL);
	if (!dl_cc)
		goto bad;

	/* Allocate the key */
	dl_cc->key = kmalloc(DATALAIR_KEY_LEN * sizeof(u8), GFP_KERNEL);
	if (!dl_cc->key)
		goto bad_free_cc;

	/* Initailize the key */
	get_random_bytes(dl_cc->key, DATALAIR_KEY_LEN * sizeof(u8));

	/* Allocate the iv */
	dl_cc->iv = kmalloc(DATALAIR_IV_LEN * sizeof(u8), GFP_KERNEL);
	if (!dl_cc->iv)
		goto bad_free_key;

	/* Initailize the iv */
	get_random_bytes(dl_cc->iv, DATALAIR_IV_LEN * sizeof(u8));

	/* Allocate tfm */
// 	dl_cc->tfm = crypto_alloc_blkcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	dl_cc->tfm = crypto_alloc_blkcipher("cbc(aes)", 0, 0);
	if (IS_ERR(dl_cc->tfm))
		goto bad_free_iv;

	/* Set the key in tfm */
	if (crypto_blkcipher_setkey(dl_cc->tfm, dl_cc->key, DATALAIR_KEY_LEN))
		goto bad_free_tfm;

	/* Initailize crypto_lock */
	sema_init(&dl_cc->crypto_lock, 1);

	/* Return */
	return dl_cc;

bad_free_tfm:
	crypto_free_blkcipher(dl_cc->tfm);
bad_free_iv:
	kfree(dl_cc->iv);
bad_free_key:
	kfree(dl_cc->key);
bad_free_cc:
	kfree(dl_cc);
bad:
	return NULL;
}

static void chen_space_management(struct chen_device *dl_dev, unsigned device_type, BLOCK data_size, double p)
{

/*
 *			Space management overview
 *			-------------------------
 *
 * Top - Logical View
 *		          		   First logical data blocks
 *				             |
 *				             V
 * -----------------------------------------------------------------
 * |   X   |   Y   |  O  | M |          D                           |
 * -----------------------------------------------------------------
 * |       |       |     | B |                                      |
 * |       |       |  O  | L |                                      |
 * |  PBM  |  PPM  |  T  | A |           Data Blocks                |
 * |  (N)  |  (N)  |  H  | N |                                      |
 * |       |       |     | K |                                      |
 * |================================================================|
 * |       |       |     |                                          |
 * |       |       |  O  |                                          |
 * |  PBM  |  PPM  |  T  |           Map + Data Blocks              |
 * |       |       |  H  |                                          |
 * |       |       |     |                                          |
 * -----------------------------------------------------------------
 * |   X   |   Y   |  O  |             N = M + D                    |
 * -----------------------------------------------------------------
 *                       |
 *                       V
 *                    First physical data block
 * Bottom - Physical view
 *
 * */

	/* Set device type */
	dl_dev->device_type = device_type;

	/* First physical block */
	dl_dev->device_first_block = 1;

	/*
	 * TODO Make more accurate calculations
	 * Block counts
	 */
	dl_dev->device_total_capacity -= 2000;	//CHEN?
	dl_dev->data_total_blocks = (BLOCK) (data_size * TUPLE_SIZE * p);
	dl_dev->device_header_block_count = 1;
	dl_dev->pbm_block_count = ceil(dl_dev->data_total_blocks, BITMAPS_PER_PBM_PAGE);	//Use BITMAPS_PER_CACHE_PAGE?
	dl_dev->ppm_block_count = ceil(dl_dev->data_total_blocks, MAPPINGS_PER_PPM_PAGE);
	dl_dev->ptm_root_block_count = 1;
// 	dl_dev->pfl_block_count_per_ma = ceil(dl_dev->device_total_capacity, MAPPINGS_PER_PFL_PAGE);
// 	dl_dev->pfl_block_count = 2 * dl_dev->pfl_block_count_per_ma;
// 	dl_dev->fbis_meta_block_count = 1;
// 	dl_dev->fbis_block_count = ceil(dl_dev->device_total_capacity, FBIS_ENTRIES_PER_PAGE);

// 	/* Data blocks or ORAM blocks */
// 	dl_dev->data_total_blocks = dl_dev->device_total_capacity - (dl_dev->device_header_block_count +
// 								     dl_dev->pbm_block_count +
// 								     dl_dev->ppm_block_count /*+
// 								     dl_dev->pfl_block_count +
// 								     dl_dev->fbis_meta_block_count +
// 								     dl_dev->fbis_block_count*/ +
// 								     dl_dev->ptm_root_block_count);
	dl_dev->map_block_count = 5 * ceil(dl_dev->device_total_capacity, FAN_OUT);	//the map for the hidden volume? 5 is the height of the tree?
	dl_dev->data_block_count =  dl_dev->data_total_blocks - dl_dev->map_block_count;
	dl_dev->allocated_data_blocks = 0;

	/* Start blocks */
	dl_dev->device_header_start_block = dl_dev->device_first_block;
	dl_dev->pbm_start_block = dl_dev->device_header_start_block + dl_dev->device_header_block_count;
	dl_dev->ppm_start_block = dl_dev->pbm_start_block + dl_dev->pbm_block_count;
	dl_dev->ptm_root_start_block = dl_dev->ppm_start_block + dl_dev->ppm_block_count;
 	dl_dev->data_start_block = dl_dev->ptm_root_start_block + dl_dev->ptm_root_block_count;
// 	dl_dev->pfl_start_block = dl_dev->ppm_start_block + dl_dev->ppm_block_count;
// 	dl_dev->fbis_meta_start_block = dl_dev->pfl_start_block + dl_dev->pfl_block_count;
// 	dl_dev->fbis_start_block = dl_dev->fbis_meta_start_block + dl_dev->fbis_meta_block_count;
// 	dl_dev->oram_header_start_block = dl_dev->fbis_start_block + dl_dev->fbis_block_count;
// 	dl_dev->data_start_block = dl_dev->oram_header_start_block + dl_dev->oram_header_block_count;

	if (DEBUG_ON)
		DMERR("head= %llu, pbm_start=%llu, ppm_start=%llu, ptm_start=%llu, data_start=%llu, data_total=%llu", dl_dev->device_header_start_block, dl_dev->pbm_start_block, dl_dev->ppm_start_block, dl_dev->ptm_root_start_block, dl_dev->data_start_block, dl_dev->data_total_blocks);
}

static void chen_volume_set_path(char *path, struct chen_volume *dl_vol)
{
	dl_vol->volume_name = kmalloc(strlen(path) + 1, GFP_KERNEL);
	strcpy(dl_vol->volume_name, path);
}

static int chen_bdev_lookup(char *dev_path, dev_t *dev)
{
	struct block_device *bdev;

	bdev = lookup_bdev(dev_path);
	if (IS_ERR(bdev))
		return 0;

	*dev = bdev->bd_dev;
	bdput(bdev);

	return 1;
}

static int chen_device_lookup_count(char *dev_path)
{
	struct chen_device *cur;
	dev_t dev;
	int count = 0;

	if (!chen_bdev_lookup(dev_path, &dev))
		goto out;

	list_for_each_entry(cur, &chen_device_list, list)
		if (cur->dev == dev)
			count++;

	DMCRIT("CHEN: dev count = %d", count);	//CHEN
out:
	return count;
}

static struct chen_device *chen_device_lookup(char *dev_path, char *device_name)
{
	struct chen_device *cur;
	dev_t dev;

	if (!chen_bdev_lookup(dev_path, &dev)) {
		DMCRIT("Device lookup failed");
		return NULL;
	}

	list_for_each_entry(cur, &chen_device_list, list)
		if (cur->dev == dev &&
		    strncmp(cur->device_name, device_name,
			    DATALAIR_DEVICE_NAME_LEN) == 0)
		{
			DMCRIT("current device name=device name");	//CHEN
			return cur;
		}

	return NULL;
}

static void chen_device_destroy(struct chen_device *dl_dev)
{
	unsigned i;

	if (!dl_dev)
		return;

	/* deallocate PPM, PFL & FBIS */
	/* deallocate PBM & PPM */
	if (dl_dev->pbm)
		chen_pbm_destroy(dl_dev->pbm);
	if (dl_dev->ppm)
		chen_ppm_destroy(dl_dev->ppm);
	if (dl_dev->ptm)
		chen_ptm_destroy(dl_dev->ptm);
// 	if (dl_dev->pfl)
// 		chen_pfl_destroy(dl_dev->pfl);
// 	if (dl_dev->fbis)
// 		chen_fbis_destroy(dl_dev->fbis);
// 	if (dl_dev->oram)
// 		chen_oram_destroy(dl_dev->oram);

	/* Free bioset */
	if (dl_dev->bioset)
		bioset_free(dl_dev->bioset);

	if (dl_dev->page_pool)
		mempool_destroy(dl_dev->page_pool);

	/* Deallocate DLV */
	for (i = 0; i < dl_dev->nr_volumes; ++i)
		kfree(dl_dev->dl_vol[i]);

	list_del(&dl_dev->list);
	kfree(dl_dev);
}

static int chen_device_initialize(struct chen_device *dl_dev)
{
	/* Allocate PBM */
	dl_dev->pbm = chen_pbm_init(dl_dev, dl_dev->data_total_blocks);
	if (!dl_dev->pbm) {
		DMCRIT("Failed to allocate PBM");
		goto bad;
	}

	/* Allocate PPM */
	dl_dev->ppm = chen_ppm_init(dl_dev, dl_dev->data_total_blocks);
	if (!dl_dev->ppm) {
		DMCRIT("Failed to allocate PPM");
		goto bad;
	}

	/* Allocate PTM */
	dl_dev->ptm = chen_ptm_init(dl_dev, dl_dev->data_total_blocks);
	if (!dl_dev->ptm) {
		DMCRIT("Failed to allocate PTM");
		goto bad;
	}

// 	/* Allocate PFL */
// 	dl_dev->pfl = chen_pfl_init(dl_dev, dl_dev->data_total_blocks);
// 	if (!dl_dev->pfl) {
// 		DMCRIT("Failed to allocate PFL");
// 		goto bad;
// 	}
// 
// 	/* Allocate FBIS */
// 	dl_dev->fbis = chen_fbis_init(dl_dev, dl_dev->data_total_blocks);
// 	if (!dl_dev->fbis) {
// 		DMCRIT("Failed to allocate FBIS");
// 		goto bad;
// 	}
// 
// 	/* Allocate ORAM */
// 	dl_dev->oram = chen_oram_init(dl_dev, dl_dev->fbis, dl_dev->data_block_count);
// 	if (!dl_dev->oram) {
// 		DMCRIT("Failed to allocate ORAM");
// 		goto bad;
// 	}

	return 1;

bad:
	return 0;
}

static int chen_volume_initialize(struct dm_target *ti, struct chen_volume *dl_vol)
{
	/* Sanity */
	if (!dl_vol)
		goto bad;

	/* Mempool */
	dl_vol->_io_pool = KMEM_CACHE(chen_io, 0);
	if (!dl_vol->_io_pool) {
		ti->error = "Cannot allocate chen io memcache";
		goto bad;
	}

	dl_vol->io_pool = mempool_create_slab_pool(DATALAIR_IO_POOL_SIZE,
						dl_vol->_io_pool);
	if (!dl_vol->io_pool) {
		ti->error = "Cannot allocate chen io mempool";
		goto bad_destroy_io_pool_cache;
	}

	/* Create workqueues */
	dl_vol->io_queue = alloc_workqueue("kchend_io", WQ_MEM_RECLAIM, 1);
	if (!dl_vol->io_queue) {
		ti->error = "Cannot allocate kchend_io workqueue";
		goto bad_free_io_pool;
	}

	dl_vol->crypt_queue = alloc_workqueue("kchend_crypt", WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, 1);
	if (!dl_vol->crypt_queue) {
		ti->error = "Cannot allocate kchend_io workqueue";
		goto bad_free_io_queue;
	}

	/* Crypt tfm */
	dl_vol->dl_cc = chen_crypto_context_init();
	if (!dl_vol->dl_cc) {
		ti->error = "Cannot allocate tfm for chen volume";
		goto bad_free_crypt_queue;
	}

	sema_init(&dl_vol->io_lock, 1);

	return 1;

bad_free_crypt_queue:
	destroy_workqueue(dl_vol->crypt_queue);
bad_free_io_queue:
	destroy_workqueue(dl_vol->io_queue);
bad_free_io_pool:
	mempool_destroy(dl_vol->io_pool);
bad_destroy_io_pool_cache:
	kmem_cache_destroy(dl_vol->_io_pool);
bad:
	return 0;
}

static struct chen_device *chen_device_create(char *dev_path, char *device_name)
{
	unsigned i;
	struct chen_device *dl_dev;

	dl_dev = kmalloc(sizeof(*dl_dev), GFP_KERNEL);
	if (!dl_dev)
		goto error;

	INIT_LIST_HEAD(&dl_dev->list);
	list_add(&dl_dev->list, &chen_device_list);

	if (!chen_bdev_lookup(dev_path, &dl_dev->dev)) {
		DMCRIT("Device lookup failed");
		goto bad;
	}
	strncpy(dl_dev->device_name, device_name, DATALAIR_DEVICE_NAME_LEN);
	dl_dev->nr_volumes = 0;

	/* Allocate space for each volume data structure */
	for (i = 0; i < DATALAIR_MAX_VOLUMES; ++i) {
		dl_dev->dl_vol[i] = kmalloc(sizeof(*(dl_dev->dl_vol[0])), GFP_KERNEL);
		if (!dl_dev->dl_vol[i])
			goto bad;
		dl_dev->dl_vol[i]->dl_dev = dl_dev;
	}

	/* Allocate bioset */
	dl_dev->bioset = bioset_create(DATALAIR_BIOSET_SIZE, 0);
	if (!dl_dev->bioset) {
		DMCRIT("Cannot allocate chen bioset");
		goto bad;
	}

	dl_dev->page_pool = mempool_create_page_pool(DATALAIR_PAGE_POOL_SIZE, 0);
	if (!dl_dev->page_pool) {
		DMCRIT("Cannot allocate chen page_pool");
		goto bad;
	}

	/* Initailize the data structres to NULL */
	dl_dev->pbm = NULL;
	dl_dev->ppm = NULL;
	dl_dev->ptm = NULL;
// 	dl_dev->pfl = NULL;
// 	dl_dev->fbis = NULL;
// 	dl_dev->oram = NULL;

	dl_dev->current_pub_pbn = 0;
	dl_dev->current_priv_pbn = 1;
	return dl_dev;

bad:
	chen_device_destroy(dl_dev);
error:
	return NULL;
}

static int chen_device_get_volume(struct chen_device *dl_dev, u32 *index, struct dm_target *ti)
{
	if (!dl_dev) {
		ti->error = "DATALAIR Device is null. UNEXPECTED";
		goto bad;
	}

	if (dl_dev->nr_volumes == DATALAIR_MAX_VOLUMES) {
		ti->error = "DATALAIR Device Maximum volumes created";
		goto bad;
	} else
		*index = dl_dev->nr_volumes++;

	return 1;

bad:
	return 0;
}

static inline void chen_device_put_volume(struct chen_device *dl_dev)
{
	if (!--dl_dev->nr_volumes)
		chen_device_destroy(dl_dev);
}

/*
 * Device Mapper Implementation
 */

static int chen_target_map(struct dm_target *ti, struct bio *bio);
static int chen_ctr(struct dm_target *ti, unsigned int argc, char **argv);
static void chen_dtr(struct dm_target *ti);

static struct target_type chen_target = {
	.name = "chen",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr = chen_ctr,
	.dtr = chen_dtr,
	.map = chen_target_map,
};

static int chen_target_map(struct dm_target *ti, struct bio *bio)
{
	struct chen_io *io;
	struct chen_volume *dl_vol = ti->private;

	bio->bi_bdev = dl_vol->dev->bdev;

	if (unlikely(bio->bi_rw & (REQ_FLUSH | REQ_DISCARD))) {
		bio_endio(bio, 0);
		return DM_MAPIO_SUBMITTED;
	}

	if (bio_sectors(bio)) {		//CHEN: bio_sectors() return the number of sectors in this bio.
		/* Allocate io */
		io = chen_io_alloc(dl_vol, bio, dm_target_offset(ti, bio->bi_sector));

		/* Atleast one operation is pending */
		chen_inc_pending(io);

		/* Add to queues */
		if (bio_data_dir(io->base_bio) == READ)
			kchend_queue_io(io);
		else
			kchend_queue_crypt(io);

		return DM_MAPIO_SUBMITTED;
	}

	return DM_MAPIO_REMAPPED;
}

static int chen_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct chen_device *dl_dev;
	struct chen_volume *dl_vol;
	u32 volume_index;
	char dummy;
	unsigned i;
	unsigned device_type;
	BLOCK start;
	unsigned volume_type;
	BLOCK volume_size;
	long long unsigned int _volume_size;
	double p = 1.5;

	/*
	 *	Arguments for creating the logical volume
	 *
	 *	#	Description			Example
	 * ---------------------------------------------------------------
	 * argv[0] - Physical device path		/dev/sdb1
	 * argv[1] - DATALAIR device name		dl_dev1
	 * argv[2] - DATALAIR volume name		dl_dev1_public_one
	 * argv[3] - Volume type (PUBLIC / PRIVATE)	PUBLIC
	 * argv[4] - device type
	 *	     (ONLY_PUBLIC / PUBLIC_PRIVATE)	PUBLIC_PRIVATE
	 * argv[5] - Volume size in blocks		2000
	 * ---------------------------------------------------------------
	 */
	//printk(KERN_ERR"nittin called\n");
	/* Print the arguments */
	DMCRIT("sizeof(bool)=%d, BIT_PER_LONG=%d, sizeof(BLOCK)=%d", sizeof(bool), BITS_PER_LONG, sizeof(BLOCK));
	DMCRIT("DATALAIR Constructor called with following arguments");
	for (i = 0; i < argc; ++i)
		DMCRIT("Argu Num %d : %s", i, argv[i]);

	/* Sanity */
	if (argc != 6) {
		ti->error = "Invalid argument count";
		goto bad;
	}

	/* Volume type */
	if (strncmp(argv[3], "PUBLIC", 6) == 0)
		volume_type = DATALAIR_PUBLIC_VOLUME;
	else if (strncmp(argv[3], "PRIVATE", 7) == 0)
		volume_type = DATALAIR_PRIVATE_VOLUME;
	else {
		ti->error = "Invalid volume type";
		goto bad;
	}

	/* Device type */
	if (strncmp(argv[4], "ONLY_PUBLIC", 11) == 0)
		device_type = DATALAIR_PUBLIC_VOLUME;
	else if (strncmp(argv[4], "PUBLIC_PRIVATE", 14) == 0)
		device_type = DATALAIR_PRIVATE_VOLUME;
	else {
		ti->error = "Invalid device type";
		goto bad;
	}

	/* Volume size */
	if (sscanf(argv[5], "%llu%c", &_volume_size, &dummy) != 1 || _volume_size == 0) {
		ti->error = "Invalid volume size";
		goto bad;
	}
	volume_size = (BLOCK) _volume_size;
	printk(KERN_ERR"volume size is %lu\n",volume_size);

// 	/* Overprovision */
// // 	if (sscanf(argv[6], "%f%c", &p, &dummy) != 1 || p < 1) {
// 	if (sscanf(argv[6], "%f", &p) != 1 || p < 1) {
// 		ti->error = "Invalid overprovisioning";
// 		goto bad;
// 	}

	/* Lock the device before making any changes to the device */
	down(&chen_device_lock);

	dl_dev = chen_device_lookup(argv[0], argv[1]);
	if (!dl_dev) {
		/*
		 * Check if there is a chen device on the physical device
		 * If there are no chen devices, then create one
		 * else throw error
		 */
		if (chen_device_lookup_count(argv[0]) >= 1) {
			ti->error = "chen device already exists";
			goto bad_unlock_dev;
		}

		/*
		 * ctr function called for the first time
		 * Create the device
		 */
		dl_dev = chen_device_create(argv[0], argv[1]);
		if (!dl_dev) {
			ti->error = "Error creating the chen device";
			goto bad_unlock_dev;
		}

		if (!chen_get_bdev_capacity(argv[0], &dl_dev->device_total_capacity)) {
			ti->error = "Error getting the capacity of bdev";
			goto bad_unlock_dev;
		}

		DMCRIT("Device capacity is %llu 4KB blocks, %llu MB in total",
			(long long unsigned int) dl_dev->device_total_capacity, (long long unsigned int) dl_dev->device_total_capacity * 4/1024);
	}

	if (!chen_device_get_volume(dl_dev, &volume_index, ti))
		goto bad_unlock_dev;

	DMCRIT("Creating volume with index %d", volume_index);
	dl_vol = dl_dev->dl_vol[volume_index];

	/* Real device */
	if (dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &dl_vol->dev)) {
		ti->error = "Real device lookup failed";
		goto bad_put_volume;
	}

	/* First volume */
	if (volume_index == 0) {
		/* First volume should be a public volume */
		if (volume_type != DATALAIR_PUBLIC_VOLUME) {
			ti->error = "First volume should be public";
			goto bad_dm_put_device;
		}
		/* Space management */
		chen_space_management(dl_dev, device_type, volume_size, p);

		/* Initialize */
		if (!chen_device_initialize(dl_dev)) {
			ti->error = "Datalair device initialization failed";
			goto bad_dm_put_device;
		}
	}

// 	/* Allocate data blocks */
// 	if (dl_dev->allocated_data_blocks + volume_size < dl_dev->data_total_blocks) {
// 		start = dl_dev->data_start_block + dl_dev->allocated_data_blocks;
// 		dl_dev->allocated_data_blocks += volume_size;
// 	} else {
// 		ti->error = "Unable to create volume with given size";
// 		goto bad_dm_put_device;
// 	}

	if (dl_dev->data_total_blocks >= (volume_size * TUPLE_SIZE))
	{
		start = 1;
	}
	else
	{
		ti->error = "Unable to create volume with given size since data area is small";
		goto bad_dm_put_device;
	}

	/* Fill volume information */
	dl_vol->start_block = start;
	dl_vol->volume_index = volume_index;
	dl_vol->volume_size = volume_size;
	dl_vol->volume_type = volume_type;
	chen_volume_set_path(argv[2], dl_vol);

	DMCRIT("CHEN: Info about the volume: index=%d, type=%d, size=%d, start block=%llu", volume_index, volume_type, volume_size, (long long unsigned int)start);

	/* Initialize chen volume */
	if (!chen_volume_initialize(ti, dl_vol)) {
		ti->error = "Unable to create volume with given size";
		goto bad_free_allocated_blocks;
	}

	ti->num_discard_bios = 1;
	ti->private = dl_vol;

	DMCRIT("DATALAIR Constructor - Success");
	up(&chen_device_lock);
	return 0;

bad_free_allocated_blocks:
	dl_dev->allocated_data_blocks -= volume_size;
bad_dm_put_device:
	dm_put_device(ti, dl_vol->dev);
bad_put_volume:
	chen_device_put_volume(dl_dev);
bad_unlock_dev:
	DMCRIT("DATALAIR Constructor - Failed");
	up(&chen_device_lock);
bad:
	return -EINVAL;
}

static void chen_dtr(struct dm_target *ti)
{
	struct chen_volume *dl_vol;
	struct chen_device *dl_dev;

	dl_vol = (struct chen_volume *) ti->private;
	dl_dev = dl_vol->dl_dev;

	down(&chen_device_lock);

	DMCRIT("Destructor: Volume:_%s_ at index:_%d_ of Device:_%s_",
		dl_vol->volume_name, dl_vol->volume_index, dl_dev->device_name);

	/* Destory workqueues */
	destroy_workqueue(dl_vol->crypt_queue);
	destroy_workqueue(dl_vol->io_queue);

	/* Free memory pools */
	mempool_destroy(dl_vol->io_pool);
	kmem_cache_destroy(dl_vol->_io_pool);

	/* Free the target */
	dm_put_device(ti, dl_vol->dev);

	/*
	 * Put volume will decrease the number of current volumes on the device
	 *
	 * Caveat:
	 * There is danger if a series of get and put are called on the
	 * device, as the relative positions are not tracked.
	 */
	chen_device_put_volume(dl_dev);

	up(&chen_device_lock);
}

/*
 *	proc related functions
 */

static int chen_list_mounts(struct seq_file *f, void *v)
{
	struct chen_device *cur;
	unsigned i;

	down(&chen_device_lock);

	list_for_each_entry(cur, &chen_device_list, list) {
		seq_printf(f, "%s ", cur->device_name);
		seq_printf(f, "%u ", cur->nr_volumes);
		for (i = 0; i < cur->nr_volumes; ++i)
			seq_printf(f, "%s ", cur->dl_vol[i]->volume_name);
		seq_puts(f, "\n");
	}

	up(&chen_device_lock);
	return 0;
}

static int chen_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, chen_list_mounts, NULL);
}

static const struct file_operations chen_proc_fops = {
	.owner = THIS_MODULE,
	.open = chen_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 *	Device Mapper Kernel Module
 */

static int __init dm_chen_init(void)
{
	int result;

	/* Create proc file */
	if (!proc_create(DATALAIR_PROC_FILE, 0, NULL, &chen_proc_fops)) {
		DMERR("Cannot create proc file for DATALAIR");
		return -ENOMEM;
	}

	/* Register the target */
	result = dm_register_target(&chen_target);
	if (result < 0)
		DMERR("dm_register failed %d", result);

	return result;
}

static void dm_chen_exit(void)
{
	remove_proc_entry(DATALAIR_PROC_FILE, NULL);
	dm_unregister_target(&chen_target);
}

module_init(dm_chen_init);
module_exit(dm_chen_exit);

MODULE_AUTHOR("Vinay Jain <vinay.g.jain@gmail.com>");
MODULE_DESCRIPTION("CHEN");
MODULE_LICENSE("GPL");
