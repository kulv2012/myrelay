/*
 * Copyright 2011-2013 Alibaba Group Holding Limited. All rights reserved.
 * Use and distribution licensed under the GPL license.                   
 *
 * Authors: XiaoJinliang <xiaoshi.xjl@taobao.com>
 *
 */                                                                       

/*
 * general memory pool, support page alloc and release
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "list.h"
#include "genpool.h"
#include "log.h"

#define MAGICNUM 1580166

#define PAGES_PER_CHUNK 64
#define MAX_FREE_CHUNKS 20
#define PREALLOC_CHUNKS 5

#define ALIGN_BITS 3
#define ALIGN_SIZE ((unsigned long)1 << ALIGN_BITS)
#define ALIGN_MASK (ALIGN_SIZE - 1)

typedef struct{
    void *chunk_addr;
    struct list_head link;
    struct list_head used_pages_head;
    struct list_head free_pages_head;
} chunk_t;

typedef struct{
    chunk_t *belong_chunk;
    struct list_head link;
    unsigned long magicnum;
    char mem[];
} page_t;

static int _alloc_a_chunk(genpool_handler_t *g);
static int _release_a_chunk(genpool_handler_t *g, chunk_t *c);

extern log_t *g_log;

/*
 *fun: alloc new chunk, chunk devided into page, chunk added into pool
 *arg: genpool handler
 *ret: success=0, error=-1
 */
static int _alloc_a_chunk(genpool_handler_t *g)
{
    int i;
    uint32_t n, size;
    char *ptr;
    chunk_t *c;
    page_t *p;

    if(g->total_chunks >= g->max_total_chunks){
        return -1;
    }

    n = g->pages_per_chunk;
    size = g->page_size + sizeof(page_t);
    if(size & ALIGN_MASK){
        size = (size & (~ALIGN_MASK)) + ALIGN_SIZE;
    }

    if( (c = malloc(sizeof(chunk_t))) == NULL ){
        return -2;
    }

    if( (ptr = malloc(size * n)) == NULL ){
        free(c);
        return -2;
    }

    INIT_LIST_HEAD(&(c->used_pages_head));
    INIT_LIST_HEAD(&(c->free_pages_head));
    c->chunk_addr = ptr;

    for(i = 0; i < n; i++){//将每个元素初始化一下，链表组织
        p = (page_t *)ptr;
        p->belong_chunk = c;
        p->magicnum = MAGICNUM;
        list_add_tail(&(p->link), &(c->free_pages_head));
        ptr = ptr + size;
    }

    list_add_tail(&(c->link), &(g->free_chunks_head));
    g->free_chunks++;
    g->total_chunks++;

    return 0;
}

/*
 *fun: release a chunk, it will dealloc chunk when too much
 *arg: genpool handler & chunk pointer
 *ret: success=0, error=-1
 */
static int _release_a_chunk(genpool_handler_t *g, chunk_t *c)
{//将一个chunk放到空闲的free_chunks_head里面，并适当控制总空闲数目
    chunk_t *tmp;

    list_del_init(&(c->link));
    list_add(&(c->link), &(g->free_chunks_head));
    g->free_chunks++;

    //genpool_status(g);

	//顺便看看是不是有多余的chunks，如果有就释放一个空闲的
    while(g->free_chunks > g->max_free_chunks){
        tmp = list_first_entry(&(g->free_chunks_head), chunk_t, link);
        list_del_init(&(tmp->link));
        if(tmp->chunk_addr){
            free(tmp->chunk_addr);
        }
        free(tmp);
        g->free_chunks--;
        g->total_chunks--;
    }

    return 0;
}

/*
 *fun: genpool handler init
 *arg: page size & max page
 *ret: success=pointer, error=NULL
 */
genpool_handler_t *genpool_init(size_t size, size_t max)
{
    int ret, i;
    genpool_handler_t *g;

    if( (g = malloc(sizeof(genpool_handler_t))) == NULL ){
        return NULL;
    }

    INIT_LIST_HEAD(&(g->used_chunks_head));
    INIT_LIST_HEAD(&(g->free_chunks_head));
    INIT_LIST_HEAD(&(g->full_chunks_head));

    g->free_chunks = 0;
    g->max_free_chunks = MAX_FREE_CHUNKS;
    g->prealloc_chunks = PREALLOC_CHUNKS;
    g->page_size = size;//一个元素的大小
    g->pages_per_chunk = PAGES_PER_CHUNK; //每个块包含多少个pages
    g->total_chunks = 0;
    g->max_total_chunks = max / g->pages_per_chunk + 1;

    for(i = 0; i < g->max_free_chunks; i++){
        ret = _alloc_a_chunk(g);//申请一块内存，初始化之。
        if(ret < 0){
            if(ret == -1){
                log(g_log, "reach max_total_chunk[%d]\n", g->max_free_chunks);
            }
            if(ret == -2){
                log(g_log, "chunk alloc fail\n");
            }
            break;
        }
    }

    if(i == 0){
        free(g);
        return NULL;
    }

    return g;
}
void genpool_destroy( genpool_handler_t *gpool){
	if( gpool == NULL ) {
		return ;
	}
	_free_a_chunk( gpool, &(gpool->used_chunks_head)) ;

	_free_a_chunk( gpool, &(gpool->free_chunks_head)) ;

	_free_a_chunk( gpool, &(gpool->full_chunks_head)) ;
    gpool->free_chunks = 0;

	free(gpool) ;
}

void _free_a_chunk( genpool_handler_t *gpool, struct list_head *head){
	struct list_head *pos, *n;
	chunk_t * tmpchunk = NULL ;

	list_for_each_safe(pos, n, head){
		tmpchunk = list_entry(pos, chunk_t, link);
		list_del_init(pos);
		if( tmpchunk->chunk_addr != NULL){
			//先free chunk 数据部分，再释放元数据部分
			free( tmpchunk->chunk_addr ) ;
		}
		free( tmpchunk) ;
        gpool->total_chunks--;
	}
	return ;
}


/*
 *fun: alloc page really
 *arg: genpool handler
 *ret: success=page pointer, error=NULL
 */
inline void *genpool_alloc_page(genpool_handler_t *g)
{
    int ret, i;
    chunk_t *c;
    page_t *p;
    struct list_head *entry;

    if(g->free_chunks == 0){
        for(i = 0; i < g->prealloc_chunks; i++){
            ret = _alloc_a_chunk(g);//如果没有空闲chunks了，不管如何，都尝试分配prealloc_chunks这么多
            if(ret < 0){
                if(ret == -1){
                    log(g_log, "reach max_total_chunk[%d]\n", g->max_free_chunks);
                }
                if(ret == -2){
                    log(g_log, "chunk alloc fail\n");
                }
                break;
            }
        }
    }

    if(list_empty(&(g->used_chunks_head))){
        if(list_empty(&(g->free_chunks_head))){
            return NULL;
        } else {
            c = list_first_entry(&(g->free_chunks_head), chunk_t, link);
            list_del_init(&(c->link));
            list_add_tail(&(c->link), &(g->used_chunks_head));
            g->free_chunks--;
        }
    }

    if(list_empty(&(g->used_chunks_head))){
        return NULL;
    }

    c = list_first_entry(&(g->used_chunks_head), chunk_t, link);

    if(list_empty(&(c->free_pages_head))){
        return NULL;
    }

    p = list_first_entry(&(c->free_pages_head), page_t, link);
    list_del_init(&(p->link));
    list_add_tail(&(p->link), &(c->used_pages_head));

    if(list_empty(&(c->free_pages_head))){
        list_del_init(&(c->link));
        list_add_tail(&(c->link), &(g->full_chunks_head));
    }

    //genpool_status(g);

    if(p->magicnum != MAGICNUM){
        log(g_log, "wrong magic num, something may be wrong\n");
        return NULL;
    }

    return p->mem;
}

/*
 *fun: release page
 *arg: genpool handler & page addr
 *ret: success=0
 */
inline int genpool_release_page(genpool_handler_t *g, void *mem)
{
    int i;
    chunk_t *c;
    page_t *p;
    struct list_head *entry;

    p = container_of(mem, page_t, mem);
    c = p->belong_chunk;

    list_del_init(&(p->link));
    list_add(&(p->link), &(c->free_pages_head));

    if(list_empty(&(c->used_pages_head))){
        _release_a_chunk(g, c);
    }

    return 0;
}

/*
 *fun: genpool status dump
 *arg: genpool handler
 *ret: success=0
 */
int genpool_status(genpool_handler_t *g, char *buf, size_t len)
{
    int n;

    n = snprintf(buf, len, "free_chunks[%u] total_chunks[%u]", g->free_chunks, g->total_chunks);

    return n;
}
