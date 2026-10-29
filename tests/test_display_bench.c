/* 真实 LVGL 绘制回归：外部 framebuffer 在暂停时不被覆盖，恢复后强制整屏刷新；注入设备失败。 */
#include "display_lab.h"
#include <lvgl.h>
#include <mpp_fb.h>
#include <rtthread.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int bench_command(int argc, char **argv);
static struct mpp_fb framebuffer;
static struct aicfb_layer_data layer = {.layer_id = AICFB_LAYER_TYPE_UI, .enable = 1, .cookie = 42};
static uint32_t pixels[2][16 * 16];
static unsigned flushes, heartbeats, reads, pans;
static bool device_present = true;
static int fail_command;
static char output[256];

int rt_mutex_init(struct rt_mutex *m, const char *name, int flags)
{
    (void)name; (void)flags; m->locked = false; return 0;
}
int rt_mutex_take(struct rt_mutex *m, int timeout)
{
    (void)timeout; assert(!m->locked); m->locked = true; return 0;
}
int rt_mutex_release(struct rt_mutex *m)
{
    assert(m->locked); m->locked = false; return 0;
}
int rt_mutex_detach(struct rt_mutex *m) { assert(!m->locked); return 0; }
int rt_kprintf(const char *format, ...)
{
    va_list args; va_start(args, format);
    int result = vsnprintf(output, sizeof(output), format, args);
    va_end(args); return result;
}
struct mpp_fb *mpp_fb_open(void) { return device_present ? &framebuffer : NULL; }
void mpp_fb_close(struct mpp_fb *fb) { assert(fb == &framebuffer); }
int mpp_fb_ioctl(struct mpp_fb *fb, int command, void *arg)
{
    assert(fb == &framebuffer);
    if (command == fail_command)
        return -1;
    if (command == AICFB_GET_LAYER_CONFIG)
        *(struct aicfb_layer_data *)arg = layer;
    else if (command == AICFB_UPDATE_LAYER_CONFIG)
        layer = *(struct aicfb_layer_data *)arg;
    else if (command == AICFB_PAN_DISPLAY)
    {
        assert(*(unsigned *)arg == 0u);
        layer.enable = 1;
        ++pans;
    }
    else
        assert(command == AICFB_WAIT_FOR_VSYNC);
    return 0;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    (void)area; (void)data;
    ++flushes;
    lv_display_flush_ready(display);
}
static void heartbeat(lv_timer_t *timer) { (void)timer; ++heartbeats; }
static void read_input(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input; ++reads; data->state = LV_INDEV_STATE_RELEASED;
}
static void run(void)
{
    for (unsigned i = 0; i < 10; ++i) { lv_tick_inc(10); lv_timer_handler(); }
}
static int command(const char *verb)
{
    char *args[] = {"hmi_display", (char *)verb};
    return bench_command(verb ? 2 : 1, args);
}
static void state(const char *expected)
{
    assert(command(NULL) == 0);
    assert(strstr(output, expected));
    assert(strstr(output, "pending=0"));
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(16, 16);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, pixels[0], pixels[1], sizeof(pixels[0]), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_input);
    lv_timer_create(heartbeat, 10, NULL);
    dlab_display_bench_init();
    dlab_display_bench_init();
    run();
    assert(flushes && heartbeats && reads);
    state("display=app");
    assert(command("invalid") != 0);
    assert(command("fb") == 0); run(); state("display=app");

    /* 故障不能悄悄停掉正常显示；失败后仍可重试。 */
    const int failures[] = {AICFB_GET_LAYER_CONFIG, AICFB_PAN_DISPLAY, AICFB_WAIT_FOR_VSYNC};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i)
    {
        fail_command = failures[i];
        assert(command("pause") == 0); run(); state("display=app");
        assert(lv_display_is_invalidation_enabled(display));
        assert(layer.cookie == 42 && layer.enable == 1);
    }
    fail_command = 0;
    device_present = false;
    assert(command("pause") == 0); run(); state("display=app");
    device_present = true;

    assert(command("pause") == 0);
    assert(command("resume") != 0); /* 待执行请求不能被另一条命令覆盖。 */
    run(); state("display=paused");
    assert(command("pause") == 0); run(); state("display=paused");
    memset(pixels, 0x5a, sizeof(pixels));
    const unsigned before_flush = flushes, before_heartbeat = heartbeats, before_reads = reads;
    for (unsigned i = 0; i < 20; ++i)
    {
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(i * 100), 0);
        /* 证明额外刷新请求也不会唤醒原生刷新定时器。 */
        lv_display_send_event(display, LV_EVENT_REFR_REQUEST, NULL);
        run();
    }
    assert(flushes == before_flush && heartbeats > before_heartbeat && reads == before_reads);
    for (unsigned i = 0; i < sizeof(pixels) / sizeof(uint32_t); ++i)
        assert(((uint32_t *)pixels)[i] == 0x5a5a5a5au);

    layer.enable = 0; /* 模拟 Pure Color 隐藏 UI；切 Gradient 必须重新打开缓冲 0。 */
    const unsigned old_pans = pans;
    assert(command("fb") == 0); run(); state("display=paused");
    assert(layer.enable == 1 && pans == old_pans + 1u);
    layer.cookie = 99;
    fail_command = AICFB_UPDATE_LAYER_CONFIG;
    assert(command("resume") == 0); run(); state("display=paused");
    fail_command = 0;
    assert(command("resume") == 0); run(); state("display=app");
    assert(layer.cookie == 42 && layer.enable == 1);
    assert(flushes > before_flush && reads > before_reads);
    for (unsigned i = 0; i < sizeof(pixels) / sizeof(uint32_t); ++i)
        assert(((uint32_t *)pixels)[i] != 0x5a5a5a5au);
    assert(command("resume") == 0); run();
    /* 重复暂停/恢复不能破坏 LVGL 的嵌套 invalidation 计数或唤醒原本暂停的输入。 */
    lv_timer_pause(lv_indev_get_read_timer(input));
    assert(command("pause") == 0); run();
    assert(command("resume") == 0); run();
    assert(lv_timer_get_paused(lv_indev_get_read_timer(input)));
    assert(lv_display_is_invalidation_enabled(display));
    puts("display bench: PASS");
    return 0;
}
