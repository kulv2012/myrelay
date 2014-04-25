#ifndef _IPRANGE_H_
#define _IPRANGE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ipblock{
    uint32_t ipaddr_s;
    uint32_t ipaddr_e;
}ipblock;

typedef struct _iprange_t{
    int max, num;
    ipblock *array;
} iprange_t;

iprange_t *iprange_init(const char *filename, int max);
int ipaddr_in_range(iprange_t *handler, uint32_t addr);
int iprange_dump(iprange_t *handler);
int iprange_release(iprange_t *handler);
iprange_t *iprange_reload(iprange_t *handler, const char *filename, int max);

#ifdef __cplusplus
}
#endif

#endif
