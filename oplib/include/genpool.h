#ifndef _GENPOOL_H_
#define _GENPOOL_H_

#include "list.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _genpool_handler_t{
    uint32_t free_chunks, max_free_chunks;
    uint32_t prealloc_chunks;
    uint32_t page_size, pages_per_chunk;
    uint32_t total_chunks, max_total_chunks;
    struct list_head used_chunks_head;
    struct list_head free_chunks_head;
    struct list_head full_chunks_head;
} genpool_handler_t;

genpool_handler_t *genpool_init(size_t size, size_t max);
inline void *genpool_alloc_page(genpool_handler_t *g);
inline int genpool_release_page(genpool_handler_t *g, void *mem);
int genpool_status(genpool_handler_t *g, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
