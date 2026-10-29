#include "ccm.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

/* SDK 默认 CCM 表（bsp/artinchip/drv/display/disp_ccm.h）：恒等单位阵。 */
static const int32_t sdk_identity[DLAB_CCM_TAPS] = {0x100, 0x000, 0x000, 0x000, 0x000, 0x100,
                                                     0x000, 0x000, 0x000, 0x000, 0x100, 0x000};

/* Rec.709 亮度权重，与生成器一致。 */
static const double s_luma[3] = {0.2126, 0.7152, 0.0722};

int main(void)
{
    int32_t table[DLAB_CCM_TAPS];

    /* 单位阵必须与 SDK 默认表逐字一致，否则“不校正”就不是不校正。 */
    dlab_ccm_identity(table);
    for (unsigned i = 0u; i < (unsigned)DLAB_CCM_TAPS; ++i)
    {
        assert(table[i] == sdk_identity[i]);
    }
    assert(dlab_ccm_is_identity(table));
    assert(dlab_ccm_is_identity(sdk_identity));
    assert(!dlab_ccm_is_identity(NULL));
    table[1] = 1;
    assert(!dlab_ccm_is_identity(table));

    /* 100% 生成的矩阵必须逐字等于单位阵。 */
    assert(dlab_ccm_from_saturation((unsigned)DLAB_CCM_SATURATION_NEUTRAL, table));
    for (unsigned i = 0u; i < (unsigned)DLAB_CCM_TAPS; ++i)
    {
        assert(table[i] == sdk_identity[i]);
    }

    /* 115% 手算锚点：每行系数和必须是 256（白点不动），偏移为 0。 */
    assert(dlab_ccm_from_saturation(115u, table));
    assert(table[0] == 286 && table[1] == -27 && table[2] == -3 && table[3] == 0);
    assert(table[4] == -8 && table[5] == 267 && table[6] == -3 && table[7] == 0);
    assert(table[8] == -8 && table[9] == -27 && table[10] == 291 && table[11] == 0);

    /* 全范围内：与双精度参考比较、行和恒为 256、偏移为 0、系数落在 12 位有符号范围内。 */
    for (unsigned percent = (unsigned)DLAB_CCM_SATURATION_MIN;
         percent <= (unsigned)DLAB_CCM_SATURATION_MAX; ++percent)
    {
        assert(dlab_ccm_from_saturation(percent, table));
        const double saturation = (double)percent / 100.0;
        for (unsigned row = 0u; row < 3u; ++row)
        {
            int32_t sum = 0;
            for (unsigned column = 0u; column < 3u; ++column)
            {
                const int32_t tap = table[(row * 4u) + column];
                assert((tap >= -2048) && (tap <= 2047));
                if (column == row)
                {
                    continue;
                }
                const double expected = (1.0 - saturation) * s_luma[column] * 256.0;
                assert(fabs((double)tap - expected) <= 0.5001);
                sum += tap;
            }
            assert(table[(row * 4u) + row] == (256 - sum));
            assert(table[(row * 4u) + 3u] == 0);
        }
    }

    /* 方向：大于 100 让混色更饱和（反向系数为负），小于 100 变淡（反向系数为正）。 */
    assert(dlab_ccm_from_saturation(150u, table));
    assert((table[1] < 0) && (table[2] < 0));
    assert(dlab_ccm_from_saturation(50u, table));
    assert((table[1] > 0) && (table[2] > 0));

    /* 归一：十六进制补码与负十进制落到同一个字段值；增益按 12 位、偏移按 9 位。 */
    assert(dlab_ccm_normalize(1u, 0xFFE5L) == -27);
    assert(dlab_ccm_normalize(1u, -27L) == -27);
    assert(dlab_ccm_normalize(4u, 0x0FE5L) == -27);
    assert(dlab_ccm_normalize(3u, 0xFFE5L) == -27);
    assert(dlab_ccm_normalize(3u, 511L) == -1);
    assert(dlab_ccm_normalize(0u, 0x100L) == 256);

    /* 越界与空指针一律拒绝，且不写输出。 */
    table[0] = 1234;
    assert(!dlab_ccm_from_saturation((unsigned)DLAB_CCM_SATURATION_MIN - 1u, table) &&
           table[0] == 1234);
    assert(!dlab_ccm_from_saturation((unsigned)DLAB_CCM_SATURATION_MAX + 1u, table) &&
           table[0] == 1234);
    assert(!dlab_ccm_from_saturation(100u, NULL));

    printf("display ccm ok\n");
    return 0;
}
