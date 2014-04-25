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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "my_buf.h"
#include "mysql_com.h"
#include "my_protocol.h"

static inline uint8_t G1(char **ptr)
{
    uint8_t u;
    u = *((uint8_t *)(*ptr));
    *ptr += 1;

    return u;
}

static inline uint16_t G2(char **ptr)
{
    uint16_t u;
    u = *((uint16_t *)(*ptr));
    *ptr += 2;

    return u;
}

static inline uint32_t G3(char **ptr)
{
   char buff[4];

   memcpy(buff, *ptr, 3);
   buff[3] = 0;
    *ptr += 3;

   return *((uint32_t *)buff);
}

static inline uint32_t G4(char **ptr)
{
    uint32_t u;
    u = *((uint32_t *)(*ptr));
    *ptr += 4;

    return u;
}

static inline void S1(char **ptr, uint8_t u)
{
    memcpy(*ptr, &u, 1);
    *ptr += 1;

    return;
}

static inline void S2(char **ptr, uint16_t u)
{
    memcpy(*ptr, &u, 2);
    *ptr += 2;

    return;
}

static inline void S3(char **ptr, uint32_t u)
{
    memcpy(*ptr, &u, 3);
    *ptr += 3;

    return;
}

static inline void S4(char **ptr, uint32_t u)
{
    memcpy(*ptr, &u, 4);
    *ptr += 4;

    return;
}

/*
 * fun: make init packet
 * arg: buffer, init packet elements struct
 * ret: success 0, error -1
 *
 */

int make_init(buf_t *buf, my_auth_init_t *init)
{
    char *ptr;
    int total = 0, len;
    uint16_t t16;

    buf_reset(buf);

    ptr = buf->ptr + 4;

    S1(&ptr, init->prot_ver);
    total += 1;

    len = strlen(init->srv_ver);
    memcpy(ptr, init->srv_ver, len);
    ptr[len] = '\0';
    ptr += (len + 1);
    total += (len + 1);

    S4(&ptr, init->tid);
    total += 4;

    memcpy(ptr, init->scram, 8);
    ptr += 8;
    total += 8;

    *ptr = 0;
    ptr += 1;
    total += 1;

    t16 = (init->cap) & 0xffff;
    S2(&ptr, t16);
    total += 2;

    S1(&ptr, init->lang);
    total += 1;

    S2(&ptr, init->status);
    total += 2;

    t16 = (init->cap >> 16) & 0xffff;
    S2(&ptr, t16);
    total += 2;

    S1(&ptr, init->scram_len);
    total += 1;

    bzero(ptr, 10);
    ptr += 10;
    total += 10;

    memcpy(ptr, init->plug, 12);
    ptr += 12;
    total += 12;

    *ptr = 0;
    ptr += 1;
    total += 1;

    ptr = buf->ptr;
    S3(&ptr, total);
    S1(&ptr, init->pktno);

    buf->used = total + 4;
    buf_rewind(buf);

    return total;
}

/*
 * fun: parse init packet
 * arg: buffer, init packet elements struct
 * ret: success 0, error -1
 *
 */

int parse_init(buf_t *buf, my_auth_init_t *init)
{
    char *ptr;
    int len;
    uint16_t tmp1, tmp2;

    buf_rewind(buf);
    ptr = buf->ptr;

    init->pktlen = G3(&ptr);
    init->pktno = G1(&ptr);
    init->prot_ver = G1(&ptr);

    len = strlen(ptr);
    memcpy(init->srv_ver, ptr, len);
    init->srv_ver[len] = '\0';
    ptr += (len + 1);

    init->tid = G4(&ptr);

    memcpy(init->scram, ptr, 8);
    ptr += 8;

    ptr += 1;

    tmp1 = G2(&ptr);
    //init->lang = G1(&ptr);
	init->lang = 33 ;//mysql 客户端默认是33，服务器返回默认是8，我们按照mysql客户端的来
    init->status = G2(&ptr);
    tmp2 = G2(&ptr);
    init->cap = (tmp2 << 16) + tmp1;

    init->scram_len = G1(&ptr);

    ptr += 10;

    len = strlen(ptr);
    memcpy(init->plug, ptr, len);
    init->plug[len] = '\0';
    ptr += (len + 1);

    buf_rewind(buf);

    return 0;
}

/*
 * fun: make login packet
 * arg: buffer, login packet elements struct
 * ret: success 0, error -1
 *
 */

int make_login(buf_t *buf, cli_auth_login_t *login)
{
    char *ptr;
    int total = 0, len;

    buf_reset(buf);

    ptr = buf->ptr + 4;

    S4(&ptr, login->client_flags);
    total += 4;

    S4(&ptr, login->max_pkt_size);
    total += 4;

    S1(&ptr, login->charset);
    total += 1;

    bzero(ptr, 23);
    ptr += 23;
    total += 23;

    len = strlen(login->user);
    memcpy(ptr, login->user, len);
    ptr[len] = '\0';
    ptr += (len + 1);
    total += (len + 1);

    len = login->scram[0];
    memcpy(ptr, login->scram, len + 1);
    ptr += (len + 1);
    total += (len + 1);

    len = strlen(login->db);
    if(len > 0){
        memcpy(ptr, login->db, len);
        ptr[len] = '\0';
        ptr += (len + 1);
        total += (len + 1);
    }

    ptr = buf->ptr;
    S3(&ptr, total);
    S1(&ptr, login->pktno);

    buf_rewind(buf);
    buf->used = total + 4;

    return total;
}

/*
 * fun: parse login packet
 * arg: buffer, login packet elements struct
 * ret: success 0, error -1
 *
 */

int parse_login(buf_t *buf, cli_auth_login_t *login)
{
    char *ptr;
    int len;

    buf_rewind(buf);
    ptr = buf->ptr;

    login->pktlen = G3(&ptr);
    login->pktno = G1(&ptr);
    login->client_flags = G4(&ptr);
    login->max_pkt_size = G4(&ptr);
    login->charset = G1(&ptr);

    ptr += 23;

    len = strlen(ptr);
    memcpy(login->user, ptr, len);
    login->user[len] = '\0';
    ptr += (len + 1);

    len = ptr[0];
    if(len == 0){
        login->scram[0] = '\0';
    } else {
        memcpy(login->scram, ptr + 1, len);
    }
    ptr += (len + 1);
    login->scram[len] = '\0';

    len = strlen(ptr);
    memcpy(login->db, ptr, len);
    login->db[len] = '\0';
    ptr += (len + 1);

    buf_rewind(buf);
    return 0;
}

/*
 * fun: parse auth result packet
 * arg: buffer, auth result packet elements struct
 * ret: success 0, error -1
 *
 */

int parse_auth_result(buf_t *buf, my_auth_result_t *result)
{
    char *ptr;
    int len;

    buf_rewind(buf);
    ptr = buf->ptr;

    result->pktlen = G3(&ptr);
    result->pktno = G1(&ptr);
    result->result = G1(&ptr);

    if(result->result){
        result->err = G2(&ptr);

        ptr += 6;

        len = strlen(ptr);
        memcpy(result->errmsg, ptr, len);
        result->errmsg[len] = '\0';
        ptr += (len + 1);
    } else {
        result->err = 0;
        result->errmsg[0] = '\0';
    }

    buf_rewind(buf);
    return 0;
}

/*
 * fun: make auth result packet
 * arg: buffer, auth result packet elements struct
 * ret: success 0, error -1
 *
 */

int make_auth_result(buf_t *buf, my_auth_result_t *result)
{
    char *ptr;
    int total = 0, len;
    uint16_t tmp;

    buf_reset(buf);

    ptr = buf->ptr + 4;

    S1(&ptr, 0);
    total += 1;

    S1(&ptr, 0);
    total += 1;

    S1(&ptr, 0);
    total += 1;

    tmp = 0;
    S2(&ptr, tmp);
    total += 2;

    S2(&ptr, tmp);
    total += 2;

    ptr = buf->ptr;
    S3(&ptr, total);
    S1(&ptr, result->pktno);

    buf_rewind(buf);
    buf->used = total + 4;

    return total;
}

/*
 * fun: parse command packet
 * arg: buffer, command packet elements struct
 * ret: success 0, error -1
 *
 */

int parse_com(buf_t *buf, cli_com_t *com)
{
    char *ptr;
    int len;

    buf_rewind(buf);
    ptr = buf->ptr;

    com->pktlen = G3(&ptr);
    com->pktno = G1(&ptr);
    com->comno = G1(&ptr);

    len = com->pktlen - 1;
    len = len > (sizeof(com->arg) - 1) ? (sizeof(com->arg) - 1): len;
    memcpy(com->arg, ptr, len);
    com->arg[len] = '\0';
    ptr += len;

    com->len = len;

    buf_rewind(buf);
    return 0;
}

/*
 * fun: make command packet
 * arg: buffer, command packet elements struct
 * ret: success 0, error -1
 *
 */

int make_com(buf_t *buf, cli_com_t *com)
{
    char *ptr;
    int total = 0, len;

    buf_reset(buf);

    ptr = buf->ptr + 4;

    S1(&ptr, com->comno);
    total += 1;

    if(com->len > 0){
        memcpy(ptr, com->arg, com->len);
        ptr += com->len;
        total += com->len;
    }

    ptr = buf->ptr;
    S3(&ptr, total);
    S1(&ptr, com->pktno);

    buf_rewind(buf);
    buf->used = total + 4;

    return total;
}

/*
 * fun: make error result packet
 * arg: buffer, error result packet elements struct
 * ret: success 0, error -1
 *
 */

int make_result_error(buf_t *buf, my_result_error_t *result)
{
    char *ptr;
    int total = 0, len;

    buf_reset(buf);

    ptr = buf->ptr + 4;

    S1(&ptr, result->field_count);
    total += 1;

    S2(&ptr, result->err);
    total += 2;

    S1(&ptr, result->marker);
    total += 1;

    memcpy(ptr, result->sqlstate, 5);
    ptr += 5;
    total += 5;

    len = strlen(result->msg);
    memcpy(ptr, result->msg, len);
    ptr[len] = '\0';
    ptr += (len + 1);
    total += (len + 1);

    ptr = buf->ptr;
    S3(&ptr, total);
    S1(&ptr, result->pktno);

    buf_rewind(buf);
    buf->used = total + 4;

    return total;
}
