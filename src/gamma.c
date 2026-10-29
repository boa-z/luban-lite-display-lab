#include "gamma.h"
#include <stddef.h>

#define LN2 0.69314718f

/* ln(x)，x 在 (0, 1]：先乘 2 归一到 [0.5, 1)，再用 2*atanh((x-1)/(x+1)) 的级数。 */
static float ln_unit(float x)
{
    int shift = 0;
    while (x < 0.5f)
    {
        x *= 2.0f;
        ++shift;
    }
    const float t = (x - 1.0f) / (x + 1.0f);
    const float t2 = t * t;
    float sum = 0.0f;
    float term = t;
    for (int k = 1; k <= 15; k += 2)
    {
        sum += term / (float)k;
        term *= t2;
    }
    return (2.0f * sum) - ((float)shift * LN2);
}

/* exp(y)，y <= 0：先加若干个 ln2 把 y 收进 [-ln2/2, 0]，泰勒展开后再除以 2^shift。 */
static float exp_neg(float y)
{
    int shift = 0;
    while (y < (-0.5f * LN2))
    {
        y += LN2;
        ++shift;
    }
    float term = 1.0f;
    float sum = 1.0f;
    for (int k = 1; k <= 10; ++k)
    {
        term *= y / (float)k;
        sum += term;
    }
    for (; shift > 0; --shift)
    {
        sum *= 0.5f;
    }
    return sum;
}

bool dlab_gamma_build(unsigned percent, uint32_t words[DLAB_GAMMA_WORDS])
{
    if ((words == NULL) || (percent < (unsigned)DLAB_GAMMA_PERCENT_MIN) ||
        (percent > (unsigned)DLAB_GAMMA_PERCENT_MAX))
    {
        return false;
    }
    const float exponent = (float)percent / 100.0f;
    for (unsigned w = 0u; w < (unsigned)DLAB_GAMMA_WORDS; ++w)
    {
        uint32_t packed = 0u;
        for (unsigned b = 0u; b < 4u; ++b)
        {
            const unsigned point = (w * 4u) + b;
            unsigned output = 0u;
            if (point > 0u)
            {
                const float position = (float)point / (float)(DLAB_GAMMA_POINTS - 1);
                const float scaled = 255.0f * exp_neg(exponent * ln_unit(position));
                /* 向下取整，与 SDK 默认表一致（250.95 取 250）；0.01 抵消浮点级数在整数点上的误差。 */
                output = (unsigned)(scaled + 0.01f);
                if (output > 255u)
                {
                    output = 255u;
                }
            }
            if (point == ((unsigned)DLAB_GAMMA_POINTS - 1u))
            {
                output = 255u; /* 末点钉在满量程，与 SDK 默认表一致 */
            }
            packed |= (uint32_t)output << (8u * b);
        }
        words[w] = packed;
    }
    return true;
}
