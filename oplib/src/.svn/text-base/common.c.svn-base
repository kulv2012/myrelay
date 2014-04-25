/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include "common.h"

/*
 * fun: translate xxx.xxx.xxx.xxx to uint32_t
 * arg: uint32_t ip, xxx.xxx.xxx.xxx
 * ret: success=0 error=-1
 */
int ipstr2int(uint32_t *ip, const char *ipstr)
{
    int ret;
    struct in_addr addr;

    ret = inet_pton(AF_INET, ipstr, (void *)&addr);
    if(ret <= 0){
        return -1;
    }

    *ip = ntohl(addr.s_addr);

    return 0;
}

/*
 * fun: translate uint32_t to uint32_t
 * arg: xxx.xxx.xxx.xxx, uint32_t ip
 * ret: success=0 error=-1
 */
int ipint2str(char *ipstr, size_t n, uint32_t ip)
{
    struct in_addr addr;

    addr.s_addr = htonl(ip);
    if(inet_ntop(AF_INET, (void *)&addr, ipstr, n) == NULL){
        return -1;
    }

    return 0;
}

/*
 * fun: trim buffer
 * arg: character buffer
 * ret: number of character be trim
 *
 */

int trim(char *buf)
{
    int i, len;
    int s_pos = 0, e_pos = 0, s_flag = 0, e_flag = 0;
    char *s, *e;

    len = strlen(buf);
    if(len == 0)
        return 0;

    s = buf;
    e = buf + len - 1;

    for(i = 0; i < len; i++){
        if(s_flag == 0){
            if(!isspace(*s)){
                s_flag = 1;
                if(e_flag == 1)
                    break;
            } else {
                s_pos++;
                s++;
            }
        }

        if(e_flag == 0){
            if(!isspace(*e)){
                e_flag = 1;
                if(s_flag == 1)
                    break;
            } else {
                e_pos++;
                e--;
            }
        }

        if(s_pos + e_pos >= len){
            break;
        }

    }

    if(s_pos + e_pos >= len){
        *buf = '\0';
        return len;
    }

    if(s_pos){
        memmove(buf, buf + s_pos, len - s_pos - e_pos);
    }
    buf[len - s_pos - e_pos] = '\0';

    return (s_pos + e_pos);
}
