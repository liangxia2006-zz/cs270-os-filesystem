/*
  Test of MonsterFS library
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "monsterfs_funs.h"

void test_write(void)
{
	init_storage();
	mkfs();
	dump();

	char file1[] = "/file1";
	printf("\nmknod %s\n", file1);

	if (mknod_v2(file1, 0, 0) == -1)
		printf("mknod %s fails\n", file1);
	else
		printf("%s created", file1);
	dump();

	struct in_core_inode* ci = namei_v2(file1);
	if (ci == NULL)
	{
		fprintf(stderr, "namei error\n");
		return ;
	}
	char *buf = "hello";
	int count = write_v2(ci, buf, 6, 0);
	printf("%d bytes written\n", count);
	dump();

}

void test_open(void)
{
	init_storage();
	mkfs();
	dump();

	char dir1[] = "/foo";
	printf("\nmkdir %s\n", dir1);
	if (mkdir_v2(dir1, 0) == -1)
		printf("mkdir %s fails\n", dir1);
	else
		printf("%s created\n", dir1);
	dump();

	char file1[] = "/foo/file1";
	printf("\nmknod %s\n", file1);
	if (mknod_v2(file1, 0, 0) == -1)
		printf("mknod %s fails\n", file1);
	else
		printf("%s created", file1);
	dump();
#if 0
	printf("\nopen %s\n", file1);
	int fd1 = m_open(file1, 0);
	if (fd1 == -1)
		printf("open file fails\n");
	else
		printf("File: %s is opened, fd1 = %d\n", file1, fd1);

	printf("\nopen %s\n", file1);
	int fd2 = m_open(file1, 0);
	if (fd2 == -1)
		printf("open file fails\n");
	else
		printf("File: %s is opened, fd2 = %d\n", file1, fd2);
#endif
}

void test_rmdir(void)
{
	init_storage();
	mkfs();
	dump();

	char dir1[] = "/foo";
	printf("\nmkdir %s\n", dir1);
	if (mkdir_v2(dir1, 0) == -1)
		printf("mkdir %s fails\n", dir1);
	else
		printf("%s created\n", dir1);
	dump();

	if (rmdir(dir1) != 0)
		printf("rmdir %s fails\n", dir1);
	else
		printf("rmdir %s succeeds\n", dir1);
	dump();
}

void test_mkdir_mknod(void)
{
	init_storage();
	mkfs();
	dump();

	char dir1[] = "/foo";
	printf("\nmkdir %s\n", dir1);
	if (mkdir_v2(dir1, 0) == -1)
		printf("mkdir %s fails\n", dir1);
	else
		printf("%s created\n", dir1);
	dump();

	char dir2[] = "/foo/barbar";
	printf("\nmkdir %s\n", dir2);
	if (mkdir_v2(dir2, 0) == -1)
		printf("mkdir %s fails\n", dir2);
	else
		printf("%s created\n", dir2);
	dump();

	char dir3[] = "/foo/barbar/california";
	printf("\nmkdir %s\n", dir3);
	if (mkdir_v2(dir3, 0) == -1)
		printf("mkdir %s fails\n", dir3);
	else
		printf("%s created\n", dir3);
	dump();

	char file1[] = "/foo/file1";
	printf("\nmknod %s\n", file1);
	if (mknod_v2(file1, 0, 0) == -1)
		printf("mknod %s fails\n", file1);
	else
		printf("%s created", file1);
	dump();

	char file2[] = "/file2";
	printf("\nmknod %s\n", file2);
	if (mknod_v2(file2, 0, 0) == -1)
		printf("mknod %s fails\n", file2);
	else
		printf("%s created", file2);
	dump();

	// TODO: this case crashes.
	/*if (mkdir_v2("foo", 0) == -1)
	{
		printf("mkdir /foo fails\n");
	}*/
#if 0
	printf("\nmkdir /foo/bar/soo\n");
	if (mkdir_v2("/foo/bar/soo", 0) == -1)
		printf("mkdir /foo/bar/soo fails\n");
	else
		printf("/foo/bar/soo created\n");
#endif
}

void test_namei(void)
{
	init_storage();
	mkfs();
	dump();
	printf("\nsearch for i_num of /.\n");
	struct in_core_inode *ci = namei_v2("/.");
	if (ci != NULL)
		printf("i_num of dir /. is %d\n", ci->i_num);
	else
		printf("cannot find the inode for /.\n");

	printf("\nsearch for i_num of /..\n");
	struct in_core_inode *ci2 = namei_v2("/..");
	if (ci2 != NULL)
		printf("i_num of dir /.. is %d\n", ci2->i_num);
	else
		printf("cannot find the inode for /..\n");

	printf("\nsearch for i_num of /foo\n");
	struct in_core_inode *ci3 = namei_v2("/foo");
	if (ci3 != NULL)
		printf("i_num of dir /foo is %d\n", ci3->i_num);
	else
		printf("cannot find the inode for /foo\n");
	dump();
}

// test bmap,
void test_inode2(void)
{
	printf("blk size = %d, direct blocks per inode = %d\n", BLK_SZ, DIRECT_BLKS_PER_INODE);
	printf("block range:\n");
	printf("direct block: %d, single indirect: %d, double indirect: %d\n",
		DIRECT_BLKS_PER_INODE, RANGE_SINGLE, RANGE_DOUBLE);
	init_storage();
	mkfs();
	dump();
	// init...
	struct in_core_inode* ci = ialloc();
	struct in_core_inode* ci2 = ialloc();
	struct in_core_inode* ci3 = ialloc();
	int b = balloc();  // #66
	ci->block_addr[0] = b;
	b = balloc();      // #67
	ci->single_ind_blk = b;
	char buf[BLK_SZ];
	memset(buf, 0, sizeof(buf));
	int *p = (int*)buf;
	p[9] = balloc();   // #68
	if (bwrite(b, buf) == -1)
	{
		fprintf(stderr, "bwrite error\n");
		return;
	}
	memset(buf, 0, sizeof(buf));
	p = (int*)buf;
	b = balloc();  // #69
	ci->double_ind_blk = b;
	p[61] = balloc();  // #70
	if (bwrite(b, buf) == -1)
	{
		fprintf(stderr, "bwrite error\n");
		return;
	}
	b = p[61];
	memset(buf, 0, sizeof(buf));
	p = (int*)buf;
	p[22] = balloc();  // #71
	if (bwrite(b, buf) == -1)
	{
		fprintf(stderr, "bwrite error\n");
		return;
	}
	printf("init complete\n\n\n");
	dump();
	// 1. test single indirect
	int blk_num, offset_blk;
	if (bmap(ci, 5000, &blk_num, &offset_blk) == -1)
	{
		fprintf(stderr, "bmap error\n");
		return;
	}
	printf("get final blk_num = %d\n", blk_num);
	// 2. test double indirect
	if (bmap(ci, 1024000, &blk_num, &offset_blk) == -1)
	{
		fprintf(stderr, "bmap error\n");
		return;
	}
	printf("get final blk_num = %d\n", blk_num);

	// 3. test iget
	int i_num = ci->i_num;
	struct in_core_inode *temp = iget(i_num);
	if (temp == NULL)
	{
		fprintf(stderr, "iget error\n");
		return;
	}
	dump_in_core_inode(temp);
	if (iput(ci) == -1)
	{
		fprintf(stderr, "iput error\n");
		return;
	}
	free(temp);
	dump();

	temp = iget(i_num);
	if (temp == NULL)
	{
		fprintf(stderr, "iget error\n");
		return;
	}
	dump_in_core_inode(temp);
}

// test ialloc and ifree
void test_inode(void)
{
        printf("size of disk inode = %u\n", sizeof(struct disk_inode));
	init_storage();
        mkfs();
        dump();
        struct in_core_inode *ci[257];
        int i;
        for (i = 0; i < 257; i++)
        {
                ci[i] = ialloc();
		if (ci[i] == NULL)
		{
			printf("error: fail to ialloc\n");
			break;
		}
                printf("alloc inode #%d\n", ci[i]->i_num);
        }
        dump();

        for (i = 0; i < 32; i++)
        {
                ifree(ci[i]);
                //printf("lloc inode #%d\n", ci[i]->i_num);
        }

        dump();

	ifree(ci[32]);
	dump();

	struct in_core_inode *extra = ialloc();
	dump();
	cleanup_storage();
}


int test_block_algorithms(void)
{
        printf("hello, filesystem!\n");
	if(init_storage() == -1)
	{
		printf("init_storage() FAILED\n");
		return 0;
	}
	else
		printf("init_storage() passed\n");

	mkfs();
	dump();

	int ablk_num[192];
	int i;
	for (i = 0; i < 192; i++)
	{
		ablk_num[i] = balloc();
		if (ablk_num[i] == -1)
		{
			printf("error: balloc\n");
			break;
		}
	}
	dump();

	for (i = 0; i < 192; i++)
	{
		if (bfree(ablk_num[i]) == -1)
		{
			printf("error: bfree blk#%d\n", ablk_num[i]);
			break;
		}
	}
	dump();

	if(cleanup_storage() == -1)
		printf("cleanup_storage() FAILED\n");
	else
		printf("cleanup_storage() passed\n");

	return 0;
}

int test_storage()
{
  int ret_stat;
  char *buffer;
  int i;

// Test init_storage()
  if(init_storage() == -1)
  {
    printf("init_storage() FAILED\n");
    return 0;
  }
  else
    printf("init_storage() passed\n");

  buffer = (char *)malloc(BLK_SZ);

  for(i = 0; i < BLK_SZ-1; ++i)
  {
    buffer[i] = 'B';
  }
  buffer[BLK_SZ-1] = '\0';

  printf("Buffer init:\n%s\n\n", buffer);

// Test bwrite()
  if(bwrite(10, buffer) == -1)
  {
    printf("bwrite() FAILED\n");
    return 0;
  }
  else
    printf("bwrite() passed\n");

  for(i = 0; i < BLK_SZ-1; ++i)
  {
    buffer[i] = '0';
  }
  buffer[BLK_SZ-1] = '\0';

  printf("Buffer cleared:\n%s\n\n", buffer);

// Test bread()
  if(bread(10, buffer) == -1)
  {
    printf("bread() FAILED\n");
    return 0;
  }
  else
    printf("bread() passed\n");

  printf("Buffer returned:\n%s\n\n", buffer);

  if(cleanup_storage() == -1)
    printf("cleanup_storage() FAILED\n");
  else
    printf("cleanup_storage() passed\n");

  return 0;
}

int main()
{
	//test_storage();
	//test_block_algorithms();
	//test_inode();
	//test_inode2();
	//test_namei();
	//test_mkdir_mknod();
	//test_rmdir();
	//test_open();
	test_write();
	return 0;
}
