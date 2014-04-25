/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <errno.h>
#include "sock.h"
#include "log.h"
#include "common.h"

extern log_t *g_log;

#define RESERVE_FOR_HEADER 64

/*
 * fun: make listen socket and set nonblock
 * arg: listen address string, listen port string
 * ret: success=0, error=-1
 *
 */

inline int make_listen_nonblock(const char *host, const char *serv)
{
    int                 fd;
    const int           on = 1;
    struct addrinfo     hints, *res, *ressave;

    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if( getaddrinfo(host, serv, &hints, &res) != 0 ) {
        return -1;
    } else {
        debug(g_log, "getaddrinfo success\n");
    }

    ressave = res;
    do{
        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(fd < 0){
            log_strerr(g_log, "socket error\n");
            continue;
        }
        debug(g_log, "socket success\n");
        // set socket reusable
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        // disable nagle
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

        if(bind(fd, res->ai_addr, res->ai_addrlen) == 0){
            debug(g_log, "bind success\n");
            break;
        }
        log_strerr(g_log, "bind fail\n");
        close(fd);
    }while( (res = res->ai_next) != NULL );

    if(res == NULL) {
        return -1;
    }

    if(listen(fd, 1024) < 0) {
        log_strerr(g_log, "listen error\n");
        return -1;
    }
    debug(g_log, "listen success\n");

    if(setnonblock(fd) < 0){
        close(fd);
        return -1;
    }
    debug(g_log, "setnonblock success\n");

    freeaddrinfo(ressave);

    return(fd);
}

/*
 * fun: connect remote host:port
 * arg: remote host, remote port, connect status
 * ret: success=0, error=-1
 *
 */

inline int connect_nonblock(const char *host, const char *serv, int *flag)
{
    const int on = 1;
    int ret, sockfd;
    struct addrinfo hints,*res,*ressave;

    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if( (ret = getaddrinfo(host, serv, &hints, &res)) != 0 ){
        return -1;
    }

    ressave = res;

    do{
        if( (sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0 ){
            log_strerr(g_log, "socket error\n");
            return -1;
        }

        // set socket reusable
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        // disable nagle
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

        if( (ret = setnonblock(sockfd)) != 0 ){
            close(sockfd);
            return -1;
        }

        if( (ret = connect(sockfd, res->ai_addr, res->ai_addrlen)) < 0 ){
            if(errno != EINPROGRESS){
                log_strerr(g_log, "connect error\n");
                close(sockfd);
                return -1;
            } else {
                *flag = 0;
                break;
            }
        } else {
            *flag = 1;
            break;
        }
    }while( (res = res->ai_next) != NULL );

    freeaddrinfo(ressave);

    return(sockfd);
}

/*
 * fun: set fd nonblock
 * arg: fd
 * ret: success=0, error=-1
 *
 */

inline int setnonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * fun: accept client connection
 * arg: sockfd, remote address, address length
 * ret: success=0, error=-1
 *
 */

inline int accept_client(int sockfd, struct sockaddr_in *cliaddr, socklen_t *len)
{
    int fd, ret;

    fd = accept(sockfd, (struct sockaddr *)cliaddr, len);
    if(fd < 0){
        if( (errno != EAGAIN) || (errno != EWOULDBLOCK) ){
            log_strerr(g_log, "accept error\n");
        }
        return -1;
    }

    ret = setnonblock(fd);
    if(ret < 0){
        close(fd);
        return -1;
    }

    return fd;
}
