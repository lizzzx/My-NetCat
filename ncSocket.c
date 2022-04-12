#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>

#include "ncUtils.h"
#include "ncSocket.h"


int create_serverfd(unsigned int portno, bool blocking)
{
    // Create a socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("open socket failed");
        exit(EXIT_FAILURE);
    }

    // set the serv_addr
    struct sockaddr_in serv_addr;
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    // Convert port number from host to network
    serv_addr.sin_port = htons(portno);

    int on = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        perror("setsockopt failed\n");
        exit(EXIT_FAILURE);
    }

    if (!blocking)
    {
        if ((fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK)) < 0)
        {
            perror("set nonblocking fd failed");
            close(sockfd);
            exit(-1);
        }

        nc_log("socket working in nonblocking mode\n");
    }

    // Bind the socket to the port number
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("failed on binding\n");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, MAX_CLIENT_NUM) < 0)
    {
        printf("failed on listen\n");
        exit(EXIT_FAILURE);
    }

    nc_verbose("Listening on [%s] (family %d, port %d)\n", inet_ntoa(serv_addr.sin_addr), serv_addr.sin_family, portno);

    return sockfd;
}

int create_clientfd(const char *hostname, int server_port, int local_port, int timeout)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        printf("failed opening socket\n");
        exit(EXIT_FAILURE);
    }

    /* resolve domain to ip */
    struct hostent *server = gethostbyname(hostname);
    if (server == NULL)
    {
        printf("gethostbyname failed\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in localaddr;
    socklen_t addrlen = sizeof(localaddr);
    if (local_port > 0)
    {
        memset(&localaddr, 0, sizeof(localaddr));
        localaddr.sin_family = AF_INET;
        localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localaddr.sin_port = htons(local_port);
        nc_log("set local port: %d\n", local_port);

        if (bind(sockfd, (struct sockaddr *)&localaddr, sizeof(localaddr)) < 0)
        {
            perror("bind");
            exit(1);
        }
    }

    struct sockaddr_in serv_addr;
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memmove((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(server_port);

    if (timeout > 0)
    {
        struct timeval connect_timeout;
        connect_timeout.tv_sec = timeout;
        connect_timeout.tv_usec = 0;

        socklen_t len = sizeof(connect_timeout);
        int ret = setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &connect_timeout, len);
        if (ret == -1)
        {
            return -1;
        }

    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        if (errno == EINPROGRESS)
        {
            printf("connecting timeout\n");
            return -1;
        }

        printf("failed connecting\n");
        exit(EXIT_FAILURE);
    }

    if (getsockname(sockfd, (struct sockaddr *)&localaddr, &addrlen) < 0)
    {
        printf("getsockname\n");
        exit(EXIT_FAILURE);
    }

    nc_log("ip=%s, port=%d\n", inet_ntoa(localaddr.sin_addr), ntohs(localaddr.sin_port));
    return sockfd;
}
