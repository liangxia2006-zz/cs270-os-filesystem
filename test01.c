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

#define DI_ID_BNDRY     2560      // first byte in indirect space
#define ID_ID2_BNDRY    18944     // first byte in double indirect space
#define ID2_END_BNDRY   1067520   // first byte past double indirect space (1 byte over max file size)

int main()
{
	printf("size of long int = %d\n", sizeof(long int));
	printf("size of long long = %d\n", sizeof(long long));
	printf("size of long long int = %d\n", sizeof(long long int));
	return 0;
}
