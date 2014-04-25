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
#include <sys/types.h>
#include <string.h>
#include "my_buf.h"

/*
 * fun: init mem buffer
 * arg: buffer pointer
 * ret: always return 0
 *
 */

int buf_init(buf_t *buf)
{
    //bzero(buf->mem, sizeof(buf->mem));
    buf->ptr = buf->mem;
    buf->reloc = 0;
    buf->size = sizeof(buf->mem);
    buf->used = 0;
    buf->pos = 0;

    return 0;
}

/*
 * fun: reset mem buffer
 * arg: buffer pointer
 * ret: always return 0
 *
 */

int buf_reset(buf_t *buf)
{
    if(buf->reloc){
        free(buf->ptr);
    }

    return buf_init(buf);
}

/*
 * fun: realloc buffer memory if prealloc memory is not enough
 * arg: buffer pointer, new size
 * ret: buffer pointer
 *
 */

buf_t *buf_realloc(buf_t *buf, size_t size)
{
    int reloc;
    char *ptr, *old;

    if(size <= buf->size){
        return buf;
    }

    reloc = buf->reloc;
    if(reloc){
        old = buf->ptr;
    }

    if( (ptr = malloc(size)) == NULL ){
        return NULL;
    }

    buf->ptr = ptr;
    buf->size = size;
    buf->reloc = 1;

    if(buf->used > 0){
        if(reloc){
            memcpy(ptr, old, buf->used);
            free(old);
        } else {
            memcpy(ptr, buf->mem, buf->used);
        }
    }

    return buf;
}

/*
 * fun: rewind buffer position
 * arg: buffer pointer
 * ret: always return 0
 *
 */

int buf_rewind(buf_t *buf)
{
    buf->pos = 0;
    return 0;
}

/*
 * fun: copy mem buffer
 * arg: dest buffer, source buffer
 * ret: success return 0, error return -1
 *
 */

/*
int buf_copy(buf_t *dst, buf_t *src)
{
    char *ptr;

    buf_reset(dst);

    if(src->reloc){
        if( (ptr = malloc(src->size)) == NULL ){
            return -1;
        }

        dst->ptr = ptr;
        dst->reloc = 1;
        dst->size = src->size;
    }

    memcpy(dst->ptr, src->ptr, src->size);
    dst->used = src->used;
    dst->pos = src->pos;

    return 0;
}
*/
