#ifndef DLAB_CCM_H
#define DLAB_CCM_H
#include <stdbool.h>
#include <stdint.h>

/* 显示引擎（DE）的色彩矩阵（CCM）：3x3 增益 + 3x1 偏移，表序
   [C00 C01 C02 OFFSET0 C10 C11 C12 OFFSET1 C20 C21 C22 OFFSET2]。
   增益是 12 位有符号数、8 位小数（0x100 = 1.0），偏移是 9 位有符号整数，单位是输出码值。
   硬件按“(C0*R + C1*G + C2*B) / 256 + OFFSET”求值并截断到 0..255，作用在混合后的整幅画面上
   （含 UI 图层）。D13x 上电时就是单位阵且已使能，写单位阵等于什么都不做。 */
enum
{
    DLAB_CCM_TAPS = 12,
    DLAB_CCM_UNITY = 0x100,
    DLAB_CCM_SATURATION_MIN = 50,
    DLAB_CCM_SATURATION_NEUTRAL = 100,
    DLAB_CCM_SATURATION_MAX = 200
};

/** @brief 写入单位阵：等同 SDK 默认表，不做任何校正。 */
void dlab_ccm_identity(int32_t table[DLAB_CCM_TAPS]);

/** @brief 表是否为单位阵（用来区分“未校正”和“已校正”）。 */
bool dlab_ccm_is_identity(const int32_t table[DLAB_CCM_TAPS]);

/** @brief 生成饱和度矩阵：out = s*C + (1-s)*亮度，亮度用 Rec.709 权重。
 *  percent 为 100 时逐字等于单位阵，大于 100 加深混色饱和度（对抗面板发淡），小于 100 变淡；
 *  纯黑、纯白与灰不变（每行系数和恒为 256，偏移为 0）。percent 超出 [50, 200] 返回 false。
 *  只用四则运算，不依赖 libm。 */
bool dlab_ccm_from_saturation(unsigned percent, int32_t table[DLAB_CCM_TAPS]);

/** @brief 把外部写法归一进字段：增益按 12 位、偏移按 9 位取补码，返回带符号值。
 *  十六进制补码（0xFFE5）与负十进制（-27）会落到同一个字段值。 */
int32_t dlab_ccm_normalize(unsigned index, long value);
#endif
