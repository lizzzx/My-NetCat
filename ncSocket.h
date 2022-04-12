#ifndef NC_SOCKET_H
#define NC_SOCKET_H

#include <unistd.h>
#include <stdbool.h>

#define MAX_CLIENT_NUM 5

/* create a fd to server */
int create_clientfd(const char *hostname, int server_port, int local_port, int timeout);

int create_serverfd(unsigned int portno, bool blocking);

#endif
