#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hash.h"

/*
 * murmurhash
 */

inline uint64_t mmhash64(const void *key, int len)
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;
    const uint32_t seed = 106774;

    if(len <= 0){
        return 0;
    }
    uint64_t h = seed ^ (len * m);

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = data + (len/8);

    while(data != end) {
        uint64_t k = *data++;

        k *= m; 
        k ^= k >> r; 
        k *= m; 

        h ^= k;
        h *= m; 
    }

    const unsigned char * data2 = (const unsigned char*)data;

    switch(len & 7)
    {
    case 7: h ^= ((uint64_t)(data2[6]) << 48);
    case 6: h ^= ((uint64_t)(data2[5]) << 40);
    case 5: h ^= ((uint64_t)(data2[4]) << 32);
    case 4: h ^= ((uint64_t)(data2[3]) << 24);
    case 3: h ^= ((uint64_t)(data2[2]) << 16);
    case 2: h ^= ((uint64_t)(data2[1]) << 8);
    case 1: h ^= ((uint64_t)(data2[0]));
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

/*
 * naivehash
 *
 */

inline uint64_t naivehash64(const void *key, int len)
{
    int i;
    uint64_t h = 0;
    char *ptr = (char *)&h;
    char *k = (char *)key;

    if(len <= 0){
        return 0;
    }

    if(len < 8){
        memcpy(ptr, key, len);
    } else if(len < 16) {
        ptr[0] = k[1];
        ptr[1] = k[3];
        ptr[2] = k[4];
        ptr[3] = k[7];
        ptr[4] = k[9];
        ptr[5] = k[11];
        ptr[6] = k[13];
        ptr[7] = k[15];
    } else if(len < 32) {
        ptr[0] = k[2];
        ptr[1] = k[5];
        ptr[2] = k[9];
        ptr[3] = k[14];
        ptr[4] = k[17];
        ptr[5] = k[20];
        ptr[6] = k[25];
        ptr[7] = k[30];
    } else {
        ptr[0] = k[3];
        ptr[1] = k[6];
        ptr[2] = k[7];
        ptr[3] = k[12];
        ptr[4] = k[16];
        ptr[5] = k[len - 7];
        ptr[6] = k[len - 4];
        ptr[7] = k[len - 1];
    }

    return h;
}
