#define LOG_TAG "dlab.display"
#define LOG_LVL LOG_LVL_INFO
#include "display_lab.h"
#include <finsh.h>
#include <lvgl.h>
#include <mpp_fb.h>
#include <rtthread.h>
#include <string.h>
#include <ulog.h>

typedef enum
{
    REQUEST_NONE,
    REQUEST_PAUSE,
    REQUEST_FB,
    REQUEST_RESUME,
} request_t;

static struct rt_mutex state_lock;
static bool started, paused;
static request_t pending;
/* 以下句柄仅由 UI owner 使用；不挂起 LVGL 线程，present/诊断心跳仍正常推进。 */
static lv_display_t *display;
static lv_timer_t *refresh_timer, *input_timer;
static bool input_was_running;
static struct aicfb_layer_data saved_layer;

static bool select_framebuffer(struct mpp_fb *fb)
{
    /* 官方 test_fb -b 只画 framebuffer 起始地址，不会跟随 LVGL 的双缓冲切换。 */
    unsigned index = 0u;
    return mpp_fb_ioctl(fb, AICFB_PAN_DISPLAY, &index) == 0 &&
           mpp_fb_ioctl(fb, AICFB_WAIT_FOR_VSYNC, NULL) == 0;
}

static bool apply_request(request_t request)
{
    if ((request == REQUEST_PAUSE && paused) || (request == REQUEST_RESUME && !paused))
        return true;
    if (request == REQUEST_FB && !paused)
        return false;

    struct mpp_fb *fb = mpp_fb_open();
    if (!fb)
        return false;
    bool ok = false;
    if (request == REQUEST_PAUSE)
    {
#if defined(LV_DISPLAY_ROTATE_EN)
        /* SDK 的旋转显示可能有独立异步搬运线程；尚未实现该路径的接管握手。 */
        LOG_E("display bench does not support asynchronous rotated display");
#else
        refresh_timer = lv_display_get_refr_timer(display);
        saved_layer = (struct aicfb_layer_data){.layer_id = AICFB_LAYER_TYPE_UI};
        if (refresh_timer && lv_display_is_invalidation_enabled(display) &&
            mpp_fb_ioctl(fb, AICFB_GET_LAYER_CONFIG, &saved_layer) == 0)
        {
            /* 在同一个 UI owner 内排空已有绘制；仅丢弃 flush 无法阻止 LVGL 写 framebuffer。 */
            lv_refr_now(display);
            lv_display_enable_invalidation(display, false);
            lv_timer_pause(refresh_timer);
            if (select_framebuffer(fb))
            {
                input_timer = NULL;
                for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
                {
                    if (lv_indev_get_display(input) == display &&
                        lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER)
                    {
                        input_timer = lv_indev_get_read_timer(input);
                        break;
                    }
                }
                input_was_running = input_timer && !lv_timer_get_paused(input_timer);
                if (input_timer)
                    lv_timer_pause(input_timer);
                paused = true;
                ok = true;
            }
            else
            {
                /* 接管失败立即恢复，不能把应用留在不可见的半接管状态。 */
                (void)mpp_fb_ioctl(fb, AICFB_UPDATE_LAYER_CONFIG, &saved_layer);
                lv_display_enable_invalidation(display, true);
                lv_obj_invalidate(lv_display_get_screen_active(display));
                lv_timer_resume(refresh_timer);
            }
        }
#endif
    }
    else if (request == REQUEST_FB)
    {
        /* Pure Color 直接关闭前景寄存器；切到 Gradient 前重新打开 UI 并选中缓冲 0。 */
        ok = select_framebuffer(fb);
    }
    else if (request == REQUEST_RESUME)
    {
        ok = mpp_fb_ioctl(fb, AICFB_UPDATE_LAYER_CONFIG, &saved_layer) == 0;
        if (ok)
        {
            paused = false;
            lv_display_enable_invalidation(display, true);
            /* 外部工具可能改写了任一缓冲；整屏重绘，不能只依赖暂停前的脏区。 */
            lv_obj_invalidate(lv_display_get_screen_active(display));
            lv_obj_invalidate(lv_display_get_layer_top(display));
            lv_obj_invalidate(lv_display_get_layer_sys(display));
            lv_refr_now(display);
            /* DIRECT 双缓冲在下一次刷新时才同步上一帧；再推进一次，清除另一缓冲的外部图案。 */
            lv_refr_now(display);
            lv_timer_resume(refresh_timer);
            lv_timer_ready(refresh_timer);
            if (input_was_running)
                lv_timer_resume(input_timer);
        }
    }
    mpp_fb_close(fb);
    return ok;
}

/* LVGL 的内置 REFR_REQUEST 回调会唤醒刷新定时器；在它之后再次暂停，防止意外请求抢回显示。 */
static void refresh_request(lv_event_t *event)
{
    (void)event;
    if (paused)
        lv_timer_pause(refresh_timer);
}

static void bench_poll(lv_timer_t *timer)
{
    (void)timer;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    const request_t request = pending;
    if (request == REQUEST_NONE)
    {
        rt_mutex_release(&state_lock);
        return;
    }
    /* 锁内完成状态切换，shell 的 status 不会把排队中误报为已接管。仅 UI owner 调用 LVGL。 */
    const bool ok = apply_request(request);
    pending = REQUEST_NONE;
    const bool active = paused;
    rt_mutex_release(&state_lock);
    if (ok)
        LOG_I("display=%s%s", active ? "paused" : "app", request == REQUEST_FB ? " framebuffer=0" : "");
    else
        LOG_E("display request failed; display=%s", active ? "paused" : "app");
}

void dlab_display_bench_init(void)
{
    if (started)
        return;
    display = lv_display_get_default();
    if (!display || rt_mutex_init(&state_lock, "dispbench", RT_IPC_FLAG_PRIO) != RT_EOK)
        return;
    if (!lv_timer_create(bench_poll, 30, NULL))
    {
        (void)rt_mutex_detach(&state_lock);
        return;
    }
    lv_display_add_event_cb(display, refresh_request, LV_EVENT_REFR_REQUEST, NULL);
    started = true;
}

static int dlab_display_msh(int argc, char **argv)
{
    if (!started)
    {
        rt_kprintf("display bench unavailable\n");
        return -RT_ERROR;
    }
    request_t request = REQUEST_NONE;
    if (argc == 2 && !strcmp(argv[1], "pause"))
        request = REQUEST_PAUSE;
    else if (argc == 2 && !strcmp(argv[1], "fb"))
        request = REQUEST_FB;
    else if (argc == 2 && !strcmp(argv[1], "resume"))
        request = REQUEST_RESUME;
    else if (argc != 1)
    {
        rt_kprintf("usage: hmi_display [pause|fb|resume]\n");
        return -RT_ERROR;
    }
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    const bool busy = pending != REQUEST_NONE;
    if (request != REQUEST_NONE && !busy)
        pending = request;
    const bool active = paused;
    const bool queued = pending != REQUEST_NONE;
    rt_mutex_release(&state_lock);
    rt_kprintf("display=%s pending=%u%s\n", active ? "paused" : "app", queued ? 1u : 0u,
               request != REQUEST_NONE ? (busy ? " busy; retry" : "; wait for display result") : "");
    return request != REQUEST_NONE && busy ? -RT_ERROR : RT_EOK;
}
MSH_CMD_EXPORT_ALIAS(dlab_display_msh, hmi_display, Temporarily hand display to AiPQ);
