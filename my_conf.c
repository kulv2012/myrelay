/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <stdio.h>
#include <string.h>
#include <conf.h>
#include <log.h>
#include "my_conf.h"

#define CONF_FILL_STR(arg) \
            do{g_conf.arg = get_conf_str(#arg, conf_def_ ## arg);}while(0)
#define CONF_FILL_INT(arg) \
            do{g_conf.arg = get_conf_int(#arg, conf_def_ ## arg);}while(0)

struct conf_t g_conf;

extern log_t *g_log;

/*
 * fun: init and fill myproxy config
 * arg: myproxy config path
 * ret: success 0, error -1
 *
 */

int my_conf_init(const char *log)
{
    int res;

    if( (res = conf_init(log)) < 0 ){
        return res;
    }

    CONF_FILL_INT(daemon);
    CONF_FILL_INT(worker);
    CONF_FILL_INT(max_connections);
    CONF_FILL_STR(ip);
    CONF_FILL_STR(port);
    CONF_FILL_INT(read_client_timeout);
    CONF_FILL_INT(write_mysql_timeout);
    CONF_FILL_INT(read_mysql_write_client_timeout);
    CONF_FILL_INT(prepare_mysql_timeout);
    CONF_FILL_INT(idle_timeout);
    CONF_FILL_INT(mysql_ping_timeout);
    CONF_FILL_STR(user);
    CONF_FILL_STR(passwd);
    CONF_FILL_STR(mysql_conf);
    CONF_FILL_STR(log);
    CONF_FILL_STR(loglevel);
    CONF_FILL_STR(sqllog);

    return 0;
}

#define MAX_LINE_LEN 1024

/*
 * fun: parse and fill mysql config
 * arg: mysql config path, mysql config struct
 * ret: success 0, error -1
 *
 */

int mysql_conf_parse(const char *conf, my_conf_t *myconf)
{
    int i, res, line = 0;
    FILE *fp;
    char buf[MAX_LINE_LEN];
    char type[64], host[128], port[128], user[64], pass[64];
    int  cnum;
    int  mcount = 0, scount = 0;

    my_node_conf_t *mynode;

    myconf->mcount = 0;
    myconf->scount = 0;

    for(i = 0; i < 1; i++){
        mynode = myconf->master + i;
        bzero(mynode->host, sizeof(mynode->host));
        bzero(mynode->port, sizeof(mynode->port));
        bzero(mynode->user, sizeof(mynode->user));
        bzero(mynode->pass, sizeof(mynode->pass));

        mynode->cnum = 0;
    }

    for(i = 0; i < 64; i++){
        mynode = myconf->slave + i;
        bzero(mynode->host, sizeof(mynode->host));
        bzero(mynode->port, sizeof(mynode->port));
        bzero(mynode->user, sizeof(mynode->user));
        bzero(mynode->pass, sizeof(mynode->pass));

        mynode->cnum = 0;
    }

    if( (fp = fopen(conf, "r")) == NULL ){
        log(g_log, "fopen error\n");
        return -1;
    }

    fgets(buf, MAX_LINE_LEN, fp);
    while(!feof(fp)){
        line++;
        trim(buf);
        if( (*buf != '#') && (*buf != '\0') ){
            res = sscanf(buf, "%s %s %s %s %s %d", \
                            type, host, port, user, pass, &cnum);
            if(res == 6){
                if(!strcmp(type, "master")){
                    if(mcount >= 1){
                        log(g_log, "line[%d] error, master num limit\n", line);
                        return -1;
                    }
                    mynode = &(myconf->master[mcount++]);
                } else if(!strcmp(type, "slave")) {
                    if(scount >= 64){
                        log(g_log, "line[%d] error, slave num limit\n", line);
                        return -1;
                    }
                    mynode = &(myconf->slave[scount++]);
                } else {
                    log(g_log, "line[%d] error, unknown mysql type\n", line);
                    return -1;
                }

                strncpy(mynode->host, host, sizeof(mynode->host) - 1);
                strncpy(mynode->port, port, sizeof(mynode->port) - 1);
                strncpy(mynode->user, user, sizeof(mynode->user) - 1);
                strncpy(mynode->pass, pass, sizeof(mynode->pass) - 1);
                mynode->cnum = cnum;
            } else {
                log(g_log, "line[%d] error\n", line);
                return -1;
            }
        }
        fgets(buf, sizeof(buf), fp);
    }

    fclose(fp);
    myconf->mcount = mcount;
    myconf->scount = scount;

    return 0;
}
