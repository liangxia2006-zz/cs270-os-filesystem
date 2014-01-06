#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>

#include "monsterfs_funs.h"

static char *storage;                 // pointer to in-memory storage
static unsigned int store_blk_off;    // storage block offset
static int storage_fd;                // on-disk storage file desc

static int max_single = DIRECT_BLKS_PER_INODE + RANGE_SINGLE; // max single indirect blk num
static int max_double = DIRECT_BLKS_PER_INODE + RANGE_SINGLE + RANGE_DOUBLE; // max double indirect blk num
struct namei_cache_element namei_cache[NAMEI_CACHE_SZ];
struct timeval tv;


/********************* Layer0: storage algorithms ***************************/
int init_storage()
{

#if IN_MEM_STORE
  int dev_size = BLK_SZ * NUM_BLKS;

  storage = (char *)malloc(dev_size);
  char *initPtr;
  int i;

  initPtr = &storage[0];

  if(storage != NULL)
  {
    for(i = 0; i < dev_size; ++i)
    {
      initPtr = 0;
      initPtr++;
    }
  }

  storage_fd = IN_MEM_FD;

#else

  storage_fd = open(BLOCK_DEV_PATH, O_RDWR); // | O_CREAT);

  if (storage_fd == -1)
  {
    fprintf(stderr, "error open storage.\n");
    return -ENODEV;
  }
  printf("storage fd = %d\n", storage_fd);

#endif

  init_namei_cache();

  return storage_fd;
}

int cleanup_storage()
{
  int ret_status;

#if IN_MEM_STORE

  free(storage);
  ret_status = 0;

#else

  ret_status = close(storage_fd);
  if (ret_status != 0)
  {
    fprintf(stderr, "close storage error\n");
  }

#endif

  return ret_status;
}

static int reset_storage(void)
{
	int i;
	char buf[BLK_SZ];
	memset(buf, 0, sizeof(buf));
	for (i = 0; i < NUM_BLKS; i++)
	{
		if (bwrite(i, buf) == -1)
		{
			fprintf(stderr, "bwrite error blk#%d in reset_storage()\n", i);
			return -1;
		}
	}
	return 0;
}

int bread(unsigned int blk, char *buffer)
{
  int ret_status;

  if (blk >= NUM_BLKS)
  {
    fprintf(stderr, "bread error: blk num #%d exceeds max block num\n", blk);
    return -1;
  }

#if IN_MEM_STORE

  memcpy(buffer, &storage[blk*BLK_SZ], BLK_SZ);
  ret_status = 0;

#else

  ret_status = lseek(storage_fd, blk * BLK_SZ, SEEK_SET);

  if(ret_status == -1)
    return ret_status;

  ret_status = read(storage_fd, buffer, BLK_SZ);
  if(ret_status != BLK_SZ)
    ret_status = -1;
  else
    ret_status = 0;

#endif

  return ret_status;
}

int bwrite(unsigned int blk, const char *buffer)
{
  int ret_status;

  if (blk >= NUM_BLKS)
  {
    fprintf(stderr, "bwrite error: blk num exceeds max block num\n");
    return -1;
  }

#if IN_MEM_STORE

  memcpy(&storage[blk*BLK_SZ], buffer, BLK_SZ);
  ret_status = 0;

#else

  ret_status = lseek(storage_fd, blk * BLK_SZ, SEEK_SET);

  if(ret_status == -1)
    return ret_status;

  ret_status = write(storage_fd, buffer, BLK_SZ);
  if(ret_status != BLK_SZ)
    ret_status = -1;
  else
    ret_status = 0;

#endif

  return ret_status;
}

/********************* Layer1: block algorithms ***************************/
static struct super_block* super;

static void dump_buffer(char* buf)
{
	int *p = (int*)buf;
	int j;

  for (j = 0; j < BLK_SZ/4; j++)
    printf("%d ", *p++);

  printf("\n");
}

void dump(void)
{
	dump_super();
	//dump_datablks();
}

void dump_super(void)
{
        int i, j;

        printf("\n\ndumping super block...\n\n");
        printf("sizeof superblock = %u\n", sizeof(struct super_block));
        printf("sizeof disk_inode = %u, in_core_inode = %u\n", sizeof(struct disk_inode), sizeof(struct in_core_inode));
        printf("block size = %d, total blocks = %d, filesystem size = %lld\n", super->blk_size, super->num_blks, super->fs_size);
        printf("max free blks = %d, num of free blks = %d\n", super->max_free_blks, super->num_free_blks);
        printf("data block offset = %d\n", super->data_blk_offset);
        printf("free_blk_list_head = %d\n", super->free_blk_list_head);
        printf("next_free_blk_idx = %d\n", super->next_free_blk_idx);
        printf("max free inodes = %d, num of free inodes = %d\n", super->max_free_inodes, super->num_free_inodes);
        printf("remembered inode = %d\n", super->remembered_inode);
        printf("free inode list: ");

        for (i = 0; i < MAX_FREE_ILIST_SIZE; i++)
                printf("%d ", super->free_ilist[i]);

        printf("\nnext_free_inode_idx = %d\n", super->next_free_inode_idx);
}

void dump_datablks(void)
{
      	char buf[BLK_SZ];
	int i;
        for (i = 0; i < NUM_BLKS; i++)
        {
                printf("blk #%d: ", i);
                //int *p = (int*)(disk[i].data);
            		if(bread(i, buf) == -1)
            		{
            			fprintf(stderr, "error: bread failure.\n");
            			return;
             		}
            		dump_buffer(buf);
        }
}

static int update_super(void)
{
        // write superblk to disk
	char buf[BLK_SZ];
        memset(buf, 0, sizeof(buf));
        memcpy(buf, super, sizeof(struct super_block));
        if (bwrite(0, buf) == -1)
        {
                fprintf(stderr, "error: bwrite superblk#0 when update superblk\n");
                return -1;
        }
	return 0;
}

/* create free blk list on top of the disk blocks */
static int init_free_blk_list(void)
{
        int offset = super->data_blk_offset;
        int* p;
        int i;
      	char buf[BLK_SZ];

      	printf("\n\ninit free blk list....\n");
        for (;offset < super->num_blks; offset += FREE_BLKS_PER_LINK)
        {
		memset(buf, 0, sizeof(buf));

            		p = (int*)buf;

                // set next data index block pointer
                if (offset + FREE_BLKS_PER_LINK >= super->num_blks)
                        *p++ = 0; // end of the free blocks?  -JH
                                  // this is the blk_num that points to the next link,
                                  // if there are no more links of free blks, then it should
                                  // be zero, so that you know this is the last link.
                                  // not necessarily, because you can also know from the
                                  // super->num_free_blks that there are no more free blks.
                                  // but just leave it here. We can discuss more later.
                else
                        *p++ = offset + FREE_BLKS_PER_LINK;

                // fill in the data index blocks with free block indices
                for(i = 1; i < FREE_BLKS_PER_LINK && offset + i < super->num_blks; i++)
                        *p++ = offset + i;

                // write data index block back to storage
            		if (bwrite(offset, buf) == -1)
            		{
            			fprintf(stderr, "error: bwrite wrong when init free blk list\n");
            			return -1;
            		}
        }

        printf("complete init free blk list\n");
        return 0;
}

int balloc(void)
{
        int blk_num;
/*
        if (super->locked == 1)
        {
                fprintf(stderr, "error: super block locked\n");
                return -1;
        }
*/
        if (super->num_free_blks == 0)
        {
                fprintf(stderr, "no free block: num_free_blks==0\n");
                return -1;
        }
#if _DEBUG
        printf("  next free blk idx = %d\n", super->next_free_blk_idx);
#endif
      	char buf[BLK_SZ];
	      int old_list_head = super->free_blk_list_head;

        if (bread(old_list_head, buf) == -1)
	      {
		      fprintf(stderr, "error: bread wrong when balloc\n");
		      return -1;
	      }

        if (super->next_free_blk_idx == 0)
        {
                int* freelist_head = (int*)buf;

		            blk_num = super->free_blk_list_head;
                super->free_blk_list_head = freelist_head[0];
#if _DEBUG
                printf("  blk #%d allocated, free_blk_list_head changed\n", blk_num);
#endif
                super->next_free_blk_idx = 1;
        }
        else if (super->next_free_blk_idx < FREE_BLKS_PER_LINK)
        {
                int* freelist_head = (int*)buf;
                blk_num = freelist_head[super->next_free_blk_idx];
                /* This is the case when there are no more free blks in the final
                link. The final blk is the link itself. So, need to change the
                blk_num to the link itself, and update free_blk_list_head. */
                if (blk_num == 0)
                {
                        blk_num = super->free_blk_list_head;
                        super->free_blk_list_head = 0;
                        super->next_free_blk_idx = 1;
#if _DEBUG
                        printf("  blk #%d allocated\n", blk_num);
#endif
                }
                else
                {
                        freelist_head[super->next_free_blk_idx] = 0;
#if _DEBUG
                        printf("  blk #%d allocated\n", blk_num);
#endif
                        super->next_free_blk_idx += 1;
                        super->next_free_blk_idx %= FREE_BLKS_PER_LINK;
                }
        }
        else
        {
                fprintf(stderr, "wrong next_free_blk_idx\n");
                return -1;
        }

      	if (bwrite(old_list_head, buf) == -1)
	      {
		      fprintf(stderr, "error: bwrite when balloc\n");
		      return -1;
	      }

        super->num_free_blks -= 1;
	if (update_super() == -1)
	{
		fprintf(stderr, "update superblk error\n");
		return -1;
	}
/*
        super->locked = 0;
        super->modified = 1;
*/
        return blk_num;
}

int bfree(int blk_num)
{
/*
        if (super->locked == 1)
        {
                fprintf(stderr, "error: super block locked\n");
                return -1;
        }
*/
#if _DEBUG
        printf("  want to free blk_num = %d\n", blk_num);
#endif
        if (blk_num < super->data_blk_offset)
        {
                fprintf(stderr, "error: trying to free a non-data block %d, or disk is busted: super->data_blk_offset = %d\n", blk_num, super->data_blk_offset);
                return -1;
        }
	if (blk_num >= NUM_BLKS)
	{
		fprintf(stderr, "error: try to free a blk#%d that exceeds num_blks\n", blk_num);
		return -1;
	}
	else if (blk_num == 0)
	{
                fprintf(stderr, "error: trying to free blk #0\n");
                return -1;
	}
#if _DEBUG
        printf("  next free blk idx = %d\n", super->next_free_blk_idx);
#endif
	char buf[BLK_SZ];
	char zero_buf[BLK_SZ];
	memset(zero_buf, 0, sizeof(zero_buf));
	/* indicates current free list link head full, so need to put the freed
	   blk_num as the new free list link head. */
        if (super->next_free_blk_idx == 1)
        {
/*
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "error: bread wrong when bfree\n");
			return -1;
		}
*/
		memset(buf, 0, sizeof(buf));
                int* freelist_head = (int*)buf;
                freelist_head[0] = super->free_blk_list_head;
                super->free_blk_list_head = blk_num;
                super->next_free_blk_idx = 0;
#if _DEBUG
                printf("  blk# %d freed\n", blk_num);
#endif
		if (bwrite(blk_num, buf) == -1)
		{
			fprintf(stderr, "error: bwrite wrong when bfree\n");
			return -1;
		}
        }
        else if (super->next_free_blk_idx == 0)
        {
		if (bread(super->free_blk_list_head, buf) == -1)
		{
			fprintf(stderr, "error: bread wrong when bfree\n");
			return -1;
		}
                super->next_free_blk_idx = FREE_BLKS_PER_LINK - 1;
                int* freelist_head = (int*)buf;
                freelist_head[super->next_free_blk_idx] = blk_num;
		if (bwrite(blk_num, zero_buf) == -1)
		{
			fprintf(stderr, "bwrite error when zeroing the blk #%d\n", blk_num);
			return -1;
		}
#if _DEBUG
                printf("  blk# %d freed\n", blk_num);
#endif
		if (bwrite(super->free_blk_list_head, buf) == -1)
		{
			fprintf(stderr, "error: bwrite wrong when bfree\n");
			return -1;
		}
        }
        else if (super->next_free_blk_idx > 1
                && super->next_free_blk_idx < FREE_BLKS_PER_LINK)
        {
		if (bread(super->free_blk_list_head, buf) == -1)
		{
			fprintf(stderr, "error: bread wrong when bfree\n");
			return -1;
		}
                super->next_free_blk_idx -= 1;
                int* freelist_head = (int*)buf;
                freelist_head[super->next_free_blk_idx] = blk_num;
		if (bwrite(blk_num, zero_buf) == -1)
		{
			fprintf(stderr, "bwrite error when zeroing the blk #%d\n", blk_num);
			return -1;
		}
#if _DEBUG
                printf("  blk# %d freed\n", blk_num);
#endif
		if (bwrite(super->free_blk_list_head, buf) == -1)
		{
			fprintf(stderr, "error: bwrite wrong when bfree\n");
			return -1;
		}
        }
        else
        {
                fprintf(stderr, "wrong next_free_blk_idx\n");
                return -1;
        }
        super->num_free_blks += 1;
	if (update_super() == -1)
	{
		fprintf(stderr, "update superblk error in bfree\n");
		return -1;
	}
/*
        super->locked = 0;
        super->modified = 1;
*/
        return 0;
}


/************************* Layer 1: inode algorithms ********************************/

// search on disk free inodes and add them on the free ilist
// when free ilist is empty.
static int fill_free_ilist(void)
{
        int blk_num;
        int i, offset;
        char buf[BLK_SZ]; // bigger than block size
        int k = super->remembered_inode;
        int remembered_i = k;
        if (k >= super->max_free_inodes)
        {
                fprintf(stderr, "error: inode %d exceeds maximum inode num\n", k);
                return -1;
        }
        for (i = 0; i < MAX_FREE_ILIST_SIZE; i++)
        {
                //printf("i = %d\n", i);
                while(k < super->max_free_inodes)
                {
                        blk_num = 1 + k / INODES_PER_BLK;
                        offset = k % INODES_PER_BLK;
                        //printf("blk_num = %d, offset = %d\n", blk_num, offset);
                        if (bread(blk_num, buf) == -1)
			{
				fprintf(stderr, "error: bread when fill free ilist\n");
				return -1;
			}
                        //printf("after bread\n");
                        struct disk_inode* pi = (struct disk_inode*)buf;
                        struct disk_inode* q = pi + offset;
                        //printf("after q\n");
                        if (q->file_type == UNUSED)
                        {
                                //printf("free_ilist[%d] = %d\n", i, k);
                                super->free_ilist[i] = k;
                                if (k > remembered_i)
                                        remembered_i = k;
                                k++;
                                break;
                        }
                        k++;
                }
        }
        super->remembered_inode = remembered_i;
        //printf("complete: i = %d\n", i);
        while (i < MAX_FREE_ILIST_SIZE)
        {
                printf("empty free ilist i=%d\n", i);
                super->free_ilist[i] = -1;
        }
        return 0;
}

static int init_free_ilist(void)
{
        fill_free_ilist();
        printf("complete init free ilist\n");
        return 0;
}

int get_time(void)
{
	//struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0)
	{
		fprintf(stderr, "error in get_time\n");
		return 0;
	}
	int res = tv.tv_sec;
	return res;
}

static int init_disk_inode(struct disk_inode* di)
{
        di->file_type = REGULAR;
        strncpy(di->owner_id, "unknown", FILE_OWNER_ID_LEN);
        di->access_permission = 0777;
        di->last_accessed = get_time();
        di->last_modified = get_time();
        di->inode_last_mod = get_time();
        di->link_count = 0;  // TODO: really?
        di->file_size = 0;
        di->blks_in_use = 0;
        int i;
        for(i = 0; i < DIRECT_BLKS_PER_INODE; i++)
        {
                di->block_addr[i] = 0;
        }
	di->single_ind_blk = 0;
	di->double_ind_blk = 0;
}

static int init_in_core_inode(struct in_core_inode* ci, int i_num)
{
        ci->file_type = REGULAR;
        strncpy(ci->owner_id, "unknown", FILE_OWNER_ID_LEN);
        ci->access_permission = 0777;
        ci->last_accessed = get_time();
        ci->last_modified = get_time();
        ci->inode_last_mod = get_time();
        ci->link_count = 1;
        ci->file_size = 0;
        ci->blks_in_use = 0;
        int i;
        for(i = 0; i < DIRECT_BLKS_PER_INODE; i++)
        {
                ci->block_addr[i] = 0;
        }
	ci->single_ind_blk = 0;
	ci->double_ind_blk = 0;
        ci->locked = 0;
        ci->modified = 1;
        ci->i_num = i_num;
        return 0;
}

static int init_inode_from_disk(struct in_core_inode* ci, const struct disk_inode* di, int i_num)
{
        ci->file_type = di->file_type;
        strncpy(ci->owner_id, di->owner_id, FILE_OWNER_ID_LEN);
        ci->access_permission = di->access_permission;
        ci->last_accessed = di->last_accessed;
        ci->last_modified = di->last_modified;
        ci->inode_last_mod = di->inode_last_mod;
        ci->link_count = di->link_count;
        ci->file_size = di->file_size;
        ci->blks_in_use = di->blks_in_use;
        int i;
        for(i = 0; i < DIRECT_BLKS_PER_INODE; i++)
        {
                ci->block_addr[i] = di->block_addr[i];
        }
	ci->single_ind_blk = di->single_ind_blk;
	ci->double_ind_blk = di->double_ind_blk;
        ci->locked = 0;
        ci->modified = 0;
        ci->i_num = i_num;
        return 0;
}

static int init_inode_from_kernel(struct disk_inode* dst, const struct in_core_inode* src)
{
        dst->file_type = src->file_type;
        strncpy(dst->owner_id, src->owner_id, FILE_OWNER_ID_LEN);
        dst->access_permission = src->access_permission;
        dst->last_accessed = src->last_accessed;
        dst->last_modified = src->last_modified;
        dst->inode_last_mod = src->inode_last_mod;
        dst->link_count = src->link_count;
        dst->file_size = src->file_size;
        dst->blks_in_use = src->blks_in_use;
        int i;
        for(i = 0; i < DIRECT_BLKS_PER_INODE; i++)
        {
                dst->block_addr[i] = src->block_addr[i];
        }
	dst->single_ind_blk = src->single_ind_blk;
	dst->double_ind_blk = src->double_ind_blk;
        return 0;
}

/* allocate in-core inodes */
struct in_core_inode* ialloc(void)
{
        int ret;
        struct in_core_inode* ci = NULL;
        char buf[BLK_SZ];
	if (super->num_free_inodes <= 0)
	{
		fprintf(stderr, "error: no more free inodes on disk\n");
		return NULL;
	}
        while (1)
        {
/*
                if (super->locked == 1)
                {
                        // TODO: sleep
                        continue;
                }
*/
                /* ilist empty in superblock */
                if (super->next_free_inode_idx == MAX_FREE_ILIST_SIZE)
                {
//                        super->locked = 1;
                        ret = fill_free_ilist();
//                        super->locked = 0;
                        if (ret == -1)
                        {
                                fprintf(stderr, "error: free ilist not filled\n");
                                return NULL;
                        }
                }
                int i_num = super->free_ilist[super->next_free_inode_idx];
#if _DEBUG
                printf("  i_num = %d\n", i_num);
#endif
                super->free_ilist[super->next_free_inode_idx] = -1;
                super->next_free_inode_idx += 1;
                ci = (struct in_core_inode*)malloc(sizeof(struct in_core_inode));
                init_in_core_inode(ci, i_num);
                int blk_num = 1 + i_num / INODES_PER_BLK;
                int offset = i_num % INODES_PER_BLK;
                if (bread(blk_num, buf) == -1)
                {
                        fprintf(stderr, "error: bread blk#%d when ialloc\n", blk_num);
                        return NULL;
                }
                struct disk_inode* di = (struct disk_inode*)buf;
                di = di + offset;
                if (di->file_type != 0) /* inode is not free */
                {
                        //TODO
                        fprintf(stderr, "error: inode not free after all\n");
                        continue;
                }
                init_disk_inode(di);
                /////
                /////
                if (bwrite(blk_num, buf) == -1)
                {
                        fprintf(stderr, "error: bwrite blk#%d when ialloc\n", blk_num);
                        return NULL;
                }
                super->num_free_inodes -= 1;
		if (update_super() == -1)
		{
			fprintf(stderr, "update superblk error\n");
			return NULL;
		}
                return ci;
        }
}

int ifree(struct in_core_inode* ci)
{
        int i_num = ci->i_num;
/*
        if (super->locked == 1)
        {
                fprintf(stderr, "error: superblk locked\n");
                return -1;
        }
*/

#if _DEBUG
        printf(" free i_num = %d\n", i_num);
#endif
        if (i_num >= super->max_free_inodes)
        {
                fprintf(stderr, "error: i_num exceeds max inode num\n");
                return -1;
        }
        if (super->next_free_inode_idx == 0) /* free ilist full*/
        {
                if (i_num < super->remembered_inode)
                {
                        super->remembered_inode = i_num;
                }
        }
        else if (super->next_free_inode_idx <= MAX_FREE_ILIST_SIZE)
        {
                super->next_free_inode_idx --;
                super->free_ilist[super->next_free_inode_idx] = i_num;
        }
        else
        {
                fprintf(stderr, "error: wrong next_free_inode_idx\n");
                return -1;
        }
        char buf[BLK_SZ];
        int blk_num = 1 + i_num / INODES_PER_BLK;
        int offset = i_num % INODES_PER_BLK;
        if (bread(blk_num, buf) == -1)
        {
                fprintf(stderr, "error: bread blk#%d\n", blk_num);
                return -1;
        }
        struct disk_inode* di = (struct disk_inode*)buf;
        di = di + offset;
        di->file_type = UNUSED;
        if (bwrite(blk_num, buf) == -1)
        {
                fprintf(stderr, "error: bwrite blk#%d\n", blk_num);
                return -1;
        }
        free(ci); // ultimately free the in-core inode structure
	ci = NULL;
        super->num_free_inodes += 1;
	if (update_super() == -1)
	{
		fprintf(stderr, "update superblk error\n");
		return -1;
	}
        return 0;
}

// map a logical file byte offset to file system block
// given an inode and byte offset, return a blk_num and byte offset in the block
int bmap(const struct in_core_inode* ci, const int off, int* ret_blk_num,
	int* ret_off_blk)
{
	int logical_blk;
	int blk_num;
	int off_blk;
	logical_blk = off / BLK_SZ;
	off_blk = off % BLK_SZ;
	char buf[BLK_SZ];
	if (logical_blk < DIRECT_BLKS_PER_INODE)
	{
		blk_num = ci->block_addr[logical_blk];
	}
	else if (logical_blk < max_single)
	{
		blk_num = ci->single_ind_blk;
		logical_blk -= DIRECT_BLKS_PER_INODE;
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error when bmap blk#%d\n", blk_num);
			return -1;
		}
		int *p = (int*)buf;
		blk_num = p[logical_blk];
	}
	else if (logical_blk < max_double)
	{
		blk_num = ci->double_ind_blk;
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error when bmap blk#%d\n", blk_num);
			return -1;
		}
		int *p = (int*)buf;
		logical_blk -= max_single;
		int indirect_blk = logical_blk / RANGE_SINGLE;
		int indirect_off = logical_blk % RANGE_SINGLE;
		blk_num = p[indirect_blk];
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error when bmap blk#%d\n", blk_num);
			return -1;
		}
		p = (int*)buf;
		blk_num = p[indirect_off];
	}
	else
	{
		fprintf(stderr, "logical blk num %d out of max range\n", logical_blk);
		return -1;
	}
	*ret_blk_num = blk_num;
	*ret_off_blk = off_blk;
	return 0;
}

struct in_core_inode* iget(int i_num)
{
	int blk_num = 1 + i_num / INODES_PER_BLK;
	int offset = i_num % INODES_PER_BLK;
	char buf[BLK_SZ];
	if (bread(blk_num, buf) == -1)
	{
		fprintf(stderr, "error: bread blk#%d when iget\n", blk_num);
		return NULL;
	}
	struct disk_inode *di = (struct disk_inode*)buf;
	di = di + offset;
	struct in_core_inode *ci = (struct in_core_inode*)malloc(sizeof(struct in_core_inode));
	init_inode_from_disk(ci, di, i_num);
	ci->locked = 1;
	return ci;
}

static int free_disk_blocks(struct in_core_inode* ci)
{
	int i;
	int blk_num;
	// free direct blocks
#if _DEBUG
	printf("free direct blocks\n");
#endif
	for (i = 0; i < DIRECT_BLKS_PER_INODE; i++)
	{
		blk_num = ci->block_addr[i];
		if (blk_num == 0)
			continue;
		if (bfree(blk_num) == -1)
		{
			fprintf(stderr, "bfree error when free disk blk#%d\n", blk_num);
			return -1;
		}
		ci->block_addr[i] = 0;
	}

	// free single indirect blks
	if (ci->blks_in_use <= DIRECT_BLKS_PER_INODE)
		goto finish_free_disk_blks;
#if _DEBUG
	printf("free single indirect blocks\n");
#endif
	blk_num = ci->single_ind_blk;
	if (blk_num == 0) // doesn't in use.
		goto free_double_indirect;
	char buf[BLK_SZ];
	if (bread(blk_num, buf) == -1)
	{
		fprintf(stderr, "bread error blk#%d when free single indirect\n", blk_num);
		return -1;
	}
	int *p = (int*)buf;
	for (i = 0; i < RANGE_SINGLE; i++)
	{
		blk_num = p[i];
		if (blk_num == 0)
			continue;
		if (bfree(blk_num) == -1)
		{
			fprintf(stderr, "bfree error when free disk blk#%d\n", blk_num);
			return -1;
		}
	}
	// TODO: fatal error, no update to the disk!!!
	blk_num = ci->single_ind_blk;
	if (bfree(blk_num) == -1)
	{
		fprintf(stderr, "bfree error blk#%d when free single indirect\n", blk_num);
		return -1;
	}
	ci->single_ind_blk = 0;

	// free double indirect blks
	if (ci->blks_in_use <= max_single)
		goto finish_free_disk_blks;
free_double_indirect:
#if _DEBUG
	printf("free double indirect blocks\n");
#endif
	blk_num = ci->double_ind_blk;
	if (blk_num == 0) // doesn't in use.
		return 0;
	if (bread(blk_num, buf) == -1)
	{
		fprintf(stderr, "bread error blk#%d when free single indirect\n", blk_num);
		return -1;
	}
	p = (int*)buf;
	for (i = 0; i < RANGE_SINGLE; i++)
	{
		blk_num = p[i];
		if (blk_num == 0)
			continue;
		char buf2[BLK_SZ];
		if (bread(blk_num, buf2) == -1)
		{
			fprintf(stderr, "bread error blk#%d when free double indirect\n", blk_num);
			return -1;
		}
		int *q = (int*)buf2;
		int j;
		for (j = 0; j < RANGE_SINGLE; j++)
		{
			if (q[j] == 0)
				continue;
			if (bfree(q[j]) == -1)
			{
				fprintf(stderr, "bfree error blk# %d when free single indirect in double indirect\n", q[j]);
				return -1;
			}
		}
		if (bfree(p[i]) == -1)
		{
			fprintf(stderr, "bfree error blk# %d for single indirect blk table in double indirect\n", p[i]);
			return -1;
		}
	}
	blk_num = ci->double_ind_blk;
	if (bfree(blk_num) == -1)
	{
		fprintf(stderr, "bfree error blk#%d when free single indirect\n", blk_num);
		return -1;
	}
	ci->double_ind_blk = 0;
finish_free_disk_blks:
	return 0;
}

static int free_blks_for_truncate(struct in_core_inode *ci, int new_blks_in_use);

// release an inode
int iput(struct in_core_inode* ci)
{
	/*if (ci->locked == 1)
	{
		fprintf(stderr, "warn: inode locked\n");
		return -1;
	}*/
	if (ci == NULL)
	{
		fprintf(stderr, "ci is null pointer\n");
		return -1;
	}
	ci->locked = 1;
	if (1)
	{
		if (ci->link_count == 0)
		{
			// free all disk blocks
			//if (truncate_v2(ci, 0) != 0)
			if (free_disk_blocks(ci) == -1)
			//if (free_blks_for_truncate(ci, 0) != 0)
			{
				fprintf(stderr, "error truncate all disk blocks in iput\n");
				return -1;
			}
			//ci->file_type = 0; // will be set in ifree().
			// free inode
			if (ifree(ci) == -1)
			{
				fprintf(stderr, "error: ifree when iput\n");
				return -1;
			}
		}
		if (ci && ci->modified == 1) // update disk inode from in-core inode
		{
			int i_num = ci->i_num;
			int blk_num = 1 + i_num / INODES_PER_BLK;
			int offset = i_num % INODES_PER_BLK;
			// the reason why we need to call bread() is because we just want to
			// update the inode in question, and leave the other inodes unmodified.
			char buf[BLK_SZ];
			if (bread(blk_num, buf) == -1)
			{
				fprintf(stderr, "bread error blk# %d when iput\n", blk_num);
				return -1;
			}
			struct disk_inode* di = (struct disk_inode*)buf;
			di = di + offset;
			init_inode_from_kernel(di, ci);
			if (bwrite(blk_num, buf) == -1)
			{
				fprintf(stderr, "bwrite error blk#%d when iput\n", blk_num);
				return -1;
			}
		}
		// no free list for inode cache.
	}
	ci->locked = 0;
	return 0;
}

void dump_in_core_inode(struct in_core_inode* ci)
{
	printf("\n\ndumping in-core inode\n\n");
	printf("file_type = %d\n", ci->file_type);
	printf("owner = %s\n", ci->owner_id);
	printf("access_permission = %d\n", ci->access_permission);
	printf("last_accessed = %d\n", ci->last_accessed);
	printf("last_modified = %d\n", ci->last_modified);
	printf("inode_last_mod = %d\n", ci->inode_last_mod);
	printf("link_count = %d\n", ci->link_count);
	printf("file_size = %d\n", ci->file_size);
	printf("blks_in_use = %d\n", ci->blks_in_use);
	printf("direct block addr = \n");
	int i;
	for (i = 0; i < DIRECT_BLKS_PER_INODE; i++)
	{
		printf("%d ", ci->block_addr[i]);
	}
	printf("\nindirect single blk# = %d, indirect double blk# = %d\n",
		ci->single_ind_blk, ci->double_ind_blk);
	printf("locked = %d\n", ci->locked);
	printf("i_num = %d\n", ci->i_num);
}

/************************* Layer 1: make fs ***********************************/
static int create_superblk(void)
{
	super = (struct super_block*)malloc(sizeof(struct super_block));
	if (super == NULL)
	{
		fprintf(stderr, "create superblk error: no memory\n");
		return -1;
	}
        super->blk_size = BLK_SZ;
        super->num_blks = NUM_BLKS;
        super->fs_size = (long)(super->blk_size) * (long)(super->num_blks);
        /* disk blocks */
        super->max_free_blks = super->num_free_blks = NUM_BLKS - 1 - ILIST_SPACE;
        super->data_blk_offset = ILIST_SPACE + 1;
        super->free_blk_list_head = super->data_blk_offset;
        super->next_free_blk_idx = 1;
        /* inodes */
        super->max_free_inodes = super->num_free_inodes = INODES_PER_BLK * ILIST_SPACE;
        memset(super->free_ilist, 0, sizeof(super->free_ilist));
        super->next_free_inode_idx = 0;
/*
        super->modified = 0;
        super->locked = 0;
*/

        return 0;
}

static int read_superblk(void)
{
	int blk_num = 0;
	char buf[BLK_SZ];
	memset(buf, 0, sizeof(buf));
	if (bread(blk_num, buf) == -1)
	{
		fprintf(stderr, "bread error in read_superblk\n");
		return -1;
	}
	super = (struct super_block*)malloc(sizeof(struct super_block));
	if (super == NULL)
	{
		fprintf(stderr, "create superblk error: no memory\n");
		return -1;
	}
	memcpy(super, buf, sizeof(struct super_block));
	return 0;
}

static int root_i_num;
//static struct in_core_inode* curr_dir_inode;
static int curr_dir_i_num;

int mkrootdir(void)
{
	struct in_core_inode *r = ialloc();
	if (r == NULL)
	{
		fprintf(stderr, "ialloc error in mkrootdir\n");
		return -1;
	}
	root_i_num = r->i_num;
	r->file_type = DIRECTORY;
	strncpy(r->owner_id, "root2", FILE_OWNER_ID_LEN);
	r->file_size = BLK_SZ;  // root dir has one block that is equal to BLK_SZ.
	r->blks_in_use = 1;  // root dir has one block that is equal to BLK_SZ.
	int blk_num = balloc();
	if (blk_num == -1)
	{
		fprintf(stderr, "balloc error in mkrootdir\n");
		return -1;
	}
	r->block_addr[0] = blk_num;
	char buf[BLK_SZ];
	memset(buf, 0, sizeof(buf));
	struct directory_entry *entry;
	entry = (struct directory_entry*)buf;
	strncpy(entry->file_name, ".", FILE_NAME_LEN);
	entry->inode_num = root_i_num;
	entry++;
	strncpy(entry->file_name, "..", FILE_NAME_LEN);
	entry->inode_num = root_i_num;
	// indicate the end of a directory
	entry++;
	entry->inode_num = BAD_I_NUM;
	if (bwrite(blk_num, buf) == -1)
	{
		fprintf(stderr, "bwrite error blk#%d in mkrootdir\n", blk_num);
		return -1;
	}
	if (iput(r) == -1)
	{
		fprintf(stderr, "iput error in mkrootdir\n");
		return -1;
	}
	return 0;
}

/* the mkfs system call creates superblock, free blk list and free ilist on disk
 * return 0: successful; return -1: failure.
 */
int mkfs(void)
{
	printf("reset storage...\n");
	if (reset_storage() == -1)
	{
		fprintf(stderr, "error: reset storage\n");
		return -1;
	}
        create_superblk();
        init_free_blk_list();
        init_free_ilist();
	if (mkrootdir() == -1)
	{
		fprintf(stderr, "error when mkrootdir\n");
		return -1;
	}
	if (update_super() == -1)
	{
		fprintf(stderr, "update superblk error\n");
		return -1;
	}
	curr_dir_i_num = root_i_num; // init current directory
        return 0;
}

int init_super(void)
{
	if (read_superblk() != 0)
	{
		fprintf(stderr, "read_superblk error in init_super\n");
		return -1;
	}
	//init_free_ilist();
	curr_dir_i_num = root_i_num; // init current directory
	return 0;
}

void init_namei_cache()
{
	int j;

	//gettimeofday(&tv, NULL);
	get_time();

	for(j = 0; j < NAMEI_CACHE_SZ; ++j)
	{
		namei_cache[j].path[0] = '\0';
		namei_cache[j].iNode = NULL;
		namei_cache[j].timestamp = tv.tv_sec;	// stamp everything NOW
	}

	return;
}

struct namei_cache_element *find_namei_cache_by_path(const char *path)
{
	int i;

	for(i = 0; i < NAMEI_CACHE_SZ; ++i)
	{
		if(strcmp(path, namei_cache[i].path) == 0)
			return &namei_cache[i];
	}

	return NULL;
}

struct namei_cache_element *find_namei_cache_by_oldest()
{
	int tstamp, oldest, i;

	get_time();
	tstamp = tv.tv_sec;

	for(i = 0; i < NAMEI_CACHE_SZ; ++i)
	{
		if(tstamp > namei_cache[i].timestamp)
		{
			oldest = i;
			tstamp = namei_cache[i].timestamp;
		}
	}

	return &namei_cache[oldest];
}

struct in_core_inode* namei_v2(const char* path_name)
{
	struct in_core_inode* working_inode;
	char *path_tok;
	struct directory_entry* dir_entry;
	char path[MAX_PATH_LEN];
#if USE_NAMEI_CACHE
	struct namei_cache_element *cached_path = NULL;

	// look in cache for this path
	if((cached_path = find_namei_cache_by_path(path_name)) !=
		NULL)
	{
		//gettimeofday(&tv, NULL);
		get_time();
		cached_path->timestamp = tv.tv_sec;
//#if _DEBUG
		printf("namei: found cached path for %s\n",
			path_name);
//#endif

		return cached_path->iNode;
	}

	// path not cached, search fs for path
#endif
	strncpy(path, path_name, MAX_PATH_LEN);
	if (path == NULL)
	{
		fprintf(stderr, "error: path = NULL\n");
		return NULL;
	}

	if (path[0] == '/')
	{
		working_inode = iget(root_i_num);
		if (working_inode == NULL)
		{
			fprintf(stderr, "error: get root inode fails in namei_v2\n");
			return NULL;
		}
	}
	else
	{
		working_inode = iget(curr_dir_i_num);
		if (working_inode == NULL)
		{
			fprintf(stderr, "error: get current directory inode fails in namei_v2\n");
			return NULL;
		}
	}

	path_tok = strtok(path, "/");
	while (path_tok)
	{
#if _DEBUG
		printf("path_tok = %s\n", path_tok);
#endif
		if (working_inode->file_type != DIRECTORY)
		{
			fprintf(stderr, "error: curr working dir is not a directory\n");
			return NULL;
		}
		// TODO: check access permissions
		if (working_inode->i_num == root_i_num && strcmp(path_tok, "..") == 0)
		{
			path_tok = strtok(NULL, "/");
			continue;
		}
		// read directory entry by entry
		//dir_entry = (struct directory_entry*)(working_inode->block_addr[0]);
		int off;  // the offset of dir entry in a directory
		for (off = 0; off < MAX_ENTRY_OFFSET; off += DIR_ENTRY_LENGTH)
		{
#if _DEBUG
			printf("off of the directory is %d\n", off);
#endif
			int blk_num;
			int offset_blk; // offset in a disk block
			if (bmap(working_inode, off, &blk_num, &offset_blk) == -1)
			{
				fprintf(stderr, "bmap error in namei_v2\n");
				return NULL;
			}
			char buf[BLK_SZ];
			if (bread(blk_num, buf) == -1)
			{
				fprintf(stderr, "bread error blk# %d in namei_v2\n", blk_num);
				return NULL;
			}
			dir_entry = (struct directory_entry*)(buf + offset_blk);
#if _DEBUG
			printf("get a dir entry, i_num = %d, filename = (%s)\n", dir_entry->inode_num, dir_entry->file_name);
#endif
			if (dir_entry->inode_num == BAD_I_NUM)
			{// the search reaches the end of the dir entry. TODO: remove printf.
#if _DEBUG
				printf("cannot find the inode in curr dir for token %s\n", path_tok);
#endif
				return NULL;
			}
			if (dir_entry->inode_num == EMPTY_I_NUM)
				continue;
			if (strcmp(dir_entry->file_name, path_tok) == 0)
			{
				int i_num = (int)(dir_entry->inode_num);
				if (iput(working_inode) == -1)
				{
					fprintf(stderr, "iput error in namei_v2\n");
					return NULL;
				}
				working_inode = iget(i_num);
				if (working_inode == NULL)
				{
					fprintf(stderr, "iget error in namei_v2\n");
					return NULL;
				}
				break;
			}
		}
		if (off == MAX_ENTRY_OFFSET)
		{// already reaches the max entries, but not found.
			printf("cannot find the inode until the max entries in curr_dir for token %s\n", path_tok);
			return NULL;
		}

		path_tok = strtok(NULL, "/");
	}

#if USE_NAMEI_CACHE
	// replace oldest cached path with this one
	//gettimeofday(&tv, NULL);
	get_time();
	if(cached_path == NULL)
	{
		cached_path = find_namei_cache_by_oldest();
		strncpy(cached_path->path, path_name, strlen(path_name));
		cached_path->iNode = working_inode;
	}
	cached_path->timestamp = tv.tv_sec;
//#if _DEBUG
	printf("namei: cached mapping to path %s\n",
		path_name);
//#endif
#endif

	working_inode->locked = 1;
	return working_inode;
}

int separate_node_name(const char *path, char *node_name)
{
  return separate_node(path, node_name, 1);
}

int separate_node_path(const char *path, char *node_path)
{
  return separate_node(path, node_path, 0);
}

int separate_node(const char *path, char *node_part, int part)
{
  char node_path[strlen(path)];
  char node_name[strlen(path)];
  int l = 0;
  int substr_len = 0;

  l = strlen(path);
  do
  {
    ++substr_len;

    if(path[l] == '/')
    {
      if(l == strlen(path)-1)
      {
        //printf("no node name\n");
        return 0;
      }

      memcpy(node_name, &path[l+1], substr_len);
      break;
    }

    --l;

  } while(l >= 0);

  //printf("l = %d\n", l);
  if(l == 0)
  {
    memcpy(node_path, "/\0", 2);
  }
  else
  {
    memcpy(node_path, path, l);
    node_path[l] = '\0';  // fix a bug: need to add an end char.
  }

  if(part == 0)
  {
    memcpy(node_part, node_path, strlen(node_path) + 1);
  }
  else
  {
    memcpy(node_part, node_name, strlen(node_name) + 1);
  }

  return 1;
}

/* use separate utilities to split the end of the path and the rest of the path.
   the end of the path should exist in the current filesystem, and should be a directory
*/
int mkdir_v2(const char* path_name, int mode)
{
	struct in_core_inode *ci;
	char path[MAX_PATH_LEN];
	strncpy(path, path_name, MAX_PATH_LEN);
	char node_name[strlen(path_name)];
	char node_path[strlen(path_name)];
	if (path == NULL)
	{
		fprintf(stderr, "error: path = NULL during mkdir\n");
		return -1;
	}
	// seperate node and path to node in 2 strings (mkdir needs this)
	separate_node_name(path, node_name);
	separate_node_path(path, node_path);
#if _DEBUG
	printf("node_path = %s\n", node_path);
	printf("node_name = %s\n", node_name);
#endif

	ci = namei_v2(node_path);
	if (ci == NULL)
	{
		fprintf(stderr, "mkdir: cannot create directory %s: No such file or directory\n", path);
		return -1;
	}
#if _DEBUG
	printf("i_num of working_dir = %d\n", ci->i_num);
#endif
	// create a directory node_path under inode ci

	int off;
	struct directory_entry* dir_entry;
	// TODO: needs optimization: read a block at a time for several dir entries look-up.
	// TODO: for each entry, needs to see if inode num is either -2 or -1, if it is, then it is a free entry, meaning dir could be added to this entry.
	for (off = 0; off < MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH; off += DIR_ENTRY_LENGTH)
	{
#if _DEBUG
		printf("offset = %d\n", off);
#endif
		int blk_num;
		int offset_blk;
		if (bmap(ci, off, &blk_num, &offset_blk) == -1)
		{
			fprintf(stderr, "bmap error in mkdir_v2\n");
			return -EFAULT; // bad address
		}
#if _DEBUG
		printf("blk_num = %d\n", blk_num);
		printf("offset_blk = %d\n", offset_blk);
#endif
		char buf[BLK_SZ];
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error in mkdir_v2\n");
			return -EIO; // cannot read disk
		}
		dir_entry = (struct directory_entry*)(buf + offset_blk);
#if _DEBUG
		printf("i_num of curr dir entry = %d\n", dir_entry->inode_num);
#endif
		if (dir_entry->inode_num != BAD_I_NUM
		  && dir_entry->inode_num != EMPTY_I_NUM)
		{ // meaning current dir entry is occupied.
			continue;
		}
		// there is dir entry space left starting from this
		struct in_core_inode* new_inode = ialloc();
		if (new_inode == NULL)
		{
			fprintf(stderr, "ialloc error in mkdir_v2\n");
			return -1;
		}
		new_inode->file_type = DIRECTORY;
		new_inode->file_size = BLK_SZ;
		new_inode->blks_in_use = 1;
		// need to add ".", ".." and -1 (end) to a new directory inode.
		int new_dir_blk = balloc();
		if (new_dir_blk == -1)
		{
			fprintf(stderr, "balloc error in mknod_v2\n");
			return -1;
		}
		new_inode->block_addr[0] = new_dir_blk;
		char new_buf[BLK_SZ];
		memset(new_buf, 0, sizeof(buf));
		struct directory_entry *new_dir_entry;
		new_dir_entry = (struct directory_entry*)new_buf;
		strncpy(new_dir_entry->file_name, ".", FILE_NAME_LEN);
		new_dir_entry->inode_num = new_inode->i_num;  // himself
		new_dir_entry++;
		strncpy(new_dir_entry->file_name, "..", FILE_NAME_LEN);
		new_dir_entry->inode_num = ci->i_num;   // his parent inode
		// indicate the end of a directory
		new_dir_entry++;
		new_dir_entry->inode_num = BAD_I_NUM;
		if (bwrite(new_dir_blk, new_buf) == -1)
		{
			fprintf(stderr, "bwrite error in mknod_v2\n");
			return -1;
		}

		// the last step is to update to the disk
		if (iput(new_inode) == -1)
		{
			fprintf(stderr, "iput error in mknod_v2\n");
			return -1;
		}
		if (dir_entry->inode_num == EMPTY_I_NUM)
		{
			dir_entry->inode_num = (new_inode->i_num);
			strncpy(dir_entry->file_name, node_name, FILE_NAME_LEN);
		}
		else if (dir_entry->inode_num == BAD_I_NUM)
		{
			dir_entry->inode_num = (new_inode->i_num);
			strncpy(dir_entry->file_name, node_name, FILE_NAME_LEN);
			// if the dir has the last space for a bad inode num, then
			// write a BAD_I_NUM indicating the end of the directory.
			dir_entry ++;
			dir_entry->inode_num = BAD_I_NUM;
		}
		else
		{
			fprintf(stderr, "fatal error: dir_entry->inode_num %d wrong in mkdir_v2\n", dir_entry->inode_num);
			return -1;
		}

		if (bwrite(blk_num, buf) == -1)
		{
			fprintf(stderr, "bwrite error in mknod_v2\n");
			return -1;
		}
		break;
	}
	if (off == MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH)
	{
		fprintf(stderr, "error: directory is full, no entries can be added\n");
		return -1;
	}
	return 0;
}

// removes a directory and a file
int unlink(const char *path_name)
{
	struct in_core_inode *ci;
	char path[MAX_PATH_LEN];
	strncpy(path, path_name, MAX_PATH_LEN);
	char node_name[strlen(path_name)];
	char node_path[strlen(path_name)];
	if (path == NULL)
	{
		fprintf(stderr, "error: path = NULL in unlink\n");
		return -1;
	}
	// seperate node and path to node in 2 strings (mkdir needs this)
	separate_node_name(path, node_name);
	separate_node_path(path, node_path);
#if _DEBUG
	printf("node_path = %s\n", node_path);
	printf("node_name = %s\n", node_name);
#endif

	ci = namei_v2(node_path);
	if (ci == NULL)
	{
		fprintf(stderr, "rmdir: cannot find path to directory %s\n", path);
		return -ENOENT;
	}
#if _DEBUG
	printf("i_num of working_dir = %d\n", ci->i_num);
#endif
	// create a directory node_path under inode ci

	int off = 2 * DIR_ENTRY_LENGTH; // start from the 3rd entry.
	struct directory_entry* dir_entry;
	// TODO: needs optimization: read a block at a time for several dir entries look-up.
	for (; off < MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH; off += DIR_ENTRY_LENGTH)
	{
#if _DEBUG
		printf("offset = %d\n", off);
#endif
		int blk_num;
		int offset_blk;
		if (bmap(ci, off, &blk_num, &offset_blk) == -1)
		{
			fprintf(stderr, "bmap error in unlink\n");
			return -EFAULT; // bad address
		}
#if _DEBUG
		printf("blk_num = %d\n", blk_num);
		printf("offset_blk = %d\n", offset_blk);
#endif
		char buf[BLK_SZ];
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error in unlink\n");
			return -EIO; // cannot read disk
		}
		dir_entry = (struct directory_entry*)(buf + offset_blk);
#if _DEBUG
		printf("i_num of curr dir entry = %d\n", dir_entry->inode_num);
#endif
		if (dir_entry->inode_num == BAD_I_NUM)
		{
			fprintf(stderr, "reaches the end, cannot find dir %s", path);
			return -ENOENT;
		}
		if (dir_entry->inode_num == EMPTY_I_NUM)
			continue;
		if (strcmp(dir_entry->file_name, node_name) != 0)
			continue;
		// find the directory. read it and delete it.
		int i_num = dir_entry->inode_num;
		struct in_core_inode *target_inode = iget(i_num);
		if (target_inode == NULL)
		{
			fprintf(stderr, "no disk inode corresponding to this i_num %d\n", i_num);
			return -ENOENT;
		}
#if 0		// not necessary, since we are now also deleting files.
		if (target_inode->file_type != DIRECTORY)
		{
			fprintf(stderr, "the inode is not a dir\n");
			return -ENOTDIR;
		}
#endif
		// TODO: remove all entries in the target_inode as a directory if it contains entries.
		// remove the directory
		if (target_inode->link_count > 0)
			target_inode->link_count --;
		if (iput(target_inode) == -1)
		{
			fprintf(stderr, "iput error i_num = %d in unlink\n", i_num);
			return -EIO;
		}
		dir_entry->inode_num = EMPTY_I_NUM; // reset inode num to indicate it is free.
		if (bwrite(blk_num, buf) == -1)
		{
			fprintf(stderr, "bwrite error in unlink\n");
			return -EIO;
		}
		break;
	}
	if (off == MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH)
	{
		fprintf(stderr, "cannot find the directory to delete\n");
		return -ENOENT;
	}
	return 0;
}

/* use separate utilities to split the end of the path and the rest of the path.
   the end of the path should exist in the current filesystem, and should be a directory
*/
int mknod_v2(const char* path_name, int mode, int dev)
{
	struct in_core_inode *ci;
	char path[MAX_PATH_LEN];
	strncpy(path, path_name, MAX_PATH_LEN);
	char node_name[strlen(path_name)];
	char node_path[strlen(path_name)];
	if (path == NULL)
	{
		fprintf(stderr, "error: path = NULL during mknod_v2\n");
		return -1;
	}
	separate_node_name(path, node_name);
	separate_node_path(path, node_path);
#if _DEBUG
	printf("node_path = %s\n", node_path);
	printf("node_name = %s\n", node_name);
#endif

	ci = namei_v2(node_path);
	if (ci == NULL)
	{
		fprintf(stderr, "mknod_v2: cannot create directory %s: No such file or directory\n", path);
		return -1;
	}
#if _DEBUG
	printf("i_num of working_dir = %d\n", ci->i_num);
#endif
	// create a directory node_path under inode ci

	int off;
	struct directory_entry* dir_entry;
	// TODO: needs optimization: read a block at a time for several dir entries look-up.
	for (off = 0; off < MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH; off += DIR_ENTRY_LENGTH)
	{
#if _DEBUG
		printf("offset = %d\n", off);
#endif
		int blk_num;
		int offset_blk;
		if (bmap(ci, off, &blk_num, &offset_blk) == -1)
		{
			fprintf(stderr, "bmap error in mknod_v2\n");
			return -1;
		}
#if _DEBUG
		printf("blk_num = %d\n", blk_num);
		printf("offset_blk = %d\n", offset_blk);
#endif
		char buf[BLK_SZ];
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error in mknod_v2\n");
			return -1;
		}
		dir_entry = (struct directory_entry*)(buf + offset_blk);
#if _DEBUG
		printf("i_num of curr dir entry = %d\n", dir_entry->inode_num);
#endif
		if (dir_entry->inode_num != BAD_I_NUM
		  && dir_entry->inode_num != EMPTY_I_NUM)
		{ // meaning current dir entry is occupied.
			continue;
		}
		// there is dir entry space left starting from this
		struct in_core_inode* new_inode = ialloc();
		if (new_inode == NULL)
		{
			fprintf(stderr, "ialloc error in mknod_v2\n");
			return -1;
		}
		new_inode->file_type = REGULAR;
		new_inode->file_size = 0;
		new_inode->blks_in_use = 0;
		// the last step is to update to the disk
		if (iput(new_inode) == -1)
		{
			fprintf(stderr, "iput error in mknod_v2\n");
			return -1;
		}
                if (dir_entry->inode_num == EMPTY_I_NUM)
                {
                        dir_entry->inode_num = (new_inode->i_num);
                        strncpy(dir_entry->file_name, node_name, FILE_NAME_LEN);
                }
                else if (dir_entry->inode_num == BAD_I_NUM)
                {
                        dir_entry->inode_num = (new_inode->i_num);
                        strncpy(dir_entry->file_name, node_name, FILE_NAME_LEN);
                        // if the dir has the last space for a bad inode num, then
                        // write a BAD_I_NUM indicating the end of the directory.
                        dir_entry ++;
                        dir_entry->inode_num = BAD_I_NUM;
                }
                else
                {
                        fprintf(stderr, "fatal error: dir_entry->inode_num %d wrong in mkdir_v2\n", dir_entry->inode_num);
                        return -1;
                }

		if (bwrite(blk_num, buf) == -1)
		{
			fprintf(stderr, "bwrite error in mknod_v2\n");
			return -1;
		}
		break;
	}
	if (off == MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH)
	{
		fprintf(stderr, "error: directory is full, no entries can be added\n");
		return -1;
	}
	return 0;
}

int rmdir(const char* path)
{
	return unlink(path);
}

/******************* file operations *************************************/

#if 0
int open_v2(const char* path, int mode)
{
	struct in_core_inode* ci;
	struct file_table_entry* ft_ent;
	int fd;

	ci = namei_v2(path);
	if (ci == NULL)
	{
		fprintf(stderr, "error: file not found\n");
		return -1;
	}
	// TODO: check permissions

	// allocate file table entry for inode

	// TODO: how to truncate a file

	ci->locked = 0;
	return fd;
}
#endif

// on success: returns the number of bytes read is returned. 0: end of file
// on failure: returns -1
int read_v2(struct in_core_inode* ci, char* buf, int size, int offset)
{
	// from offset, copy size of bytes from the file indicated by path to buf
        if (ci == NULL)
                return -ENOENT;
	int res;
	int count = 0; // the bytes that are copied to buf
	if (offset > ci->file_size)
	{
		fprintf(stderr, "read error: offset %d exceeds file size %d\n", offset, ci->file_size);
		return 0;
	}
	if (offset + size > ci->file_size)
	{
		size = ci->file_size - offset;  // update the max bytes to read.
#if _DEBUG
		printf("the bytes to read exceeds file_size %d, size updated = %d\n", ci->file_size, size);
#endif
	}
	while (count < size)
	{
		int blk_num;
		int offset_blk;  // offset in disk block
		res = bmap(ci, offset + count, &blk_num, &offset_blk);
		if (res != 0)
		{
			fprintf(stderr, "bmap error in read\n");
			memset(buf + count, 0, size - count);
			return -EFAULT;
		}
#if _DEBUG
		printf("blk_num = %d\n", blk_num);
		printf("offset_blk = %d\n", offset_blk);
#endif
		char blk_buf[BLK_SZ];
		res = bread(blk_num, blk_buf);
		if (res != 0)
		{
			fprintf(stderr, "bread error blk# %d in read\n", blk_num);
			memset(buf + count, 0, size - count);
			return -EIO;
		}
		if (offset_blk + (size - count) <= BLK_SZ)
		{ // the left bytes to copy doesn't exceeds blk_size
			memcpy(buf + count, blk_buf + offset_blk, size - count);
			count = size;
		}
		else
		{
			int to_copy = BLK_SZ - offset_blk;
			memcpy(buf + count, blk_buf + offset_blk, to_copy);
			count += to_copy;
		}
	}
	ci->last_accessed = get_time();
	ci->modified = 1;
	res = iput(ci);
	if (res != 0)
	{
		fprintf(stderr, "iput error in read_v2\n");
		return -EIO;
	}
	else
	{
#if _DEBUG
		printf("iput is successful\n");
#endif
	}
	return count;
}

static int alloc_blks_for_write(struct in_core_inode *ci, int blks_to_alloc)
{
	//int bytes_to_alloc = offset + size - ;
	//int blks_to_alloc = (bytes_to_alloc + BLK_SZ) / BLK_SZ;
	int i;
	int count = ci->blks_in_use; // blks allocated.
	int limit = ci->blks_in_use + blks_to_alloc;

	// first alloc disk blks for direct blks.
	while (count < limit && count < DIRECT_BLKS_PER_INODE)
	{
		int blk_num = balloc();
		if (blk_num == -1)
		{
			fprintf(stderr, "balloc error blk# %d in alloc_blks\n", blk_num);
			return -ENOMEM;
		}
		ci->block_addr[count] = blk_num;
		ci->blks_in_use += 1;
		count += 1;
	}
	// if count < limit, alloc blks for single indirect.
	while (count < limit && count < max_single)
	{
		int blk_num;
		if (ci->single_ind_blk == 0) // single hasn't been alloc
		{
			blk_num = balloc();
			if (blk_num == -1)
			{
				fprintf(stderr, "balloc error blk# %d in alloc_blks\n", blk_num);
				return -ENOMEM;
			}
			// init blk
			char sub_buf[BLK_SZ];
			memset(sub_buf, 0, sizeof(sub_buf));
			if (bwrite(blk_num, sub_buf) == -1)
			{
				fprintf(stderr, "bwrite error blk# %d in alloc_blks\n", blk_num);
				return -EFAULT;
			}
			ci->single_ind_blk = blk_num;
			//ci->blks_in_use += 1; // count should not be incremented.
		}
		else
			blk_num = ci->single_ind_blk;
		char buf[BLK_SZ];
		memset(buf, 0, sizeof(buf));
		if (bread(blk_num, buf) == -1)
		{
			fprintf(stderr, "bread error blk# %d in alloc_blks\n", blk_num);
			return -EFAULT;
		}
		int *p = (int*)buf;
		i = count - DIRECT_BLKS_PER_INODE;
		if (i < 0 || i >= RANGE_SINGLE)
		{
			fprintf(stderr, "index error %d for single indirect in alloc_blks\n", i);
			return -EFAULT;
		}
		p[i] = balloc();
		if (p[i] == -1)
		{
			fprintf(stderr, "balloc error blk# %d in alloc_blks\n", p[i]);
			return -ENOMEM;
		}
		// init blk
		char sub_buf[BLK_SZ];
		memset(sub_buf, 0, sizeof(sub_buf));
		if (bwrite(p[i], sub_buf) == -1)
		{
			fprintf(stderr, "bwrite error blk# %d in alloc_blks\n", blk_num);
			return -EFAULT;
		}
		//
		if (bwrite(blk_num, buf) == -1)
		{
			fprintf(stderr, "bwrite error blk# %d in alloc_blks\n", blk_num);
			return -EFAULT;
		}
		count ++;
		ci->blks_in_use += 1;
	}
	// TODO: double indirect blocks.

	return 0;
}

static int alloc_blks_for_truncate(struct in_core_inode *ci, int new_blks_in_use);
// on success: returns the number of bytes written is returned. 0: nothing is written.
// on failure: returns -1.
int write_v2(struct in_core_inode* ci, const char* buf, int size, int offset)
{
	int res;
	// copy buf to the file from the offset, update to size of bytes.
        if (ci == NULL)
                return -ENOENT;
	if (offset + size > MAX_FILE_SIZE)
	{
		fprintf(stderr, "new file size exceeds maximum file size\n");
		return -1;
	}
	if (offset + size > ci->file_size)
	{
#if _DEBUG
		printf("offset+size = %d exceeds file_size %d\n", offset+size, ci->file_size);
#endif
		int new_blks_in_use = (offset + size + BLK_SZ - 1 )/BLK_SZ;
		if (new_blks_in_use > ci->blks_in_use)
		{
                        // allocate more blks.
                        if (alloc_blks_for_truncate(ci, new_blks_in_use) != 0)
                        {
                                fprintf(stderr, "alloc blks error in write\n");
				dump_super();
				dump_datablks();
				exit(0);
                                return -1;
                        }
			ci->blks_in_use = new_blks_in_use;
			ci->modified = 1;
		}
	}
	int count = 0; // the bytes that are copied to buf
	while (count < size)
	{
		int blk_num;
		int offset_blk;
		res = bmap(ci, offset + count, &blk_num, &offset_blk);
		if (res != 0)
		{
			fprintf(stderr, "bmap error in write\n");
			return -EFAULT;
		}
#if _DEBUG
		printf("blk_num = %d\n", blk_num);
		printf("offset_blk = %d\n", offset_blk);
#endif
                char blk_buf[BLK_SZ];
                res = bread(blk_num, blk_buf);
                if (res != 0)
                {
                        fprintf(stderr, "bread error blk# %d in write\n", blk_num);
                        return -EIO;
                }
		if (offset_blk + (size - count) <= BLK_SZ)
		{
			memcpy(blk_buf + offset_blk, buf + count, size - count);
#if _DEBUG
			printf("%d copied to disk buf\n", size - count);
#endif
			count = size;
		}
		else
		{
			int to_copy = BLK_SZ - offset_blk;
			memcpy(blk_buf + offset_blk, buf + count, to_copy);
#if _DEBUG
			printf("%d copied to disk buf\n", to_copy);
#endif
			count += to_copy;
		}
		res = bwrite(blk_num, blk_buf);
#if _DEBUG
		printf("bwriting to blk #%d\n", blk_num);
#endif
		if (res != 0)
		{
			fprintf(stderr, "bwrite error blk# %d in write\n", blk_num);
			return -EIO;
		}
	}
	ci->file_size = offset + size;
	ci->last_modified = get_time();
	ci->inode_last_mod = get_time();
	ci->modified = 1;
	res = iput(ci);
	if (res != 0)
	{
		fprintf(stderr, "iput error in write\n");
		return -EIO;
	}
	else
	{
#if _DEBUG
		printf("iput is successful\n");
#endif
	}
	return count;
}

// start is included, end is not. Free [start, end).
static int free_direct_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if (abs_start < 0)
		return -1;
	else if (abs_end > DIRECT_BLKS_PER_INODE)
		return -1;
	else if (abs_start > abs_end)
		return -1;
	int i;
	int blk_num;
	int start = abs_start;
	int end = abs_end;
#if _DEBUG
	printf("free_direct_blks, start = %d, end = %d\n", start, end);
#endif
	for (i = start; i < end; i++)
	{
		blk_num = ci->block_addr[i];
		if (blk_num == 0)
			continue;
		if (bfree(blk_num) == -1)
		{
			fprintf(stderr, "bfree error blk# %d when free direct blks\n", blk_num);
			return -1;
		}
		ci->block_addr[i] = 0;
	}
	return 0;
}

static int alloc_direct_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if (abs_start < 0)
		return -1;
	else if (abs_end > DIRECT_BLKS_PER_INODE)
		return -1;
	else if (abs_start > abs_end)
		return -1;
	int i;
	int blk_num;
	int start = abs_start;
	int end = abs_end;
#if _DEBUG
	printf("alloc_direct_blks, start = %d, end = %d\n", start, end);
#endif
	for (i = start; i < end; i++)
	{
		blk_num = balloc();
		if (blk_num == -1)
		{
			fprintf(stderr, "balooc error when alloc direct blks\n");
			return -1;
		}
		ci->block_addr[i] = blk_num;
	}
	return 0;
}

static int multi_bfree(int s_blk_num, int start, int end)
{
	char buf[BLK_SZ];
	if (s_blk_num == 0)
		return 0;
	if (bread(s_blk_num, buf) == -1)
	{
		fprintf(stderr, "bread error blk#%d in multi_bfree\n", s_blk_num);
		return -1;
	}
	int *p = (int*)buf;
	int i;
	int blk_num;
	for (i = start; i < end; i++)
	{
		blk_num = p[i];
		if (blk_num == 0)
			continue;
		if (bfree(blk_num) == -1)
		{
			fprintf(stderr, "bfree error blk#%d when multi_bfree\n", blk_num);
			return -1;
		}
		p[i] = 0;
	}
	if (bwrite(s_blk_num, buf) == -1)
	{
		fprintf(stderr, "bwrite error blk#%d when multi_bfree\n", s_blk_num);
		return -1;
	}
	return 0;
}

static int multi_balloc(int s_blk_num, int start, int end)
{
	char buf[BLK_SZ];
	if (bread(s_blk_num, buf) == -1)
	{
		fprintf(stderr, "bread error blk#%d in multi_balloc\n", s_blk_num);
		return -1;
	}
	int *p = (int*)buf;
	int i;
	int blk_num;
	for (i = start; i < end; i++)
	{
		blk_num = balloc();
		if (blk_num == -1)
		{
			fprintf(stderr, "balloc error when multi_balloc\n");
			return -1;
		}
		p[i] = blk_num;
	}
	if (bwrite(s_blk_num, buf) == -1)
	{
		fprintf(stderr, "bwrite error blk#%d when multi_balloc\n", s_blk_num);
		return -1;
	}
	return 0;
}

// start is included, end is not. Free [start, end).
static int free_single_ind_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if ((abs_start < DIRECT_BLKS_PER_INODE) || (abs_end > max_single) || (abs_start > abs_end))
	{
		fprintf(stderr, "range error when free single indirect blks: start %d, end %d\n", abs_start, abs_end);
		return -1;
	}
	// start, end is relative to single indirect blocks
	int start = abs_start - DIRECT_BLKS_PER_INODE;
	int end = abs_end - DIRECT_BLKS_PER_INODE;
	int i;
	int s_blk_num;
	int blk_num;
	s_blk_num = ci->single_ind_blk;
	if (multi_bfree(s_blk_num, start, end) == -1)
	{
		fprintf(stderr, "multi_bfree error blk#%d when free single indirect blks\n", s_blk_num);
		return -1;
	}
	if (start == 0 && end == RANGE_SINGLE)
	{// need to release the single indirect table blk.
                if (bfree(s_blk_num) == -1)
                {
                        fprintf(stderr, "bfree error blk#%d when free single indirect blks\n", s_blk_num);
                        return -1;
                }
		ci->single_ind_blk = 0;
		ci->modified = 1;
	}
	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error in free_single_ind_blks\n");
		return -1;
	}
	return 0;
}

// start is included, end is not. Free [start, end).
static int alloc_single_ind_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if ((abs_start < DIRECT_BLKS_PER_INODE) || (abs_end > max_single) || (abs_start > abs_end))
	{
		fprintf(stderr, "range error when alloc single indirect blks: start %d, end %d\n", abs_start, abs_end);
		return -1;
	}
	// start, end is relative to single indirect blocks
	int start = abs_start - DIRECT_BLKS_PER_INODE;
	int end = abs_end - DIRECT_BLKS_PER_INODE;
	int i;
	int s_blk_num;
	int blk_num;
	if (ci->single_ind_blk == 0)
	{ // need to alloc a blk for single ind.
		s_blk_num = balloc();
		if (s_blk_num == -1)
		{
			fprintf(stderr, "balloc error when alloc single indirect blks\n");
			return -1;
		}
		ci->single_ind_blk = s_blk_num;
		ci->modified = 1;
	}
	s_blk_num = ci->single_ind_blk;

	if (multi_balloc(s_blk_num, start, end) == -1)
	{
		fprintf(stderr, "multi_balloc error blk#%d when alloc single indirect blks\n", s_blk_num);
		return -1;
	}
	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error in alloc_single_ind_blks\n");
		return -1;
	}
	return 0;
}

static int free_double_ind_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if ((abs_start < max_single) || (abs_end > max_double) || (abs_start > abs_end))
	{
		fprintf(stderr, "range error when free double indirect blks: start %d, end %d\n", abs_start, abs_end);
		return -1;
	}
	// start, end is relative to double indirect blocks
	int start = abs_start - max_single;
	int end = abs_end - max_single;
	int first_ind_blk_idx = start / RANGE_SINGLE;
	int first_ind_blk_off = start % RANGE_SINGLE;
	int last_ind_blk_idx = end / RANGE_SINGLE;
	int last_ind_blk_off = end % RANGE_SINGLE;
	int i;
	int d_blk_num; // double indirect blk num.
	d_blk_num = ci->double_ind_blk;
	if (d_blk_num == 0)
	{
		fprintf(stderr, "error: d_blk_num = 0, not possible in free_double_ind_blks\n");
		return -1;
	}
	char d_ind_buf[BLK_SZ];
	if (bread(d_blk_num, d_ind_buf) == -1)
	{
		fprintf(stderr, "bread error blk# %d when free double ind blks\n", d_blk_num);
		return -1;
	}
	int *d_p = (int*)d_ind_buf;
	int s_blk_num;
	// get the first ind blk and free from the off until the end;
	if (first_ind_blk_idx == last_ind_blk_idx)
	{  // just free blks in one indirect block
		s_blk_num = d_p[first_ind_blk_idx];
		if (multi_bfree(s_blk_num, first_ind_blk_off, last_ind_blk_off) == -1)
		{
			fprintf(stderr, "multi_bfree error blk# %d when free double ind blks\n", s_blk_num);
			return -1;
		}
		if (first_ind_blk_off == 0 && last_ind_blk_off == RANGE_SINGLE-1)
		{
                	if (bfree(s_blk_num) == -1)
                	{
                	        fprintf(stderr, "bfree error blk#%d when free double indirect blks\n", s_blk_num);
                	        return -1;
                	}
			d_p[first_ind_blk_idx] = 0;
		}
	}
	else if (first_ind_blk_idx < last_ind_blk_idx)
	{ // free blks in the first, the last and full middle indirect blocks
		// free blks in the first_ind_blk
		s_blk_num = d_p[first_ind_blk_idx];
		if (multi_bfree(s_blk_num, first_ind_blk_off, RANGE_SINGLE) == -1)
		{
			fprintf(stderr, "multi_bfree error blk# %d when free double ind blks\n", s_blk_num);
			return -1;
		}
		if (first_ind_blk_off == 0)
		{
                	if (bfree(s_blk_num) == -1)
                	{
                	        fprintf(stderr, "bfree error blk#%d when free double indirect blks\n", s_blk_num);
                	        return -1;
                	}
			d_p[first_ind_blk_idx] = 0;
		}
		// free blks in the middle ind blks
		for (i = first_ind_blk_idx + 1; i <= last_ind_blk_idx - 1; i++)
		{
			s_blk_num = d_p[i];
			if (multi_bfree(s_blk_num, 0, RANGE_SINGLE) == -1)
			{
				fprintf(stderr, "multi_bfree error blk# %d when free double ind blks\n", s_blk_num);
				return -1;
			}
                	if (bfree(s_blk_num) == -1)
                	{
                	        fprintf(stderr, "bfree error blk#%d when free double indirect blks\n", s_blk_num);
                	        return -1;
                	}
			d_p[i] = 0;

		}
		// free blks in the last_ind_blk
		s_blk_num = d_p[last_ind_blk_idx];
		if (multi_bfree(s_blk_num, 0, last_ind_blk_off) == -1)
		{
			fprintf(stderr, "multi_bfree error blk# %d when free double ind blks\n", s_blk_num);
			return -1;
		}
		if (last_ind_blk_off == RANGE_SINGLE - 1)
		{
                	if (bfree(s_blk_num) == -1)
                	{
                	        fprintf(stderr, "bfree error blk#%d when free double indirect blks\n", s_blk_num);
                	        return -1;
                	}
			d_p[last_ind_blk_idx] = 0;
		}
	}
	else
	{
		fprintf(stderr, "error: first_ind_blk_idx > last_ind_blk_idx\n");
		return -1;
	}

	// write d_ind_buf back to disk.
	if (bwrite(d_blk_num, d_ind_buf) == -1)
	{
		fprintf(stderr, "bwrite error blk# %d when free double ind blks\n", d_blk_num);
		return -1;
	}

	if (start == 0 && end == RANGE_DOUBLE)
	{ // free the double indirect table
		if (bfree(d_blk_num) == -1)
		{
			fprintf(stderr, "bfree error blk# %d when free double ind blks\n", d_blk_num);
			return -1;
		}
		ci->double_ind_blk = 0;
		ci->modified = 1;
	}
	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error in free_double_ind_blks\n");
		return -1;
	}

	return 0;
} // free_double_ind_blks()

static int alloc_double_ind_blks(struct in_core_inode *ci, int abs_start, int abs_end)
{
	if ((abs_start < max_single) || (abs_end > max_double) || (abs_start > abs_end))
	{
		fprintf(stderr, "range error when alloc double indirect blks: start %d, end %d\n", abs_start, abs_end);
		return -1;
	}
	// start, end is relative to double indirect blocks
	int start = abs_start - max_single;
	int end = abs_end - max_single;
	int first_ind_blk_idx = start / RANGE_SINGLE;
	int first_ind_blk_off = start % RANGE_SINGLE;
	int last_ind_blk_idx = end / RANGE_SINGLE;
	int last_ind_blk_off = end % RANGE_SINGLE;
	int i;
	int d_blk_num; // double indirect blk num.
	if (ci->double_ind_blk == 0)
	{
		d_blk_num = balloc();
		if (d_blk_num == -1)
		{
			fprintf(stderr, "balloc error in alloc double ind blks\n");
			return -1;
		}
		ci->double_ind_blk = d_blk_num;
		ci->modified = 1;
	}
	d_blk_num = ci->double_ind_blk;
	char d_ind_buf[BLK_SZ];
	if (bread(d_blk_num, d_ind_buf) == -1)
	{
		fprintf(stderr, "bread error blk# %d when alloc double ind blks\n", d_blk_num);
		return -1;
	}
	int *d_p = (int*)d_ind_buf;
	int s_blk_num;
	// get the first ind blk and alloc from the off until one blk less than the end;
	if (first_ind_blk_idx == last_ind_blk_idx)
	{  // just alloc blks in one indirect block
		if (d_p[first_ind_blk_idx] == 0)
		{
			s_blk_num = balloc();
			if (s_blk_num == -1)
			{
				fprintf(stderr, "balloc error in alloc double ind blks\n");
				return -1;
			}
			d_p[first_ind_blk_idx] = s_blk_num;
		}
		s_blk_num = d_p[first_ind_blk_idx];
		if (multi_balloc(s_blk_num, first_ind_blk_off, last_ind_blk_off) == -1)
		{
			fprintf(stderr, "multi_balloc error blk# %d when alloc double ind blks - place 1, d_blk_num = %d, first_ind_blk_idx = %d\n", s_blk_num, d_blk_num, first_ind_blk_idx);
			return -1;
		}
	}
	else if (first_ind_blk_idx < last_ind_blk_idx)
	{ // alloc blks in the first, the last and full middle indirect blocks
		// alloc blks in the first_ind_blk
		if (d_p[first_ind_blk_idx] == 0)
		{
			s_blk_num = balloc();
			if (s_blk_num == -1)
			{
				fprintf(stderr, "balloc error in alloc double ind blks\n");
				return -1;
			}
			d_p[first_ind_blk_idx] = s_blk_num;
		}
		s_blk_num = d_p[first_ind_blk_idx];
		if (multi_balloc(s_blk_num, first_ind_blk_off, RANGE_SINGLE) == -1)
		{
			fprintf(stderr, "multi_balloc error blk# %d when alloc double ind blks - place 2\n", s_blk_num);
			return -1;
		}
		// free blks in the middle ind blks
		for (i = first_ind_blk_idx + 1; i <= last_ind_blk_idx - 1; i++)
		{
			s_blk_num = balloc();
			if (s_blk_num == -1)
			{
				fprintf(stderr, "balloc error in alloc double ind blks\n");
				return -1;
			}
			d_p[i] = s_blk_num;
			if (multi_balloc(s_blk_num, 0, RANGE_SINGLE) == -1)
			{
				fprintf(stderr, "multi_balloc error blk# %d when alloc double ind blks - place 3\n", s_blk_num);
				return -1;
			}
		}
		// free blks in the last_ind_blk
		s_blk_num = balloc();
		if (s_blk_num == -1)
		{
			fprintf(stderr, "balloc error in alloc double ind blks\n");
			return -1;
		}
		d_p[last_ind_blk_idx] = s_blk_num;
		if (multi_balloc(s_blk_num, 0, last_ind_blk_off) == -1)
		{
			fprintf(stderr, "multi_balloc error blk# %d when alloc double ind blks - place 4\n", s_blk_num);
			return -1;
		}
	}
	else
	{
		fprintf(stderr, "error: first_ind_blk_idx > last_ind_blk_idx\n");
		return -1;
	}

	// write d_ind_buf back to disk.
	if (bwrite(d_blk_num, d_ind_buf) == -1)
	{
		fprintf(stderr, "bwrite error blk# %d when alloc double ind blks\n", d_blk_num);
		return -1;
	}

	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error in alloc_double_ind_blks\n");
		return -1;
	}

	return 0;
} // alloc_double_ind_blks()


// new_blks_in_use < ci->blks_in_use
// (x, y): x and y belong to {A, B, C}
// A: x < DIRECT_BLKS_PER_INODE
// B: DIRECT_BLKS_PER_INODE < x < max_single
// C: max_single < x < max_double
static int free_blks_for_truncate(struct in_core_inode *ci, int new_blks_in_use)
{
#if _DEBUG
	printf("curr blks_in_use = %d, new_blks_in_use = %d\n", ci->blks_in_use, new_blks_in_use);
#endif
	int abs_start = new_blks_in_use;
	int abs_end = ci->blks_in_use;
	if (abs_start >= max_double)
	{
		fprintf(stderr, "abs_start %d exceeds max_double %d in free_blks_for_truncate\n", abs_start, max_double);
		return -1;
	}
	if (abs_end > max_double)
	{
		fprintf(stderr, "abs_end %d exceeds max_double %d in free_blks_for_truncate\n", abs_end, max_double);
		return -1;
	}

	if (abs_start >= 0 && abs_start < DIRECT_BLKS_PER_INODE)
	{
		if (abs_end <= DIRECT_BLKS_PER_INODE)  // (A, A)
		{
			if (free_direct_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "free_direct_blks error\n");
				return -1;
			}
		}
		else if (abs_end > DIRECT_BLKS_PER_INODE && abs_end <= max_single) // (A, B)
		{
			if (free_direct_blks(ci, abs_start, DIRECT_BLKS_PER_INODE) != 0)
			{
				fprintf(stderr, "free_direct_blks error\n");
				return -1;
			}
			if (free_single_ind_blks(ci, DIRECT_BLKS_PER_INODE, abs_end) != 0)
			{
				fprintf(stderr, "free_single_ind_blks error\n");
				return -1;
			}
		}
		else if (abs_end > max_single && abs_end <= max_double) // (A, C)
		{
			if (free_direct_blks(ci, abs_start, DIRECT_BLKS_PER_INODE) != 0)
			{
				fprintf(stderr, "free_direct_blks error\n");
				return -1;
			}
			if (free_single_ind_blks(ci, DIRECT_BLKS_PER_INODE, max_single) != 0)
			{
				fprintf(stderr, "free_single_ind_blks error\n");
				return -1;
			}
			if (free_double_ind_blks(ci, max_single, abs_end) != 0)
			{
				fprintf(stderr, "free_double_ind_blks error\n");
				return -1;
			}
		}
	}
	else if (abs_start >= DIRECT_BLKS_PER_INODE && abs_start < max_single)
	{
		if (abs_end > DIRECT_BLKS_PER_INODE && abs_end <= max_single) // (B, B)
		{
			if (free_single_ind_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "free_single_ind_blks error\n");
				return -1;
			}
		}
		else if (abs_end > max_single && abs_end <= max_double) // (B, C)
		{
			if (free_single_ind_blks(ci, abs_start, max_single) != 0)
			{
				fprintf(stderr, "free_single_ind_blks error\n");
				return -1;
			}
			if (free_double_ind_blks(ci, max_single, abs_end) != 0)
			{
				fprintf(stderr, "free_double_ind_blks error\n");
				return -1;
			}
		}
	}
	else if (abs_start >= max_single && abs_start < max_double)
	{
		if (abs_end > max_single && abs_end <= max_double) // (C, C)
		{
			if (free_double_ind_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "free_double_ind_blks error\n");
				return -1;
			}
		}
	}

	return 0;
} // free_blks_for_truncate()

// new_blks_in_use > ci->blks_in_use
// (x, y): x and y belong to {A, B, C}
// A: x < DIRECT_BLKS_PER_INODE
// B: DIRECT_BLKS_PER_INODE < x < max_single
// C: max_single < x < max_double
static int alloc_blks_for_truncate(struct in_core_inode *ci, int new_blks_in_use)
{
#if _DEBUG
	printf("curr blks_in_use = %d, new_blks_in_use = %d\n", ci->blks_in_use, new_blks_in_use);
#endif
	int abs_start = ci->blks_in_use;
	int abs_end = new_blks_in_use;
	if (abs_start >= max_double)
	{
		fprintf(stderr, "abs_start %d exceeds max_double %d in alloc_blks_for_truncate\n", abs_start, max_double);
		return -1;
	}
	if (abs_end > max_double)
	{
		fprintf(stderr, "abs_end %d exceeds max_double %d in alloc_blks_for_truncate\n", abs_end, max_double);
		return -1;
	}

	if (abs_start >= 0 && abs_start < DIRECT_BLKS_PER_INODE)
	{
		if (abs_end <= DIRECT_BLKS_PER_INODE)  // (A, A)
		{
			if (alloc_direct_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "alloc_direct_blks error\n");
				return -1;
			}
		}
		else if (abs_end > DIRECT_BLKS_PER_INODE && abs_end <= max_single) // (A, B)
		{
			if (alloc_direct_blks(ci, abs_start, DIRECT_BLKS_PER_INODE) != 0)
			{
				fprintf(stderr, "alloc_direct_blks error\n");
				return -1;
			}
			if (alloc_single_ind_blks(ci, DIRECT_BLKS_PER_INODE, abs_end) != 0)
			{
				fprintf(stderr, "alloc_single_ind_blks error\n");
				return -1;
			}
		}
		else if (abs_end > max_single && abs_end <= max_double) // (A, C)
		{
			if (alloc_direct_blks(ci, abs_start, DIRECT_BLKS_PER_INODE) != 0)
			{
				fprintf(stderr, "alloc_direct_blks error\n");
				return -1;
			}
			if (alloc_single_ind_blks(ci, DIRECT_BLKS_PER_INODE, max_single) != 0)
			{
				fprintf(stderr, "alloc_single_ind_blks error\n");
				return -1;
			}
			if (alloc_double_ind_blks(ci, max_single, abs_end) != 0)
			{
				fprintf(stderr, "alloc_double_ind_blks error\n");
				return -1;
			}
		}
	}
	else if (abs_start >= DIRECT_BLKS_PER_INODE && abs_start < max_single)
	{
		if (abs_end > DIRECT_BLKS_PER_INODE && abs_end <= max_single) // (B, B)
		{
			if (alloc_single_ind_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "alloc_single_ind_blks error\n");
				return -1;
			}
		}
		else if (abs_end > max_single && abs_end <= max_double) // (B, C)
		{
			if (alloc_single_ind_blks(ci, abs_start, max_single) != 0)
			{
				fprintf(stderr, "alloc_single_ind_blks error\n");
				return -1;
			}
			if (alloc_double_ind_blks(ci, max_single, abs_end) != 0)
			{
				fprintf(stderr, "alloc_double_ind_blks error\n");
				return -1;
			}
		}
	}
	else if (abs_start >= max_single && abs_start < max_double)
	{
		if (abs_end > max_single && abs_end <= max_double) // (C, C)
		{
			if (alloc_double_ind_blks(ci, abs_start, abs_end) != 0)
			{
				fprintf(stderr, "alloc_double_ind_blks error\n");
				return -1;
			}
		}
	}

	return 0;
} // alloc_blks_for_truncate()


/*If the file previously was larger than this size, the extra data is lost. If the file previously was shorter, it is extended, and the extended part reads as null bytes ('\0').*/
int truncate_v2(struct in_core_inode* ci, int length)
{
#if _DEBUG
	printf("\ntruncate_v2 called\n");
#endif
	int res;
	if (ci == NULL)
		return -ENOENT;
	if (length < ci->file_size)
	{
		int new_blks_in_use = (length-1 + BLK_SZ)/BLK_SZ;
		if (new_blks_in_use < ci->blks_in_use)
		{
			// truncate blks.
			if (free_blks_for_truncate(ci, new_blks_in_use) != 0)
			{
				fprintf(stderr, "free blks error in truncate\n");
				return -1;
			}
		}
		else if (new_blks_in_use == ci->blks_in_use)
		{
			// truncate file.
			ci->file_size = length;
		}
		else
		{
			fprintf(stderr, "error: new_blks_in_use > blk_in_use, while length < file_size\n");
			return -1;
		}
		ci->blks_in_use = new_blks_in_use;
		ci->modified = 1;
	}
	else if (length == ci->file_size)
	{ // nothing to do.
	}
	else  // length > file_size
	{ // TODO
		int new_blks_in_use = (length-1 + BLK_SZ)/BLK_SZ;
		if (new_blks_in_use > ci->blks_in_use)
		{
			// allocate more blks.
			if (alloc_blks_for_truncate(ci, new_blks_in_use) != 0)
			{
				fprintf(stderr, "alloc blks error in truncate\n");
				return -1;
			}
		}
		else if (new_blks_in_use == ci->blks_in_use)
		{
			// just update the file size;
			ci->file_size = length;
		}
		else
		{
			fprintf(stderr, "error: new_blks_in_use < blks_in_use, while length > file_fize\n");
			return -1;
		}
		ci->blks_in_use = new_blks_in_use;
		ci->modified = 1;
	}
	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error when truncate\n");
		return -1;
	}
	return 0;
}







