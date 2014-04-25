#ifndef _MY_PROTOCOL_H_
#define _MY_PROTOCOL_H_

#include "my_buf.h"
#include <stdint.h>

typedef struct{
    uint32_t pktlen;
    uint8_t pktno;
    uint8_t prot_ver;
    char srv_ver[64];
    uint32_t tid;
    char scram[64];
    uint32_t cap;
    uint8_t lang;
    uint16_t status;
    uint8_t scram_len;
    char plug[64];
}my_auth_init_t;

typedef struct{
    uint32_t pktlen;
    uint8_t pktno;
    uint32_t client_flags;
    uint32_t max_pkt_size;
    uint8_t charset;
    char user[128];
    char scram[128];
    char db[128];
}cli_auth_login_t;

typedef struct{
    uint32_t pktlen;
    uint8_t pktno;
    uint8_t result;
    uint16_t err;
    char errmsg[128];
}my_auth_result_t;

typedef struct{
    uint32_t pktlen;
    uint8_t pktno;
    uint8_t comno;
    char arg[4096];
    uint32_t len;
}cli_com_t;

typedef struct{
    uint32_t pktlen;
    uint8_t pktno;
    uint8_t field_count;
    uint16_t err;
    uint8_t marker;
    char sqlstate[5];
    char msg[512];
}my_result_error_t;

int make_init(buf_t *buf, my_auth_init_t *init);
int make_login(buf_t *buf, cli_auth_login_t *login);
int make_auth_result(buf_t *buf, my_auth_result_t *result);
int make_com(buf_t *buf, cli_com_t *com);

int make_result_error(buf_t *buf, my_result_error_t *result);

int parse_init(buf_t *buf, my_auth_init_t *init);
int parse_login(buf_t *buf, cli_auth_login_t *login);
int parse_auth_result(buf_t *buf, my_auth_result_t *result);
int parse_com(buf_t *buf, cli_com_t *com);

#endif
