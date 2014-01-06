//  MonsterFS: clear, reset the storage and mkfs
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

int main(int argc, char *argv[])
{
	printf("open storage...\n");
	if (init_storage() == -1)
	{
		fprintf(stderr, "error: cannot init storage with error: %s\n", strerror(errno));
		return -1;
	}
        printf("start mkfs...\n");
	if (mkfs() != 0)
	{
		fprintf(stderr, "error: mkfs\n");
		return -1;
	}
	printf("mkfs done\n");
	dump();
	printf("close storage...\n");
	if (cleanup_storage() != 0)
	{
		fprintf(stderr, "error: cannot close storage with error: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}
