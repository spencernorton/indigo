#!/usr/bin/env python3
"""Decode the target-baked texture and reject incomplete publication."""
import os
import sys
from test_frame_budget import HERE, measure


def driver() -> str:
    source = (HERE / "frame_budget.c").read_text(encoding="utf-8")
    hooks = r'''
#include <assert.h>
#include <stdlib.h>
#include <zlib.h>
static unsigned allocationCall, failAllocation, liveAllocations;
static void *assetMalloc(size_t bytes)
{
    if(++allocationCall == failAllocation) return NULL;
    void *p = malloc(bytes); if(p) liveAllocations++; return p;
}
static void *assetCalloc(size_t count, size_t bytes)
{
    if(++allocationCall == failAllocation) return NULL;
    void *p = calloc(count,bytes); if(p) liveAllocations++; return p;
}
static void *assetMemalign(size_t align, size_t bytes)
{
    if(++allocationCall == failAllocation) return NULL;
    void *p = NULL; if(posix_memalign(&p,align,bytes)) return NULL; liveAllocations++; return p;
}
static void *assetRealloc(void *old, size_t bytes)
{
    if(++allocationCall == failAllocation) return NULL;
    void *p = realloc(old,bytes); if(p && !old) liveAllocations++; return p;
}
static void assetFree(void *p)
{
    if(p) { assert(liveAllocations); liveAllocations--; } free(p);
}
#define malloc assetMalloc
#define calloc assetCalloc
#define realloc assetRealloc
#define memalign assetMemalign
#define free assetFree
'''
    source = source.replace('#include "ui_jewel_case.c"', hooks + '\n#include "ui_jewel_case.c"\n#undef malloc\n#undef calloc\n#undef realloc\n#undef memalign\n#undef free', 1)
    source = source.replace('int main(void)', 'int frameBudgetMain(void)', 1)
    return source + r'''
static void checkGuards(const u8 *bytes)
{
    for(unsigned i = 0; i < 32; i++) assert(bytes[i] == 0xa5 && bytes[21792 + i] == 0xa5);
}
static void cleanTest(void)
{
    releaseAll();
    prepared = recorded = recordFailed = false;
    assert(liveAllocations == 0 && !UIJewelCase_Ready());
}
int main(void)
{
    u8 guarded[21760 + 64], original[21760], corrupt[sizeof(jewelMatcapPacked) + 1];
    memset(guarded,0xa5,sizeof(guarded));
    u8 *out = guarded + 32;
    assert(decodeMatcap(out,21760,jewelMatcapPacked,sizeof(jewelMatcapPacked)));
    checkGuards(guarded);
    assert(crc32(0,out,21760) == JEWEL_MATCAP_CRC32);
    memcpy(original,out,sizeof(original));
    assert(!decodeMatcap(NULL,21760,jewelMatcapPacked,sizeof(jewelMatcapPacked)));
    assert(!decodeMatcap(out,21760,NULL,sizeof(jewelMatcapPacked)));
    assert(!decodeMatcap(out,21759,jewelMatcapPacked,sizeof(jewelMatcapPacked)));
    assert(!decodeMatcap(out,21761,jewelMatcapPacked,sizeof(jewelMatcapPacked)));
    for(unsigned n = 0; n < sizeof(jewelMatcapPacked); n++) {
        memset(guarded,0xa5,sizeof(guarded));
        assert(!decodeMatcap(out,21760,jewelMatcapPacked,n));
        checkGuards(guarded);
    }
    memcpy(corrupt,jewelMatcapPacked,sizeof(jewelMatcapPacked));
    corrupt[sizeof(jewelMatcapPacked)] = 0;
    assert(!decodeMatcap(out,21760,corrupt,sizeof(corrupt)));
    for(unsigned i = 0; i < sizeof(jewelMatcapPacked); i += 31) {
        corrupt[i] ^= 0x41;
        memset(guarded,0xa5,sizeof(guarded));
        if(decodeMatcap(out,21760,corrupt,sizeof(jewelMatcapPacked)))
            assert(memcmp(out,original,sizeof(original)) == 0);
        checkGuards(guarded);
        corrupt[i] ^= 0x41;
    }
    allocationCall = failAllocation = 0;
    assert(UIJewelCase_Prepare() && UIJewelCase_Ready());
    unsigned calls = allocationCall;
    assert(calls > 20 && calls < 100);
    assert(memcmp(matcapData,original,sizeof(original)) == 0);
    assert(((uintptr_t)matcapData & 31u) == 0);
    assert(UIJewelCase_Prepare() && allocationCall == calls);
    cleanTest();
    for(unsigned i = 1; i <= calls; i++) {
        allocationCall = 0; failAllocation = i;
        assert(!UIJewelCase_Prepare());
        assert(!UIJewelCase_Ready() && liveAllocations == 0);
        allocationCall = failAllocation = 0;
        assert(UIJewelCase_Prepare() && UIJewelCase_Ready());
        assert(memcmp(matcapData,original,sizeof(original)) == 0);
        cleanTest();
    }
    printf("{\"scene\":\"jewel-matcap\",\"truncations\":%u,\"allocation_failures\":%u}\n",(unsigned)sizeof(jewelMatcapPacked),calls);
    return 0;
}
'''


if __name__ == "__main__":
    if "--sanitize" in sys.argv[1:]:
        os.environ["CC"] = os.environ.get("CC", "cc") + " -fsanitize=address,undefined -fno-omit-frame-pointer"
        if sys.platform.startswith("linux"):
            os.environ["CC"] += " -no-pie"
    result = measure(driver_source=driver())["jewel-matcap"]
    assert result["truncations"] == 12726
    print(f"matcap: exact decode, {result['truncations']} truncations and "
          f"{result['allocation_failures']} allocation-failure/retry paths passed")
