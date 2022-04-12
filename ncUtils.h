#ifndef NC_UTILS_H
#define NC_UTILS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void enable_verbose();

void nc_log(const char *format, ...);

void nc_verbose(const char *format, ...);

long get_current_time();

#endif