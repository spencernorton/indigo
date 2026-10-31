#!/usr/bin/env python3
"""Compare shared rainbow samples with the former emitter's complete stream."""
import hashlib
import os
import sys
from test_frame_budget import HERE, measure

REFERENCE_SHA = "e906a00fb5ec2da18a147efbd2f071b712fa4c24556160e4668f38a2809b227e"


def driver() -> str:
    reference = (HERE / "fixtures/jewel_spokes.reference.c").read_text(encoding="utf-8")
    assert hashlib.sha256(reference.encode()).hexdigest() == REFERENCE_SHA
    source = (HERE / "frame_budget.c").read_text(encoding="utf-8")
    hooks = r'''
#include <assert.h>
typedef struct { unsigned kind, count; float xyz[3]; u8 rgba[4]; } sample_t;
static sample_t samples[2][2048];
static unsigned sampleCount, lane, spokeExpCalls;
static bool sampling;
static void sampleBegin(unsigned type, unsigned fmt, unsigned count)
{
    if(!sampling) return;
    assert(sampleCount < 2048);
    samples[lane][sampleCount++] = (sample_t){.kind = type + 1u, .count = count + fmt * 65536u};
}
static void samplePosition(float x, float y, float z)
{
    if(!sampling) return;
    assert(sampleCount < 2048);
    samples[lane][sampleCount++] = (sample_t){.xyz = {x,y,z}};
}
static void sampleColor(u8 r, u8 g, u8 b, u8 a)
{
    if(!sampling) return;
    assert(sampleCount && samples[lane][sampleCount - 1].kind == 0);
    u8 c[] = {r,g,b,a};
    memcpy(samples[lane][sampleCount - 1].rgba, c, sizeof(c));
}
'''
    source = source.replace("static frameCost_t cost;", "static frameCost_t cost;" + hooks, 1)
    source = source.replace("cost.trigCalls++; return expf(x);", "cost.trigCalls++; spokeExpCalls++; return expf(x);", 1)
    source = source.replace('#include "ui_jewel_case.c"', '#include "ui_jewel_case.c"\n' + reference.replace("drawSpokes(", "referenceSpokes(", 1), 1)
    source = source.replace("void GX_Position3f32(float x, float y, float z)\n{", "void GX_Position3f32(float x, float y, float z)\n{\n samplePosition(x,y,z);", 1)
    source = source.replace("void GX_Color4u8(u8 r, u8 g, u8 b, u8 a)\n{", "void GX_Color4u8(u8 r, u8 g, u8 b, u8 a)\n{\n sampleColor(r,g,b,a);", 1)
    marker = "void GX_Begin(u8 type, u8 format, u16 count)\n{"
    assert marker in source
    source = source.replace(marker, marker + "\n sampleBegin(type,format,count);", 1)
    source = source.replace("int main(void)", "int frameBudgetMain(void)", 1)
    return source + r'''
int main(void)
{
    assert(UIJewelCase_Prepare());
    unsigned scenarios = 0, visible = 0;
    for(unsigned i = 0; i < 1024; i++) {
        Mtx x, y, mv;
        guMtxRotRad(x, 'x', ((int)(i % 17) - 8) * .17f);
        guMtxRotRad(y, 'y', ((int)((i / 17) % 19) - 9) * .36f);
        guMtxConcat(x,y,mv);
        float sc = i & 1 ? .65f : 1.7f;
        for(int row = 0; row < 3; row++) for(int col = 0; col < 3; col++) mv[row][col] *= sc;
        mv[0][3] = ((int)(i % 7) - 3) * 37.0f;
        mv[1][3] = ((int)(i % 5) - 2) * 29.0f;
        mv[2][3] = i & 2 ? -320.0f : -850.0f;
        float z = i & 4 ? -1.2f : -2.4f, nz = i & 4 ? 1.0f : -1.0f;
        const float fades[] = {0,.01f,.35f,1};
        float fade = fades[(i / 8) % 4];
        unsigned counts[2], exps[2];
        memset(samples,0,sizeof(samples));
        for(lane = 0; lane < 2; lane++) {
            sampleCount = spokeExpCalls = 0;
            sampling = true;
            if(lane) drawSpokes(mv,z,nz,fade);
            else referenceSpokes(mv,z,nz,fade);
            sampling = false;
            counts[lane] = sampleCount;
            exps[lane] = spokeExpCalls;
        }
        assert(counts[0] == counts[1]);
        assert(memcmp(samples[0],samples[1],counts[0]*sizeof(sample_t)) == 0);
        if(counts[0]) {
            assert(exps[0] == 832 && exps[1] == 520);
            visible++;
        } else assert(!exps[0] && !exps[1]);
        scenarios++;
    }
    assert(visible > 250);
    printf("{\"scene\":\"rainbow-shared-samples\",\"scenarios\":%u,\"visible\":%u}\n",scenarios,visible);
    return 0;
}
'''


if __name__ == "__main__":
    if "--sanitize" in sys.argv[1:]:
        os.environ["CC"] = os.environ.get("CC", "cc") + " -fsanitize=address,undefined -fno-omit-frame-pointer"
        if sys.platform.startswith("linux"):
            os.environ["CC"] += " -no-pie"
    result = measure(driver_source=driver())["rainbow-shared-samples"]
    assert result["scenarios"] == 1024
    print("rainbow samples: 1024 exact streams; 832 to 520 exponentials per visible disc")
