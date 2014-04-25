#ifndef _MY_OPS_H_
#define _MY_OPS_H_

#include "my_buf.h"
#include "conn_pool.h"

int my_hs_stage1_cb(int fd, void *arg);
int my_hs_stage2_cb(int fd, void *arg);
int my_hs_stage3_cb(int fd, void *arg);

int cli_hs_stage1_prepare(conn_t *c);
int cli_hs_stage1_cb(int fd, void *arg);
int cli_hs_stage2_cb(int fd, void *arg);
int cli_hs_stage3_cb(int fd, void *arg);

int cli_query_cb(int fd, void *arg);
int my_query_cb(int fd, void *arg);
int my_answer_cb(int fd, void *arg);
int cli_answer_cb(int fd, void *arg);

int my_ping_prepare(my_conn_t *my);

#endif
