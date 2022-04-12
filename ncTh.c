#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "ncUtils.h"
#include "ncSocket.h"
#include "Thread.h"
#include <semaphore.h>
#include <limits.h>

#define MAX_LINE_LENGTH 80
static int listen_fd;
static bool client_thread_quit = false;

static int fds[MAX_CLIENT_NUM];

static sem_t client_sem;

static unsigned int portno = -1;
static bool keep = false;
static bool resent = false;

static long last_active = 0;

static int get_client_num()
{
  int count = 0;
  for (int i = 0; i < MAX_CLIENT_NUM; i++)
  {
    if (fds[i] > 0)
    {
      count++;
    }
  }

  return count;
}

static int get_free_slot()
{
  int index = -1;
  for (int i = 0; i < MAX_CLIENT_NUM; i++)
  {
    if (fds[i] <= 0)
    {
      index = i;
      break;
    }
  }

  return index;
}

static bool close_client_fd(int fd)
{
  for (int i = 0; i < MAX_CLIENT_NUM; i++)
  {
    if (fds[i] == fd)
    {
      nc_log("close fd %d\n", fd);
      close(fds[i]);
      fds[i] = -1;
      return true;
    }
  }

  /* should never reach here */
  return false;
}

void *server_socket_thread(void *arg)
{
  int client_fd = (long)arg;

  char line[MAX_LINE_LENGTH];
  while (!client_thread_quit)
  {
    memset(line, 0, MAX_LINE_LENGTH);
    int len = read(client_fd, line, MAX_LINE_LENGTH);
    if (len <= 0)
    {
      break;
    }

    last_active = get_current_time();
    printf("%s", line);
    fflush(stdout);

    /* resent content to other clients */
    if (resent)
    {
      for (int i = 0; i < MAX_CLIENT_NUM; i++)
      {
        if (fds[i] <= 0 || fds[i] == client_fd)
        {
          continue;
        }

        if (send(fds[i], line, len, 0) < 0)
        {
          perror("send failed");
          continue;
        }
      }
    }
  }

  /* release semephore when thread quit */
  close_client_fd(client_fd);
  sem_post(&client_sem);

  /* close server */
  if (!keep && get_client_num() == 0)
  {
    nc_log("close listen_fd\n");
    shutdown(listen_fd, SHUT_RD);
    close(listen_fd);
  }

  return NULL;
}

static void *server_stdin_thread(void *arg)
{
  char line[MAX_LINE_LENGTH];
  while (true)
  {
    memset(line, 0, MAX_LINE_LENGTH);
    /* read data from stdin and resent to clients */
    if (fgets(line, MAX_LINE_LENGTH, stdin) == NULL)
    {
      break;
    }

    last_active = get_current_time();

    /* send input to clients */
    if (resent)
    {
      for (int i = 0; i < MAX_CLIENT_NUM; i++)
      {
        if (fds[i] <= 0)
        {
          continue;
        }

        if (send(fds[i], line, strlen(line), 0) < 0)
        {
          perror("send failed");
          continue;
        }
      }
    }
  }

  return NULL;
}

static void work_as_server()
{
  /* create a socket to accept client, the socket is working in blocking mode */
  listen_fd = create_serverfd(portno, true);

  for (int i = 0; i < MAX_CLIENT_NUM; i++)
  {
    fds[i] = -1;
  }

  /* create a thread to read from stdin */
  struct Thread *thread = createThread(server_stdin_thread, NULL);
  runThread(thread, NULL);

  sem_init(&client_sem, 0, 5);

  while (true)
  {
    /* limit client number by semaphore */
    sem_wait(&client_sem);
    int new_client_fd = accept(listen_fd, NULL, NULL);
    if (new_client_fd < 0)
    {
      break;
    }

    /* save socket fd of client */
    int slot = get_free_slot();
    fds[slot] = new_client_fd;

    /* commmunicate the client fd in a thread */
    struct Thread *thread = createThread(server_socket_thread, (void *)(long)new_client_fd);
    runThread(thread, NULL);

    if (resent)
    {
      detachThread(thread);
    }
    else
    {
      /* wait the last connection finish */
      joinThread(thread, NULL);
    }

    /* only serve one connection */
    if (!keep && get_client_num() == 0)
    {
      break;
    }
  }
}

static void *client_socket_thread(void *arg)
{
  char line[MAX_LINE_LENGTH];
  int client_fd = (long)arg;
  while (!client_thread_quit)
  {
    memset(line, 0, MAX_LINE_LENGTH);
    if (read(client_fd, line, MAX_LINE_LENGTH) <= 0)
    {
      break;
    }

    printf("%s", line);
  }

  return NULL;
}

static void *client_timeout_thread(void *arg)
{
  int timeout = (long)arg;
  if (timeout == 0)
  {
    timeout = INT_MAX;
  }
  while (1)
  {
    long now = get_current_time();
    if (now - last_active > timeout)
    {
      client_thread_quit = true;
      break;
    }
    sleep(1);
  }

  nc_log("timeout thread quit\n");
  return NULL;
}

static void *client_stdin_thread(void *arg)
{
  int client_fd = (long)arg;
  char line[MAX_LINE_LENGTH];
  while (true)
  {
    memset(line, 0, MAX_LINE_LENGTH);
    /* read data from stdin and send to server */
    if (fgets(line, MAX_LINE_LENGTH, stdin) == NULL)
    {
      break;
    }

    /* send input to server */
    if (write(client_fd, line, strlen(line)) < 0)
    {
      break;
    }
  }

  return NULL;
}

static void work_as_client(char *hostname, unsigned int port, unsigned int source_port, int timeout)
{
  nc_log("start to connect server\n");
  long client_fd = create_clientfd(hostname, port, source_port, timeout);
  if (client_fd <= 0)
  {
    nc_log("create client fd failed, quit\n");
    return;
  }

  nc_verbose("Connection to %s %d port [tcp/*] succeeded!\n", hostname, port);

  last_active = get_current_time();

  /* create a thread to watch timeout */
  struct Thread *timeout_thread = createThread(client_timeout_thread, (void *)(long)timeout);
  runThread(timeout_thread, NULL);

  /* create a thread to communicate with server */
  struct Thread *socket_thread = createThread(client_socket_thread, (void *)client_fd);
  runThread(socket_thread, NULL);

  /* create a thread to read from stdin */
  struct Thread *stdin_thread = createThread(client_stdin_thread, (void *)client_fd);
  runThread(stdin_thread, NULL);

  joinThread(timeout_thread, NULL);
  /* close socket fd */
  shutdown(client_fd, SHUT_RD);
  close(client_fd);

  client_thread_quit = true;
  joinThread(socket_thread, NULL);

  free(timeout_thread);
  free(socket_thread);
  cancelThread(stdin_thread);
  joinThread(stdin_thread, NULL);
  free(stdin_thread);
}

int main(int argc, char **argv)
{

  // This is some sample code feel free to delete it
  // This is the main program for the thread version of nc

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
    portno = cmdOps.port;
    keep = cmdOps.option_k;
    resent = cmdOps.option_r;
    work_as_server();
  }
  else if (cmdOps.hostname != NULL && cmdOps.port > 0)
  {
    work_as_client(cmdOps.hostname, cmdOps.port, cmdOps.source_port, cmdOps.timeout);
  }
}
