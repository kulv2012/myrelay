/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */

#include <time.h>
#include "log.h"
#include "timer.h"

extern log_t *g_log;

struct timer_func_map{
    timer_func_t func;
    char *info;
    unsigned long arg;
    time_t lasttime;
    int interval;
};

#define MAX_FUNC_MAP 256

static struct timer_func_map func_map[MAX_FUNC_MAP];
static int func_map_index;

/*
 * fun: init timer
 * arg: void
 * ret: success=0, error=-1
 *
 */

int timer_init(void)
{
    int i;

    for(i = 0; i < MAX_FUNC_MAP; i++){
        func_map[i].func = NULL;
        func_map[i].info = NULL;
        func_map[i].arg = 0;
        func_map[i].lasttime= 0;
        func_map[i].interval = 0;
    }
    func_map_index = 0;

    return 0;
}

/*
 * fun: register timer func
 * arg: callback function, argument, message, call interval
 * ret: success=0, error=-1
 *
 */

int timer_register(timer_func_t func, unsigned long arg, char *info, int interval)
{
    if(func_map_index >= MAX_FUNC_MAP){
        return -1;
    }

    if(func == NULL){
        return -1;
    }

    if(info == NULL){
        info = "";
    }

    func_map[func_map_index].func = func;
    func_map[func_map_index].info = info;
    func_map[func_map_index].arg  = arg;
    func_map[func_map_index].lasttime = 0;
    func_map[func_map_index].interval= interval;

    func_map_index++;

    return 0;
}

/*
 * fun: run timer loop
 * arg: void
 * ret: always return 0
 *
 */

int timer(void)
{
    int i, ret;
    time_t now = time(NULL);
    struct timer_func_map *fmap;

    for(i = 0; i < func_map_index; i++){
        fmap = &(func_map[i]);
        if(fmap->func == NULL){
            break;
        } else {
            if(now - fmap->lasttime >= fmap->interval){
                fmap->lasttime = now;
                ret = fmap->func(fmap->arg);
                if(ret > 0){
                    debug(g_log, "%s, ret[%d]\n", fmap->info, ret);
                } else if(ret < 0) {
                    log(g_log, "%s error, ret[%d]\n", fmap->info, ret);
                }
            }
        }
    }

    return 0;
}
