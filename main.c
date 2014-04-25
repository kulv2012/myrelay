/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>                          
 *
 */                                                                       

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <log.h>
#include <handler.h>
#include "cli_pool.h"
#include "my_pool.h"
#include "conn_pool.h"
#include "my_conf.h"

#define VERSION "0.3.2"

int g_usr1_reload = 0;
log_t *g_log = NULL;

extern struct conf_t g_conf;

static int signal_init(void);
static void signal_usr1(int signal);

#define USAGE(){ \
    fprintf(stderr, "Version: %s\n", VERSION); \
    fprintf(stderr, "Usage: %s config\n\n", argv[0]); \
    fprintf(stderr, "Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.\n"); \
    fprintf(stderr, "Use and distribution licensed under the GPL license.\n\n"); \
    fprintf(stderr, "Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>\n"); \
}

int main(int argc, char *argv[])
{
    int i, listenfd;
    pid_t pid;

    // argument parse
    if(argc != 2){
        USAGE();
        exit(-1);
    }
    if(!strcmp(argv[1], "-h") || !strcmp(argv[1], "-v")){
        USAGE();
        exit(-1);
    }
    if(my_conf_init(argv[1]) < 0){
        log(g_log, "conf[%s] init error\n", argv[1]);
        exit(-1);
    } else {
        log(g_log, "conf[%s] init success\n", argv[1]);
    }

    // make listen
    while(1){
        listenfd = make_listen_nonblock(g_conf.ip, g_conf.port);
        if(listenfd < 0){
            log_err(g_log, "%s:%s listen socket error\n", g_conf.ip, g_conf.port);
        } else {
            log(g_log, "make listen socket success\n");
            break;
        }

        sleep(5);
    }

    // signal init
    if( (signal_init()) < 0 ){
        log(g_log, "signal init error\n");
        exit(-1);
    } else {
        log(g_log, "signal init success\n");
    }

    if(g_conf.daemon){
        daemon(1, 1);
    }

    // fork children
    for(i = 0; i < g_conf.worker; i++){
        while( (pid = fork()) < 0 ){
            log_err(g_log, "fork error\n");
            sleep(3);
        }

        if(pid == 0){
            work(listenfd);
            exit(-1);
        }
    }

    // wait for children exit and restart it
    while(1){
        while( (pid = waitpid(-1, NULL, 0)) < 0 ){
            sleep(3);
        }

        log(g_log, "process exit, pid = %d\n", pid);
        sleep(3);

        while( (pid = fork()) < 0 ){
            log_err(g_log, "fork error\n");
            sleep(3);
        }

        if(pid == 0){
            work(listenfd);
            exit(-1);
        }
    }

    return 0;
}

/*
 * fun: init signal handler
 * arg: 
 * ret: always return 0
 *
 */

static int signal_init(void)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGILL, SIG_IGN);
    signal(SIGTRAP, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    signal(SIGKILL, SIG_IGN);
    signal(SIGUSR1, signal_usr1);
    signal(SIGUSR2, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGALRM, SIG_IGN);
    signal(SIGCONT, SIG_IGN);

    return 0;
}

/*
 * fun: set USR1 signal flag
 * arg: signal number
 * ret: 
 *
 */

static void signal_usr1(int signal)
{
    g_usr1_reload = 1;

    return;
}
