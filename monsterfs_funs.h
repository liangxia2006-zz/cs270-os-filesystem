#ifndef MONSTERFS_FUNS

#define MONSTERFS_FUNS

#define IN_MEM_STORE    0     // 1, using in-memory storage emulator
                              // 0, attaching to block device storage
#define BLOCK_DEV_PATH  "/dev/vdc"
//#define BLK_SZ          256   // in bytes (old=128). superblock is 184 bytes
                              // so, BLK_SZ should be at least 256 bytes
#define BLK_SZ		4096
//#define NUM_BLKS        256   // number of blocks of storage (old=1024)
                              // device size = BLK_SZ * NUM_BLKS
//#define NUM_BLKS	1310720	// 5GB/4KB = 1,310,720 blocks
#define NUM_BLKS	1048576 // 4GB/4KB
#define ADDR_SZ         4     // unsigned int is 4 bytes
#define IN_MEM_FD       -2    // in-memory fake file descriptor
#define FREE_BLKS_PER_LINK (BLK_SZ>>2)     // # of blk idx #s in a block

#define ILIST_SPACE (64)                  // # of blocks that contains inodes

#define MAX_FREE_ILIST_SIZE (32)          // # of free inodes in superblk ilist
#define FILE_OWNER_ID_LEN 16              // length of file owner's ID
#define DIRECT_BLKS_PER_INODE 10             // # of direct block addr in an inode
#define RANGE_SINGLE   (BLK_SZ>>2)            // blk range of single indirect
#define RANGE_DOUBLE   (RANGE_SINGLE*RANGE_SINGLE)   // bLK range of double indirect
#define INODES_PER_BLK  (BLK_SZ/sizeof(struct disk_inode))    // # of inodes per block
#define DIR_ENTRY_LENGTH  256  // in bytes
#define DIR_ENTRIES_PER_BLK   (BLK_SZ/DIR_ENTRY_LENGTH)
#define FILE_NAME_LEN     (DIR_ENTRY_LENGTH-4)    // file name size in a dir entry
#define MAX_ENTRY_OFFSET  (100*DIR_ENTRY_LENGTH) // max entries in a directory
#define BAD_I_NUM         (-1)    // inode num indicates the end of a dir entry
#define EMPTY_I_NUM         (-2)    // inode num indicates an unused dir entry
#define MAX_PATH_LEN      (100)   // max characters in a path
#define MAX_FILE_SIZE     (1<<30)//(2147483647)  // 2GB

#define NAMEI_CACHE_SZ		32	// number of path->inode mappings

#define _DEBUG       0 // 1: show debug info
#define USE_NAMEI_CACHE		1

struct super_block {
        int blk_size;           // the block size
        int num_blks;           // total number of blks on the disk.
        long long fs_size;            // file system size
        int max_free_blks;      // max number of free blocks
        int num_free_blks;      // current number of free blocks
        int data_blk_offset;    // the blk number of the first data block on the file system
        /* free block list */
        int free_blk_list_head; // the blk # that contains free blk list numbers.
        int next_free_blk_idx;  // the index which points to the first available free blk
        /*TODO: int free_blk_list_cache[FREE_BLKS_PER_LINK];*/
        /* list of free inodes*/
        int max_free_inodes;    // max number of free inodes on disk
        int num_free_inodes;    // current number of free inodes on disk
        int remembered_inode;   // starting from this inode, a search routine can find as many
				// free inodes to fill the free_ilist. Before this number, there
				// should be no free inode.
        int free_ilist[MAX_FREE_ILIST_SIZE];    // a cache for the list of free inodes
        int next_free_inode_idx;                // the index which points to the next available inode.
        /*TODO: lock fields for free blks and ilists.*/
//        int modified; /* 0: original, 1: modified */
//        int locked; /* 0: free, 1: locked */
};

enum FILE_TYPE {
        UNUSED=0,
        REGULAR=1,
        DIRECTORY,
        CHARACTER,     // not used in our fs
        BLOCK_SPECIAL, // not used in our fs 
        FIFO           // not used in our fs
};

struct disk_inode {
        enum FILE_TYPE file_type;
        char owner_id[FILE_OWNER_ID_LEN];     // the id of inode owner
        int access_permission;      // default is 0777
        int last_accessed;          // last access time of the file    
        int last_modified;          // last modification time of the file
        int inode_last_mod;         // last modification time to the inode
        int link_count;             // hard link for the inode.
        int file_size;
	int blks_in_use;  // currently allocated blks. indexing blks not counted.
        int block_addr[DIRECT_BLKS_PER_INODE];    // direct blks on the inode
	int single_ind_blk;  // single indirect block addr
	int double_ind_blk;  // double indirect block addr
}; //size = 96 bytes?

struct in_core_inode { // the same version of disk inode except it is in-core.
        enum FILE_TYPE file_type;
        char owner_id[FILE_OWNER_ID_LEN];
        int access_permission;
        int last_accessed;
        int last_modified;
        int inode_last_mod;
        int link_count;
        int file_size;
	int blks_in_use;  // allocated blks, doesn't count indexing blks.
	// for directory, file_size is always a multiple of blks_in_use.
	// but file doesn't.
        int block_addr[DIRECT_BLKS_PER_INODE];
	int single_ind_blk;  // single indirect block addr
	int double_ind_blk;  // double indirect block addr
	// below are in-core fields
        int locked;         // TODO: currently not in meaningful use.
        int modified;       // if modified == 1, then write disk when iput() is called.
        int i_num;          // the inode number
        //int ref_count;      // reference count is used by open file table.
};

struct directory_entry {
  int inode_num;
  char file_name[FILE_NAME_LEN];
};

struct namei_cache_element {
	char path[DIR_ENTRY_LENGTH];
	struct in_core_inode *iNode;
	int timestamp;
};

// TODO: init_storage(), cleanup_storage(), bread() and bwrite() should
//  be declared static once testing is complete

// Procedures required to prepare a storage device for mounting. Returns
// 0 on success and -1 on failure.
int init_storage();

// Procedures required to prepare for demounting. Returns 0 on success and
// -1 on failure.
int cleanup_storage();

// Reads block at specified storage block number. Fills provided buffer
// with a block of data and returns 0 on success, -1 on failure.
int bread(unsigned int blk, char *buffer);

// Writes contents of buffer to specified storage block offset. Returns
// 0 on success and -1 on failure.
int bwrite(unsigned int blk, const char *buffer);

// dump info about superblk, free lists, and disk data
void dump(void); 
void dump_super(void); // only dump super
void dump_datablks(void); // only dump datablks

// get current time
int get_time(void);

// the mkfs system call creates superblock, free blk list and free ilist on disk
// return 0: successful; return -1: failure.
int mkfs(void);

// initialize superblk in memory. This function doesn't write to disk.
int init_super(void);

// allocate a block
int balloc(void);

// free a block
int bfree(int);

// read inode from disk, and make an in-core copy
struct in_core_inode* iget(int i_num);

// called when the kernel releases an inode
int iput(struct in_core_inode* pi);

void dump_in_core_inode(struct in_core_inode* ci);

// alloc an in-core inode from free ilist
struct in_core_inode* ialloc(void);

// free an in-core inode, put it back on free ilist
int ifree(struct in_core_inode* ci);

// map a logical file byte offset to file system block
// TODO: bytes of I/O in block?
// input : ci - in_core inode
//	   off - byte offset
// output: blk_num - disk blk#
//	   offset_blk - byte offset in the block
int bmap(const struct in_core_inode* ci, const int off, int* blk_num, 
	int* offset_blk);

// setup namei cache
void init_namei_cache();

struct namei_cache_element *find_namei_cache_by_path(const char *path);

struct namei_cache_element *find_namei_cache_by_oldest();

// a slight modified version of namei
struct in_core_inode* namei_v2(const char *path);

// a slight modified version of mkdir
int mkdir_v2(const char *path, int mode);

// remove a directory
int rmdir(const char *path);

// create root directory, 0 success, -1 failure
int mkrootdir();

// modified version of mknod
int mknod_v2(const char *path, int mode, int dev);

// delete a name and possibly the file it refers to
int unlink(const char *pathname);

// Utility functions for splitting paths into node name and path to node
int separate_node_name(const char *path, char *node_name);
int separate_node_path(const char *path, char *node_path);
int separate_node(const char *path, char *node_part, int part); //0 path, 1 node name


/* below are system calls for open/close/read/write a file*/

#if 0
// on success, returns a non-negative smallest number
// on failure, returns -1
int m_open(const char* path, int mode);

// close a file descriptor.
// returns 0 on success, -1 on error
int m_close(int fd);
#endif

// on success: returns the number of bytes read is returned. 0: end of file
// on failure: returns -1
int read_v2(struct in_core_inode* ci, char* buf, int size, int offset1);

// on success: returns the number of bytes written is returned. 0: nothing is written.
// on failure: returns -1.
int write_v2(struct in_core_inode* ci, const char* buf, int size, int offset1);

/*If the file previously was larger than this size, the extra data is lost. If the file previously was shorter, it is extended, and the extended part reads as null bytes ('\0').*/
int truncate_v2(struct in_core_inode* ci, int length);

#endif
