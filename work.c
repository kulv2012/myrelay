/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <log.h>
#include <sock.h>
#include <handler.h>
#include "my_ops.h"
#include "conn_pool.h"
#include "my_pool.h"
#include "my_conf.h"

extern log_t *g_log;
extern struct conf_t g_conf;
extern int g_usr1_reload;

static my_conf_t myconf_cur, myconf_new;

static int accept_client_cb(int listenfd, void *arg);
static int usr1_reload(void);

/*
 * fun: real work process
 * arg: listen fd
 * ret: it should not return
 *
 */

int work(int fd)
{
    int i, res = 0, level;
    my_node_conf_t *mynode;

    // log init
    if(!strcmp(g_conf.loglevel, "none")){
        level = LOG_NONE;
    } else if(!strcmp(g_conf.loglevel, "log")) {
        level = LOG_LEVEL_LOG;
    } else if(!strcmp(g_conf.loglevel, "debug")) {
        level = LOG_LEVEL_DEBUG;
    } else if(!strcmp(g_conf.loglevel, "info")) {
        level = LOG_LEVEL_INFO;
    } else {
        level = LOG_LEVEL_LOG;
    }

    if( (g_log = log_init(g_conf.log, level)) == NULL ){
        fprintf(stderr, "log init error\n");
        exit(-1);
    }

    if(init_handler(100000) < 0){
        log(g_log, "handler init error\n");
        exit(-1);
    } else {
        log(g_log, "handler init success\n");
    }

    // timer init must before cli_pool_init conn_pool_init my_pool_init
    if(timer_init() < 0){
        log(g_log, "timer_inti error\n");
        exit(-1);
    } else {
        log(g_log, "timer_init success\n");
    }

    // client connection pool init
    if(cli_pool_init(g_conf.max_connections) < 0){
        log(g_log, "client pool init error\n");
        exit(-1);
    } else {
        log(g_log, "client pool init success\n");
    }

    // connection pool init
    if(conn_pool_init(g_conf.max_connections) < 0){
        log(g_log, "conn pool init error\n");
        exit(-1);
    } else {
        log(g_log, "conn pool init success\n");
    }

    // mysql connection pool init
    if(my_pool_init(g_conf.max_connections) < 0){
        log(g_log, "mysql pool init error\n");
        exit(-1);
    } else {
        log(g_log, "mysql pool init success\n");
    }

    // mysql dump log init
    if(sqldump_init(g_conf.sqllog) < 0){
        log(g_log, "sqldump %s init error\n", g_conf.sqllog);
        exit(-1);
    } else {
        log(g_log, "sqldump init success\n");
    }

    log(g_log, "all init success\n");

    // mysql conf parse
    res = mysql_conf_parse(g_conf.mysql_conf, &myconf_cur);
    if(res < 0){
        log(g_log, "mysql_conf_parse %s error\n", g_conf.mysql_conf);
    }

    res = mysql_conf_parse(g_conf.mysql_conf, &myconf_new);
    if(res < 0){
        log(g_log, "mysql_conf_parse %s error\n", g_conf.mysql_conf);
    }

    // mysql register
    for(i = 0; i < myconf_cur.mcount; i++){
        mynode = &(myconf_cur.master[i]);
        res = my_master_reg(mynode->host, mynode->port, \
                    mynode->user, mynode->pass, mynode->cnum);
        if(res < 0){
            log(g_log, "my_master_reg error\n");
        }
    }

    for(i = 0; i < myconf_cur.scount; i++){
        mynode = &(myconf_cur.slave[i]);
        res = my_slave_reg(mynode->host, mynode->port, \
                    mynode->user, mynode->pass, mynode->cnum);
        if(res < 0){
            log(g_log, "my_slave_reg error\n");
        }
    }

    // listen fd epoll
    if( (res = add_handler(fd, EPOLLIN, accept_client_cb, NULL)) < 0 ){
        log(g_log, "add_handler listenfd[%d] fail\n", fd);
        return -1;
    } else {
        debug(g_log, "add_handler listenfd[%d] success\n", fd);
    }

    while(1){
        debug(g_log, "epoll_handler\n");
        res = epoll_handler(1000);
        // timer
        timer();
        // catch usr1 signal
        if(g_usr1_reload){
            usr1_reload();
        }
    }

    return 0;
}

/*
 * fun: accept client callback
 * arg: listen fd, arg(not used)
 * ret: success 0, error -1
 *
 */

static int accept_client_cb(int listenfd, void *arg)
{
    int clientfd, res = 0;
    uint32_t clientip;
    uint16_t clientport;
    struct sockaddr_in cliaddr;
    socklen_t clen;

    conn_t *c;

    debug(g_log, "accept_client_cb callback\n");

    while(1){
        if(!my_pool_have_conn()){
            debug(g_log, "mysql pool is empty, waiting\n");
            break;
        }
        clen = sizeof(cliaddr);
        clientfd = accept_client(listenfd, &cliaddr, &clen);
        if(clientfd < 0){
            if(errno != EAGAIN){
                log_err(g_log, "accept client error\n");
            }
            break;
        }

        if( (res = setnonblock(clientfd)) < 0 ){
            log(g_log, "fd[%d] setnonblock error\n", clientfd);
            close(clientfd);
        }

        clientip = ntohl(cliaddr.sin_addr.s_addr);
        clientport = ntohs(cliaddr.sin_port);

        if( (c = conn_open(clientfd, clientip, clientport)) == NULL ){
            log(g_log, "connection alloc fail, close connection\n");
            close(clientfd);
        } else {
            debug(g_log, "conn:%d connection alloc success\n", c->connid);
        }

        log(g_log, "conn:%d client[%s:%d] connection accept\n", \
                    c->connid, inet_ntoa(cliaddr.sin_addr), \
                    ntohs(cliaddr.sin_port));

        if( (res = cli_hs_stage1_prepare(c)) < 0 ){
            log(g_log, "conn:%d cli_hs_sate1_prepare error, \
                            close connection\n", c->connid);
            conn_close(c);
        }
    }

    return 0;
}

/*
 * fun: reload mysql config
 * arg:
 * ret: success 0, error -1
 *
 */

static int usr1_reload(void)
{
    int i, j, res;
    my_node_conf_t *cur, *new;

    if(!g_usr1_reload){
        return 0;
    } else {
        log(g_log, "catch usr1 signal\n");
    }

    g_usr1_reload = 0;

    res = mysql_conf_parse(g_conf.mysql_conf, &myconf_new);
    if(res < 0){
        log(g_log, "mysql_conf_parse %s error\n", g_conf.mysql_conf);
        return -1;
    }

    for(i = 0; i < myconf_cur.mcount; i++){
        cur = &(myconf_cur.master[i]);
        for(j = 0; j < myconf_new.mcount; j++){
            new = &(myconf_new.master[j]);
            if((!strcmp(new->host, cur->host)) && \
                                (!strcmp(new->port, cur->port))){
                break;
            }
        }

        if(j == myconf_new.mcount){
            my_unreg(cur->host, cur->port);
        }
    }

    for(i = 0; i < myconf_cur.scount; i++){
        cur = &(myconf_cur.slave[i]);
        for(j = 0; j < myconf_new.scount; j++){
            new = &(myconf_new.slave[j]);
            if((!strcmp(new->host, cur->host)) && \
                                (!strcmp(new->port, cur->port))){
                break;
            }
        }

        if(j == myconf_new.scount){
            my_unreg(cur->host, cur->port);
        }
    }

    for(i = 0; i < myconf_new.mcount; i++){
        new = &(myconf_new.master[i]);
        for(j = 0; j < myconf_cur.mcount; j++){
            cur = &(myconf_cur.master[j]);
            if((!strcmp(new->host, cur->host)) && \
                                (!strcmp(new->port, cur->port))){
                break;
            }
        }

        if(j == myconf_cur.mcount){
            my_master_reg(new->host, new->port, new->user, \
                                            new->pass, new->cnum);
        }
    }

    for(i = 0; i < myconf_new.scount; i++){
        new = &(myconf_new.slave[i]);
        for(j = 0; j < myconf_cur.scount; j++){
            cur = &(myconf_cur.slave[j]);
            if((!strcmp(new->host, cur->host)) && \
                                (!strcmp(new->port, cur->port))){
                break;
            }
        }

        if(j == myconf_cur.scount){
            my_slave_reg(new->host, new->port, new->user, \
                                            new->pass, new->cnum);
        }
    }

    myconf_cur = myconf_new;

    return 0;
}
