#define LOG_TAG "dlab.color"
#define LOG_LVL LOG_LVL_INFO
#include "ccm.h"
#include "gamma.h"
#include <finsh.h>
#include <mpp_fb.h>
#include <rtthread.h>
#include <stdlib.h>
#include <string.h>
#include <ulog.h>

/* Display Lab 不读应用的配置：第一次执行 hmi_color 时记下显示引擎当时的 gamma 与 CCM，
   那就是应用开机加载的结果（例如 luban-lite-hmi 的 AIC_HMI_DISPLAY_GAMMA/CCM，或 SDK 默认单位阵）。
   ccm reset 回到这张表；之后的修改只在 RAM 里，断电不保留。命令只在 shell 线程执行，无需加锁。 */
static bool boot_taken;
static unsigned boot_percent;
static bool boot_ccm_valid;
static int32_t boot_ccm[DLAB_CCM_TAPS];
/* 0 表示当前 gamma 表不是由某个百分比生成的（应用写了自定义表）。 */
static unsigned active_percent = DLAB_GAMMA_PERCENT_NEUTRAL;

/* gamma 由 DE 全局控制寄存器使能，不挑图层；SDK 的色彩调整（亮度/对比度/饱和度）按文档只作用于
   视频层，改不到 LVGL 所在的 UI 层，所以实机色彩只能靠这张表（或 CCM）校准。三个通道用同一条曲线。 */
static bool dlab_color_apply(unsigned percent)
{
    uint32_t words[DLAB_GAMMA_WORDS];
    if (!dlab_gamma_build(percent, words))
    {
        return false;
    }
    struct mpp_fb *fb = mpp_fb_open();
    if (fb == NULL)
    {
        return false;
    }
    struct aicfb_gamma_config gamma;
    memset(&gamma, 0, sizeof(gamma));
    /* 先取当前配置，保留驱动已有的状态，只覆盖查找表与使能。 */
    (void)mpp_fb_ioctl(fb, AICFB_GET_GAMMA_CONFIG, &gamma);
    for (unsigned channel = 0u; channel < 3u; ++channel)
    {
        memcpy(gamma.gamma_lut[channel], words, sizeof(words));
    }
    gamma.enable = 1u;
    const bool ok = mpp_fb_ioctl(fb, AICFB_UPDATE_GAMMA_CONFIG, &gamma) == 0;
    mpp_fb_close(fb);
    if (ok)
    {
        active_percent = percent;
    }
    return ok;
}

/* 读回 gamma 表并反推百分比：未使能等于 DE 默认（100），三通道同一条且能由 50..300 中某个百分比
   生成就报那个百分比，否则报 0（自定义表）。只在第一次执行命令时跑一次。 */
static bool read_gamma_percent(unsigned *percent)
{
    struct mpp_fb *fb = mpp_fb_open();
    if (fb == NULL)
    {
        return false;
    }
    struct aicfb_gamma_config gamma;
    memset(&gamma, 0, sizeof(gamma));
    const bool ok = mpp_fb_ioctl(fb, AICFB_GET_GAMMA_CONFIG, &gamma) == 0;
    mpp_fb_close(fb);
    if (!ok)
    {
        return false;
    }
    *percent = DLAB_GAMMA_PERCENT_NEUTRAL;
    if (gamma.enable == 0u)
    {
        return true;
    }
    *percent = 0u;
    uint32_t words[DLAB_GAMMA_WORDS];
    for (unsigned candidate = DLAB_GAMMA_PERCENT_MIN; candidate <= DLAB_GAMMA_PERCENT_MAX; ++candidate)
    {
        (void)dlab_gamma_build(candidate, words);
        if ((memcmp(gamma.gamma_lut[0], words, sizeof(words)) == 0) &&
            (memcmp(gamma.gamma_lut[1], words, sizeof(words)) == 0) &&
            (memcmp(gamma.gamma_lut[2], words, sizeof(words)) == 0))
        {
            *percent = candidate;
            break;
        }
    }
    return true;
}

/* CCM 与 gamma 一样是 DE 全局模块（作用于混合后的整幅画面，含 UI 图层），上电默认单位阵且已使能。
   注意 CCM 没有影子寄存器，写入当场生效（台架上会看到瞬时跳变，可接受）。 */
static bool dlab_color_ccm_apply(const int32_t table[DLAB_CCM_TAPS])
{
    if (table == NULL)
    {
        return false;
    }
    struct mpp_fb *fb = mpp_fb_open();
    if (fb == NULL)
    {
        return false;
    }
    struct aicfb_ccm_config ccm;
    memset(&ccm, 0, sizeof(ccm));
    /* 先取当前配置，保留驱动已有状态，只覆盖系数与使能。 */
    (void)mpp_fb_ioctl(fb, AICFB_GET_CCM_CONFIG, &ccm);
    memcpy(ccm.ccm_table, table, sizeof(ccm.ccm_table));
    ccm.enable = 1u;
    const bool ok = mpp_fb_ioctl(fb, AICFB_UPDATE_CCM_CONFIG, &ccm) == 0;
    mpp_fb_close(fb);
    return ok;
}

static bool dlab_color_ccm_read(int32_t table[DLAB_CCM_TAPS], bool *enabled)
{
    if (table == NULL)
    {
        return false;
    }
    struct mpp_fb *fb = mpp_fb_open();
    if (fb == NULL)
    {
        return false;
    }
    struct aicfb_ccm_config ccm;
    memset(&ccm, 0, sizeof(ccm));
    const bool ok = mpp_fb_ioctl(fb, AICFB_GET_CCM_CONFIG, &ccm) == 0;
    mpp_fb_close(fb);
    if (!ok)
    {
        return false;
    }
    memcpy(table, ccm.ccm_table, sizeof(ccm.ccm_table));
    if (enabled != NULL)
    {
        *enabled = ccm.enable != 0u;
    }
    return true;
}

/* 解析一个 CCM 系数：0x 前缀、前导 0 或含 a..f 的写法按十六进制（0xFFE5 这种补码就是负值），
   其余按十进制。这样 0100 不会被 strtol 当成八进制，disp_ccm.h 里粘出来的值也能直接用。 */
static bool parse_ccm_tap(const char *token, long *value)
{
    bool hex = ((token[0] == '0') && ((token[1] == 'x') || (token[1] == 'X'))) ||
               ((token[0] == '0') && (token[1] != '\0'));
    for (const char *c = token; (*c != '\0') && !hex; ++c)
    {
        hex = ((*c >= 'a') && (*c <= 'f')) || ((*c >= 'A') && (*c <= 'F'));
    }
    char *end = NULL;
    const long parsed = strtol(token, &end, hex ? 16 : 10);
    if ((end == token) || (*end != '\0'))
    {
        return false;
    }
    *value = parsed;
    return true;
}

/* 第一次执行命令时记下开机表；显示设备还打不开时下次再试。 */
static void take_boot_snapshot(void)
{
    if (boot_taken || !read_gamma_percent(&boot_percent))
    {
        return;
    }
    active_percent = boot_percent;
    boot_ccm_valid = dlab_color_ccm_read(boot_ccm, NULL);
    boot_taken = true;
}

/* 台架调色：hmi_color 查看；hmi_color 120 调 gamma；hmi_color sat 115 或
   hmi_color ccm <r|g|b> <4 个系数> 逐行调 CCM。全部即时生效、断电不保留；reset 复原到开机表
   （第一次执行命令时读回的那张），identity 清成单位阵。
   之所以逐行设置而不是整表 12 个系数一条命令：板端 finsh 单行限 80 字符、参数限 10 个
   （FINSH_CMD_SIZE / FINSH_ARG_MAX），整表一条命令既超长也超参数数，会被截断误执行。 */
static void hmi_color_usage(void)
{
    rt_kprintf("usage: hmi_color                show gamma, ccm and the boot default table\n");
    rt_kprintf("       hmi_color 50..300        set gamma percent (100 = DE default)\n");
    rt_kprintf("       hmi_color sat 50..200    build ccm from saturation (100 = identity)\n");
    rt_kprintf("       hmi_color ccm            show the ccm table\n");
    rt_kprintf("       hmi_color ccm r|g|b C0 C1 C2 OFF  set one row (Q8 gain / 9-bit offset)\n");
    rt_kprintf("       hmi_color ccm reset      restore the boot default (read on first use)\n");
    rt_kprintf("       hmi_color ccm identity   clear all correction (identity table)\n");
    rt_kprintf("       taps: decimal, or hex with 0x/leading 0 (e.g. 0xFFD8 = -40)\n");
}

/* 一次解析一行（3 个增益 + 1 个偏移），写进整表的 row 行。 */
static bool parse_ccm_row(int row, char **tokens, int32_t table[DLAB_CCM_TAPS])
{
    for (unsigned column = 0u; column < 4u; ++column)
    {
        long value = 0;
        if (!parse_ccm_tap(tokens[column], &value))
        {
            return false;
        }
        const unsigned index = ((unsigned)row * 4u) + column;
        table[index] = dlab_ccm_normalize(index, value);
    }
    return true;
}

/* r/g/b 行名转行号；不是单个字母返回 -1。 */
static int ccm_row_of(const char *token)
{
    if ((token[0] == '\0') || (token[1] != '\0'))
    {
        return -1;
    }
    switch (token[0])
    {
    case 'r':
    case 'R':
        return 0;
    case 'g':
    case 'G':
        return 1;
    case 'b':
    case 'B':
        return 2;
    default:
        return -1;
    }
}

/* 12 个系数按 16 位补码打成 0x 形式，和 disp_ccm.h 的写法一致，方便来回粘贴。 */
static void print_ccm_taps(const int32_t table[DLAB_CCM_TAPS])
{
    for (unsigned i = 0u; i < (unsigned)DLAB_CCM_TAPS; ++i)
    {
        rt_kprintf(" 0x%04X", (unsigned)(uint16_t)table[i]);
    }
}

static void print_ccm(bool with_table)
{
    int32_t table[DLAB_CCM_TAPS];
    bool enabled = false;
    if (!dlab_color_ccm_read(table, &enabled))
    {
        rt_kprintf("display ccm: unavailable\n");
        return;
    }
    rt_kprintf("display ccm=%s, %s", enabled ? "on" : "off",
               dlab_ccm_is_identity(table) ? "identity" : "custom");
    if (with_table)
    {
        print_ccm_taps(table);
    }
    rt_kprintf("\n");
}

/* 回显开机表，用来确认应用是否带上了调好的矩阵；读不到时明确说 unknown。 */
static void print_ccm_default(void)
{
    if (!boot_ccm_valid)
    {
        rt_kprintf("display ccm default=unknown\n");
        return;
    }
    rt_kprintf("display ccm default=%s", dlab_ccm_is_identity(boot_ccm) ? "identity" : "custom");
    print_ccm_taps(boot_ccm);
    rt_kprintf("\n");
}

/* 百分比为 0 表示自定义表，不是由某个百分比生成的。 */
static void print_percent(unsigned percent)
{
    if (percent == 0u)
    {
        rt_kprintf("custom");
    }
    else
    {
        rt_kprintf("%u%%", percent);
    }
}

static int dlab_color_msh(int argc, char **argv)
{
    take_boot_snapshot();
    if ((argc >= 2) && (strcmp(argv[1], "ccm") == 0))
    {
        if (argc == 2)
        {
            print_ccm(true);
            print_ccm_default();
            return 0;
        }
        if ((argc == 3) && (strcmp(argv[2], "reset") == 0))
        {
            int32_t table[DLAB_CCM_TAPS];
            const bool configured = boot_ccm_valid;
            if (configured)
            {
                memcpy(table, boot_ccm, sizeof(table));
            }
            else
            {
                dlab_ccm_identity(table);
            }
            if (!dlab_color_ccm_apply(table))
            {
                rt_kprintf("display ccm: apply failed\n");
                return -1;
            }
            rt_kprintf("display ccm: reset to %s\n",
                       configured ? "boot default" : "identity (boot table unknown)");
            print_ccm(true);
            return 0;
        }
        if ((argc == 3) && (strcmp(argv[2], "identity") == 0))
        {
            int32_t table[DLAB_CCM_TAPS];
            dlab_ccm_identity(table);
            if (!dlab_color_ccm_apply(table))
            {
                rt_kprintf("display ccm: apply failed\n");
                return -1;
            }
            print_ccm(true);
            return 0;
        }
        /* 逐行设置：先读回当前整表，只覆盖选中行，再整体写回，其余行保持不动。 */
        const int row = (argc == 7) ? ccm_row_of(argv[2]) : -1;
        if (row >= 0)
        {
            int32_t table[DLAB_CCM_TAPS];
            if (!dlab_color_ccm_read(table, NULL))
            {
                rt_kprintf("display ccm: unavailable\n");
                return -1;
            }
            if (!parse_ccm_row(row, &argv[3], table) || !dlab_color_ccm_apply(table))
            {
                hmi_color_usage();
                return -1;
            }
            print_ccm(true);
            return 0;
        }
        hmi_color_usage();
        return -1;
    }
    if ((argc == 3) && (strcmp(argv[1], "sat") == 0))
    {
        const long percent = strtol(argv[2], NULL, 10);
        int32_t table[DLAB_CCM_TAPS];
        if ((percent < DLAB_CCM_SATURATION_MIN) || (percent > DLAB_CCM_SATURATION_MAX) ||
            !dlab_ccm_from_saturation((unsigned)percent, table) ||
            !dlab_color_ccm_apply(table))
        {
            hmi_color_usage();
            return -1;
        }
        print_ccm(true);
        return 0;
    }
    if (argc >= 2)
    {
        const long value = strtol(argv[1], NULL, 10);
        if ((value < DLAB_GAMMA_PERCENT_MIN) || (value > DLAB_GAMMA_PERCENT_MAX) ||
            !dlab_color_apply((unsigned)value))
        {
            hmi_color_usage();
            return -1;
        }
    }
    rt_kprintf("display gamma=");
    print_percent(active_percent);
    rt_kprintf(" (100 = DE default; boot default ");
    print_percent(boot_percent);
    rt_kprintf(")\n");
    print_ccm(false);
    print_ccm_default();
    return 0;
}
MSH_CMD_EXPORT_ALIAS(dlab_color_msh, hmi_color, Show or set display gamma and ccm);
