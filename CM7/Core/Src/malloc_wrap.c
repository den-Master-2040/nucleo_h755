/*
 * malloc_wrap.c
 *
 *  Created on: 13 июл. 2026 г.
 *      Author: DANIL
 */


#include <string.h>
#include "FreeRTOS.h"
volatile size_t g_fail_req;      /* сколько просили          */
volatile size_t g_fail_min;      /* минимум за всю работу    */
void *__wrap_malloc(size_t n)
{
    void *p = pvPortMalloc(n);
    if (!p) {
        g_fail_req  = n;
        g_fail_min  = xPortGetMinimumEverFreeHeapSize();
        for(;;);
    }
    return p;
}

void  __wrap_free(void *p)     { vPortFree(p); }
void *__wrap_calloc(size_t n, size_t s) {
    void *p = pvPortMalloc(n * s);
    if (p) memset(p, 0, n * s);
    return p;
}
