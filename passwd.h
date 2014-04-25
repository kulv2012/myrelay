#ifndef _PASSWD_H_
#define _PASSWD_H_

int make_rand_scram(char *scram, int len);
void scramble(char *to, const char *message, const char *password);

#endif
