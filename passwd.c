#include <stdint.h>
#include <string.h>
#include "sha1.h"
#include "mysql_com.h"
#include "passwd.h"

static void my_crypt(char *to, const char *s1, const char *s2, uint32_t len)
{
    const char *s1_end = s1 + len;

    while(s1 < s1_end){
        *to++= *s1++ ^ *s2++;
    }
}

/*
 * fun: calculate scram token
 * arg: token, scram, password
 * ret:
 *
 */

void scramble(char *to, const char *message, const char *password)
{
    SHA1_CONTEXT sha1_context;
    uint8_t hash_stage1[SHA1_HASH_SIZE];
    uint8_t hash_stage2[SHA1_HASH_SIZE];
    
    mysql_sha1_reset(&sha1_context);
    /* stage 1: hash password */
    mysql_sha1_input(&sha1_context, (char *) password, (uint32_t) strlen(password));
    mysql_sha1_result(&sha1_context, hash_stage1);
    /* stage 2: hash stage 1; note that hash_stage2 is stored in the database */
    mysql_sha1_reset(&sha1_context);
    mysql_sha1_input(&sha1_context, hash_stage1, SHA1_HASH_SIZE);
    mysql_sha1_result(&sha1_context, hash_stage2);
    /* create crypt string as sha1(message, hash_stage2) */
    mysql_sha1_reset(&sha1_context);
    mysql_sha1_input(&sha1_context, (const char *) message, SCRAMBLE_LENGTH);
    mysql_sha1_input(&sha1_context, hash_stage2, SHA1_HASH_SIZE);
    /* xor allows 'from' and 'to' overlap: lets take advantage of it */
    mysql_sha1_result(&sha1_context, (char *) to);
    my_crypt(to, (const char *) to, hash_stage1, SCRAMBLE_LENGTH);
}

/*
 * fun: make random scram
 * arg: scram buffer, buffer len
 * ret: success 0, error -1
 *
 */

int make_rand_scram(char *scram, int len)
{
    int i, size;
    char vec_c[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890~!@#$%^&*()_+}{><?";

    size = sizeof(vec_c) - 1;

    for(i = 0; i < len; i++){
        scram[i] = vec_c[rand() % size];
    }

    return len;
}
