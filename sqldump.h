#ifndef _SQLDUMP_H_
#define _SQLDUMP_H_

#include "conn_pool.h"

int sqldump_init(const char *fname);
int sqldump(conn_t *c);
int sqldump_close(void);

#endif
