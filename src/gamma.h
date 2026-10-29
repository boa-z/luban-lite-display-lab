#ifndef DLAB_GAMMA_H
#define DLAB_GAMMA_H
#include <stdbool.h>
#include <stdint.h>

/* 显示引擎（DE）的 gamma 查找表：每通道 64 个 8 位采样点，一个 32 位字装 4 个点（低字节在前）。
   64 个点均匀铺在输入 0..1 上（第 i 点位置 i/63）。SDK 默认表（255*i/63 取整）就是 percent = 100 的结果。 */
enum
{
    DLAB_GAMMA_POINTS = 64,
    DLAB_GAMMA_WORDS = 16,
    DLAB_GAMMA_PERCENT_MIN = 50,
    DLAB_GAMMA_PERCENT_NEUTRAL = 100,
    DLAB_GAMMA_PERCENT_MAX = 300
};

/** @brief 生成指数为 percent/100 的逐通道 gamma 表：out = 255 * (in/255)^(percent/100)。
 *  percent 大于 100 压暗中间调、使混色更饱和，纯黑、纯白与原色不变；超出 [50, 300] 返回 false。
 *  不依赖 libm，固件无需额外链接数学库。 */
bool dlab_gamma_build(unsigned percent, uint32_t words[DLAB_GAMMA_WORDS]);
#endif
