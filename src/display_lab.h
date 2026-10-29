#ifndef DISPLAY_LAB_H
#define DISPLAY_LAB_H

/* 以下初始化只能在 SDK 的 LVGL 线程调用：display_lab.c 在该线程第一次进入 lv_timer_handler() 时
   调用它们，之后 hmi_testcard / hmi_display 可用。失败只记日志，不影响应用的显示。 */
void dlab_testcard_shell_init(void);
void dlab_display_bench_init(void);

#endif
