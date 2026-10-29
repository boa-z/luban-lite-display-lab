#ifndef DLAB_TESTCARD_H
#define DLAB_TESTCARD_H
#include <stdbool.h>
#include <stdint.h>

/** patch 任意色没有对应预置序号。 */
#define DLAB_TESTCARD_NO_INDEX 0xFFFFFFFFu
/** 追加预置色的上限。 */
#define DLAB_TESTCARD_MAX_EXTRA 16u

/** 色卡状态；off 时 应用界面原样可见。 */
typedef enum
{
    DLAB_TESTCARD_MODE_OFF = 0,
    DLAB_TESTCARD_MODE_GRID,
    DLAB_TESTCARD_MODE_PATCH,
} dlab_testcard_mode_t;

/* 色卡画在 lv_layer_top() 上，不碰 应用的屏幕和控件。LVGL 由 UI owner 独占，
   以下接口只能在 UI 线程调用。色块一律 LVGL 纯色填充，不做软件补偿：CCM/gamma 仍由
   显示引擎在合成之后处理，相机拍到的是实机最终颜色。 */
void dlab_testcard_show_grid(void);
void dlab_testcard_show_patch(uint32_t rgb);
bool dlab_testcard_show_index(unsigned index);
void dlab_testcard_next(void);
void dlab_testcard_prev(void);
void dlab_testcard_off(void);

bool dlab_testcard_active(void);
dlab_testcard_mode_t dlab_testcard_mode(void);
uint32_t dlab_testcard_rgb(void);
unsigned dlab_testcard_index(void);
unsigned dlab_testcard_page(void);
unsigned dlab_testcard_page_count(void);

/* 预置表：内置 13 个灰阶/RGB/CMY；构建配置（DLAB_TESTCARD_EXTRA）可补充关键色。
   set_extra 覆盖追加列表（不是追加），只接受 RGB888，返回实际保留的个数。 */
unsigned dlab_testcard_set_extra(const uint32_t *colors, unsigned count);
unsigned dlab_testcard_preset_count(void);
bool dlab_testcard_preset(unsigned index, uint32_t *rgb);
#endif
