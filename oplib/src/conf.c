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
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include "dict.h"
#include "conf.h"
#include "log.h"
#include "common.h"

#define MAX_LINE_LEN 1024
#define MAX_HASH_BUCKET 67
#define MAX_KEY_LEN 512
#define MAX_VALUE_LEN 1024

static int inited;
static dict_t *dict;

static char *conf_get(const char *var);
extern log_t *g_log;

/*
 * fun: init and parse conf file
 * arg: conf file
 * ret: success=0, error=-1
 *
 */

int conf_init(const char *conf)
{
    int ret, line = 0;
    FILE *fp;
    char buf[MAX_LINE_LEN];
    char key[MAX_KEY_LEN], value[MAX_VALUE_LEN];
    char *k, *v, *ptr;

    if(conf == NULL){
        return -1;
    }

    if(inited == 1){
        log(g_log, "conf[%s] inited duplicate\n", conf);
        return -1;
    }

    dict = dict_init(MAX_HASH_BUCKET);
    if(dict == NULL){
        log(g_log, "dict_init error\n");
        return -1;
    }

    if( (fp = fopen(conf, "r")) == NULL){
        log_strerr(g_log, "fopen[%s] error\n", conf);
        return -1;
    }

    key[MAX_KEY_LEN - 1] = '\0';
    value[MAX_VALUE_LEN - 1] = '\0';

    fgets(buf, MAX_LINE_LEN, fp);
    while(!feof(fp)){
        line++;
        trim(buf);
        if( (*buf != '#') && (*buf != '\0') ){
            ret = sscanf(buf, "%511s %1023s", key, value);
            if(ret != 2){
                log(g_log, "line %d, parse error, \"%s\"\n", line, buf);
                return -1;
            } else {
                debug(g_log, "%s: %s\n", key, value);
                k = strdup(key);
                v = strdup(value);
                ptr = dict_insert(dict, k, v);
                if(ptr == NULL){
                    log(g_log, "line %d, dict_insert %s error\n", line, buf);
                    return -1;
                } else if(ptr != v) {
                    log(g_log, "line %d, dict_insert error, %s duplicate\n", line, buf);
                    return -1;
                }
            }
        }
        fgets(buf, sizeof(buf), fp);
    }

    fclose(fp);
    inited = 1;

    return 0;
}

/*
 * fun: query value of var
 * arg: var string
 * ret: success!=NULL, error=NULL
 *
 */

static char *conf_get(const char *var)
{
    struct list_head *head, *pos;
    uint64_t key;
    char *value = NULL;

    if(inited == 0){
        log(g_log, "conf not inited\n");
        return NULL;
    }

    return dict_search(dict, (void *)var);
}

/*
 * fun: get int variable to string
 * arg: string & default variable
 * ret: int variable
 *
 */

int get_conf_int(const char *str, int def)
{
    long int var = 0;
    char *endptr, *ptr;

    if(str == NULL){
        log(g_log, "argument null, return default[%d]\n", str, def);
        return def;
    }

    ptr = conf_get(str);
    if(ptr == NULL){
        log(g_log, "argument[%s] not exist, return default[%d]\n", str, def);
        return def;
    }

    if(!strncmp(ptr, "", 1)){
        log(g_log, "argument[%s] empty, return default[%d]\n", str, def);
        return def;
    }

    errno = 0;
    var = strtol(ptr, &endptr, 10);

    if( (errno == ERANGE && (var == LONG_MAX || var == LONG_MIN)) 
                                            || (errno != 0 && var == 0) ) {
        log(g_log, "strtol[%s] error, return default[%d]\n", ptr, def);
        return def;
    }

    if(endptr == ptr){
        log(g_log, "No digits were found, return default[%d]\n", def);
        return def;
    }

    return var;
}

/*
 * fun: get string variable
 * arg: string and default string
 * ret: string variable
 *
 */

char *get_conf_str(const char *str, const char *def)
{
    int var = 0;
    char *endptr, *ptr;

    if(str == NULL){
        log(g_log, "argument null, return default[%s]\n", str, def);
        return (char *)def;
    }

    ptr = conf_get(str);
    if(ptr == NULL){
        log(g_log, "argument[%s] not exist, return default[%s]\n", str, def);
        return (char *)def;
    }

    if(!strncmp(ptr, "", 1)){
        log(g_log, "argument[%s] empty, return default[%s]\n", str, def);
        return (char *)def;
    }

    return ptr;
}
