#ifndef __LOG_H_
#define __LOG_H_

#include <pthread.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _log_t{
    char *fname;
    int fd;
    struct stat statbuf;
    int level;
    pthread_mutex_t lock;
} log_t;

log_t *log_init(const char *fname, int level);
int log_deinit(log_t *log);
int log_ret(log_t *log, int level, int flag, const char *, int, const char *, const char *, ...);

enum{
    LOG_WITHOUT_STRERROR = 0,
    LOG_WITH_STRERROR
};

enum{
    LOG_NONE = 0,
    LOG_LEVEL_ERR,
    LOG_LEVEL_LOG,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO
};

#define log(log, ...) log_ret(log, LOG_LEVEL_LOG, LOG_WITHOUT_STRERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_err(log, ...) log_ret(log, LOG_LEVEL_ERR, LOG_WITHOUT_STRERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_strerr(log, ...) log_ret(log, LOG_LEVEL_ERR, LOG_WITH_STRERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define debug(log, ...) log_ret(log, LOG_LEVEL_DEBUG, LOG_WITHOUT_STRERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define info(log, ...) log_ret(log, LOG_LEVEL_INFO, LOG_WITHOUT_STRERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
