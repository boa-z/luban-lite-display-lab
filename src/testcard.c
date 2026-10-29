#include "testcard.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

/* 显示诊断色卡：网格总览与单色测量两页，画在 lv_layer_top()，不改变 应用的页面状态。
   色块用 lv_obj_remove_style_all + 纯色填充，不加边框、阴影和透明层；网格的文字放在色块
   下方，单色模式的信息行放在主色块之外，采样区域始终只有原始 RGB。 */

#define GRID_COLUMNS 4
#define GRID_ROWS 3
#define GRID_PER_PAGE (GRID_COLUMNS * GRID_ROWS)
#define GRID_MARGIN 16
#define GRID_GAP 12
#define GRID_HEADER 40
#define GRID_LABEL 16

/* 网格底色比 #000000 亮一档，灰阶两端的色块都还能分辨边界。 */
#define GRID_BG 0x101010u
/* 单色模式的固定中性暗背景与同框灰参考块。 */
#define PATCH_BG 0x202020u
#define PATCH_REF 0x808080u

/* 内置预置：灰阶、RGB、CMY；追加预置色来自构建配置（DLAB_TESTCARD_EXTRA）。 */
static const uint32_t builtin_presets[] = {
    0x000000u, 0x202020u, 0x404040u, 0x606060u, 0x808080u, 0xC0C0C0u, 0xFFFFFFu,
    0xFF0000u, 0x00FF00u, 0x0000FFu, 0x00FFFFu, 0xFF00FFu, 0xFFFF00u,
};
#define BUILTIN_PRESETS ((unsigned)(sizeof(builtin_presets) / sizeof(builtin_presets[0])))

static uint32_t extra_presets[DLAB_TESTCARD_MAX_EXTRA];
static unsigned extra_count;

static lv_obj_t *card_root;
static lv_obj_t *page_root;
static dlab_testcard_mode_t card_mode;
static uint32_t card_rgb;
static unsigned card_index = DLAB_TESTCARD_NO_INDEX;
static unsigned card_page;

static void render_grid(void);
static void destroy_card(void);

static unsigned preset_count(void)
{
    return BUILTIN_PRESETS + extra_count;
}

static uint32_t preset_rgb(unsigned index)
{
    return index < BUILTIN_PRESETS ? builtin_presets[index] : extra_presets[index - BUILTIN_PRESETS];
}

static unsigned page_count(void)
{
    return (preset_count() + GRID_PER_PAGE - 1u) / GRID_PER_PAGE;
}

/* 布局按显示分辨率计算，色卡不依赖 应用的屏幕对象。 */
static int32_t screen_width(void)
{
    return lv_display_get_horizontal_resolution(NULL);
}

static int32_t screen_height(void)
{
    return lv_display_get_vertical_resolution(NULL);
}

/* 纯色块：去掉主题样式，只留 bg_color/bg_opa，保证采样到的就是原始 RGB。 */
static lv_obj_t *fill(lv_obj_t *parent, uint32_t rgb, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

/* 说明文字；w 大于 0 时按给定宽度居中，方便放在色块正下方。 */
static lv_obj_t *text(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, const lv_font_t *font, uint32_t rgb,
                      const char *value)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(label, value);
    lv_obj_set_pos(label, x, y);
    if (w > 0)
    {
        lv_obj_set_width(label, w);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    return label;
}

static bool make_root(uint32_t rgb)
{
    card_root = fill(lv_layer_top(), rgb, 0, 0, LV_PCT(100), LV_PCT(100));
    page_root = NULL;
    if (card_root == NULL)
    {
        return false;
    }
    /* 整屏盖住 应用界面；可点击但无回调，触摸在这里被吸收，不会落到下面的控件。 */
    lv_obj_add_flag(card_root, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

static void destroy_card(void)
{
    if (card_root != NULL)
    {
        lv_obj_delete(card_root);
        card_root = NULL;
        page_root = NULL;
    }
}

static void render_grid(void)
{
    if (card_root == NULL && !make_root(GRID_BG))
    {
        return;
    }
    if (page_root != NULL)
    {
        lv_obj_delete(page_root);
        page_root = NULL;
    }
    page_root = lv_obj_create(card_root);
    if (page_root == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(page_root);
    lv_obj_remove_flag(page_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(page_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(page_root, 0, 0);
    lv_obj_set_size(page_root, LV_PCT(100), LV_PCT(100));

    const int32_t width = screen_width();
    const int32_t height = screen_height();
    char title[32];
    snprintf(title, sizeof(title), "TEST CARD %u/%u", card_page + 1u, page_count());
    (void)text(page_root, GRID_MARGIN, 10, 0, &lv_font_montserrat_16, 0xFFFFFFu, title);

    const int32_t swatch_w = (width - (2 * GRID_MARGIN) - ((GRID_COLUMNS - 1) * GRID_GAP)) / GRID_COLUMNS;
    const int32_t cell_h =
        ((height - GRID_HEADER - (2 * GRID_MARGIN)) - ((GRID_ROWS - 1) * GRID_GAP)) / GRID_ROWS;
    const int32_t swatch_h = cell_h - GRID_LABEL - 4;
    const unsigned first = card_page * GRID_PER_PAGE;
    const unsigned count = preset_count();
    for (unsigned slot = 0; slot < GRID_PER_PAGE; ++slot)
    {
        const unsigned index = first + slot;
        if (index >= count)
        {
            break;
        }
        const int32_t x = GRID_MARGIN + ((int32_t)(slot % GRID_COLUMNS) * (swatch_w + GRID_GAP));
        const int32_t y = GRID_HEADER + ((int32_t)(slot / GRID_COLUMNS) * (cell_h + GRID_GAP));
        (void)fill(page_root, preset_rgb(index), x, y, swatch_w, swatch_h);
        char code[16];
        snprintf(code, sizeof(code), "0x%06X", (unsigned)preset_rgb(index));
        (void)text(page_root, x, y + swatch_h + 4, swatch_w, &lv_font_montserrat_14, 0xC0C0C0u, code);
    }
}

static void render_patch(void)
{
    destroy_card();
    if (!make_root(PATCH_BG))
    {
        return;
    }
    const int32_t width = screen_width();
    const int32_t height = screen_height();
    const int32_t patch_w = (width * 65) / 100;
    const int32_t patch_h = (height * 625) / 1000;
    const int32_t patch_x = (width - patch_w) / 2;
    const int32_t patch_y = height / 8;
    (void)fill(card_root, card_rgb, patch_x, patch_y, patch_w, patch_h);

    /* 同框灰参考块放在主色块之外，标签在块体下方。 */
    const int32_t ref = width / 8;
    const int32_t ref_x = width - ref - (width / 80);
    (void)fill(card_root, PATCH_REF, ref_x, patch_y, ref, ref);
    (void)text(card_root, ref_x, patch_y + ref + 4, 0, &lv_font_montserrat_14, 0xC0C0C0u, "#808080");

    /* 色值和序号放在主色块之外，采样区域不出现文字。 */
    char info[48];
    if (card_index != DLAB_TESTCARD_NO_INDEX)
    {
        snprintf(info, sizeof(info), "PATCH #%06X  index %u", (unsigned)card_rgb, card_index);
    }
    else
    {
        snprintf(info, sizeof(info), "PATCH #%06X", (unsigned)card_rgb);
    }
    (void)text(card_root, patch_x, patch_y + patch_h + 16, 0, &lv_font_montserrat_16, 0xFFFFFFu, info);
    (void)text(card_root, GRID_MARGIN, height - 28, 0, &lv_font_montserrat_14, 0x808080u,
               "hmi_testcard next|prev|off");
}

unsigned dlab_testcard_set_extra(const uint32_t *colors, unsigned count)
{
    extra_count = 0;
    if (colors != NULL)
    {
        for (unsigned i = 0; i < count && extra_count < DLAB_TESTCARD_MAX_EXTRA; ++i)
        {
            extra_presets[extra_count++] = colors[i] & 0xFFFFFFu;
        }
    }
    if (card_mode == DLAB_TESTCARD_MODE_GRID && card_root != NULL)
    {
        if (card_page >= page_count())
        {
            card_page = 0;
        }
        render_grid();
    }
    return extra_count;
}

unsigned dlab_testcard_preset_count(void)
{
    return preset_count();
}

bool dlab_testcard_preset(unsigned index, uint32_t *rgb)
{
    if (index >= preset_count())
    {
        return false;
    }
    if (rgb != NULL)
    {
        *rgb = preset_rgb(index);
    }
    return true;
}

void dlab_testcard_show_grid(void)
{
    card_mode = DLAB_TESTCARD_MODE_GRID;
    if (card_index != DLAB_TESTCARD_NO_INDEX)
    {
        card_page = card_index / GRID_PER_PAGE;
    }
    if (card_page >= page_count())
    {
        card_page = 0;
    }
    destroy_card();
    render_grid();
}

void dlab_testcard_show_patch(uint32_t rgb)
{
    card_mode = DLAB_TESTCARD_MODE_PATCH;
    card_rgb = rgb & 0xFFFFFFu;
    card_index = DLAB_TESTCARD_NO_INDEX;
    render_patch();
}

bool dlab_testcard_show_index(unsigned index)
{
    if (index >= preset_count())
    {
        return false;
    }
    card_mode = DLAB_TESTCARD_MODE_PATCH;
    card_index = index;
    card_rgb = preset_rgb(index);
    render_patch();
    return true;
}

void dlab_testcard_next(void)
{
    if (card_mode == DLAB_TESTCARD_MODE_GRID)
    {
        card_page = (card_page + 1u) % page_count();
        render_grid();
        return;
    }
    const unsigned count = preset_count();
    if (count == 0u)
    {
        return;
    }
    const unsigned next = (card_index != DLAB_TESTCARD_NO_INDEX && card_mode == DLAB_TESTCARD_MODE_PATCH)
                              ? (card_index + 1u) % count
                              : 0u;
    (void)dlab_testcard_show_index(next);
}

void dlab_testcard_prev(void)
{
    if (card_mode == DLAB_TESTCARD_MODE_GRID)
    {
        const unsigned pages = page_count();
        card_page = (card_page + pages - 1u) % pages;
        render_grid();
        return;
    }
    const unsigned count = preset_count();
    if (count == 0u)
    {
        return;
    }
    const unsigned prev = (card_index != DLAB_TESTCARD_NO_INDEX && card_mode == DLAB_TESTCARD_MODE_PATCH)
                              ? (card_index + count - 1u) % count
                              : count - 1u;
    (void)dlab_testcard_show_index(prev);
}

void dlab_testcard_off(void)
{
    card_mode = DLAB_TESTCARD_MODE_OFF;
    destroy_card();
}

bool dlab_testcard_active(void)
{
    return card_mode != DLAB_TESTCARD_MODE_OFF;
}

dlab_testcard_mode_t dlab_testcard_mode(void)
{
    return card_mode;
}

uint32_t dlab_testcard_rgb(void)
{
    return card_rgb;
}

unsigned dlab_testcard_index(void)
{
    return card_index;
}

unsigned dlab_testcard_page(void)
{
    return card_page;
}

unsigned dlab_testcard_page_count(void)
{
    return page_count();
}
