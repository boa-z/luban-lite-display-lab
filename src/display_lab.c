#include "display_lab.h"
#include <lvgl.h>
#include <stdbool.h>

/* SDK 的 LVGL 线程（lvgl-ui 的 lv_rt_thread_port.c）在 lv_user_gui_init() 之后循环调用
   lv_task_handler()，也就是 lv_timer_handler()。SConscript 用 -Wl,--wrap=lv_timer_handler 把调用
   接到这里：第一次进入且默认显示已建好时，在 LVGL 线程里挂上色卡与显示接管的轮询定时器。
   应用不需要任何钩子，也不依赖应用的 API；SDK 源码不改。 */
uint32_t __real_lv_timer_handler(void);

uint32_t __wrap_lv_timer_handler(void)
{
    static bool started;
    if (!started && (lv_display_get_default() != NULL))
    {
        started = true;
#ifdef DLAB_TESTCARD
        dlab_testcard_shell_init();
#endif
#ifdef DLAB_BENCH
        dlab_display_bench_init();
#endif
    }
    return __real_lv_timer_handler();
}
