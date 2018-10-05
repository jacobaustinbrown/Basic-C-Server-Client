#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <netdb.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_UNRESTRICTED 4
#define O_EXCLUSIVE 5
#define O_TRANSACTION 6

extern int fakeBool; // hostname, to be set by netserverinit
extern struct addrinfo connector;
extern struct addrinfo * remote;

int netserverinit(char * hostname, int filemode);
int netopen(const char* pathname, int flags);
ssize_t netread(int fildes, void *buf, size_t nbyte);
ssize_t netwrite(int fildes, const void *buf, size_t nbyte);
int netclose(int fd);
