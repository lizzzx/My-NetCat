#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "ncSocket.h"
#include <poll.h>

#include "ncUtils.h"

#define MAX_LINE_LENGTH 80
static struct pollfd fds[MAX_CLIENT_NUM + 2];
static int listen_fd;

static int get_client_num()
{
  int count = 0;
  for (int i = 2; i < MAX_CLIENT_NUM + 2; i++)
  {
    if (fds[i].fd > 0)
    {
      count++;
    }
  }

  return count;
}

static int get_free_slot()
{
  int index = -1;
  for (int i = 2; i < MAX_CLIENT_NUM + 2; i++)
  {
    if (fds[i].fd <= 0)
    {
      index = i;
      break;
    }
  }

  return index;
}

static bool accept_new_client(bool keep, bool resent)
{
  int active_client_num = get_client_num();
  // nc_log("active_client_num:%d\n", active_client_num);
  if (!keep && !resent && active_client_num > 0)
  {
    /* server only serve one client, ignore client in waiting queue */
    return true;
  }

  if (active_client_num > 0 && !resent)
  {
    /* server could serve multiply clients, but should wait the active client quit */
    return true;
  }

  if (resent && active_client_num > MAX_CLIENT_NUM)
  {
    /* server could serve multiply clients simultaneously, but reach the number limit */
    return true;
  }

  int new_client_fd;
  do
  {
    struct sockaddr_in client_addr;
    socklen_t addrlen;
    new_client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (new_client_fd < 0)
    {
      if (errno != EWOULDBLOCK)
      {
        /* some error occured */
        return false;
      }

      /* no more client in waiting queue */
      return true;
    }

    printf("Connection from %s %d received!\n", inet_ntoa(client_addr.sin_addr), htons(client_addr.sin_port));
    /* accept new client */
    int slot = get_free_slot();
    if (slot <= 0)
    {
      break;
    }

    fds[slot].fd = new_client_fd;
    fds[slot].events = POLLIN;

  } while (new_client_fd != -1);

  /* never reach here */
  return false;
}

static bool close_client_fd(int fd)
{
  for (int i = 2; i < MAX_CLIENT_NUM + 2; i++)
  {
    if (fds[i].fd == fd)
    {
      nc_log("close fd %d\n", fd);
      close(fds[i].fd);
      fds[i].fd = -1;
      return true;
    }
  }

  /* should never reach here */
  return false;
}

static bool read_data_from_client(int fd, bool resent)
{
  char buffer[MAX_LINE_LENGTH];

  do
  {
    memset(buffer, 0, MAX_LINE_LENGTH);
    
    int len;
    if (fd != STDIN_FILENO)
    {
      /* read data from client without blocking */
      len = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
      if (len < 0)
      {
        if (errno != EWOULDBLOCK)
        {
          perror("recv failed");
          close_client_fd(fd);
          return false;
        }

        /* recv failed for no more data */
        return true;
      }

      if (len == 0)
      {
        close_client_fd(fd);
        break;
      }

      /* print content to stdout */
      printf("%s", buffer);
      fflush(stdout);
    }
    else
    {
      /* read data from stdin */
      if (fgets(buffer, MAX_LINE_LENGTH, stdin) == NULL)
      {
        break;
      }

      len = strlen(buffer);
    }

    /* resent content to all client */
    if (resent)
    {
      nc_log("resent content to all clients\n");
      for (int j = 2; j < MAX_CLIENT_NUM + 2; j++)
      {
        if (fds[j].fd <= 0 || fds[j].fd == fd)
        {
          continue;
        }

        len = send(fds[j].fd, buffer, len, 0);
        if (len < 0)
        {
          perror("send failed");
          close(fds[j].fd);
          continue;
        }
      }
      nc_log("finish resent\n");
    }

    if (fd == STDIN_FILENO)
    {
      break;
    }

  } while (true);

  return false;
}

static void work_as_server(unsigned int portno, bool keep, bool resent)
{
  memset(fds, 0, sizeof(fds));
  for (int i = 0; i < MAX_CLIENT_NUM + 2; i++)
  {
    fds[i].fd = -1;
  }

  // socket working in nonblocking mode for polling
  listen_fd = create_serverfd(portno, false);
  if (listen_fd <= 0)
  {
    nc_log("create nc server failed!\n");
    exit(EXIT_FAILURE);
  }

  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;

  fds[1].fd = listen_fd;
  fds[1].events = POLLIN;

  bool end_server = false;
  int timeout = 300 * 1000;

  do
  {
    int rc = poll(fds, MAX_CLIENT_NUM + 2, timeout);
    if (rc < 0)
    {
      perror("poll() failed");
      break;
    }

    if (rc == 0)
    {
      /* poll timeout */
      continue;
    }

    for (int i = 0; i < MAX_CLIENT_NUM + 2; i++)
    {
      if (fds[i].fd < 0 || fds[i].revents == 0)
        continue;

      if ((fds[i].revents & POLLIN) == 0)
      {
        end_server = true;
        break;
      }

      if (fds[i].fd == listen_fd)
      {
        if (!accept_new_client(keep, resent))
        {
          end_server = true;
          break;
        }
      }
      else
      {
        nc_log("client %d is readable\n", fds[i].fd);
        bool success = read_data_from_client(fds[i].fd, resent);
        if (!keep && !success && get_client_num() == 0)
        {
          /* the client quit, no need keep waiting other client, server quit too */
          nc_log("read_data_from_client failed!\n");
          end_server = true;
          break;
        }
      }
    }

  } while (!end_server);

  nc_log("server quit\n");

  /* close all socket */
  for (int i = 1; i < MAX_CLIENT_NUM + 2; i++)
  {
    if (fds[i].fd > 0)
    {
      close(fds[i].fd);
    }
  }
}

static void work_as_client(char *hostname, unsigned int port, unsigned int source_port, int timeout)
{
  nc_log("start to connect server\n");
  int client_fd = create_clientfd(hostname, port, source_port, timeout);
  if (client_fd <= 0)
  {
    nc_log("create client fd failed, quit\n");
    return;
  }

  nc_verbose("Connection to %s %d port [tcp/*] succeeded!\n", hostname, port);
  
  /* poll stdin */
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;

  /* poll socket */
  fds[1].fd = client_fd;
  fds[1].events = POLLIN;

  bool end_client = false;
  char line[MAX_LINE_LENGTH];
  do
  {
    if (timeout == 0)
    {
      timeout = -1;
    }

    int rc = poll(fds, 2, timeout * 1000);
    if (rc < 0)
    {
      perror("poll failed");
      break;
    }

    if (rc == 0)
    {
      /* poll timeout */
      break;
    }

    for (int i = 0; i < 2; i++)
    {
      if (fds[i].fd < 0 || fds[i].revents == 0)
        continue;

      if ((fds[i].revents & POLLIN) == 0)
      {
        printf("  Error! revents = %d\n", fds[i].revents);
        end_client = true;
        break;
      }

      if (fds[i].fd == STDIN_FILENO)
      {
        /* read data from stdin and send to server */
        if (fgets(line, MAX_LINE_LENGTH, stdin) == NULL)
        {
          end_client = true;
          break;
        }

        /* send input to server */
        if (write(client_fd, line, strlen(line)) < 0)
        {
          end_client = true;
          break;
        }
      }
      else
      {
        do
        {
          memset(line, 0, MAX_LINE_LENGTH);

          /* read data from client without blocking */
          int len = recv(client_fd, line, sizeof(line), MSG_DONTWAIT);
          if (len < 0)
          {
            if (errno != EWOULDBLOCK)
            {
              perror("recv failed");
              end_client = true;
              break;
            }

            /* recv failed for no more data */
            break;
          }

          if (len == 0)
          {
            break;
          }

          /* print content to stdout */
          printf("%s", line);
        } while (true);
        
      }
    }

  } while (!end_client);

  close(client_fd);
}

int main(int argc, char **argv)
{

  // This is some sample code feel free to delete it
  // This is the main program for the poll version of nc

  struct commandOptions cmdOps;
  int retVal = parseOptions(argc, argv, &cmdOps);
  nc_log("Command parse outcome %d\n", retVal);

  nc_log("-k = %d\n", cmdOps.option_k);
  nc_log("-l = %d\n", cmdOps.option_l);
  nc_log("-v = %d\n", cmdOps.option_v);
  nc_log("-r = %d\n", cmdOps.option_r);
  nc_log("-p = %d\n", cmdOps.option_p);
  nc_log("-p port = %u\n", cmdOps.source_port);
  nc_log("-w  = %d\n", cmdOps.option_w);
  nc_log("Timeout value = %u\n", cmdOps.timeout);
  nc_log("Host to connect to = %s\n", cmdOps.hostname);
  nc_log("Port to connect to = %u\n", cmdOps.port);

  if (cmdOps.option_v)
  {
    enable_verbose();
  }

  if (cmdOps.option_l)
  {
    work_as_server(cmdOps.port, cmdOps.option_k, cmdOps.option_r);
  }
  else if (cmdOps.hostname != NULL && cmdOps.port > 0)
  {
    work_as_client(cmdOps.hostname, cmdOps.port, cmdOps.source_port, cmdOps.timeout);
  }
}
