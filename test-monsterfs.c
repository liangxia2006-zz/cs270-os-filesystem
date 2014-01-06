/* test-monsterfs.c
 *
 * Test that file API works with MonsterFS.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "monsterfs_funs.h"

#define DI_ID_BNDRY     DIRECT_BLKS_PER_INODE * BLK_SZ
#define ID_ID2_BNDRY    (DIRECT_BLKS_PER_INODE + RANGE_SINGLE) * BLK_SZ
//#define ID2_END_BNDRY   (DIRECT_BLKS_PER_INODE + RANGE_SINGLE + RANGE_DOUBLE) * BLK_SZ
#define ID2_END_BNDRY	0xFFFFFFFF - 1

int main()
{
	//printf("ID2_END_BNDRY = %llu\n", RANGE_DOUBLE * BLK_SZ);
/*
	// test reading and writing to a file

	int fd = open("tm.txt", O_WRONLY|O_CREAT);
	if(fd == -1)
	{
		fprintf(stderr, "Unable to open text file.\n%s\n",
			strerror(errno));
	}

	char beef[] = "BEEFBEEFBEEF";
	int sz = write(fd, &beef, 12);
	if(sz != 12)
	{
		fprintf(stderr, "Unable to write to text file.\n");
	}

	if(close(fd) != 0)
	{
		fprintf(stderr, "Unable to close text file.\n");
	}

	fd = open("tm.txt", O_RDONLY);
	if(fd == -1)
	{
		fprintf(stderr, "Unable to re-open text file.\n");
	}

	char buf[12];
	sz = read(fd, &buf, 12);
	if(sz != 12)
	{
		fprintf(stderr, "Unable to open text file for read.\n");
	}

	if(memcmp(&buf, &beef, 12) != 0)
	{
		fprintf(stderr, "Data read != data wrote at offset 0.\n");
	}

	if(close(fd) != 0)
	{
		fprintf(stderr, "Unable to close text file.\n");
	}
	//printf("Succeeded in all basic read/write tests.\n");

	// test truncating file
	fd = open("tm2.txt", O_WRONLY|O_CREAT);
	sz = lseek(fd, 500, SEEK_SET);
	char testjunk[] = "testjunk0";
	sz = write(fd, &testjunk, 9);
	close(fd);

	fd = open("tm2.txt", O_WRONLY | O_TRUNC);
	testjunk[8] = '1';
	sz = write(fd, &testjunk, 9);
	close(fd);

	fd = open("tm2.txt", O_RDONLY);
	sz = read(fd, &buf, 9);
	buf[9] = '\0';
	if(sz != 9)
	{
		fprintf(stderr, "Unable to read in truncate test. Sz: %d\n", sz);
		return -1;
	}
	if(memcmp(&buf, &testjunk, 9) != 0)
	{
		fprintf(stderr, "Truncate test failed at offset 0.\n");
		return -1;
	}
	close(fd);
	//printf("Succeeded in truncate file test.\n");

	// test extending file
	fd = open("tm3.txt", O_WRONLY|O_CREAT);
	testjunk[8] = '2';
	sz = write(fd, &testjunk, 9);
	close(fd);
	
	fd = open("tm3.txt", O_WRONLY);
	sz = lseek(fd, 500, SEEK_SET);
	testjunk[8] = '3';
	sz = write(fd, &testjunk, 9);
	close(fd);

	fd = open("tm3.txt", O_RDONLY);
	sz = read(fd, &buf, 9);
	buf[9] = '\0';
	if(sz != 9)
	{
		fprintf(stderr, "Unable to read in extend test.\n");
		return -1;
	}
	testjunk[8] = '2';
	if(memcmp(&buf, &testjunk, 9) != 0)
	{
		fprintf(stderr, "Extend test failed at offset 0.\n");
		return -1;
	}
	sz = lseek(fd, 500, SEEK_SET);
	sz = read(fd, &buf, 9);
	buf[9] = '\0';
	testjunk[8] = '3';
	if(memcmp(&buf, &testjunk, 9) != 0)
	{
		fprintf(stderr, "Failed to read correctly from extended file.\n");
		return -1;
	}
	close(fd);
	//printf("Succeeded in extending file test.\n");

	// test direct/indirect boundary
	fd = open("tm4.txt", O_WRONLY|O_CREAT);
	long unsigned int wridx = DI_ID_BNDRY - 4;    // "testjunk should span direct/indirect boundary
	sz = lseek(fd, wridx, SEEK_SET);
	testjunk[8] = '4';
	sz = write(fd, &testjunk, 9);
	close(fd);
	fd = open("tm4.txt", O_RDONLY);
	sz = lseek(fd, wridx, SEEK_SET);
	sz = read(fd, &buf, 9);
	buf[9] = '\0';
	if(memcmp(&buf, &testjunk, 9) != 0)
	{
		fprintf(stderr, "Unable to successfully write/read at DI-ID boundary.\n");
		fprintf(stderr, " %s != %s\n", testjunk, buf);
		//return -1;
	}
	close(fd);
	//printf("Succeeded in direct/indirect boundary test.\n");

	// test indirect/double indirect boundary
	fd = open("tm5.txt", O_WRONLY|O_CREAT);
	wridx = ID_ID2_BNDRY - 4;    // "testjunk should span indirect/double indirect boundary
	sz = lseek(fd, wridx, SEEK_SET);
	testjunk[8] = '5';
	sz = write(fd, &testjunk, 9);
	close(fd);
	fd = open("tm5.txt", O_RDONLY);
	sz = lseek(fd, wridx, SEEK_SET);
	sz = read(fd, &buf, 9);
	buf[9] = '\0';
	if(memcmp(&buf, &testjunk, 9) != 0)
	{
		fprintf(stderr, "Unable to successfully write/read at ID-ID2 boundary.\n");
		fprintf(stderr, " %s != %s\n", testjunk, buf);
		//return -1;
	}
	close(fd);
	//printf("Succeeded in single indirect/double indirect boundary test.\n");

	// test max file size boundary
	fd = open("tm6.txt", O_WRONLY|O_CREAT);
	//wridx = ID2_END_BNDRY;      // one byte before 4GB
	sz = lseek(fd, MAX_FILE_SIZE, SEEK_SET);
	sz = lseek(fd, 2, SEEK_CUR);	// this is 1 byte past 4GB
	testjunk[8] = '6';
	sz = write(fd, &testjunk, 9);
	if(sz != -1)
	{
		fprintf(stderr, "Error: Able to write past max file size.\n");
		//return -1;
	}
	close(fd);
	//printf("Succeeded in write-past max file size test.\n");
*/
int sz, fd;
	// test writing a huge file
	int maxToWrite = 100;	// one million bytes
	int modIdx = 0, currWrite = 0;
	char blah[10];
	memcpy(&blah, "ABCABCABC", 9);

	// time this guy to test namei cache usefulness
	//clock_t clockEnd = 0;
	//clock_t clockStart = clock();
	struct timeval tvStart, tvEnd;
	gettimeofday(&tvStart, NULL);

	fd = open("tm7.txt", O_WRONLY|O_CREAT);
	while(currWrite < maxToWrite)
	{
		blah[9] = '0' + modIdx;
		sz = write(fd, &blah, 10);
		if(sz != 10)
		{
			fprintf(stderr, "Failed to write to tm7.txt at %d bytes.\n", currWrite);
			break;
		}
		currWrite += 10;
		modIdx = ++modIdx % 10;
	}
	
	close(fd);

	//clockEnd = clock();
	gettimeofday(&tvEnd, NULL);
	double elapsedTime = (tvEnd.tv_sec + (tvEnd.tv_usec/1000000.0)) - 
		(tvStart.tv_sec + (tvStart.tv_usec/1000000.0));
	printf("\tTest ran in %f s.\n", elapsedTime);

//	printf("Succeeded in writing a huge file test.\n");

	printf("All tests completed.\n");
	return 0;
}
