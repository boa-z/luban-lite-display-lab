#include "ccm.h"
#include <stddef.h>

/* Rec.709 亮度权重（sRGB 内容的亮度构成），与生成器共用。 */
static const float s_luma[3] = {0.2126f, 0.7152f, 0.0722f};

/* 四舍五入到最近整数（0.5 远离零），不依赖 libm。 */
static int32_t round_to_int(float value)
{
    return (int32_t)(value + ((value >= 0.0f) ? 0.5f : -0.5f));
}

void dlab_ccm_identity(int32_t table[DLAB_CCM_TAPS])
{
    for (unsigned i = 0u; i < (unsigned)DLAB_CCM_TAPS; ++i)
    {
        table[i] = 0;
    }
    table[0] = DLAB_CCM_UNITY;
    table[5] = DLAB_CCM_UNITY;
    table[10] = DLAB_CCM_UNITY;
}

bool dlab_ccm_is_identity(const int32_t table[DLAB_CCM_TAPS])
{
    if (table == NULL)
    {
        return false;
    }
    for (unsigned i = 0u; i < (unsigned)DLAB_CCM_TAPS; ++i)
    {
        const int32_t expected = ((i == 0u) || (i == 5u) || (i == 10u)) ? (int32_t)DLAB_CCM_UNITY : 0;
        if (table[i] != expected)
        {
            return false;
        }
    }
    return true;
}

bool dlab_ccm_from_saturation(unsigned percent, int32_t table[DLAB_CCM_TAPS])
{
    if ((table == NULL) || (percent < (unsigned)DLAB_CCM_SATURATION_MIN) ||
        (percent > (unsigned)DLAB_CCM_SATURATION_MAX))
    {
        return false;
    }
    const float saturation = (float)percent / 100.0f;
    for (unsigned row = 0u; row < 3u; ++row)
    {
        int32_t sum = 0;
        for (unsigned column = 0u; column < 3u; ++column)
        {
            if (column == row)
            {
                continue;
            }
            /* 反向混入亮度分量：(1 - s) * 权重，Q8 定点。 */
            const int32_t tap = round_to_int((1.0f - saturation) * s_luma[column] * 256.0f);
            table[(row * 4u) + column] = tap;
            sum += tap;
        }
        /* 对角项由同行反推，保证每行系数和恒为 256：白点严格不动。 */
        table[(row * 4u) + row] = (int32_t)DLAB_CCM_UNITY - sum;
        table[(row * 4u) + 3u] = 0;
    }
    return true;
}

int32_t dlab_ccm_normalize(unsigned index, long value)
{
    const bool offset = ((index == 3u) || (index == 7u) || (index == 11u));
    const unsigned bits = offset ? 9u : 12u;
    const long mask = (1L << bits) - 1L;
    long field = value & mask;
    if (field > (mask >> 1))
    {
        field -= (mask + 1L);
    }
    return (int32_t)field;
}
