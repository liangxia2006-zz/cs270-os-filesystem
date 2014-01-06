# MonsterFS
=========

This project is for the Fall '13 UC Santa Barbara CS 270 Advanced Operating
Systems course. In it we will be implementing our own file system, called
MonsterFS. See below for our progress and plans.

A large portion of our implementation is based on Maurice Bach's book, The Design of 
the Unix Operating System.

Three testing devices are provided. 'test.c' is the program we used to test
our implementation as we developed. It is largely undocumented. The tests
in 'test-monsterfs.c' demonstrated to us that our file system worked with
the standard file routines available through Linux. It should be called from
inside the FUSE-mounted filesystem. The shell script 'test-script.sh' 
tests that our file system is compatible enough to support standard Linux
shell commands. 


=========
## Phase 1

### [X] Layer 0
  - [X] In-memory storage device emulator
    * [X] Tested
  - [X] Interface to real block storage device
    * [X] Tested

### [X] Layer 1
  - [X] Superblock data structure
  - [X] Inode data structure
  - [X] make-fs
  - [X] Inode routines
    * [X] Allocate
    * [X] Free
    * [X] Read
    * [X] Write
  - [X] Block/buffer routines
    * [X] Allocate
    * [X] Free
    * [X] Read
    * [X] Write
  - [X] Tested

### [X] Layer 2
  - [X] mkdir
  - [X] rmdir
  - [X] mknod
  - [X] readdir
  - [X] unlink
  - [X] read/write
  - [X] open/close

### [X] Layer 3
  - [X] Determine requirements
  - [X] TODO list:
    * [X] truncate 
    * [X] improve write when seeking.
    * [X] test results for Phase 1

=========
## Phase 2

  - [X] Refinement and bug fixing from Phase 1 review
    * [X] update all errno
  - [X] Migrate Phase 1 from storage emulator to Eucalyptus/block device

=========
## Phase 3

  - [X] Optimize!


