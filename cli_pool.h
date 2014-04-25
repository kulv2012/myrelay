#ifndef _CLI_POOL_H_
#define _CLI_POOL_H_

#include <time.h>
#include <list.h>
#include <stdint.h>
#include "my_buf.h"
#include "conn_pool.h"
#include "mysql_com.h"

typedef struct{
    int fd;
    uint32_t ip;
    uint16_t port;
    struct list_head link;
    conn_t *conn;
    buf_t buf;
    char scram[SCRAMBLE_LENGTH + 1];
} cli_conn_t;

int cli_pool_init(int count);
int cli_conn_open(conn_t *conn, int fd, uint32_t ip, uint16_t port);
int cli_conn_close(cli_conn_t *conn);

#endif
