#ifndef _TIMER_H_
#define _TIMER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*timer_func_t)(unsigned long arg);
int timer_init(void);
int timer_register(timer_func_t func, unsigned long arg, char *info, int interval);
int timer(void);

#ifdef __cplusplus
}
#endif

#endif
