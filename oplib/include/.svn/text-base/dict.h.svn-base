#ifndef __DICT_H_
#define __DICT_H_

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ditem{
    void    *var;
    void    *value;
    struct list_head link;
} ditem_t;

typedef struct dict{
    struct list_head *head_array;
    unsigned long   (*sign)(void *);
    int             (*cmp)(void *, void *);
    unsigned long   attr;
} dict_t;

dict_t *dict_init(unsigned long size);
int  dict_clear(dict_t *op);
void *dict_search(dict_t *op, void *);
void *dict_insert(dict_t *op,void *, void *);

int  dict_setsign(dict_t *op, unsigned long (*)(void *));
int  dict_setcmp(dict_t *op, int (*)(void *, void *));

#ifdef __cplusplus
}
#endif

#endif
