#define LOG_TAG "dlab.testcard"
#define LOG_LVL LOG_LVL_INFO
#include "display_lab.h"
#include "testcard.h"
#include <finsh.h>
#include <lvgl.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>
#include <ulog.h>
/* SConscript 生成：DLAB_TESTCARD_EXTRA 是空格分隔的 0xRRGGBB 追加预置色，可为空串。 */
#include "dlab_config.h"

/* LVGL 由 UI owner 独占：MSH 命令跑在 shell 线程，只把请求放进单槽邮箱（短锁深拷贝），
   由 UI 线程里的 LVGL 定时器取走并绘制。连续两条命令来不及处理时保留最后一条，
   台架操作是人手节奏，这样比加队列简单。 */
#define POLL_MS 30

typedef enum
{
    REQUEST_NONE = 0,
    REQUEST_GRID,
    REQUEST_PATCH_RGB,
    REQUEST_PATCH_INDEX,
    REQUEST_NEXT,
    REQUEST_PREV,
    REQUEST_OFF,
} request_kind_t;

typedef struct
{
    request_kind_t kind;
    uint32_t value;
} request_t;

typedef struct
{
    dlab_testcard_mode_t mode;
    uint32_t rgb;
    unsigned index;
    unsigned page;
} applied_t;

static struct rt_mutex state_lock;
static bool started;
static request_t pending;
static applied_t applied;

static const char *mode_name(dlab_testcard_mode_t mode)
{
    switch (mode)
    {
    case DLAB_TESTCARD_MODE_GRID:
        return "grid";
    case DLAB_TESTCARD_MODE_PATCH:
        return "patch";
    default:
        return "off";
    }
}

/* UI 线程：在 lv_timer_handler() 里取走请求并作用到 LVGL，然后把实际状态抄一份给 shell 查询。 */
static void testcard_poll(lv_timer_t *timer)
{
    (void)timer;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    const request_t request = pending;
    pending.kind = REQUEST_NONE;
    rt_mutex_release(&state_lock);
    if (request.kind == REQUEST_NONE)
    {
        return;
    }
    bool accepted = true;
    switch (request.kind)
    {
    case REQUEST_GRID:
        dlab_testcard_show_grid();
        break;
    case REQUEST_PATCH_RGB:
        dlab_testcard_show_patch(request.value);
        break;
    case REQUEST_PATCH_INDEX:
        accepted = dlab_testcard_show_index(request.value);
        break;
    case REQUEST_NEXT:
        dlab_testcard_next();
        break;
    case REQUEST_PREV:
        dlab_testcard_prev();
        break;
    case REQUEST_OFF:
        dlab_testcard_off();
        break;
    default:
        accepted = false;
        break;
    }
    if (!accepted)
    {
        LOG_W("testcard request rejected");
        return;
    }
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    applied.mode = dlab_testcard_mode();
    applied.rgb = dlab_testcard_rgb();
    applied.index = dlab_testcard_index();
    applied.page = dlab_testcard_page();
    const applied_t state = applied;
    rt_mutex_release(&state_lock);
    LOG_I("testcard %s page=%u/%u rgb=0x%06X index=%u", mode_name(state.mode), state.page + 1u,
          dlab_testcard_page_count(), (unsigned)state.rgb, state.index);
}

static void post(request_kind_t kind, uint32_t value)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    pending.kind = kind;
    pending.value = value;
    rt_mutex_release(&state_lock);
}

/* 构建配置里的追加预置色：0x 前缀可省，非法项跳过并记日志。应用代码不调用色卡 API，
   台架需要的产品关键色只写在 Display Lab 的配置里。 */
static void load_extra_presets(void)
{
    char buffer[sizeof(DLAB_TESTCARD_EXTRA)];
    memcpy(buffer, DLAB_TESTCARD_EXTRA, sizeof(buffer));
    uint32_t colors[DLAB_TESTCARD_MAX_EXTRA];
    unsigned count = 0u;
    char *save = NULL;
    for (char *token = strtok_r(buffer, " \t", &save); token != NULL; token = strtok_r(NULL, " \t", &save))
    {
        char *end = NULL;
        const unsigned long value = strtoul(token, &end, 16);
        if ((end == token) || (*end != '\0') || (value > 0xFFFFFFul) || (count >= DLAB_TESTCARD_MAX_EXTRA))
        {
            LOG_W("testcard: extra preset '%s' skipped", token);
            continue;
        }
        colors[count++] = (uint32_t)value;
    }
    if (count > 0u)
    {
        (void)dlab_testcard_set_extra(colors, count);
    }
}

void dlab_testcard_shell_init(void)
{
    if (started)
    {
        return;
    }
    if (rt_mutex_init(&state_lock, "testcard", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        LOG_E("testcard: mutex init failed");
        return;
    }
    if (lv_timer_create(testcard_poll, POLL_MS, NULL) == NULL)
    {
        LOG_E("testcard: LVGL timer create failed");
        return;
    }
    load_extra_presets();
    started = true;
}

/* patch 参数：0x 前缀按 RGB888（最多 0xFFFFFF），纯十进制按预置序号。 */
static bool parse_patch(const char *token, uint32_t *rgb, unsigned *index)
{
    if ((token[0] == '0') && ((token[1] == 'x') || (token[1] == 'X')))
    {
        char *end = NULL;
        const unsigned long value = strtoul(token, &end, 16);
        if ((end == token) || (*end != '\0') || (value > 0xFFFFFFul))
        {
            return false;
        }
        *rgb = (uint32_t)value;
        *index = DLAB_TESTCARD_NO_INDEX;
        return true;
    }
    if (token[0] == '\0')
    {
        return false;
    }
    for (const char *c = token; *c != '\0'; ++c)
    {
        if ((*c < '0') || (*c > '9'))
        {
            return false;
        }
    }
    *index = (unsigned)strtoul(token, NULL, 10);
    *rgb = 0u;
    return true;
}

static void print_usage(void)
{
    rt_kprintf("usage: hmi_testcard                show state, presets and usage\n");
    rt_kprintf("       hmi_testcard grid           grid overview; next|prev flip pages\n");
    rt_kprintf("       hmi_testcard patch 0xRRGGBB show one full-screen colour\n");
    rt_kprintf("       hmi_testcard patch INDEX    show preset colour by index\n");
    rt_kprintf("       hmi_testcard next|prev      grid: page; patch: preset\n");
    rt_kprintf("       hmi_testcard off            close the test card\n");
}

static void print_state(void)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    const applied_t state = applied;
    rt_mutex_release(&state_lock);
    switch (state.mode)
    {
    case DLAB_TESTCARD_MODE_GRID:
        rt_kprintf("testcard=grid page=%u/%u\n", state.page + 1u, dlab_testcard_page_count());
        break;
    case DLAB_TESTCARD_MODE_PATCH:
        if (state.index != DLAB_TESTCARD_NO_INDEX)
        {
            rt_kprintf("testcard=patch #%06X index=%u\n", (unsigned)state.rgb, state.index);
        }
        else
        {
            rt_kprintf("testcard=patch #%06X free\n", (unsigned)state.rgb);
        }
        break;
    default:
        rt_kprintf("testcard=off\n");
        break;
    }
    const unsigned count = dlab_testcard_preset_count();
    rt_kprintf("presets=%u\n", count);
    for (unsigned i = 0u; i < count; ++i)
    {
        uint32_t rgb = 0u;
        (void)dlab_testcard_preset(i, &rgb);
        rt_kprintf("  %u 0x%06X\n", i, (unsigned)rgb);
    }
    print_usage();
}

static int dlab_testcard_msh(int argc, char **argv)
{
    if (!started)
    {
        rt_kprintf("testcard unavailable (display not started)\n");
        return -1;
    }
    if (argc == 1)
    {
        print_state();
        return 0;
    }
    if (argc == 2)
    {
        if (strcmp(argv[1], "grid") == 0)
        {
            post(REQUEST_GRID, 0u);
            return 0;
        }
        if (strcmp(argv[1], "next") == 0)
        {
            post(REQUEST_NEXT, 0u);
            return 0;
        }
        if (strcmp(argv[1], "prev") == 0)
        {
            post(REQUEST_PREV, 0u);
            return 0;
        }
        if (strcmp(argv[1], "off") == 0)
        {
            post(REQUEST_OFF, 0u);
            return 0;
        }
    }
    if ((argc == 3) && (strcmp(argv[1], "patch") == 0))
    {
        uint32_t rgb = 0u;
        unsigned index = DLAB_TESTCARD_NO_INDEX;
        if (parse_patch(argv[2], &rgb, &index))
        {
            if (index != DLAB_TESTCARD_NO_INDEX)
            {
                uint32_t preset = 0u;
                if (!dlab_testcard_preset(index, &preset))
                {
                    rt_kprintf("testcard: index %u out of range 0..%u\n", index,
                               dlab_testcard_preset_count() - 1u);
                    return -1;
                }
                post(REQUEST_PATCH_INDEX, index);
                rt_kprintf("testcard patch index %u 0x%06X\n", index, (unsigned)preset);
                return 0;
            }
            post(REQUEST_PATCH_RGB, rgb);
            rt_kprintf("testcard patch 0x%06X\n", (unsigned)rgb);
            return 0;
        }
    }
    print_usage();
    return -1;
}
MSH_CMD_EXPORT_ALIAS(dlab_testcard_msh, hmi_testcard, Display colour test card: grid or patch);
