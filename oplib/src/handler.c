/*                                                            
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.       
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>             
 *
 */                                                           

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <errno.h>
#include <log.h>
#include "handler.h"

extern log_t *g_log;

#define MAX_EVENT 10000

typedef int (cb_func)(int fd, void *arg);

typedef struct{
    cb_func *callback;
    void *arg;
    int fd;
} handler_callback_t;

static int epfd;
static handler_callback_t *hcptr = NULL;
static int hccount = 0;

/*
 * fun: init handler
 * arg: max handler num
 * ret: success=0, error=-1
 *
 */

int init_handler(int count)
{
    int i;
    handler_callback_t *ptr;

    if( (epfd = epoll_create(MAX_EVENT)) < 0 ){
        log_err(g_log, "epoll_create error\n");
        return -1;
    }

    hcptr = (handler_callback_t *)malloc(sizeof(handler_callback_t) * count);
    if(hcptr == NULL){
        log_err(g_log, "malloc error\n");
        close(epfd);
        return -1;
    }

    for(i = 0; i < count; i++){
        ptr = hcptr + i;
        ptr->callback = NULL;
        ptr->fd = -1;
        ptr->arg = NULL;
    }

    hccount = count;

    return 0;
}

/*
 * fun: check fd is legal
 * arg: fd
 * ret: legal=1, illegal=0
 *
 */

static int fd_is_legal(int fd)
{
    return (fd >= 0) && (fd < hccount);
}

/*
 * fun: add handler into handler pool
 * arg: handler fd, handler event, handler callback and arg
 * ret: success=0, error=-1
 *
 */

int add_handler(int fd, uint32_t event, void *cb, void *arg)
{
    int res = 0;
    struct epoll_event ev;
    handler_callback_t *ptr;

    if(!fd_is_legal(fd)){
        log(g_log, "handler[%d] exceed limit[0, %d)\n", fd, hccount);
        return -1;
    }

    if(in_handler(fd)){
        debug(g_log, "warning: in handler when add handler, ignore it\n");

        if( (res = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev)) < 0 ){
            log_err(g_log, "epoll_ctl error, ignore it\n");
        }
    }

    ptr = hcptr + fd;

    ptr->callback = cb;
    ptr->arg = arg;
    ptr->fd = fd;

    ev.data.ptr = ptr;
    ev.events = event;
    if( (res = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) < 0 ){
        log_err(g_log, "epoll_ctl error\n");
        ptr->callback = NULL;
        ptr->arg = NULL;
        ptr->fd = -1;

        return res;
    }

    return res;
}

/*
 * fun: del handler
 * arg: handler fd
 * ret: success=0, error=-1
 *
 */

int del_handler(int fd)
{
    int res = 0;
    struct epoll_event ev;
    handler_callback_t *ptr;

    if(!fd_is_legal(fd)){
        log(g_log, "handler[%d] exceed limit[0, %d)\n", fd, hccount);
        return -1;
    }

    if(in_handler(fd)){
        if( (res = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev)) < 0 ){
            log_err(g_log, "epoll_ctl error\n");
        } else {
            debug(g_log, "epoll_ctl success\n");
        }
    } else {
        debug(g_log, "warning: not in handler when del handler, ignore it\n");
    }

    ptr = hcptr + fd;
    ptr->callback = NULL;
    ptr->arg = NULL;
    ptr->fd = -1;

    return res;
}

/*
 * fun: modify handler
 * arg: handler fd, handler event, handler callback and arg
 * ret: success=0, error=-1
 *
 */

int mod_handler(int fd, uint32_t event, void *cb, void *arg)
{
    int res = 0;
    struct epoll_event ev;
    handler_callback_t *ptr;

    if(!fd_is_legal(fd)){
        log(g_log, "handler[%d] exceed limit[%d]\n", fd, hccount);
        return -1;
    }

    if(! in_handler(fd)){
        log(g_log, "not in handler when mod handler\n");
        return -1;
    }

    ptr = hcptr + fd;
    if(cb){
        ptr->callback = cb;
        ptr->arg = arg;
        ptr->fd = fd;
    }

    ev.data.ptr = ptr;
    ev.events = event;
    if( (res = epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev)) < 0 ){
        log_err(g_log, "epoll_ctl error\n");
        return res;
    }

    return res;
}

/*
 * fun: check if handler in pool
 * arg: handler fd
 * ret: in=1, not=0
 *
 */

int in_handler(int fd)
{
    handler_callback_t *ptr;

    if(!fd_is_legal(fd)){
        return 0;
    }

    ptr = hcptr + fd;

    if(ptr->fd == -1){
        return 0;
    } else {
        return 1;
    }
}

/*
 * fun: epoll handler poll
 * arg: epoll wait timeout
 * ret: number of events epoll
 *
 */

int epoll_handler(int timeout)
{
    int i, nfds, res = 0;
    struct epoll_event events[MAX_EVENT];
    handler_callback_t *ptr;

    nfds = epoll_wait(epfd, events, MAX_EVENT, timeout);
    debug(g_log, "nfds: %d ready\n", nfds);

    for(i = 0; i < nfds; i++){
        ptr = events[i].data.ptr;
        if(ptr->callback){
            res = ptr->callback(ptr->fd, ptr->arg);
        }
    }

    return nfds;
}
