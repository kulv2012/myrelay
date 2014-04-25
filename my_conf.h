#ifndef _MY_CONF_H_
#define _MY_CONF_H_

#define conf_def_daemon 1
#define conf_def_worker 2
#define conf_def_max_connections 100000

#define conf_def_ip "0.0.0.0"
#define conf_def_port "13306"

#define conf_def_read_client_timeout 60
#define conf_def_write_mysql_timeout 60
#define conf_def_read_mysql_write_client_timeout 300
#define conf_def_prepare_mysql_timeout 15
#define conf_def_idle_timeout 60
#define conf_def_mysql_ping_timeout 10

#define conf_def_user ""
#define conf_def_passwd ""

#define conf_def_mysql_conf "./conf/mysql.conf"

#define conf_def_log "./myproxy.log"
#define conf_def_loglevel "log"
#define conf_def_sqllog "./sql.log"

typedef struct{
    char host[64];
    char port[16];
    char user[16];
    char pass[16];
    int  cnum;
}my_node_conf_t;

typedef struct{
    int mcount;
    int scount;
    my_node_conf_t master[1];
    my_node_conf_t slave[64];
}my_conf_t;

struct conf_t{
    int daemon;
    int worker;
    int max_connections;
    char *ip;
    char *port;
    int read_client_timeout;
    int write_mysql_timeout;
    int read_mysql_write_client_timeout;
    int prepare_mysql_timeout;
    int idle_timeout;
    int mysql_ping_timeout;
    char *user;
    char *passwd;
    char *mysql_conf;
    char *log;
    char *loglevel;
    char *sqllog;
};

int my_conf_init(const char *log);
int mysql_conf_parse(const char *conf, my_conf_t *myconf);

#endif
