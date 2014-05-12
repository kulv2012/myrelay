#ifndef _MY_POOL_H_
#define _MY_POOL_H_

#include <time.h>
#include <list.h>
#include <stdint.h>
#include "my_buf.h"
#include "def.h"


typedef struct{
    uint8_t dirty;
    char curdb[64];
} my_ctx_t;

typedef struct{
    int fd;//mysql连接对应的tcp socket fd
    void *node;//这个mysql连接所属的机器节点是哪个
    struct list_head link;
    void *conn;
    buf_t buf;
    my_ctx_t ctx;
    time_t state_time;
} my_conn_t;

typedef struct{
    int avail;
    uint8_t protocol;
    uint8_t lang;
    uint16_t status;
    uint32_t cap;
    char ver[64];
    time_t update_time;
} my_info_t;

typedef struct{
    char host[MAX_HOST_LEN];
    char srv[MAX_SRV_LEN];//这是端口····这名字 
    char user[MAX_USER_LEN];
    char pass[MAX_PASS_LEN];
    struct list_head used_head;//已经分配给某个客户端的mysql连接list
    struct list_head avail_head;//成功建立连接，并且空闲可用的mysql连接
    struct list_head dead_head;
    struct list_head raw_head;
    struct list_head fail_head;
    struct list_head ping_head;
    my_info_t *info;
    unsigned int avail_count;//这个机器的可用mysql连接数目
    int closing;
	int role ;
    time_t closing_time;

	int curall_connection ;//当前的连接数，包括活的，死的
	int min_connection ;//初始申请的连接数目
	int max_connection ;//最大连接数
} my_node_t;

typedef struct{
    //my_node_t master[MAX_MASTER_NODE];
    my_node_t slave[MAX_SLAVE_NODE];
    int slave_num;
    //int master_num;
} my_pool_t;

int my_pool_init(int count);
int my_pool_have_conn(void);

int my_slave_reg(char *host, char *srv, char *user, char *pass, int mincount, int maxcount);

int my_unreg(char *host, char *srv);

my_conn_t *my_slave_conn_get(void *c, uint32_t ip, uint16_t port);

int my_conn_put(my_conn_t *my);
int my_conn_close(my_conn_t *my);
int my_conn_close_on_fail(my_conn_t *my);

int my_conn_set_avail(my_conn_t *my);

int my_conn_ctx_set_dirty(my_conn_t *my);
int my_conn_ctx_is_dirty(my_conn_t *my);

int my_info_set(uint8_t prot, uint8_t lang, uint16_t status, uint32_t cap, char *ver, int ver_len);

int my_try_increase_connection( ) ;

#endif
