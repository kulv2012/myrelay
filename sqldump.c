/*                                                            
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.       
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>             
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <common.h>
#include <sock.h>
#include "conn_pool.h"
#include "cli_pool.h"
#include "my_pool.h"
#include "mysql_com.h"

static int sql_fd = -1;
static int sql_count = 0;
static char sqldump_fname[1024] = "./sql.log";

static int parse_req_sql(conn_t *c, char *buf, int len);

/*
 * fun: init mysql dump
 * arg: mysql dump file
 * ret: success 0, error -1
 *
 */

int sqldump_init(const char *fname)
{
    if( (sql_fd = open(fname, O_WRONLY|O_APPEND|O_CREAT, S_IRWXU)) < 0 ){
        return -1;
    }

    if(setnonblock(sql_fd) < 0){
        close(sql_fd);
        return -1;
    }

    strncpy(sqldump_fname, fname, sizeof(sqldump_fname) - 1);

    return sql_fd;
}

/*
 * fun: mysql sql dump
 * arg: connection
 * ret: success 0, error -1
 *
 */

int sqldump(conn_t *c)
{
    int res = 0, n, msec;
    char buf[8192], tmp[4096];
    char timebuf[64];
    char ipstr[64];
    time_t t;
    struct tm tm;

    cli_conn_t *cli = c->cli;
    my_conn_t *my = c->my;
    my_node_t *node = my->node;

    t = time(NULL);
    localtime_r(&t, &tm);
    strftime(timebuf, sizeof(timebuf), "%F %T", &tm);

    if((++sql_count % 1024) == 0){
        if(sql_fd >= 0){
            close(sql_fd);
        }
        if( (res = sqldump_init(sqldump_fname)) < 0 ){
            return res;
        }
    }

    ipint2str(ipstr, sizeof(ipstr), cli->ip);
    msec = (c->tv_end.tv_sec - c->tv_start.tv_sec) * 1000 + \
                        (c->tv_end.tv_usec - c->tv_start.tv_usec) / 1000;

    parse_req_sql(c, tmp, sizeof(tmp));

    n = snprintf(buf, sizeof(buf), "%s conn:%u %s:%d %s:%s %ums - %s\n", \
                timebuf, c->connid, ipstr, cli->port, node->host, node->srv, msec, tmp);
    res = write(sql_fd, buf, n);

    return res;
}

/*
 * fun: close mysql sql dump
 * arg: 
 * ret: success 0, error -1
 *
 */

int sqldump_close(void)
{
    return close(sql_fd);
}

/*
 * fun: parse mysql sql
 * arg: connection, buffer, buffer len
 * ret: success 0, error -1
 *
 */

static int parse_req_sql(conn_t *c, char *buf, int len)
{
    int n;

    switch(c->comno)
    {
        case COM_QUIT:
            n = snprintf(buf, len - 1, "%s", "quit");
            break;
        case COM_SHUTDOWN:
            n = snprintf(buf, len - 1, "%s", "shutdown");
            break;
        case COM_REFRESH:
            n = snprintf(buf, len - 1, "%s", "refresh");
            break;
        case COM_PROCESS_KILL:
            n = snprintf(buf, len - 1, "%s", "kill");
            break;
        case COM_DEBUG:
            n = snprintf(buf, len - 1, "%s", "debug");
            break;
        case COM_INIT_DB:
            n = snprintf(buf, len - 1, "use %s", c->arg); 
            break;
        case COM_BINLOG_DUMP:
            n = snprintf(buf, len - 1, "%s", \
                                    "unsupported command[dump binlog]");
            break;
        case COM_TABLE_DUMP:
            n = snprintf(buf, len - 1, "%s", \
                                    "unsupported command[dump table]");
            break;
        case COM_REGISTER_SLAVE:
            n = snprintf(buf, len - 1, "%s", \
                                    "unsupported command[register slave]");
            break;
        case COM_CREATE_DB:
            n = snprintf(buf, len - 1, "create database %s", c->arg);
            break;
        case COM_DROP_DB:
            n = snprintf(buf, len - 1, "drop database %s", c->arg);
            break;
        case COM_QUERY:
            n = snprintf(buf, len - 1, "%s", c->arg);
            break;
        default:
            n = snprintf(buf, len - 1, "%s", "unknown command");
    }

    buf[n] = '\0';

    return n;
}
