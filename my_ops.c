/*                                                            
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.       
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>             
 *
 */                                                           

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include <time.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <log.h>
#include <handler.h>
#include "my_ops.h"
#include "my_buf.h"
#include "conn_pool.h"
#include "my_pool.h"
#include "cli_pool.h"
#include "mysql_com.h"
#include "my_protocol.h"
#include "sqldump.h"
#include "passwd.h"
#include "my_conf.h"

extern log_t *g_log;
extern struct conf_t g_conf;

static int my_real_read(int fd, buf_t *buf, int *done);
static int my_real_read_result_set(int fd, buf_t *buf);
static int my_real_write(int fd, buf_t *buf, int *done);
static int pr_cap(uint32_t cap);

static int cli_com_ignored(conn_t *c);
static int cli_com_ok_write_cb(int fd, void *arg);
static int cli_com_forward(conn_t *c);
static int cli_com_unsupported(conn_t *c);

static int my_use_db_prepare(conn_t *c);
static int my_use_db_resp_cb(int fd, void *arg);
static int my_use_db_req_cb(int fd, void *arg);

static int my_ping_req_cb(int fd, void *arg);
static int my_ping_resp_cb(int fd, void *arg);

static int cli_hs_auth_fail_cb(int fd, void *arg);

static uint32_t cap_umask = CLIENT_FOUND_ROWS | CLIENT_NO_SCHEMA | \
                            CLIENT_ODBC | CLIENT_COMPRESS | CLIENT_SSL_VERIFY_SERVER_CERT | CLIENT_LOCAL_FILES | \
							CLIENT_IGNORE_SPACE | CLIENT_IGNORE_SIGPIPE | CLIENT_RESERVED | CLIENT_CONNECT_WITH_DB ;

/*
 * fun: mysql handshake stage1 callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

int my_hs_stage1_cb(int fd, void *arg)
{//读取mysql的连接认证原始数据
    int done, res = 0;
    my_conn_t *my;
    buf_t *buf;
    char *user, *pass, token[64], message[64];
    my_node_t *node;
    my_auth_init_t init;
    cli_auth_login_t login;
    my_info_t *info;

    my = (my_conn_t *)arg;
    node = my->node;
    buf = &(my->buf);

    user = node->user;
    pass = node->pass;
    info = node->info;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "read mysql error res[%d]\n", res);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ) {
            log(g_log, "del_handler fd[%d] error\n", fd);
            goto end;
        } else {
            debug(g_log, "del_handler fd[%d] success\n", fd);
        }

        res = add_handler(fd, EPOLLOUT, my_hs_stage2_cb, arg);
        if(res < 0){
            log(g_log, "add_handler fd[%d] error\n", fd);
            goto end;
        } else {
            debug(g_log, "add_handler fd[%d] success\n", fd);
        }

        if( (res = parse_init(buf, &init)) < 0 ){
            log(g_log, "parse init packet error\n");
            goto end;
        } else {
            debug(g_log, "parse init packet success\n");
        }

		memcpy(message, init.scram, 8);
        memcpy(message + 8, init.plug, 12);
        //memcpy(message, "%@R[SoWC", 8);
        //memcpy(message + 8, "+L|LG_+R={tV", 12);
        message[8+12] = '\0';

        my_info_set(init.prot_ver, init.lang, init.status, init.cap, init.srv_ver, strlen(init.srv_ver));

        login.pktno = 1;
        login.client_flags = init.cap & (~cap_umask);
        login.max_pkt_size = 16777216;
        login.charset = init.lang;
        strncpy(login.user, user, sizeof(login.user) - 1);
        login.user[sizeof(login.user) - 1] = '\0';
        if(pass[0] == '\0'){
            login.scram[0] = 0;
        } else {
            login.scram[0] = 20;
            scramble(token, message, pass);
            memcpy(login.scram + 1, token, 20);
        }
        strncpy(login.db, "", sizeof(login.db) - 1);
        login.db[sizeof(login.db) - 1] = '\0';

        if( (res = make_login(buf, &login)) < 0 ){
            log(g_log, "make login packet error\n");
            goto end;
        } else {
            debug(g_log, "make login packet success\n");
        }
    }

    return res;

end:
    my_conn_close_on_fail(my);

    return res;
}

/*
 * fun: mysql handshake stage2 callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

int my_hs_stage2_cb(int fd, void *arg)
{//给服务器发送认证数据，并且注册读取结果回调
    int done, res = 0;
    my_conn_t *my;
    buf_t *buf;

    my = (my_conn_t *)arg;
    buf = &(my->buf);

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "my_real_write error[%d], errno:%d, errmsg:%s\n", res, errno, strerror(errno) );
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "del_handler fd[%d] error\n", fd);
            goto end;
        } else {
            debug(g_log, "del_handler fd[%d] success\n", fd);
        }
		//认证数据发送完成后，下一步进行结果验证，登陆结果
        if( (res = add_handler(fd, EPOLLIN, my_hs_stage3_cb, arg)) < 0 ){
            log(g_log, "add_handler fd[%d] error\n", fd);
            goto end;
        } else {
            debug(g_log, "add_handler fd[%d] success\n", fd);
        }

        buf_reset(buf);
    }

    return res;

end:
    my_conn_close_on_fail(my);

    return res;
}

/*
 * fun: mysql handshake stage3 callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

int my_hs_stage3_cb(int fd, void *arg)
{
    int done, res = 0;
    my_conn_t *my;
    buf_t *buf;
    my_auth_result_t result;

    my = (my_conn_t *)arg;
    buf = &(my->buf);

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "my_real_read[%d]\n", res);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "del_handler fd[%d]\n", fd);
            goto end;
        } else {
            debug(g_log, "del_handler fd[%d] success\n");
        }

        if( (res = parse_auth_result(buf, &result)) < 0 ){
            log(g_log, "parse_auth_result error\n");
            goto end;
        } else {
            debug(g_log, "parse_auth_result success\n");
        }

        buf_reset(buf);

        if(result.result == 0){
            debug(g_log, "mysql authorized success\n");
            res = my_conn_set_avail(my);//跟mysql直接的验证成功了，下面标记这个连接为可用的,放入node的avail_head上面
        } else {
            log(g_log, "mysql authorized error, errmsg:[%s]\n", result.errmsg);
            goto end;
        }
    }

    return res;

end:
    my_conn_close_on_fail(my);

    return res;
}

/*
 * fun: prepare for client connection stage1
 * arg: connection
 * ret: success 0, error -1
 *
 */

int cli_hs_stage1_prepare(conn_t *c)
{
    int res = 0;
    cli_conn_t *cli;
    buf_t *buf;
    my_auth_init_t init;
    my_info_t *info;
    my_node_t *node;
    my_conn_t *my;

    debug(g_log, "%s called\n", __func__);

    cli = c->cli;

    if( (res = conn_alloc_my_conn(c)) < 0 ){
        log(g_log, "conn:%u conn_alloc_my_conn error\n", c->connid);
        return -1;
    } else {
        debug(g_log, "conn:%u conn_alloc_my_conn success\n", c->connid);
    }

    my = c->my;
    node = my->node;
    info = node->info;

    buf = &(cli->buf);

    bzero(&init, sizeof(init));
    init.pktno = 0;
    init.prot_ver = info->protocol;
    strncpy(init.srv_ver, info->ver, sizeof(init.srv_ver) - 1);
    init.srv_ver[sizeof(init.srv_ver) - 1] = '\0';
    init.tid = c->connid;
    memcpy(init.scram, cli->scram, 8);
    init.cap = info->cap;
    init.lang = 8;//info->lang;
    init.status = info->status;
    strncpy(init.plug, cli->scram + 8, 12);
    init.scram_len = 21;
    init.plug[12] = '\0';

    if( (res = make_init(buf, &init)) < 0 ){
        log(g_log, "conn:%u make_init error\n", c->connid);
        return res;
    } else {
        debug(g_log, "conn:%u make_init success\n", c->connid);
    }

    res = add_handler(cli->fd, EPOLLOUT, cli_hs_stage1_cb, cli);
    if(res < 0){
        log(g_log, "conn:%u add_handler fail\n", c->connid);
        return -1;
    } else {
        debug(g_log, "conn:%u add_handler success\n", c->connid);
    }

    return res;
}

/*
 * fun: client handshake stage1 callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

int cli_hs_stage1_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    conn_t *c;
    buf_t *buf;

    debug(g_log, "%s called\n", __func__);

    cli = (cli_conn_t *)arg;
    c = cli->conn;
    buf = &(cli->buf);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        res = add_handler(fd, EPOLLIN, cli_hs_stage2_cb, arg);
        if(res < 0){
            log(g_log, "conn:%u add_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        buf_reset(buf);
    }

    return res;

end:
    conn_close(c);

    return res;
}

/*
 * fun: client handshake stage2 callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

int cli_hs_stage2_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    conn_t *c;
    buf_t *buf;
    my_auth_result_t result;
    cli_auth_login_t login;
    my_result_error_t error;
    char token[128];

    cli = (cli_conn_t *)arg;
    c = cli->conn;
    buf = &(cli->buf);

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_read error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
            goto end;
        }

        if( (res = parse_login(buf, &login)) < 0 ){
            log(g_log, "conn:%u parse login error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u parse login success\n", c->connid);
        }

        if(!strcmp(login.user, g_conf.user)){
            scramble(token, cli->scram, g_conf.passwd);
            if(memcmp(token, login.scram, 20) == 0){
                log(g_log, "conn:%u login auth success\n", c->connid);

                strncpy(c->curdb, login.db, sizeof(c->curdb) - 1);
                result.pktno = 2;
                if( (res = make_auth_result(buf, &result)) < 0 ){
                    log(g_log, "conn:%u make auth result error\n", c->connid);
                    goto end;
                } else {
                    debug(g_log, "conn:%u make auth result success\n", c->connid);
                }

                res = add_handler(fd, EPOLLOUT, cli_hs_stage3_cb, arg);
                if(res < 0){
                    log(g_log, "conn:%u add_handler error\n", c->connid);
                    goto end;
                }

                return res;
            }
        }

        log(g_log, "login auth fail, wrong user or passwd\n");

        error.pktno = 2;
        error.field_count = 0xff;
        error.err = 1045;
        error.marker = '#';
        memcpy(error.sqlstate, "28000", 5);
        strncpy(error.msg, "Access denied", sizeof(error.msg) - 1);
        error.msg[sizeof(error.msg) - 1] = '\0';

        make_result_error(buf, &error);

        res = add_handler(fd, EPOLLOUT, cli_hs_auth_fail_cb, arg);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        }
    }

    return res;

end:
    conn_close(c);

    return res;
}

/*
 * fun: client handshake stage3 callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

int cli_hs_stage3_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    conn_t *c;
    buf_t *buf;

    cli = (cli_conn_t *)arg;
    buf = &(cli->buf);
    c = cli->conn;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
            goto end;
        }

        res = add_handler(fd, EPOLLIN, cli_query_cb, arg);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        }

        buf_reset(buf);
        buf_reset(&(c->buf));

        conn_state_set_idle(c);
    }

    return res;

end:
    //fix me
    conn_close(c);

    return res;
}

/*
 * fun: client handshake fail callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

static int cli_hs_auth_fail_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    conn_t *c;
    buf_t *buf;

    cli = (cli_conn_t *)arg;
    buf = &(cli->buf);
    c = cli->conn;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        conn_close(c);
    }

    return res;

end:
    conn_close(c);
    return res;
}

/*
 * fun: client query callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

int cli_query_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    buf_t *buf;
    conn_t *c;
    my_conn_t *my;
    cli_com_t com;

    debug(g_log, "%s called\n", __func__);

    cli = (cli_conn_t *)arg;
    c = cli->conn;
    buf = &(c->buf);
    my = c->my;

    if(c->state == STATE_IDLE){
        conn_state_set_reading_client(c);
        gettimeofday(&(c->tv_start), NULL);
    } else if( (c->state == STATE_PREPARE_MYSQL) || (c->state == STATE_WRITING_MYSQL) ){
        log(g_log, "conn:%u client can be read when preparing or writing mysql\n", c->connid);
        goto end;
    } else if(c->state == STATE_READ_MYSQL_WRITE_CLIENT) {
        conn_state_set_reading_client(c);
        sqldump(c);
        gettimeofday(&(c->tv_start), NULL);

        if( (res = del_handler(my->fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
        }
    } else {
        gettimeofday(&(c->tv_end), NULL);
    }

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_read error\n", c->connid);
        goto end;//客户端数据读取出错,关闭2端的连接?
    }

    if(done){//读取了一个完整的包，下面准备处理 
        if( (res = parse_com(buf, &com)) < 0 ){
            log(g_log, "conn:%u parse com error\n", c->connid);
            goto end;
        }
        c->comno = com.comno;
        strncpy(c->arg, com.arg, sizeof(c->arg) - 1);
        c->arg[sizeof(c->arg) - 1] = '\0';

        switch(c->comno)
        {
            // command ignored and quit
            case COM_QUIT:
                log(g_log, "quit\n");
            case COM_SHUTDOWN:
                log(g_log, "shutdown\n");
                log(g_log, "conn:%u command ignored\n", c->connid);
                res = cli_com_ignored(c);
                goto end;//挂掉这个连接

            // command ignored
            case COM_REFRESH:
                log(g_log, "refresh\n");
            case COM_PROCESS_KILL:
                log(g_log, "kill\n");
            case COM_DEBUG:
                log(g_log, "debug\n");
                log(g_log, "conn:%u command ignored\n", c->connid);
                res = cli_com_ignored(c);
                break;

            case COM_INIT_DB:
                log(g_log, "init db, ignore frist.\n");
				res = cli_com_ignored(c);//先忽略这个数据库初始化请求，待会query的时候再看数据库是否一样。这样能避免重复use db
                strncpy(c->curdb, c->arg, sizeof(c->curdb) - 1);
				/*
                if( (res = cli_com_forward(c)) < 0 ){
                    log(g_log, "conn:%u cli_com_forward error\n", c->connid);
                    goto end;
                } else {
                    debug(g_log, "conn:%u cli_com_forward success\n", c->connid);
                }

                strncpy(my->ctx.curdb, c->curdb, sizeof(my->ctx.curdb) - 1);
                my->ctx.curdb[sizeof(my->ctx.curdb) - 1] = '\0';

                conn_state_set_writing_mysql(c);
				*/
                break;

            // command unsupported
            case COM_BINLOG_DUMP:
                log(g_log, "binlog dump\n");
            case COM_TABLE_DUMP:
                log(g_log, "table dump\n");
            case COM_REGISTER_SLAVE:
                log(g_log, "register slave\n");
            case COM_CHANGE_USER:
                log(g_log, "change user\n");
                res = cli_com_unsupported(c);
                log(g_log, "conn:%u client command unsupported\n", c->connid);
                goto end;

            case COM_CREATE_DB:
                log(g_log, "create db\n");
            case COM_DROP_DB:
                log(g_log, "drop db\n");
            case COM_QUERY:
				//下面为了选一个合适的连接，虽然当前分配了，但可能需要切换主从
                if( (res = conn_alloc_my_conn(c)) < 0 ){ 
                    log(g_log, "conn:%u alloc mysql conn error\n", c->connid);
                    goto end;
                }

                my = c->my;
                if(strcmp(my->ctx.curdb, c->curdb)){//还需要给服务器发送切换数据库的命令 
                    if( (res = my_use_db_prepare(c)) < 0 ){
                        log(g_log, "conn:%u my_use_db_prepare error\n", c->connid);
                        goto end;
                    } else {
                        debug(g_log, "conn:%u my_use_db_prepare success\n", c->connid);
                    }

                    conn_state_set_prepare_mysql(c);//标记为这个在等待切换数据库，完成后才能做后面的事情，
					//就是真正处理命令转发my_use_db_prepare里面会放回调的

                    break;
                }
            default:
                if( (res = cli_com_forward(c)) < 0 ){
                    log(g_log, "conn:%u cli_com_forward error\n", c->connid);
                    goto end;
                } else {
                    debug(g_log, "conn:%u cli_com_forward success\n", c->connid);
                }

                conn_state_set_writing_mysql(c);
        }
    }

    return res;

end:
    //conn_close_with_my(c);
	conn_close(c) ;

    return res;
}

/*
 * fun: mysql query callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

int my_query_cb(int fd, void *arg)
{
    int done, res = 0;
    my_conn_t *my;
    conn_t *c;
    buf_t *buf;

    my = (my_conn_t *)arg;
    c = my->conn;
    buf = &(c->buf);

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u del_handler success\n", c->connid);
        }

        res = add_handler(fd, EPOLLIN, my_answer_cb, my);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u add_handler success\n", c->connid);
        }

        buf_reset(buf);

        conn_state_set_read_mysql_write_client(c);

    }

    return res;

end:
    conn_close_with_my(c);

    return res;
}

/*
 * fun: mysql answer callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

int my_answer_cb(int fd, void *arg)
{
    int res = 0;
    my_conn_t *my;
    cli_conn_t *cli;
    conn_t *c;
    buf_t *buf;

    my = (my_conn_t *)arg;
    c = my->conn;
    buf = &(c->buf);
    cli = c->cli;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_read_result_set(fd, buf)) < 0 ){
        log_err(g_log, "conn:%u my_real_read_result_set error\n", c->connid);
        goto end;
    } else {
        debug(g_log, "conn:%u my_real_read success, res[%d]\n", c->connid, res);
    }

    if( (res = del_handler(fd)) < 0 ){
        log(g_log, "conn:%u del_handler error\n", c->connid);
        goto end;
    } else {
        debug(g_log, "conn:%u del_handler success\n", c->connid);
    }

    if( (res = del_handler(cli->fd)) < 0 ){
        log(g_log, "conn:%u del_handler error\n", c->connid);
        goto end;
    }

    res = add_handler(cli->fd, EPOLLOUT, cli_answer_cb, cli);
    if(res < 0){
        log(g_log, "conn:%u add_handler error\n", c->connid);
        goto end;
    } else {
        debug(g_log, "conn:%u add_handler success\n", c->connid);
    }

    buf_rewind(buf);

    return res;

end:
    conn_close_with_my(c);

    return res;
}

/*
 * fun: client answer callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

int cli_answer_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    buf_t *buf;
    my_conn_t *my;
    conn_t *c;

    cli = (cli_conn_t *)arg;
    c = cli->conn;
    buf = &(c->buf);
    my = c->my;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u del_handler success\n", c->connid);
        }

        res = add_handler(fd, EPOLLIN, cli_query_cb, cli);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u add_handler success\n", c->connid);
        }

        res = add_handler(my->fd, EPOLLIN, my_answer_cb, my);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u add_handler success\n", c->connid);
        }

        gettimeofday(&(c->tv_end), NULL);

        buf_reset(buf);

    }

    return res;

end:
    conn_close(c);

    return res;
}

/*
 * fun: real read socket
 * arg: fd, buffer, flag
 * ret: success return num of read, error -1
 *
 */

static int my_real_read(int fd, buf_t *buf, int *done)
{
    int left, n;
    uint32_t pktlen;
    char *ptr;

    ptr = buf->ptr;
    *done = 0;

    left = buf->size - buf->used;
    ptr = buf->ptr + buf->used;

AGAIN:
    if( (n = read(fd, ptr, left)) < 0 ){
        if(errno == EINTR){
            goto AGAIN;
		} else if( errno == EAGAIN || errno == EWOULDBLOCK){
			return 0 ;
        } else {
            return n;
        }
    } else if(n == 0) {
        return n;
    } else {
        buf->used += n;
        buf->pos += n;

        if(buf->used >= HEADER_SIZE){
			//如果已经读取到了4个字节的固定长度，那么就可以拿到数据包有多大了，也就是这次应该读取的长度是多少大
            ptr = buf->ptr;
            pktlen = 0;
            memcpy(&pktlen, ptr, 3);
			if((pktlen + HEADER_SIZE) > buf->size){//总大小是否超过了当前缓冲的大小，如果是需要重新申请一块大内存
				if(buf_realloc(buf, pktlen + HEADER_SIZE) == NULL){
					return -1;
				}
			}
            if(buf->used >= (pktlen + HEADER_SIZE)){//读取完毕了
                *done = 1;
            }
        }
		return n;
	}
	return 0 ;
}

/*
 * fun: real read result for socket
 * arg: fd, buffer
 * ret: success return num of read, error -1
 *
 */

static int my_real_read_result_set(int fd, buf_t *buf)
{
    int left, n;
    char *ptr;

    left = buf->size - buf->used;
    ptr = buf->ptr + buf->used;

AGAIN:
    if( (n = read(fd, ptr, left)) < 0 ){
        if(errno == EINTR){
            goto AGAIN;
		}else if( errno == EAGAIN || errno == EWOULDBLOCK){
			return 0 ;
        } else {
            return n;
        }
    } else if(n == 0) {
        return n;
    } else {
        buf->used += n;
        buf->pos += n;

        return n;
    }
}

/*
 * fun: real write for socket
 * arg: fd, buffer, flag
 * ret: success return num of write, error -1
 *
 */

static int my_real_write(int fd, buf_t *buf, int *done)
{
    int left, n;
    char *ptr;

    *done = 0;

    left = buf->used - buf->pos;
    ptr = buf->ptr + buf->pos;

AGAIN:
    if( (n = write(fd, ptr, left)) < 0 ){
		//返回小于0，可能有问题
        if(errno == EINTR){
            goto AGAIN;
		} else if( errno == EAGAIN || errno == EWOULDBLOCK ){
			return 0 ;//可能会被阻塞，过会在写
        } else {
            return n;
        }
    } else if(n == 0) {//等于0，也就是啥也没法送出
        return n;
    } else {
        buf->pos += n;
        if(buf->pos >= buf->used){
            *done = 1;
        }

        return n;
    }
	return 0 ;
}

/*
 * fun: ignore some client command
 * arg: connection
 * ret: success 0, error -1
 *
 */

static int cli_com_ignored(conn_t *c)
{
#define CLI_COM_IGNORE_OK_PKT_SIZE 7

    int res = 0, fd;
    uint32_t pktlen;
    uint8_t pktno;

    buf_t *buf;
    cli_conn_t *cli;
    char *ptr;

    debug(g_log, "%s called\n", __func__);

    pktlen = 0;
    pktno = 1;
    buf = &(c->buf);
    cli = c->cli;
    fd = cli->fd;

    buf_reset(buf);
    ptr = buf->ptr;

    bzero(ptr + 4, CLI_COM_IGNORE_OK_PKT_SIZE);
    pktlen = CLI_COM_IGNORE_OK_PKT_SIZE;
    memcpy(ptr, &pktlen, 3);
    memcpy(ptr + 3, &pktno, 1);

    buf->used += (CLI_COM_IGNORE_OK_PKT_SIZE + 4);
    buf->pos += (CLI_COM_IGNORE_OK_PKT_SIZE + 4);
    buf_rewind(buf);

    res = add_handler(fd, EPOLLOUT, cli_com_ok_write_cb, cli);
    if(res < 0){
        log(g_log, "conn:%u add_handler error\n", c->connid);
        return res;
    } else {
        debug(g_log, "conn:%u add_handler success\n", c->connid);
    }

    return 0;
}

/*
 * fun: write ok packet to client callback
 * arg: fd, client connection
 * ret: success 0, error -1
 *
 */

static int cli_com_ok_write_cb(int fd, void *arg)
{
    int done, res = 0;
    cli_conn_t *cli;
    buf_t *buf;
    my_conn_t *my;
    conn_t *c;

    cli = (cli_conn_t *)arg;
    c = cli->conn;
    buf = &(c->buf);
    my = c->my;

    debug(g_log, "%s called\n", __func__);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u del_handler success\n", c->connid);
        }

        res = add_handler(fd, EPOLLIN, cli_query_cb, cli);
        if(res < 0){
            log(g_log, "conn:%u add_handler error\n", c->connid);
            goto end;
        } else {
            debug(g_log, "conn:%u add_handler success\n", c->connid);
        }

        buf_reset(buf);

        conn_state_set_reading_client(c);
    }

    return res;

end:
    conn_close(c);

    return res;
}

/*
 * fun: forward client command to mysql
 * arg: connection
 * ret: success 0, error -1
 *
 */

static int cli_com_forward(conn_t *c)
{
    int res = 0, fd;
    my_conn_t *my;
    buf_t *buf;
    cli_conn_t *cli;
    my_node_t *node;

    my = c->my;
    fd = my->fd;
    buf = &(c->buf);
    cli = c->cli;
    node = my->node;

    log(g_log, "conn:%u mysql[%s:%s]\n", c->connid, node->host, node->srv);

    res = add_handler(fd, EPOLLOUT, my_query_cb, my);
    if(res < 0){
        log(g_log, "conn:%u add_handler error\n", c->connid);
        return res;
    } else {
        debug(g_log, "conn:%u add_handler success\n", c->connid);
    }

    buf_rewind(buf);

    return res;
}

/*
 * fun: unsupported client command, do nothing
 * arg: onnection
 * ret: always return 0
 *
 */

static int cli_com_unsupported(conn_t *c)
{
    return 0;
}

/*
 * fun: prepare send "use db" command to mysql
 * arg: connection
 * ret: success 0, error -1
 *
 */

static int my_use_db_prepare(conn_t *c)
{
    int fd, len, res = 0;
    buf_t *buf;
    my_conn_t *my;
    cli_com_t com;

    my = c->my;
    fd = my->fd;
    buf = &(my->buf);

    com.pktno = 0;
    com.comno = COM_INIT_DB;
    len = strlen(c->curdb);
    memcpy(com.arg, c->curdb, len);
    com.len = len;

    make_com(buf, &com);
    res = add_handler(fd, EPOLLOUT, my_use_db_req_cb, my);
    if(res < 0){
        log(g_log, "conn:%u add_handler error\n", c->connid);
    } else {
        debug(g_log, "conn:%u add_handler success\n", c->connid);
    }

    buf_rewind(buf);

    return res;
}

/*
 * fun: send "use db" command to mysql callback
 * arg: fd, mysql connection 
 * ret: success 0, error -1
 *
 */

static int my_use_db_req_cb(int fd, void *arg)
{
    int res = 0, done;
    my_conn_t *my;
    conn_t *c;
    buf_t *buf;

    my = (my_conn_t *)arg;
    c = my->conn;
    buf = &(my->buf);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_write error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        res = add_handler(fd, EPOLLIN,my_use_db_resp_cb,arg);
        if(res < 0){
            log(g_log, "conn:%u add_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        buf_reset(buf);
    }

    return res;

end:
    conn_close_with_my(c);

    return res;
}

/*
 * fun: read mysql resp callback after sending "use db"
 * arg: fd, mysql connection 
 * ret: success 0, error -1
 *
 */

static int my_use_db_resp_cb(int fd, void *arg)
{
    int res = 0, done;
    my_conn_t *my;
    conn_t *c;
    buf_t *buf;

    my = (my_conn_t *)arg;
    c = my->conn;
    buf = &(my->buf);

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "conn:%u my_real_read error\n", c->connid);
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "conn:%u del_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        strncpy(my->ctx.curdb, c->curdb, sizeof(my->ctx.curdb) - 1);
        my->ctx.curdb[sizeof(my->ctx.curdb) - 1] = '\0';

        res = add_handler(fd, EPOLLOUT, my_query_cb, arg);
        if(res < 0){
            log(g_log, "conn:%u add_handler fd[%d] error\n", c->connid, fd);
            goto end;
        }

        buf_reset(buf);

        conn_state_set_writing_mysql(c);
    }

    return res;

end:
    conn_close_with_my(c);

    return res;
}

/*
 * fun: prepare send "ping" command to mysql
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

int my_ping_prepare(my_conn_t *my)
{
    int fd, len, res = 0;
    buf_t *buf;
    cli_com_t com;

    fd = my->fd;
    buf = &(my->buf);

    com.pktno = 0;
    com.comno = COM_PING;
    com.len = 0;

    make_com(buf, &com);
    res = add_handler(fd, EPOLLOUT, my_ping_req_cb, my);
    if(res < 0){
        log(g_log, "add_handler error\n");
    }

    buf_rewind(buf);

    return res;
}

/*
 * fun: send "ping" command to mysql callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

static int my_ping_req_cb(int fd, void *arg)
{
    int res = 0, done;
    my_conn_t *my;
    buf_t *buf;

    my = (my_conn_t *)arg;
    buf = &(my->buf);

    if( (res = my_real_write(fd, buf, &done)) < 0 ){
        log_err(g_log, "my_real_write error\n");
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "del_handler fd[%d] error\n", fd);
            goto end;
        }

        res = add_handler(fd, EPOLLIN,my_ping_resp_cb,arg);
        if(res < 0){
            log(g_log, "add_handler fd[%d] error\n", fd);
            goto end;
        }

        buf_reset(buf);
    }

    return res;

end:
    my_conn_close(my);

    return res;
}

/*
 * fun: mysql resp for "ping" callback
 * arg: fd, mysql connection
 * ret: success 0, error -1
 *
 */

static int my_ping_resp_cb(int fd, void *arg)
{
    int res = 0, done;
    my_conn_t *my;
    buf_t *buf;

    my = (my_conn_t *)arg;
    buf = &(my->buf);

    if( (res = my_real_read(fd, buf, &done)) < 0 ){
        log_err(g_log, "my_real_read error\n");
        goto end;
    }

    if(done){
        if( (res = del_handler(fd)) < 0 ){
            log(g_log, "del_handler fd[%d] error\n", fd);
            goto end;
        }

        buf_reset(buf);

        my_conn_put(my);
    }

    return res;

end:
    my_conn_close(my);

    return res;
}
