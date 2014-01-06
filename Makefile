all: monsterfs test rebuild

lib: monsterfs_funs.o

test: test.o monsterfs_funs.o test-monsterfs.c
	gcc -g test.o monsterfs_funs.o -o test
	gcc -g -o test-monsterfs test-monsterfs.c

monsterfs: monsterfs_funs.o monsterfs.c
	gcc -g monsterfs.c monsterfs_funs.o -D_FILE_OFFSET_BITS=64 -I/usr/local/include/fuse  -pthread -L/usr/local/lib -lfuse -o monsterfs

rebuild: monsterfs_funs.o rebuild.c
	gcc -g rebuild.c monsterfs_funs.o -D_FILE_OFFSET_BITS=64 -I/usr/local/include/fuse  -pthread -L/usr/local/lib -lfuse -o rebuild

test.o: monsterfs_funs.o test.c
	gcc -c -g test.c

monsterfs_funs.o: monsterfs_funs.h monsterfs_funs.c
	gcc -c -g monsterfs_funs.c

clean:
	rm -f *.o test monsterfs test-monsterfs rebuild

run:
	./monsterfs -f tmp

umount:
	umount tmp

fu:
	fusermount -u tmp
