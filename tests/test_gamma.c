#include "gamma.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

/* SDK 默认 gamma 表（bsp/artinchip/drv/display/disp_gamma.h 的 gam_lut[0]）：恒等曲线。 */
static const uint32_t sdk_identity[DLAB_GAMMA_WORDS] = {
    0x0C080400u, 0x1C181410u, 0x2C282420u, 0x3C383430u, 0x4C484440u, 0x5D595550u, 0x6D696561u, 0x7D797571u,
    0x8D898581u, 0x9D999591u, 0xAEAAA5A1u, 0xBEBAB6B2u, 0xCECAC6C2u, 0xDEDAD6D2u, 0xEEEAE6E2u, 0xFFFAF6F2u};

static unsigned point(const uint32_t *words, unsigned index)
{
    return (words[index / 4u] >> (8u * (index % 4u))) & 0xFFu;
}

int main(void)
{
    uint32_t words[DLAB_GAMMA_WORDS];

    /* percent = 100 必须逐字等于 SDK 默认表，否则“不校准”就不是不校准。 */
    assert(dlab_gamma_build(100u, words));
    for (unsigned i = 0u; i < DLAB_GAMMA_WORDS; ++i)
    {
        assert(words[i] == sdk_identity[i]);
    }

    /* 范围外与空指针一律拒绝，且不写输出。 */
    words[0] = 0x12345678u;
    assert(!dlab_gamma_build(DLAB_GAMMA_PERCENT_MIN - 1u, words) && words[0] == 0x12345678u);
    assert(!dlab_gamma_build(DLAB_GAMMA_PERCENT_MAX + 1u, words));
    assert(!dlab_gamma_build(100u, NULL));

    /* 与 libm 的 pow 对比：整个允许范围内每个点误差不超过 1（向下取整），两端钉死，曲线单调不减。 */
    for (unsigned percent = DLAB_GAMMA_PERCENT_MIN; percent <= DLAB_GAMMA_PERCENT_MAX; ++percent)
    {
        assert(dlab_gamma_build(percent, words));
        assert(point(words, 0u) == 0u && point(words, DLAB_GAMMA_POINTS - 1u) == 255u);
        unsigned previous = 0u;
        for (unsigned i = 0u; i < DLAB_GAMMA_POINTS; ++i)
        {
            const double expected =
                255.0 * pow((double)i / (double)(DLAB_GAMMA_POINTS - 1u), (double)percent / 100.0);
            const unsigned got = point(words, i);
            assert(fabs((double)got - expected) <= 1.02);
            assert(got >= previous);
            previous = got;
        }
    }

    /* 大于 100 压暗中间调：中间点（第 32 点，位置约 0.51）必须低于恒等表；小于 100 则相反。 */
    assert(dlab_gamma_build(120u, words));
    assert(point(words, 32u) < point(sdk_identity, 32u));
    assert(dlab_gamma_build(80u, words));
    assert(point(words, 32u) > point(sdk_identity, 32u));

    printf("display gamma ok\n");
    return 0;
}
