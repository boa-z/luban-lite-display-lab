#ifndef HMI_TESTS_CHECK_H
#define HMI_TESTS_CHECK_H
#include <stdio.h>

/* 失败时打印文件与行号并让当前函数返回 1。与 assert 不同，Release 构建下也不会被去掉。 */
#define CHECK(x)                                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
        {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                          \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)

#endif
