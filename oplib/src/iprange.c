/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "iprange.h"
#include "common.h"
#include "log.h"

extern log_t *g_log;

static int iprange_parse(iprange_t *handler, const char *filename);
static int iprange_sort(iprange_t *handler);
static int mycmp(const void *arg1, const void *arg2);
static int ipaddr_bsearch(ipblock *base, size_t nelem, uint32_t addr);
static int iprange_unique(iprange_t *handler);

/*
 * fun: iprange init
 * arg: ip range file: filename, max line: max
 * ret: error=NULL, success=iprange handler
 *
 */

iprange_t *iprange_init(const char *filename, int max)
{
    int ret;
    ipblock *ptr;
    iprange_t *handler;

    ptr = malloc(sizeof(ipblock) * max);
    if(ptr != NULL){
        handler = malloc(sizeof(iprange_t));
        if(handler != NULL){
            handler->max = max;
            handler->num = 0;
            handler->array = ptr;
        } else {
            free(ptr);
            return NULL;
        }
    } else {
        return NULL;
    }

    ret = iprange_parse(handler, filename);
    if(ret < 0){
        log(g_log, "iprange[%s] parse error\n", filename);
        free(handler);
        free(ptr);
        return NULL;
    }

    ret = iprange_sort(handler);
    if(ret < 0){
        log(g_log, "iprange sort error\n");
        free(handler);
        free(ptr);
        return NULL;
    }

    ret = iprange_unique(handler);
    if(ret < 0){
        log(g_log, "iprange unique error\n");
        free(handler);
        free(ptr);
        return NULL;
    }

    return handler;
}

/*
 * fun: parse ip file and fill iprange handler
 * arg: iprange handler, ip file
 * ret: error=-1, success=0
 *
 */

static int iprange_parse(iprange_t *handler, const char *filename)
{
    int ret, i;
    FILE *fp;
    char buf[1024], s[128], e[128];
    uint32_t ipaddr_s, ipaddr_e;
    ipblock *ptr;

    if( (fp = fopen(filename, "r")) == NULL ){
        log_strerr(g_log, "fopen[%s] error[%s]\n", filename);
        return -1;
    }

    while(fgets(buf, sizeof(buf), fp)){
        if(*buf == '#'){
            continue;
        }
        ret = sscanf(buf, "%s%s", s, e);
        if(ret == 0){
            continue;
        } else if(ret == 1) {
            strcpy(e, s);
        }

        if( (ret = ipstr2int(&ipaddr_s, s)) < 0 ){
            continue; 
        }

        if( (ret = ipstr2int(&ipaddr_e, e)) < 0 ){
            continue; 
        }

        if(handler->num < handler->max){
            ptr = &(handler->array[handler->num]);
            ptr->ipaddr_s = ipaddr_s;
            ptr->ipaddr_e = ipaddr_e;
            handler->num = handler->num + 1;
        } else {
            log(g_log, "ipblock too much, maxblock[%d]\n", handler->max);
            fclose(fp);
            return -1;
        }
    }

    if(ferror(fp)){
        log_strerr(g_log, "fscanf[%s] error[%s]\n", filename);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

/*
 * fun: unique iprange
 * arg: iprange handler
 * ret: always return 0
 *
 */

static int iprange_unique(iprange_t *handler)
{
    int i, j, num;
    ipblock *ptr, tmp;

    ptr = handler->array;
    num = handler->num;

    for(i = 0; i < num; i++){
        for(j = i + 1; j < num; j++){
            if(ptr[i].ipaddr_e >= ptr[j].ipaddr_e){
                ptr[j].ipaddr_e = 0;
            }
        }
    }

    for(i = 0; i < num; i++){
        if(ptr[i].ipaddr_s > ptr[i].ipaddr_e){
            for(j = i + 1; j < num; j++){
                if(ptr[j].ipaddr_s <= ptr[j].ipaddr_e){
                    tmp = ptr[i];
                    ptr[i] = ptr[j];
                    ptr[j] = tmp;
                    break;
                }
            }
            if(j == num){
                break;
            }
        }
    }

    handler->num = i;

    return 0;
}

/*
 * fun: sort iprange
 * arg: iprange handler
 * ret: always return 0
 *
 */

static int iprange_sort(iprange_t *handler)
{
    ipblock *base;
    int  nelem, width;

    base = handler->array;
    nelem = handler->num;
    width = sizeof(ipblock);
    qsort(base, nelem, width, mycmp);

    return 0;
}

/*
 * fun: comprare two ipblock
 * arg: ipblock1 & ipblock2
 * ret: equal=0, greater=1, less=-1
 *
 */

static int mycmp(const void *arg1, const void *arg2)
{
    ipblock *p1, *p2;

    p1 = (ipblock *)arg1;
    p2 = (ipblock *)arg2;

    if(p1->ipaddr_s > p2->ipaddr_s){
        return 1;
    } else if(p1->ipaddr_s == p2->ipaddr_s) {
        return 0;
    } else {
        return -1;
    }
}

/*
 * fun: check ip addr is in range or not
 * arg: iprange handler, ip addr
 * ret: in=1, not_in=0
 *
 */

int ipaddr_in_range(iprange_t *handler, uint32_t addr)
{
    ipblock *base;
    size_t nelem;

    base = handler->array;
    nelem = handler->num;

    return ipaddr_bsearch(base, nelem, addr);
}

/*
 * fun: bsearch ip addr in iprange
 * arg: first ipblock: base, number of ipblock: nelem, ip addr: addr
 * ret: founded=1, not_founded=0
 *
 */

static int ipaddr_bsearch(ipblock *base, size_t nelem, uint32_t addr)
{
    size_t m;
    uint32_t ipaddr_s, ipaddr_e;
    ipblock *ptr;

    if(nelem == 0){
        return 0;
    } else if(nelem == 1) {
        ipaddr_s = base->ipaddr_s;
        ipaddr_e = base->ipaddr_e;
        return ((ipaddr_s <= addr) && (ipaddr_e >= addr));
    } else {
        m = nelem / 2;
        ptr = base + m;
        ipaddr_s = ptr->ipaddr_s;
        ipaddr_e = ptr->ipaddr_e;

        if(ipaddr_s > addr){
            return ipaddr_bsearch(base, m, addr);
        } else if(ipaddr_s == addr){
            return 1;
        } else {
            if(ipaddr_e >= addr){
                return 1;
            } else {
                return ipaddr_bsearch(ptr, nelem - m, addr);
            }
        }
    }

    return 0;
}

/*
 * fun: dump iprange config
 * arg: iprange handler
 * ret: always return 0
 *
 */

int iprange_dump(iprange_t *handler)
{
    int i, num = handler->num;
    ipblock *ptr;
    char ipstr1[32], ipstr2[32];

    for(i = 0; i < num; i++){
        ptr = &(handler->array[i]);
        ipint2str(ipstr1, sizeof(ipstr1), ptr->ipaddr_s);
        ipint2str(ipstr2, sizeof(ipstr2), ptr->ipaddr_e);
        log(g_log, "line[%d] iprange[%s,%s]\n", i, ipstr1, ipstr2);
    }

    return 0;
}

/*
 * fun: release iprange
 * arg: iprange handler
 * ret: always return 0
 *
 */

int iprange_release(iprange_t *handler)
{
    ipblock *array;

    if(handler){
        array = handler->array;
        if(array){
            free(array);
        }
        free(handler);
    }

    return 0;
}

/*
 * fun: reload iprange(when catch SIGUSR1)
 * arg: iprange handler, ip file, max line of ip file
 * ret: success=iprange_t, error=NULL
 *
 */

iprange_t *iprange_reload(iprange_t *handler, const char *filename, int max)
{
    iprange_t *n;

    n = iprange_init(filename, max);
    if(n != NULL){
        log(g_log, "iprange[%s] reload success\n", filename);
        iprange_release(handler);
        return n;
    } else {
        log(g_log, "iprange[%s] reload error\n", filename);
        return handler;
    }
}
