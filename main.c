/* Display Lab 作为独立应用（PRJ_APP="luban-lite-display-lab"）时的入口。SDK 自己启动 LVGL 线程、
   显示与触摸；台架工具挂在 shell 上（hmi_color、hmi_testcard、hmi_display），这里无事可做。
   作为另一个应用的组件编译时不编这个文件。 */
#include <rtthread.h>
#ifdef RT_USING_ULOG
#include <ulog.h>
#endif

int main(void)
{
#ifdef ULOG_USING_FILTER
    ulog_global_filter_lvl_set(ULOG_OUTPUT_LVL);
#endif
    return 0;
}
