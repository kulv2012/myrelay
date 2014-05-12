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
#include <string.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <genpool.h>
#include <sock.h>
#include <log.h>
#include <timer.h>
#include <handler.h>
#include "my_pool.h"
#include "my_buf.h"
#include "my_ops.h"
#include "conn_pool.h"
#include "my_conf.h"
#include "def.h"

extern log_t *g_log;
extern struct conf_t g_conf;

static my_pool_t *mypool;
static genpool_handler_t *handler;//mysql 的连接池，在my_pool_init分配

static my_info_t myinfo;

static int my_conn_init(my_conn_t *my, my_node_t *n);
static int my_node_init(my_node_t *n);
static my_conn_t *my_conn_alloc(my_node_t *n);
static int make_my_conn(my_conn_t *my);
static int _my_reg(my_node_t *node, char *host, char *srv, char *user, char *pass, int mincount, int maxcount);
static int my_conn_set_used(my_conn_t *my, void *ptr);
static int my_conn_set_dead(my_conn_t *my);
static int my_conn_set_raw(my_conn_t *my);
static int my_conn_set_fail(my_conn_t *my);
static int my_conn_set_ping(my_conn_t *my);

static int my_conn_dead_reconnect_timer(unsigned long arg);
static int my_conn_fail_reconnect_timer(unsigned long arg);
static int my_conn_pool_status_timer(unsigned long arg);
static int my_conn_pool_ping_timer(unsigned long arg);
static int my_conn_pool_ping_timeout_timer(unsigned long arg);

static int my_node_set_closing(my_node_t *node);
static int my_node_is_closing(my_node_t *node);
static int my_node_closing_cleanup_timer(unsigned long arg);

/*
 * fun: init mysql context
 * arg: mysql context
 * ret: always return 0
 *
 */

static int my_ctx_init(my_ctx_t *ctx)
{
    ctx->dirty = 0;

    bzero(ctx->curdb, sizeof(ctx->curdb));

    return 0;
}

/*
 * fun: set mysql info
 * arg: mysql protocol, language, status, cap, version, verlen
 * ret: success 0, error -1
 *
 */

int my_info_set(uint8_t prot, uint8_t lang, uint16_t status, \
                            uint32_t cap, char *ver, int ver_len)
{
    int len;
    time_t now = time(NULL);

    if(now - myinfo.update_time < 10){
        return 0;
    }

    myinfo.avail = 1;
    myinfo.protocol = prot;
    myinfo.lang = lang;
    myinfo.status = status;
    myinfo.cap = cap;

    if(ver_len > (sizeof(myinfo.ver) - 1)){
        len = sizeof(myinfo.ver) - 1;
    } else {
        len = ver_len;
    }

    memcpy(myinfo.ver, ver, len);
    myinfo.ver[len] = '\0';

    myinfo.update_time = now;

    return 0;
}

/*
 * fun: init mysql connection
 * arg: mysql connection, mysql node
 * ret: success 0, error -1
 *
 */

static int my_conn_init(my_conn_t *my, my_node_t *n)
{//初始化mysql连接的字段
    my->fd = -1;
    my->node = (void *)n;//我所属的节点
    INIT_LIST_HEAD(&(my->link));
    my->conn = NULL;

    buf_init(&(my->buf));

    my_ctx_init(&(my->ctx));

    my->state_time = 0;

    return 0;
}

/*
 * fun: init mysql node
 * arg: mysql node
 * ret: success 0, error -1
 *
 */

static int my_node_init(my_node_t *n)
{
    bzero(n->host, sizeof(n->host));
    bzero(n->srv, sizeof(n->srv));
    bzero(n->user, sizeof(n->user));
    bzero(n->pass, sizeof(n->pass));

    INIT_LIST_HEAD(&(n->used_head));
    INIT_LIST_HEAD(&(n->avail_head));
    INIT_LIST_HEAD(&(n->dead_head));
    INIT_LIST_HEAD(&(n->raw_head));
    INIT_LIST_HEAD(&(n->fail_head));
    INIT_LIST_HEAD(&(n->ping_head));

    n->info = &myinfo;
    n->avail_count = 0;
    n->role = 0;
    n->closing = 0;
    n->closing_time = 0;
	n->curall_connection = 0 ;
	n->min_connection = 0 ;
	n->max_connection = 0 ;

    return 0;
}

/*
 * fun: alloc mysql connection from mysql node
 * arg: mysql node
 * ret: mysql connection
 *
 */

static my_conn_t *my_conn_alloc(my_node_t *n)
{//申请一个mysql 连接结构，初始化
    int res = 0;
    my_conn_t *my;

    if( (my = genpool_alloc_page(handler)) == NULL ){//申请一个mysql连接
        log(g_log, "genpool alloc page error\n");
        return NULL;
    }

    if( (res = my_conn_init(my, n)) < 0 ){
        genpool_release_page(handler, my);
        log(g_log, "my_conn_init error\n");
        return NULL;
    }
	++ n->curall_connection ;
    return my;
}

/*
 * fun: release mysql connection
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_release(my_conn_t *my)
{
	if( my->node != NULL){
		my_node_t *node = (my_node_t*)my->node ;
		-- node->curall_connection ;
	}
    return genpool_release_page(handler, my);
}

/*
 * fun: init mysql connection pool
 * arg: max mysql connection number
 * ret: success 0, error -1
 *
 */

int my_pool_init(int count)
{//设置mysql连接的初始化结构，各种定时器等
    int i, res = 0;

    if( (mypool = malloc(sizeof(my_pool_t))) == NULL ){
        log_err(g_log, "malloc error\n");
        return -1;
    }

    if( (handler = genpool_init(sizeof(my_conn_t), count)) == NULL ){
        log(g_log, "genpool_init error\n");
        free(mypool);
        return -1;
    }

    mypool->slave_num = 0;

    res = timer_register(my_conn_dead_reconnect_timer, 9, "my_conn_dead_reconnect_timer", 1);
    if(res < 0){
        log(g_log, "my_conn_dead_reconnect_timer register error\n");
        return -1;
    }

    res = timer_register(my_conn_fail_reconnect_timer, 1, "my_conn_fail_reconnect_timer", 1);
    if(res < 0){
        log(g_log, "my_conn_fail_reconnect_timer register error\n");
        return -1;
    }

    res = timer_register(my_conn_pool_status_timer, 30, "my_conn_pool_status_timer", 60);
    if(res < 0){
        log(g_log, "my_conn_pool_status_timer register error\n");
        return -1;
    }

    res = timer_register(my_conn_pool_ping_timer, 3, "my_conn_pool_ping_timer", 1);
    if(res < 0){
        log(g_log, "my_conn_pool_ping_timer register error\n");
        return -1;
    }

    res = timer_register(my_conn_pool_ping_timeout_timer, 3, "my_conn_pool_ping_timeout_timer", 1);
    if(res < 0){
        log(g_log, "my_conn_pool_ping_timeout_timer register error\n");
        return -1;
    }

    res = timer_register(my_node_closing_cleanup_timer, 3, "my_node_closing_cleanup_timer", 60);
    if(res < 0){
        log(g_log, "my_node_closing_cleanup_timer register error\n");
        return -1;
    }

    return 0;
}

/*
 * fun: make mysql connection
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int make_my_conn(my_conn_t *my)
{
    int fd, done, res = 0;
    my_node_t *node;

    node = my->node;

    if( (res = my_conn_init(my, node)) < 0 ){
		my_conn_release(my) ;//这里必须释放结构，因为没法重连
        return res;
    }

    fd = connect_nonblock(node->host, node->srv, &done);
    if(fd >= 0){
        my->fd = fd;
        res = add_handler(fd, EPOLLIN, my_hs_stage1_cb, my);//my为这个mysql的连接。暂时只记录了fd 和所属node
        if(res < 0){
            log(g_log, "add_handler error\n");
            return my_conn_close_on_fail(my);
        }
    } else {
        log_err(g_log, "connect_nonblock %s:%s error\n", node->host, node->srv);
        return my_conn_close_on_fail(my);
    }

    return 0;
}

/*
 * fun: register mysql
 * arg: mysql node, host, srv, user, pass, connection number
 * ret: success 0, error -1
 *
 */

static int _my_reg(my_node_t *node, char *host, char *srv, char *user, char *pass, int mincount, int maxcount )
{//初始化连接数据，并建立真实mysql连接
    int i, fd, res = 0;
    my_conn_t *my;

    if( (res = my_node_init(node)) < 0 ){//初始化节点的字段
        log(g_log, "my_node_init error\n");
        return res;
    }

	node->min_connection = mincount ;
	node->max_connection = maxcount ;
    strncpy(node->host, host, MAX_HOST_LEN - 1);
    strncpy(node->srv, srv, MAX_SRV_LEN - 1);
    strncpy(node->user, user, MAX_USER_LEN - 1);
    strncpy(node->pass, pass, MAX_PASS_LEN - 1);

    for(i = 0; i < mincount; i++){//一个个建立那么多连接
        if( (my = my_conn_alloc(node)) == NULL ){//申请一个mysql 连接结构，初始化
            log(g_log, "my_conn_alloc error\n");
            return -1;
        }

        if( (res = make_my_conn(my)) < 0 ){
            log(g_log, "make my conn error, ignore, continue next.\n");
        }
    }

    return 0;
}

/*
 * fun: register slave mysql
 * arg: host, srv, user, pass, connection number
 * ret: success 0, error -1
 *
 */

int my_slave_reg(char *host, char *srv, char *user, char *pass, int mincount, int maxcount)
{
    int i, res = 0;
    my_node_t *node;

    for(i = 0; i < MAX_SLAVE_NODE; i++){
        node = &(mypool->slave[i]);//找一个空位置，用来存储IP等信息
        if(node->role == 0){
            break;
        }
    }

    if(i == MAX_SLAVE_NODE){
        log(g_log, "slave number exceed limit[%d]\n", MAX_SLAVE_NODE);
        return -1;
    }

    if(i == mypool->slave_num){
        mypool->slave_num++;
    }

    res = _my_reg(node, host, srv, user, pass, mincount, maxcount);
    if(res < 0){
        log(g_log, "_my_reg error\n");
        return res;
    }
    node->role = 1;

    log(g_log, "host: %s, srv: %s, user: %s, cnum: %d\n", host, srv, user, mincount);

    return res;
}

/*
 * fun: set mysql node closing
 * arg: mysql node
 * ret: always return 0
 *
 */

static int my_node_set_closing(my_node_t *node)
{
    node->closing = 1;
    node->closing_time = time(NULL);

    return 0;
}

/*
 * fun: check mysql node closing
 * arg: mysql node
 * ret: yes 1, no 0
 *
 */

static int my_node_is_closing(my_node_t *node)
{
    return node->closing;
}

/*
 * fun: unregister mysql
 * arg: host, srv
 * ret: success 0, error -1
 *
 */

int my_unreg(char *host, char *srv)
{
    int i;
    my_node_t *node;

    log(g_log, "%s called\n", __func__);

    for(i = 0; i < mypool->slave_num; i++){
        node = &(mypool->slave[i]);
        if((!strcmp(node->host, host)) && (!strcmp(node->srv, srv))){
            my_node_set_closing(node);

            log(g_log, "slave %s:%s unregister\n", host, srv);
        }
    }

    return 0;
}

/*
 * fun: get a slave connection
 * arg: connection, client ip, client port
 * ret: success return mysql connection, error return NULL 
 *
 */

my_conn_t *my_slave_conn_get(void *c, uint32_t ip, uint16_t port)
{//ip:port  为客户端连接IP,端口
    int i, index;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head;
    time_t now = time(NULL);

    if(mypool->slave_num == 0){
        log(g_log, "no slave register\n");
        return NULL;
    }

    for(i = 0; i < mypool->slave_num; i++){
        index = ((ip + port) + i) % (mypool->slave_num);//用ip和Port做哈希, 从第i个开始找，其实这样就分散了的，
		//一个个slave找，注意这里是先找第一个mysql,再找第二个
        node = &(mypool->slave[index]);
        head = &(node->avail_head);//从avail_head上面找有没有可用连接
        if( (!my_node_is_closing(node)) && (!list_empty(head)) ){
            break;
        }
    }

    if(i == mypool->slave_num){//没找到`````
        log(g_log, "no slave available, slave_num:%d\n", mypool->slave_num );
        return NULL;
    }

    my = list_first_entry(head, my_conn_t, link);
    my_conn_set_used(my, c);//将一个mysql连接标记为被使用了。也就是my->conn指向中间结构conn_t

    return my;
}

/*
 * fun: close mysql connection
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

int my_conn_close(my_conn_t *my)
{
    int res;

    if( (res = del_handler(my->fd)) < 0 ){
        log(g_log, "del_handler error, ignore it\n");
    } else {
        ;//debug(g_log, "del_handler success\n");
    }

    if(my->fd >= 0){
        close(my->fd);
        my->fd = -1;
    }

    my->conn = NULL;
    buf_reset(&(my->buf));

    my_conn_set_dead(my);

    return 0;
}

/*
 * fun: close mysql connection and set fail
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

int my_conn_close_on_fail(my_conn_t *my)
{
    my_conn_close(my);
    my_conn_set_fail(my);

    return 0;
}

/*
 * fun: close mysql connection and release it
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_close_and_release(my_conn_t *my)
{
    my_conn_close(my);
    list_del_init(&(my->link));
    my_conn_release(my);

    return 0;
}

/*
 * fun: put mysql connection
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

int my_conn_put(my_conn_t *my)
{
    int res = 0;
    my_node_t *node = my->node;

    if( (res = del_handler(my->fd)) < 0 ){
        log(g_log, "del_handler error, ignore it\n");
    } else {
        ;//debug(g_log, "del_handler success\n");
    }

    if(my_conn_ctx_is_dirty(my)){
        my_conn_close(my);

        return 0;
    }

    my_conn_set_avail(my);

    return 0;
}

/*
 * fun: set mysql connection used
 * arg: mysql connection, connection pointer
 * ret: success 0, error -1
 *
 */

static int my_conn_set_used(my_conn_t *my, void *ptr)
{//将一个mysql连接标记为被使用了。也就是my->conn指向中间结构conn_t，
    int res = 0;
    my_node_t *node = my->node;
    my->conn = ptr;//指向中间结构conn_t
    buf_reset(&(my->buf));

    list_move_tail(&(my->link), &(node->used_head));//将我这个连接从之前的地方移除，然后挂到used_head上面
    my->state_time = time(NULL);

    if( (res = del_handler(my->fd)) < 0 ){//清楚，重新再来
        log(g_log, "del_handler error, ignore it\n");
    } else {
        ;//debug(g_log, "del_handler success\n");
    }

    node->avail_count--;//减少可用连接数  

    return 0;
}

/*
 * fun: set mysql connection avail
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

int my_conn_set_avail(my_conn_t *my)
{//跟mysql直接的验证成功了，下面标记这个连接为可用的,放入node的avail_head上面
    int res = 0;
    my_node_t *node = my->node;

    my->conn = NULL;
    buf_reset(&(my->buf));

    list_move_tail(&(my->link), &(node->avail_head));
    my->state_time = time(NULL);

    node->avail_count++;

    return 0;
}

/*
 * fun: set mysql connection dead 
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_set_dead(my_conn_t *my)
{
    int res = 0;
    my_node_t *node = my->node;

    my->conn = NULL;
    buf_reset(&(my->buf));

    list_move_tail(&(my->link), &(node->dead_head));
    my->state_time = time(NULL);
	node->avail_count--;//减少可用连接数

    return 0;
}

/*
 * fun: set mysql connection raw 
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_set_raw(my_conn_t *my)
{
    int res = 0;
    my_node_t *node = my->node;

    my->conn = NULL;
    buf_reset(&(my->buf));

    list_move_tail(&(my->link), &(node->raw_head));
    my->state_time = time(NULL);
	node->avail_count--;//减少可用连接数

    return 0;
}

/*
 * fun: set mysql connection fail 
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_set_fail(my_conn_t *my)
{
    int res = 0;
    my_node_t *node = my->node;

    list_move_tail(&(my->link), &(node->fail_head));
    my->state_time = time(NULL);
	node->avail_count--;//减少可用连接数

    return 0;
}

/*
 * fun: set mysql connection ping
 * arg: mysql connection
 * ret: success 0, error -1
 *
 */

static int my_conn_set_ping(my_conn_t *my)
{
    int res = 0;
    my_node_t *node = my->node;

    my->conn = NULL;
    buf_reset(&(my->buf));

    list_move_tail(&(my->link), &(node->ping_head));
    my->state_time = time(NULL);
	node->avail_count--;//减少可用连接数

    return 0;
}

/*
 * fun: dead reconnect timer
 * arg: max connection to be processed
 * ret: success 0, error -1
 *
 */

static int my_conn_dead_reconnect_timer(unsigned long arg)
{//扫描每一个机器的mysql连接dead_head链表，将里面死掉的连接恢复
    int i, res = 0;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head, *pos, *n;

    for(i = 0; i < mypool->slave_num; i++){
        node = &(mypool->slave[i]);
        if(my_node_is_closing(node)){
            continue;
        }
        head = &(node->dead_head);
        list_for_each_safe(pos, n, head){
            my = list_entry(pos, my_conn_t, link);
            list_del_init(pos);
            if( (res = make_my_conn(my)) < 0 ){
                log(g_log, "make_my_conn error\n");
            }
        }
    }

    return 0;
}

/*
 * fun: fail reconnect timer
 * arg: max connection to be processed
 * ret: success 0, error -1
 *
 */

static int my_conn_fail_reconnect_timer(unsigned long arg)
{
    int i, res = 0, count;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head, *pos, *n;

    for(i = 0; i < mypool->slave_num; i++){
        count = 0;
        node = &(mypool->slave[i]);
        if(my_node_is_closing(node)){
            continue;
        }
        head = &(node->fail_head);
        list_for_each_safe(pos, n, head){
            if(count++ >= arg){
                break;
            }
            my = list_entry(pos, my_conn_t, link);
            list_del_init(pos);
            if( (res = make_my_conn(my)) < 0 ){
                log(g_log, "make_my_conn error\n");
            }
        }
    }

    return 0;
}

/*
 * fun: mysql connection pool status timer
 * arg: void
 * ret: success 0, error -1
 *
 */

static int my_conn_pool_status_timer(unsigned long arg)
{
    int i, count1, count2, count3, count4, count5, count6;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head, *pos, *n;
    conn_t *c;

    for(i = 0; i < mypool->slave_num; i++){
        count1 = count2 = count3 = count4 = count5 = count6 = 0;
        node = &(mypool->slave[i]);
        if(my_node_is_closing(node)){
            continue;
        }

        head = &(node->used_head);
        list_for_each_safe(pos, n, head){
            my = list_entry(pos, my_conn_t, link);
            c = my->conn;
            if(c != NULL){
                debug(g_log, "connid: %d\n", c->connid);
            } else {
                debug(g_log, "no connect attach\n");
            }
            count1++;
        }

        head = &(node->avail_head);
        list_for_each_safe(pos, n, head){
            count2++;
        }

        head = &(node->dead_head);
        list_for_each_safe(pos, n, head){
            count3++;
        }

        head = &(node->raw_head);
        list_for_each_safe(pos, n, head){
            count4++;
        }

        head = &(node->fail_head);
        list_for_each_safe(pos, n, head){
            count5++;
        }

        head = &(node->ping_head);
        list_for_each_safe(pos, n, head){
            count6++;
        }

        log(g_log, \
            "slave %s:%s used,%d free,%d dead,%d raw,%d fail,%d ping,%d\n", \
                   node->host, node->srv, count1, count2, count3, count4, count5, count6);
    }

    return 0;
}

/*
 * fun: ping timer
 * arg: max connection to be processed
 * ret: success 0, error -1
 *
 */

static int my_conn_pool_ping_timer(unsigned long arg)
{
    int i, res = 0, count;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head, *pos, *n;

    for(i = 0; i < mypool->slave_num; i++){
        count = 0;
        node = &(mypool->slave[i]);
        if(my_node_is_closing(node)){
            continue;
        }
        head = &(node->avail_head);
        list_for_each_safe(pos, n, head){
            if(count++ >= arg){
                break;
            }
            my = list_entry(pos, my_conn_t, link);
            if( (res = my_conn_set_ping(my)) < 0 ){
                log(g_log, "my_conn_set_ping error\n");
            }

            my_ping_prepare(my);
        }
    }

    return 0;
}

/*
 * fun: ping timeout timer
 * arg: max connection to be processed
 * ret: success 0, error -1
 *
 */

static int my_conn_pool_ping_timeout_timer(unsigned long arg)
{
    int i, res = 0, count;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head, *pos, *n;
    time_t now = time(NULL);

    for(i = 0; i < mypool->slave_num; i++){
        count = 0;
        node = &(mypool->slave[i]);
        if(my_node_is_closing(node)){
            continue;
        }
        head = &(node->ping_head);
        list_for_each_safe(pos, n, head){
            if(count++ >= arg){
                break;
            }
            my = list_entry(pos, my_conn_t, link);

            if(now - my->state_time > g_conf.mysql_ping_timeout){
                my_conn_close_on_fail(my);
            } else {
                break;
            }
        }
    }

    return 0;
}

/*
 * fun: cleanup closing node
 * arg: mysql node
 * ret: success 0, error -1
 *
 */

static int my_node_closing_cleanup(my_node_t *node)
{
    struct list_head *head, *pos, *n;
    my_conn_t *my;
    conn_t *c;

    head = &(node->used_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        c  = my->conn;
        log(g_log, "conn:%d cleanup\n", c->connid);
        conn_close(c);
        my_conn_close_and_release(my);
    }

    head = &(node->avail_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        my_conn_close_and_release(my);
    }

    head = &(node->dead_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        my_conn_close_and_release(my);
    }

    head = &(node->raw_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        my_conn_close_and_release(my);
    }

    head = &(node->fail_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        my_conn_close_and_release(my);
    }

    head = &(node->ping_head);
    list_for_each_safe(pos, n, head){
        my = list_entry(pos, my_conn_t, link);
        my_conn_close_and_release(my);
    }

    node->role = 0;

    return 0;
}

/*
 * fun: cleanup closing node timer
 * arg: max connection to be processed
 * ret: success 0, error -1
 *
 */

static int my_node_closing_cleanup_timer(unsigned long arg)
{
    int i, num;
    my_node_t *node;
    time_t now = time(NULL);

    num = mypool->slave_num;
    for(i = 0; i < mypool->slave_num; i++){
        node = &(mypool->slave[i]);
        if( (my_node_is_closing(node)) && \
            (node->role != 0) && \
            (now - node->closing_time > MY_NODE_CLOSING_DELAY) ){
            my_node_closing_cleanup(node);
            log(g_log, "slave %s:%s connection cleanup\n", \
                                                node->host, node->srv);
        }
    }

    return 0;
}

/*
 * fun: set mysql connection context dirty
 * arg: mysql connection
 * ret: always return 0
 *
 */

int my_conn_ctx_set_dirty(my_conn_t *my)
{
    my_ctx_t *ctx = &(my->ctx);

    ctx->dirty = 1;

    return 0;
}

/*
 * fun: check mysql connection if dirty
 * arg: mysql connection
 * ret: yes return 1, no return 0
 *
 */

int my_conn_ctx_is_dirty(my_conn_t *my)
{
    my_ctx_t *ctx = &(my->ctx);

    return ctx->dirty;
}

/*
 * fun: check mysql connection pool if have avail connection
 * arg:
 * ret: yes return 1, no return 0
 *
 */

int my_pool_have_conn(void)
{//这里其实是说服务器有没有跟mysql直接的可用连接 
    int i;
    my_node_t *node;

    for(i = 0; i < mypool->slave_num; i++){
        node = &(mypool->slave[i]);
        if(node->avail_count > 0){
            return 1;
        }
    }

    return 0;
}

int my_try_increase_connection( )
{//ip:port  为客户端连接IP,端口
    int i, index;
    my_node_t *node;
    my_conn_t *my;
    struct list_head *head;
    time_t now = time(NULL);
	int res ;

    if(mypool->slave_num == 0){
        log(g_log, "no slave register\n");
        return -1;
    }

    for(i = 0; i < mypool->slave_num; i++){
        index = (now + i) % (mypool->slave_num);
		//一个个slave找，注意这里是先找第一个mysql,再找第二个
        node = &(mypool->slave[index]);
        if( (!my_node_is_closing(node)) && node->curall_connection < node->max_connection  ){
			if( (my = my_conn_alloc(node)) == NULL ){//申请一个mysql 连接结构，初始化
				log(g_log, "my_conn_alloc error\n");
				continue ;
			}

			if( (res = make_my_conn(my)) < 0 ){
				log(g_log, "make my conn error in my_try_increase_connection, ignore, continue next.\n");
				continue ;
			}
			break ;//新连接启动了，连接成功后会挂到avaliable连接上面
        }
    }

    if(i == mypool->slave_num){//没找到`````
        log(g_log, "my_try_increase_connection failed. no slave available, slave_num:%d\n", mypool->slave_num );
        return -2;
    }


    return 0;
}
