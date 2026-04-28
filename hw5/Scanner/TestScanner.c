#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>

int main() {
    int fd = open("/dev/Scanner", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/Scanner");
        exit(1);
    }
    
    /* Set custom separators: colon and comma */
    assert(ioctl(fd, 0, 0) == 0);
    assert(write(fd, ":,", 2) == 2);
    
    /* Test 1: Standard tokenization (a:b,c -> "a", "b", "c") */
    char *data1 = "a:b,c";
    assert(write(fd, data1, 5) == 5);
    char buf[10];
    
    assert(read(fd, buf, 10) == 1 && buf[0] == 'a');
    assert(read(fd, buf, 10) == 0); // End of token 1
    assert(read(fd, buf, 10) == 1 && buf[0] == 'b');
    assert(read(fd, buf, 10) == 0); // End of token 2
    assert(read(fd, buf, 10) == 1 && buf[0] == 'c');
    assert(read(fd, buf, 10) == 0); // End of token 3
    assert(read(fd, buf, 10) == -1); // End of data
    
    /* Test 2: Consecutive Separator Skipping */
    char *data2 = "a::,b";
    assert(write(fd, data2, 5) == 5);
    
    assert(read(fd, buf, 10) == 1 && buf[0] == 'a');
    assert(read(fd, buf, 10) == 0);
    assert(read(fd, buf, 10) == 1 && buf[0] == 'b');
    assert(read(fd, buf, 10) == 0);
    assert(read(fd, buf, 10) == -1);

    /* Test 3: Un-biased NUL byte handling */
    char nul_data[] = {'a', '\0', 'b', ':', 'c'};
    assert(write(fd, nul_data, 5) == 5);
    
    assert(read(fd, buf, 10) == 3);
    assert(buf[0] == 'a' && buf[1] == '\0' && buf[2] == 'b');
    assert(read(fd, buf, 10) == 0);
    assert(read(fd, buf, 10) == 1 && buf[0] == 'c');
    assert(read(fd, buf, 10) == 0);
    assert(read(fd, buf, 10) == -1);

    printf("All test suite assertions passed successfully!\n");
    close(fd);
    return 0;
}