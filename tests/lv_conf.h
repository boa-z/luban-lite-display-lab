#ifndef LV_CONF_H
#define LV_CONF_H
/* Host tests only: the SDK LVGL aligns draw buffers to the target cache line. */
#define CACHE_LINE_SIZE 64
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888
#define LV_USE_OS LV_OS_NONE
#define LV_MEM_SIZE (1U * 1024U * 1024U)
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_USE_THORVG 0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_SYSMON 0
#endif
