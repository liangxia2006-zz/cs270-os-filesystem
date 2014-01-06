//  MonsterFS
//
//  For UC Santa Barbara Fall 2013 CS 270

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "monsterfs_funs.h"

#define DEFAULT_PERMS 0777

static int map_inode_to_stat(struct in_core_inode *inode, struct stat *stbuf)
{
  // TODO: access permission is not correct.
  // TODO: access time is not correct.
  if(inode == NULL || stbuf == NULL)
  {
    fprintf(stderr, "error: map_inode_to_stat reveiced null pointer\n");
    return -1;
  }
  stbuf->st_ino = inode->i_num;
  stbuf->st_uid = 0;
  stbuf->st_gid = 0;
  stbuf->st_size = inode->file_size;
  stbuf->st_blksize = BLK_SZ;
  stbuf->st_atime = inode->last_accessed;
  stbuf->st_mtime = inode->last_modified;
  stbuf->st_ctime = inode->inode_last_mod;
  stbuf->st_nlink = inode->link_count;
  if(inode->file_type == DIRECTORY)
  {
    stbuf->st_mode = S_IFDIR | 0777;
  }
  else
  {
    stbuf->st_mode = S_IFREG | 0777;
  }

  return 0;
}

static int m_getattr(const char *path, struct stat *stbuf)
{
  struct in_core_inode *inode;
  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  inode = namei_v2(path);
  if(inode == NULL)
  {
	return -ENOENT;
  }

  if(map_inode_to_stat(inode, stbuf) == -1)
  {
	fprintf(stderr, "map inode to stat error\n");
    return -1;
  }

  return res;
}

static int m_mkdir(const char *path_name, mode_t mode)
{
#if _DEBUG
	printf("\nm_mkdir gets called\n");
#endif
	int res = mkdir_v2(path_name, mode);
	return res;
}

static int m_mknod(const char *path, mode_t mode, dev_t dev)
{
#if _DEBUG
	printf("\nm_mknod gets called\n");
#endif
	// TODO: dev is ignored.
	int res = mknod_v2(path, mode, dev);
	return res;
}

static int m_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
        (void) offset;
        (void) fi;

#if _DEBUG
	//printf("\nm_readdir gets called\n");
#endif
	struct in_core_inode* ci;
	ci = namei_v2(path);
	if (ci == NULL)
		return -ENOENT;
	if (ci->file_type != DIRECTORY)
		return -ENOTDIR;

        int off;
        struct directory_entry* dir_entry;
        // TODO: needs optimization: read a block at a time for several dir entries look-up.
        for (off = 0; off < MAX_ENTRY_OFFSET - DIR_ENTRY_LENGTH; off += DIR_ENTRY_LENGTH)
        {
                int blk_num;
                int offset_blk;

                if (bmap(ci, off, &blk_num, &offset_blk) == -1)
                        return -ENXIO;  /* No such device or address */
                char buf[BLK_SZ];
                if (bread(blk_num, buf) == -1)
                        return -ENOENT;  /* No such device or address */

                dir_entry = (struct directory_entry*)(buf + offset_blk);
		int i_num = dir_entry->inode_num;
		if (i_num == BAD_I_NUM)  // the end of dir.
			break;
		if (i_num == EMPTY_I_NUM)  // not a valid entry, may be deleted.
			continue;
		// dir entry is valid, so show the entry.
		struct in_core_inode* i_entry = iget(i_num);
		if (i_entry == NULL)
			return -ENOENT;
		struct stat *stbuf = (struct stat*)malloc(sizeof(struct stat));
		if (stbuf == NULL)
			return -ENOMEM;
		memset(stbuf, 0, sizeof(struct stat));
		if (map_inode_to_stat(i_entry, stbuf) == -1)
			return -EFAULT;
		if (iput(i_entry) == -1)
			return -ENOENT;  // TODO: needs to get a better errno
		if (dir_entry == NULL)
			return -ENOENT;
		filler(buffer, dir_entry->file_name, stbuf, 0);
	}
        ci->last_accessed = get_time();
        ci->modified = 1;
        int res = iput(ci);
        if (res != 0)
        {
                fprintf(stderr, "iput error in read_v2\n");
                return -EIO;
        }
#if _DEBUG
	//printf("readdir complete\n");
#endif
	return 0;
}

static int m_rmdir(const char *path)
{
#if _DEBUG
	printf("\nm_rmdir gets called\n");
#endif
	int res = unlink(path);
	return res;
}

static int m_unlink(const char *path)
{
#if _DEBUG
	printf("\nm_unlink gets called\n");
#endif
	int res = unlink(path);
	return 0;
}

static int m_open(const char *path, struct fuse_file_info *fi)
{
#if _DEBUG
	printf("\nm_open gets called\n");
	// TODO: check permissions. Now ignores them.
	printf("\nopen flags: %d\n", fi->flags);
#endif
	return 0;
}

// TODO: does read always return the bytes it copied?
static int m_read(const char *path, char *buf, size_t size, off_t offset1, struct fuse_file_info *fi)
{
	int res;
	int offset = (int)offset1;
#if _DEBUG
	printf("\nm_read gets called: size = %zd, offset = %d\n", size, offset);
#endif
	// from offset, copy size of bytes from the file indicated by path to buf
        struct in_core_inode* ci;
        ci = namei_v2(path);
	res = read_v2(ci, buf, size, offset);
	return res;
}

static int m_write(const char *path, const char *buf, size_t size, off_t offset1, struct fuse_file_info *fi)
{
	int res;
	int offset = (int)offset1;
#if _DEBUG
	printf("\nm_write gets called: size = %zd, offset = %d\n", size, offset);
#endif
	// copy buf to the file from the offset, update to size of bytes.
        struct in_core_inode* ci;
        ci = namei_v2(path);
	res = write_v2(ci, buf, size, offset);
	return res;
}

static int m_release(const char *path, struct fuse_file_info *fi)
{
#if _DEBUG
	printf("\nm_release gets called\n");
#endif
	return 0;
}

static int m_truncate(const char *path, off_t length1)
{
	int res;
	int length = (int)length1;
#if _DEBUG
	printf("\nm_truncate gets called, length = %d\n", length);
#endif
        struct in_core_inode* ci;
        ci = namei_v2(path);
	res = truncate_v2(ci, length);
	return res;
}

static struct fuse_operations monster_oper = {
  .getattr    =     m_getattr,
  .mkdir      =     m_mkdir,
  .mknod      =     m_mknod,
  .readdir    =     m_readdir,
  .rmdir      =     m_rmdir,
  //.create     =     m_create,  /* if create is not implemented, then mknod gets called when create a file*/
  .unlink     =     m_unlink,
  .open       =     m_open,
  .read       =     m_read,
  .write      =     m_write,
  .release    =     m_release,
  .truncate    =     m_truncate,
};

int main(int argc, char *argv[])
{
	printf("open storage...\n");
	if (init_storage() == -1)
	{
		fprintf(stderr, "error: cannot init storage with error: %s\n", strerror(errno));
		return -1;
	}
#if IN_MEM_STORE
        printf("start mkfs...\n");
	if (mkfs() == -1)
	{
		fprintf(stderr, "error: mkfs\n");
		return -1;
	}
	printf("mkfs done\n");
#else
	printf("read superblk...\n");
	if (init_super() != 0)
	{
		fprintf(stderr, "error read superblk\n");
		return -1;
	}
#endif
	dump();
	int ret = 0;
	ret = fuse_main(argc, argv, &monster_oper, NULL);
	return ret;
}
