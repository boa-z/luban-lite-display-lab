/* 显示诊断色卡的几何与取色契约：网格分页、色值/标签不重叠、单色测量区尺寸与纯色、灰参考块。 */
#include "testcard.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

static const uint32_t extra[] = {0x33AA55u, 0xEE7700u, 0xDD2222u, 0x1A1A1Au, 0x333333u, 0x555555u};
static const uint32_t builtin[] = {0x000000u, 0x202020u, 0x404040u, 0x606060u, 0x808080u,
                                   0xC0C0C0u, 0xFFFFFFu, 0xFF0000u, 0x00FF00u, 0x0000FFu,
                                   0x00FFFFu, 0xFF00FFu, 0xFFFF00u};

static uint32_t bg(lv_obj_t *obj)
{
    return lv_color_to_u32(lv_obj_get_style_bg_color(obj, 0)) & 0xFFFFFFu;
}

static uint32_t preset(unsigned index)
{
    const unsigned count = (unsigned)(sizeof(builtin) / sizeof(builtin[0]));
    return index < count ? builtin[index] : extra[index - count];
}

static bool overlap(lv_obj_t *a, lv_obj_t *b)
{
    lv_area_t area_a, area_b;
    lv_obj_get_coords(a, &area_a);
    lv_obj_get_coords(b, &area_b);
    return area_a.x1 <= area_b.x2 && area_b.x1 <= area_a.x2 && area_a.y1 <= area_b.y2 &&
           area_b.y1 <= area_a.y2;
}

static lv_obj_t *root(void)
{
    return lv_obj_get_child(lv_layer_top(), 0);
}

/* 色卡只按对象几何与样式检查，不需要真屏：800x480 的无输出显示，flush 直接完成。 */
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static bool open_display(void)
{
    static uint32_t buffer[800 * 40];
    lv_display_t *display = lv_display_create(800, 480);
    if (display == NULL)
    {
        return false;
    }
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    return true;
}

static lv_obj_t *find_label(lv_obj_t *parent, const char *text)
{
    for (unsigned i = 0; i < lv_obj_get_child_count(parent); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(parent, (int)i);
        if (lv_obj_check_type(child, &lv_label_class) && !strcmp(lv_label_get_text(child), text))
        {
            return child;
        }
    }
    return NULL;
}

/* 每个色块：颜色与预置一致、无子对象（文字不压在色块上）、纯色无描边、编码标签在色块下方且不重叠。 */
static int check_page(unsigned first, unsigned swatches)
{
    lv_obj_t *card = root();
    CHECK(card != NULL);
    CHECK(bg(card) == 0x101010u);
    CHECK(lv_obj_get_child_count(card) == 1);
    lv_obj_t *page = lv_obj_get_child(card, 0);
    unsigned fills = 0, labels = 0;
    for (unsigned i = 0; i < lv_obj_get_child_count(page); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(page, (int)i);
        if (lv_obj_check_type(child, &lv_obj_class))
        {
            ++fills;
        }
        else if (lv_obj_check_type(child, &lv_label_class))
        {
            ++labels;
        }
    }
    CHECK(fills == swatches);
    CHECK(labels == swatches + 1); /* 每块一个编码，另加标题 */
    unsigned seen = 0;
    for (unsigned i = 0; i < lv_obj_get_child_count(page); ++i)
    {
        lv_obj_t *fill = lv_obj_get_child(page, (int)i);
        if (!lv_obj_check_type(fill, &lv_obj_class))
        {
            continue;
        }
        const uint32_t expected = preset(first + seen);
        CHECK(bg(fill) == expected);
        CHECK(lv_obj_get_child_count(fill) == 0);
        CHECK(lv_obj_get_style_bg_opa(fill, 0) == LV_OPA_COVER);
        CHECK(lv_obj_get_style_radius(fill, 0) == 0);
        CHECK(lv_obj_get_style_border_width(fill, 0) == 0);
        CHECK(lv_obj_get_style_shadow_width(fill, 0) == 0);
        CHECK(lv_obj_get_width(fill) >= 160 && lv_obj_get_height(fill) >= 96);
        char code[16];
        snprintf(code, sizeof(code), "0x%06X", (unsigned)expected);
        lv_obj_t *label = find_label(page, code);
        CHECK(label != NULL);
        CHECK(!overlap(fill, label));
        lv_area_t label_area, fill_area;
        lv_obj_get_coords(label, &label_area);
        lv_obj_get_coords(fill, &fill_area);
        CHECK(label_area.y1 >= fill_area.y2);
        ++seen;
    }
    CHECK(seen == swatches);
    return 0;
}

/* 主色块是整屏最大的对象；灰参考块固定 #808080、100×100。 */
static lv_obj_t *largest_fill(lv_obj_t *parent)
{
    lv_obj_t *largest = NULL;
    int32_t area = 0;
    for (unsigned i = 0; i < lv_obj_get_child_count(parent); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(parent, (int)i);
        if (!lv_obj_check_type(child, &lv_obj_class))
        {
            continue;
        }
        const int32_t size = lv_obj_get_width(child) * lv_obj_get_height(child);
        if (size > area)
        {
            area = size;
            largest = child;
        }
    }
    return largest;
}

static int check_patch(uint32_t rgb)
{
    lv_obj_t *card = root();
    CHECK(card != NULL);
    CHECK(bg(card) == 0x202020u);
    lv_obj_t *patch = largest_fill(card);
    CHECK(patch != NULL);
    CHECK(bg(patch) == rgb);
    CHECK(lv_obj_get_width(patch) >= 480 && lv_obj_get_height(patch) >= 260);
    CHECK(lv_obj_get_x(patch) == (800 - lv_obj_get_width(patch)) / 2);
    CHECK(lv_obj_get_child_count(patch) == 0);
    CHECK(lv_obj_get_style_bg_opa(patch, 0) == LV_OPA_COVER);
    CHECK(lv_obj_get_style_radius(patch, 0) == 0);
    CHECK(lv_obj_get_style_border_width(patch, 0) == 0);
    CHECK(lv_obj_get_style_shadow_width(patch, 0) == 0);
    lv_obj_t *gray = NULL;
    for (unsigned i = 0; i < lv_obj_get_child_count(card); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(card, (int)i);
        if (lv_obj_check_type(child, &lv_obj_class) && child != patch && bg(child) == 0x808080u)
        {
            gray = child;
        }
    }
    CHECK(gray != NULL);
    CHECK(lv_obj_get_width(gray) == 100 && lv_obj_get_height(gray) == 100);
    CHECK(lv_obj_get_child_count(gray) == 0);
    CHECK(!overlap(patch, gray));
    /* 说明文字与采样区（主色块、灰参考块）不相交。 */
    for (unsigned i = 0; i < lv_obj_get_child_count(card); ++i)
    {
        lv_obj_t *child = lv_obj_get_child(card, (int)i);
        if (!lv_obj_check_type(child, &lv_label_class))
        {
            continue;
        }
        CHECK(!overlap(child, patch) && !overlap(child, gray));
    }
    return 0;
}

int main(void)
{
    lv_init();
    CHECK(open_display());
    CHECK(lv_display_get_horizontal_resolution(NULL) == 800);
    CHECK(lv_display_get_vertical_resolution(NULL) == 480);

    CHECK(dlab_testcard_set_extra(extra, (unsigned)(sizeof(extra) / sizeof(extra[0]))) == 6);
    CHECK(dlab_testcard_preset_count() == 19);
    CHECK(dlab_testcard_show_index(19) == false);

    /* 网格：12 块一页，19 色分两页；文字在色块下方。 */
    dlab_testcard_show_grid();
    lv_obj_update_layout(lv_layer_top());
    CHECK(dlab_testcard_mode() == DLAB_TESTCARD_MODE_GRID);
    CHECK(dlab_testcard_page_count() == 2);
    CHECK(dlab_testcard_page() == 0);
    CHECK(check_page(0, 12) == 0);
    dlab_testcard_next();
    lv_obj_update_layout(lv_layer_top());
    CHECK(dlab_testcard_page() == 1);
    CHECK(check_page(12, 7) == 0);
    dlab_testcard_next();
    CHECK(dlab_testcard_page() == 0);
    dlab_testcard_prev();
    CHECK(dlab_testcard_page() == 1);

    /* 单色：任意 RGB；主色块 520×300，四周 #202020，同框 #808080 参考块。 */
    dlab_testcard_show_patch(0x33AA55u);
    lv_obj_update_layout(lv_layer_top());
    CHECK(dlab_testcard_mode() == DLAB_TESTCARD_MODE_PATCH);
    CHECK(dlab_testcard_rgb() == 0x33AA55u);
    CHECK(dlab_testcard_index() == DLAB_TESTCARD_NO_INDEX);
    CHECK(check_patch(0x33AA55u) == 0);
    dlab_testcard_show_patch(0xFFFFFFu);
    lv_obj_update_layout(lv_layer_top());
    CHECK(check_patch(0xFFFFFFu) == 0);

    /* 预置序号：0x33AA55 排在 13 号，next/prev 环绕。 */
    CHECK(dlab_testcard_show_index(13));
    CHECK(dlab_testcard_rgb() == 0x33AA55u);
    CHECK(dlab_testcard_index() == 13);
    dlab_testcard_show_index(18);
    dlab_testcard_next();
    CHECK(dlab_testcard_index() == 0);
    dlab_testcard_prev();
    CHECK(dlab_testcard_index() == 18);

    /* 关闭后回到 Product 界面，层上不留对象。 */
    dlab_testcard_off();
    CHECK(!dlab_testcard_active());
    CHECK(dlab_testcard_mode() == DLAB_TESTCARD_MODE_OFF);
    CHECK(lv_obj_get_child_count(lv_layer_top()) == 0);

    lv_deinit();
    printf("testcard PASS\n");
    return 0;
}
