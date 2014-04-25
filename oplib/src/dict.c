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
#include <assert.h>
#include "list.h"
#include "hash.h"
#include "dict.h"

static unsigned long getsum(void *var);
static unsigned long getkey(dict_t *op, void *var);

/*
 * fun: init dict
 * arg: dict bucket number
 * ret: success!=NULL, error==NULL
 *
 */

dict_t *dict_init(unsigned long size)
{
    int         i;
    struct list_head *ptr;
    dict_t      *op;

    assert(size > 0);

    if(size < 11)
        size = 11;

    if( (ptr = malloc(sizeof(struct list_head) * size)) == NULL ){
        return NULL;
    }

    for(i = 0; i < size; i++){
        INIT_LIST_HEAD(ptr + i);
    }

    if( (op = malloc(sizeof(dict_t))) == NULL ){
        free(ptr);
        return NULL;
    }

    op->head_array = ptr;
    op->attr = size;
    dict_setsign(op, getsum);
    dict_setcmp(op, (int (*)(void *, void *))strcmp);

    return op;
}

/*
 * fun: dict search
 * arg: dict pointer, variable
 * ret: error=NULL, success!=NULL
 *
 */

void *dict_search(dict_t *op, void *var)
{
    unsigned long   key;
    struct list_head    *head, *pos;
    ditem_t *item;

    if( (var == NULL) || (op == NULL) ){
        return NULL;
    }
    key = getkey(op, var);
    head = op->head_array + key;

    list_for_each(pos, head){
        item = list_entry(pos, ditem_t, link);
        if( !(op->cmp(item->var, var)) ){
            return item->value;
        }
    }

    return NULL;
}

/*
 * fun: insert item into dict
 * arg: dict pointer, item
 * ret: error=NULL, duplicate!=value, success=value
 *
 */

void *dict_insert(dict_t *op, void *var, void *value)
{
    unsigned long   key;
    struct list_head *head, *pos;
    ditem_t *item;

    if( (op == NULL) || (var == NULL) || (value == NULL) ){
        return NULL;
    }

    key = getkey(op, var);
    head = op->head_array + key;

    list_for_each(pos, head){
        item = list_entry(pos, ditem_t, link);
        if( !(op->cmp(item->var, var)) ){
            return item->value;
        }
    }

    if( (item = malloc(sizeof(ditem_t))) == NULL ){
        return NULL;
    }
    item->var = (char *)var;
    item->value = value;

    list_add(&(item->link), head);

    return value;
}

/*
 * fun: clear dict
 * arg: dict pointer
 * ret: always return 0
 *
 */

int dict_clear(dict_t *ptr)
{
    free(ptr->head_array);
    free(ptr);

    return 0;
}

/*
 * fun: set checksum function
 * arg: dict pointer, checksum function
 * ret: always return 0
 *
 */

int dict_setsign(dict_t *op, unsigned long (*sign)(void *))
{
    op->sign = sign;

    return 0;
}

/*
 * fun: set compare function
 * arg: dict pointer, compare function
 * ret: always return 0
 *
 */

int dict_setcmp(dict_t *op, int (*cmp)(void *, void *))
{
    op->cmp = cmp;

    return 0;
}

/*
 * fun: change checksum to key
 * arg: dict pointer, query string
 * ret: key
 *
 */

static unsigned long getkey(dict_t *op, void *var)
{
    unsigned long size = op->attr;
    unsigned long (*sign)(void *) = op->sign;

    return sign(var) % size;
}

/*
 * fun: default checksum function
 * arg: query string
 * ret: key
 *
 */

static unsigned long getsum(void *var)
{
    unsigned long sum = 0;
    char *ptr = (char *)var;
    int len;

    len = strlen(ptr);

    return mmhash64(ptr, len);
}
