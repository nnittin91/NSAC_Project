/*
 * ===========================================================================
 *
 *       Filename:  chen_common.h
 *
 *    Description:  Header file for common definitions of chen
 *
 *        Version:  1.0
 *        Created:  10/28/2015 02:47:14 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Vinay Jain (VJ), vinay.g.jain@gmail.com
 *   Organization:  NSAC Lab, Stony Brook University
 *
 * ===========================================================================
 */

#ifndef CHEN_COMMON_H /* chen_common.h */
#define CHEN_COMMON_H
struct bio;

/* Sector - Block */
typedef sector_t SECTOR;
typedef sector_t BLOCK;
#define SECTORS_PER_BLOCK	8
#define LOG_SECTORS_PER_BLOCK	3
#define INVALID_SECTOR		ULLONG_MAX
#define INVALID_BLOCK		ULLONG_MAX

/* Persistant storage */
#define DATALAIR_READ		1
#define DATALAIR_WRITE		2

/* Block types */
#define DEVICE_HEADER_BLOCK	1
#define PPM_BLOCK		2
// #define PFL_FWD_MA_BLOCK	3
// #define PFL_REV_MA_BLOCK	4
// #define FBIS_META_BLOCK		5
// #define FBIS_BLOCK		6
// #define ORAM_HEADER_BLOCK	7
// #define ORAM_BLOCK		8
#define PBM_BLOCK		3
#define PTM_ROOT_BLOCK		4
#define PTM_BLOCK		5

/* FBIS Selection types */
#define FBIS_SELECT_FROM_INS_POOL	1
#define FBIS_SELECT_RANDOM_BLOCKS	2

/* Page */
typedef unsigned int PAGE;
typedef unsigned int OFFSET;
#define INVALID_PAGE UINT_MAX
#define PAGESIZE	4096
#define FAN_OUT		(PAGESIZE/sizeof(BLOCK))

/* Debugging */
#define FATAL_ON 1
#define DEBUG_ON 0

/* Other */
#define ceil(n, d) (((n) < 0) ? (-((-(n))/(d))) : (n)/(d) + ((n)%(d) != 0))

#define TUPLE_SIZE 4

struct chen_device;
extern int chen_read_block(struct chen_device *dl_dev, void *data,
			       BLOCK page_number, unsigned int block_type);
extern int chen_write_block(struct chen_device *dl_dev, void *data,
				BLOCK page_number, unsigned int block_type);
extern int modified_chen_write_block(struct chen_device *dl_dev, void *data,
				BLOCK page_number, unsigned int block_type,struct bio* main_bio);
extern BLOCK chen_data_limit(struct chen_device *dl_dev);

#endif /* chen_common.h */
