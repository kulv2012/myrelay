#ifndef _HANDLER_H_
#define _HANDLER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int init_handler(int count);
int add_handler(int fd, uint32_t event, void *cb, void *arg);
int del_handler(int fd);
int in_handler(int fd);
/*
int mod_handler(int fd, uint32_t event, void *cb, void *arg);
*/
int epoll_handler(int timeout);

#define MAX_EVENT 100000

#define fd_is_legal(fd)  (fd >= 0) && (fd < hccount)

#ifdef __cplusplus
}
#endif

#endif
