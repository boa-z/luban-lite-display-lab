#pragma once
#include <stdbool.h>
#define RT_EOK 0
#define RT_ERROR 1
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 0
struct rt_mutex { bool locked; };
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_mutex_detach(struct rt_mutex *);
int rt_kprintf(const char *, ...);
