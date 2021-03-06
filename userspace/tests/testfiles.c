/*
 * testfiles.c
 *
 */

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>

/*
 * Macro for assertions in unit test cases
 */
#define ASSERT(x)  do { if (!(x)) { \
        printf("Assertion %s failed at line %d in %s..", #x, __LINE__, __FILE__ ); \
        return 1 ;   \
} \
} while (0)

/*
 * Set up statistics
 */
#define INIT  int __failed=0; int __passed=0; int __rc=0 ; \
        printf("------------------------------------------\n"); \
        printf("Starting unit test %s\n", __FILE__); \
        printf("------------------------------------------\n");

/*
 * Print statistic and return
 */
#define END printf("------------------------------------------\n"); \
        printf("Overall test results (%s):\n", __FILE__); \
        printf("------------------------------------------\n"); \
        printf("Failed: %d  Passed:  %d\n", __failed, __passed); \
        printf("------------------------------------------\n"); return __rc;

/*
 * Execute a test case
 */
#define RUN_CASE(x) do { __rc= do_test_case(x, testcase##x);  \
        if (__rc) __failed++; else __passed++;} while (0)

/*
 * Forward declaration - this is in kunit.o
 */
int do_test_case(int x, int (*testcase)());

/*
 * Testcase 1: create a new, empty file testtmp1 and stat it
 */
int testcase1() {
    struct stat mystat;
    int rc;
    int fd;
    /*
     * Make sure that file does not exist yet
     * This assertion will trigger if a previous testrun has been aborted,
     * you need to manually remove the file in this case before being able
     * to proceed
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(-1==rc);
    /*
     * Create it
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    close(fd);
    /*
     * and stat again
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(0 == rc);
    /*
     * chmod
     */
    ASSERT(0 == chmod("testtmp1", 0644));
    ASSERT(0 == stat("testtmp1", &mystat));
    ASSERT((mystat.st_mode & 0777) == 0644);
    ASSERT(0 == chmod("testtmp1", 0700));
    ASSERT(0 == stat("testtmp1", &mystat));
    ASSERT((mystat.st_mode & 0777) == 0700);
    return 0;
}

/*
 * Testcase 2: write to the newly created file
 */
int testcase2() {
    char buffer[16];
    int i;
    int fd;
    /*
     * Fill buffer
     */
    for (i=0;i<16;i++)
        buffer[i]='a'+i;
    /*
     * Open file
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    /*
     * Write 16 bytes and close file again
     */
    ASSERT(16==write(fd, buffer, 16));
    close(fd);
    return 0;
}

/*
 * Testcase 3: remove file again
 */
int testcase3() {
    struct stat mystat;
    int rc;
    /*
     * Remove file
     */
    ASSERT(0==unlink("testtmp1"));
    /*
     * Make sure that file does not exist any more
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(-1==rc);
    ASSERT(ENOENT==errno);
    return 0;
}

/*
 * Testcase 4: create a file, write to it and read data back
 */
int testcase4() {
    char write_buffer[16];
    char read_buffer[16];
    int i;
    int fd;
    /*
     * Fill buffers
     */
    for (i=0;i<16;i++) {
        write_buffer[i]='a'+i;
        read_buffer[i]=0;
    }
    /*
     * Open file
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    /*
     * Write 16 bytes and close file again
     */
    ASSERT(16==write(fd, write_buffer, 16));
    close(fd);
    /*
     * Open file again and read 16 bytes
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    ASSERT(16==read(fd, read_buffer, 16));
    close(fd);
    /*
     * Compare
     */
    for (i=0;i<16;i++) {
        ASSERT(read_buffer[i]==write_buffer[i]);
    }
    return 0;
}

/*
 * Testcase 5: duplicate a file descriptor using dup and verify that the file position is shared
 */
int testcase5() {
    int fd1;
    int fd2;
    char buffer;
    fd1 = open("testtmp1", 0);
    ASSERT(fd1);
    fd2=dup(fd1);
    ASSERT(fd2);
    ASSERT(1==read(fd1, &buffer, 1));
    ASSERT(buffer=='a');
    ASSERT(1==read(fd2, &buffer, 1));
    ASSERT(buffer=='b');
    close(fd1);
    close(fd2);
    return 0;
}

/*
 * Testcase 6: duplicate a file descriptor using fcntl and verify that the file position is shared
 */
int testcase6() {
    int fd1;
    int fd2;
    char buffer;
    fd1 = open("testtmp1", 0);
    ASSERT(fd1);
    fd2=fcntl(fd1, F_DUPFD, 10);
    ASSERT(fd2>=10);
    ASSERT(1==read(fd1, &buffer, 1));
    ASSERT(buffer=='a');
    ASSERT(1==read(fd2, &buffer, 1));
    ASSERT(buffer=='b');
    close(fd1);
    close(fd2);
    return 0;
}

/*
 * Testcase 7: do fstat on the file
 */
int testcase7() {
    int fd;
    struct stat mystat;
    fd = open("testtmp1", 0);
    ASSERT(fd);
    ASSERT(0==fstat(fd, &mystat));
    ASSERT(16==mystat.st_size);
    close(fd);
    return 0;
}


/*
 * Testcase 8: remove file again
 */
int testcase8() {
    struct stat mystat;
    int rc;
    /*
     * Remove file
     */
    ASSERT(0==unlink("testtmp1"));
    /*
     * Make sure that file does not exist any more
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(-1==rc);
    return 0;
}

/*
 * Testcase 9: add a directory
 */
int testcase9() {
    struct stat mystat;
    /*
     * Create a directory
     */
    ASSERT(0 == mkdir("testdir1", 0777));
    ASSERT(0 == stat("testdir1", &mystat));
    ASSERT(S_ISDIR(mystat.st_mode));
    /*
     * and remove it again
     */
    ASSERT(0 == rmdir("testdir1"));
    ASSERT(-1 == stat("testdir1", &mystat));
    return 0;
}

/*
 * Testcase 10: rename a file
 */
int testcase10() {
    struct stat mystat;
    int fd;
    /*
     * Want to use testtmp1 - make sure it does not exist yet
     */
    ASSERT(-1 == stat("testtmp1", &mystat));
    /*
     * same for testtmp2
     */
    ASSERT(-1 == stat("testtmp2", &mystat));
    /*
     * Create testtmp1
     */
    fd = open("testtmp1", O_CREAT, 0777);
    if (fd < 0) {
        perror("open");
    }
    ASSERT(fd >= 0);
    close(fd);
    /*
     * and rename it
     */
    ASSERT(0 == rename("testtmp1", "testtmp2"));
    ASSERT(-1 == stat("testtmp1", &mystat));
    ASSERT(0 == stat("testtmp2", &mystat));
    /*
     * finally remove file again
     */
    ASSERT(0 == unlink("testtmp2"));
    return 0;
}

/*
 * Testcase 11:
 * open a file using fdopen and write to it using fprintf. Then verify result
 */
int testcase11() {
    int fd;
    FILE* file;
    char buffer[128];
    /*
     * Create testtmp1
     */
    fd = open("testtmp1", O_CREAT, 0777);
    if (fd < 0) {
        perror("open");
    }
    ASSERT(fd >= 0);
    /*
     * Open stream
     */
    file = fdopen(fd, "rw");
    ASSERT(file);
    /*
     * and write to it
     */
    fprintf(file, "%s", "hello");
    fclose(file);
    /*
     * Now open file again and read data
     */
    fd = open("testtmp1", O_RDONLY);
    ASSERT(fd >= 0);
    memset((void*) buffer, 0, 128);
    ASSERT(strlen("hello") == read(fd, buffer, strlen("hello")));
    close(fd);
    ASSERT(0 == strcmp("hello", buffer));
    /*
     * and remove file again
     */
    ASSERT(0 == unlink("testtmp1"));
    return 0;
}

/*
 * Testcase 12:
 * read an entry from the root directory
 */
int testcase12() {
    DIR* dirp;
    ASSERT((dirp = opendir("/")));
    closedir(dirp);
    return 0;
}

/*
 * Testcase 13:
 * loop once through the root directory and 
 * make sure that we have seen /tests at least once
 */
int testcase13() {
    DIR* dirp;
    struct dirent* dirent;
    int have_tests_dir = 0;
    ASSERT((dirp = opendir("/")));
    /*
     * Now loop through the directory entries
     */
    while ((dirent = readdir(dirp))) {
        if (0 == strcmp("tests", dirent->d_name)) {
            have_tests_dir = 1;
        }
    }
    closedir(dirp);
    ASSERT(1 == have_tests_dir);
    return 0;
}

/*
 * Testcase 14:
 * loop once through the root directory and 
 * make sure that we have seen /tests at least once. Then
 * rewind the directory and repeat the exercise
 */
int testcase14() {
    DIR* dirp;
    struct dirent* dirent;
    int have_tests_dir = 0;
    int entries = 0;
    int i = 0;
    ASSERT((dirp = opendir("/")));
    /*
     * Now loop through the directory entries for the first time
     */
    while ((dirent = readdir(dirp))) {
        if (0 == strcmp("tests", dirent->d_name)) {
            have_tests_dir = 1;
        }
        entries++;
    }
    /*
     * Rewind
     */
    rewinddir(dirp);
    /*
     * And repeat the exercise
     */
    have_tests_dir = 0;
    while ((dirent = readdir(dirp))) {
        if (0 == strcmp("tests", dirent->d_name)) {
            have_tests_dir = 1;
        }
        i++;
    } 
    /*
     * Verify that we have seen the same number of entries and 
     * that we have seen tests again
     */
    ASSERT(i == entries);
    ASSERT(have_tests_dir);
    closedir(dirp);
    ASSERT(1 == have_tests_dir);
    return 0;
}

/*
 * Create a new file and write some data to it. Then 
 * create a hard link. Check that the link is there and
 * that both files are identical. Remove the original file
 * and check that the new entry is still there. Finally, remove
 * the new entry again
 */
int testcase15() {
    struct stat mystat;
    int fd;
    char data[16];
    char buffer[16];
    /*
     * Check that testtmp1 is not there
     */
    int rc = stat("testtmp1", &mystat);
    ASSERT(-1 == rc);
    /*
     * Create it
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    /*
     * Now write some data to the file
     */
    for (int i = 0; i < 16; i++)
        data[i] = 'b' + i;
    write(fd, data, 16);
    close(fd);
    /*
     * and stat again
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(0 == rc);    
    /*
     * Create the link
     */
    rc = link("testtmp1", "testtmp2");
    ASSERT(0 == rc);
    /*
     * Open the second file and check that it has the 
     * same data
     */
    fd = open("testtmp2", 0, 0);
    ASSERT(fd);
    memset(buffer, 0, 16);
    read(fd, buffer, 16);
    close(fd);
    for (int i = 0; i < 16; i++) {
        ASSERT(data[i] == buffer[i]);
    }
    /*
     * Remove the original file
     */
    ASSERT(0 == unlink("testtmp1")); 
    rc = stat("testtmp1", &mystat);
    ASSERT(-1 == rc);
    /*
     * testtmp2 and data should still be there
     */
    ASSERT(0 == stat("testtmp2", &mystat));
    fd = open("testtmp2", 0, 0);
    ASSERT(fd);
    memset(buffer, 0, 16);
    read(fd, buffer, 16);
    close(fd);
    for (int i = 0; i < 16; i++) {
        ASSERT(data[i] == buffer[i]);
    }
    /*
     * Finally remove testtmp2 as well
     */
    unlink("testtmp2"); 
    ASSERT(-1 == stat("testtmp2", &mystat));
    return 0;
}

/*
 * Testcase 16
 * Create a new file and write some data to it. Then truncate the file to
 * a smaller size
 */
int testcase16() {
    struct stat mystat;
    int fd;
    char data[16];
    /*
     * Check that testtmp1 is not there
     */
    int rc = stat("testtmp1", &mystat);
    ASSERT(-1 == rc);
    /*
     * Create it
     */
    fd=open("testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    /*
     * Now write some data to the file. We write 16 bytes
     */
    for (int i = 0; i < 16; i++)
        data[i] = 'b' + i;
    write(fd, data, 16);
    close(fd);
    /*
     * and stat again
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(0 == rc);    
    ASSERT(16 == mystat.st_size);
    /*
     * Now we truncate down to 10 bytes
     */
    fd=open("testtmp1", O_RDWR);
    ASSERT(fd);
    rc = ftruncate(fd, 10);
    ASSERT(0 == rc  );
    close(fd);
    /*
     * and stat again
     */
    rc = stat("testtmp1", &mystat);
    ASSERT(0 == rc);    
    ASSERT(10 == mystat.st_size);
    /*
     * Finally remove the test file again
     */
    unlink("testtmp1"); 
    ASSERT(-1 == stat("testtmp1", &mystat));
    return 0;
}


/*
 * Testcase 17
 * Test openat
 */
int testcase17() {
    struct stat mystat;
    /*
     * We use openat to open a file for creation in /tmp
     */
    int dirfd = open("/tmp", 0, 0);
    ASSERT(dirfd);
    int fd = openat(dirfd, "testtmp1", O_CREAT, S_IRWXU);
    ASSERT(fd);
    close(fd);
    close(dirfd);
    /*
     * Now stat it
     */
    ASSERT(0 == stat("/tmp/testtmp1", &mystat));
    /*
     * and remove it again
     */
    unlink("/tmp/testtmp1");
    return 0;
}


/*
 * Testcase 18:
 * loop once through the /tests directory and 
 * make sure that we have seen /tests/testfiles at least once
 * Use fdopendir
 */
int testcase18() {
    int dirfd;
    DIR* dirp;
    struct dirent* dirent;
    int have_testfiles = 0;
    ASSERT((dirfd = open("/tests", 0, 0)));
    ASSERT((dirp = fdopendir(dirfd)));
    /*
     * Now loop through the directory entries
     */
    while ((dirent = readdir(dirp))) {
        if (0 == strcmp("testfiles", dirent->d_name)) {
            have_testfiles = 1;
        }
    }
    closedir(dirp);
    ASSERT(1 == have_testfiles);
    return 0;
}


/*
 * Testcase 19:
 * Use fchdir to temporarily switch to the directory /tmp
 * and back
 */
int testcase19() {
    /*
     * First remember the current working directory
     * both as string and as file descriptor
     */
    int old_cwd_fd = open(".", 0, 0);
    ASSERT(old_cwd_fd);
    char* old_cwd_string = (char*) malloc(512);
    ASSERT(old_cwd_string);
    getcwd(old_cwd_string, 511);
    /*
     * Now use fchdir to go to /tmp
     */
    int new_dir_fd = open("/tmp", 0, 0);
    ASSERT(new_dir_fd);
    ASSERT(0 == fchdir(new_dir_fd));
    /*
     * Check
     */
    char* new_cwd_string = (char*) malloc(512);
    ASSERT(new_cwd_string);
    getcwd(new_cwd_string, 511);
    ASSERT(0 == strcmp("/tmp", new_cwd_string));
    /*
     * Switch back
     */
    ASSERT(0 == fchdir(old_cwd_fd));
    getcwd(new_cwd_string, 511);
    ASSERT(0 == strcmp(new_cwd_string, old_cwd_string));
    free(old_cwd_string);
    free(new_cwd_string);
    return 0;
}

int main() {
    INIT;
    RUN_CASE(1);
    RUN_CASE(2);
    RUN_CASE(3);
    RUN_CASE(4);
    RUN_CASE(5);
    RUN_CASE(6);
    RUN_CASE(7);
    RUN_CASE(8);
    RUN_CASE(9);
    RUN_CASE(10);
    RUN_CASE(11);
    RUN_CASE(12);    
    RUN_CASE(13);    
    RUN_CASE(14);    
    RUN_CASE(15);    
    RUN_CASE(16);
    RUN_CASE(17);
    RUN_CASE(18);
    RUN_CASE(19);
    END;
}

