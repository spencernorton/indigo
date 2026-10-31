#!/usr/bin/env python3
"""Exercise the real disc emitter's position texgen and vertex contract.

This checks local-coordinate mapping, not texture rasterization. Actual GX
image comparisons also qualify a renderer change.
"""
from test_frame_budget import HERE, measure


def driver() -> str:
    source = (HERE / "frame_budget.c").read_text(encoding="utf-8")
    hooks = r'''
#include <assert.h>
static Mtx printMatrix;
static unsigned texSource, texMatrix, texCount, texAttribute;
static unsigned mappedVertices;
static float testSpin;
static const float *testRect;
static void discClearDesc(void) { texAttribute = 0; stubStateCalls++; }
static void discSetDesc(unsigned attr, unsigned type)
{
    if(attr == GX_VA_TEX0) texAttribute = type;
    stubStateCalls++;
}
static void discTexgen(unsigned coord, unsigned type, unsigned from, unsigned matrix)
{
    if(coord == GX_TEXCOORD0) { texSource = from; texMatrix = matrix; }
    (void)type;
    stubStateCalls++;
}
static void discTexCount(unsigned count) { texCount = count; stubStateCalls++; }
static bool printedVertex(void) { return texCount == 1 && texMatrix == GX_TEXMTX2; }
static void checkMapping(float x, float y, float z)
{
    if(!printedVertex()) return;
    assert(texSource == GX_TG_POS && texAttribute == 0);
    /* Independent double-precision reference for the former (x,y) pair.
     * The actual GX position includes z; the matrix must ignore it. */
    double cs = cos((double)testSpin), sn = sin((double)testSpin);
    double expected[2] = {
        (testRect[0] + testRect[2]) * .5 +
            (cs*x - sn*y) * (testRect[2] - testRect[0]) / 120.0,
        (testRect[1] + testRect[3]) * .5 -
            (sn*x + cs*y) * (testRect[3] - testRect[1]) / 120.0
    };
    for(unsigned r = 0; r < 2; r++) {
        double actual = printMatrix[r][0]*x + printMatrix[r][1]*y +
            printMatrix[r][2]*z + printMatrix[r][3];
        assert(fabs(actual - expected[r]) < 2e-7);
    }
    mappedVertices++;
}
#undef GX_ClearVtxDesc
#undef GX_SetVtxDesc
#undef GX_SetTexCoordGen
#undef GX_SetNumTexGens
#define GX_ClearVtxDesc() discClearDesc()
#define GX_SetVtxDesc(a,t) discSetDesc(a,t)
#define GX_SetTexCoordGen(c,t,s,m) discTexgen(c,t,s,m)
#define GX_SetNumTexGens(n) discTexCount(n)
'''
    # Install hooks before including the real renderer. The other renderer
    # sources keep their ordinary stand-ins and are not called by this test.
    source = source.replace('#include "ui_jewel_case.c"', hooks + '\n#include "ui_jewel_case.c"', 1)
    old = 'void GX_LoadTexMtxImm(Mtx m, u32 index, u8 type) { (void)m; (void)index; (void)type; }'
    assert source.count(old) == 1
    source = source.replace(old, '''void GX_LoadTexMtxImm(Mtx m, u32 index, u8 type)
{ if(index == GX_TEXMTX2) memcpy(printMatrix, m, sizeof(Mtx)); (void)type; }''', 1)
    source = source.replace('void GX_Position3f32(float x, float y, float z)\n{',
                            'void GX_Position3f32(float x, float y, float z)\n{\n checkMapping(x,y,z);', 1)
    source = source.replace('void GX_TexCoord2f32(float s, float t) { (void)s; (void)t; }',
                            'void GX_TexCoord2f32(float s, float t) { assert(!printedVertex()); (void)s; (void)t; }', 1)
    source = source.replace('int main(void)', 'int frameBudgetMain(void)', 1)
    return source + r'''
int main(void)
{
    static GXTexObj texture;
    static const float crops[][4] = {
        {0,0,1,1}, {0,.125f,.75f,.875f}, {.2f,.05f,.8f,.95f},
        {.75f,.875f,0,.125f}
    };
    const float spins[] = {0, .37f, 1.5707963f, 3.1415927f, 6.1f};
    const float alphas[] = {0, .01f, .5f, 1};
    unsigned scenarios = 0;
    assert(UIJewelCase_Prepare());
    for(unsigned crop = 0; crop < 4; crop++)
    for(unsigned spin = 0; spin < 5; spin++)
    for(unsigned alpha = 0; alpha < 4; alpha++)
    for(unsigned back = 0; back < 2; back++)
    for(unsigned missing = 0; missing < 2; missing++) {
        uiJewelCaseShot_t shot = {.print = missing ? NULL : &texture,
            .printAlpha = alphas[alpha], .alpha = 1};
        shown_t shown = {.shot = &shot, .pose = {.fade = .7f}, .scale = 1};
        memcpy(shot.rect, crops[crop], sizeof(shot.rect));
        guMtxRotRad(shown.mv, 'y', back ? 2.7f : .4f);
        shown.mv[2][3] = -400;
        testSpin = jc.spin = spins[spin];
        testRect = shot.rect;
        mappedVertices = 0;
        texCount = 0;
        drawDisc(&shown);
        assert(mappedVertices == ((!missing && alpha && !back) ? 1666u : 0u));
        scenarios++;
    }
    printf("{\"scene\":\"disc-texgen\",\"scenarios\":%u}\n", scenarios);
    return 0;
}
'''


if __name__ == "__main__":
    result = measure(driver_source=driver())
    assert result["disc-texgen"]["scenarios"] == 320
    print("disc texgen: 320 spin/crop/fade/facing/artwork scenarios passed")
