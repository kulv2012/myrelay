/*                                                            
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.       
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>             
 *
 */                                                           

#include <stdio.h>
#include <list.h>
#include <time.h>
#include <genpool.h>
#include <log.h>
#include <handler.h>
#include "cli_pool.h"
#include "my_buf.h"
#include "conn_pool.h"
#include "passwd.h"
#include "mysql_com.h"

extern log_t *g_log;

static genpool_handler_t *cli_pool;

/*
 * fun: init client connection pool
 * arg: max client connection number
 * ret: success return 0, error return -1
 *
 */

int cli_pool_init(int count)
{
    cli_pool = genpool_init(sizeof(cli_conn_t), count);
    if(cli_pool == NULL){
        log(g_log, "genpool init error\n");
        return -1;
    }

    return 0;
}

/*
 * fun: alloc client connection
 * arg: connection pointer, connection fd, ip and port
 * ret: success return client connection, error return NULL
 *
 */

static cli_conn_t *
cli_conn_alloc(conn_t *conn, int fd, uint32_t ip, uint16_t port)
{//申请一个客户端连接结构，跟当前的搭桥的连接结构挂接
    int res;
    cli_conn_t *c;

    if( (c = genpool_alloc_page(cli_pool)) == NULL ){
        log(g_log, "genpool alloc page error\n");
        return NULL;
    }

    c->fd = fd;//初始化相关结构
    c->ip = ip;
    c->port = port;
    c->conn = conn;
    INIT_LIST_HEAD(&(c->link));

    if( (res = buf_init(&(c->buf))) < 0 ){
        log(g_log, "buf_init error\n");
        genpool_release_page(cli_pool, c);
        return NULL;
    }

    if( (res = make_rand_scram(c->scram, SCRAMBLE_LENGTH)) < 0 ){
        log(g_log, "make rand scram error\n");
        genpool_release_page(cli_pool, c);
        return NULL;
    }
    c->scram[SCRAMBLE_LENGTH] = '\0';

    return c;
}

/*
 * fun: open client connection
 * arg: connection pointer, connection fd, ip and port
 * ret: success return 0, error return -1 
 *
 */

int cli_conn_open(conn_t *conn, int fd, uint32_t ip, uint16_t port)
{//调用cli_conn_alloc申请一个客户端连接结构，跟当前的中间conn挂接
    cli_conn_t *c;

    if(conn == NULL){
        return -1;
    }

    if( (c = cli_conn_alloc(conn, fd, ip, port)) == NULL ){
        log(g_log, "cli conn alloc error\n");
        return -1;
    }

    conn->cli = c;//对应这个连接结构的客户端连接

    return 0;
}

/*
 * fun: release client connection
 * arg: client connection
 * ret: success 0, error -1
 *
 */

static int cli_conn_release(cli_conn_t *conn)
{
    int res;

    if(conn == NULL){
        return -1;
    }

    conn->fd = -1;
    conn->ip = 0;
    conn->port = 0;
    list_del_init(&(conn->link));
    conn->conn = NULL;

    if( (res = buf_reset(&(conn->buf))) < 0 ){
        return -1;
    }

    return genpool_release_page(cli_pool, conn);
}

/*
 * fun: close client connection
 * arg: client connection
 * ret: success 0, error -1
 *
 */

int cli_conn_close(cli_conn_t *conn)
{
    int res;

    if(conn == NULL){
        return -1;
    }

    if(conn->fd >= 0){
        if( (res = del_handler(conn->fd)) < 0 ){
            log(g_log, "del_handler error\n");
        }
        close(conn->fd);
    }

    return cli_conn_release(conn);
}
