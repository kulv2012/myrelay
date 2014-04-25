#ifndef _MY_BUF_H_
#define _MY_BUF_H_

#include <stdint.h>

#define PREALLOC_BUF_SIZE (64 * 1024)
#define HEADER_SIZE 4

typedef struct buf_t{
    char mem[PREALLOC_BUF_SIZE];
    char *ptr;
    int reloc;
    size_t size;
    size_t used;
    size_t pos;
}buf_t;

int buf_init(buf_t *buf);
int buf_reset(buf_t *buf);
buf_t *buf_realloc(buf_t *buf, size_t size);
int buf_rewind(buf_t *buf);
int buf_copy(buf_t *dst, buf_t *src);

#endif
