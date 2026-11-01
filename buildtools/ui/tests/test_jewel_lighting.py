#!/usr/bin/env python3
"""Verify exact shared lighting values, interpolation and failure recovery."""
import hashlib
import os
import sys
from test_frame_budget import HERE, measure

REFERENCE_SHA = "93b3e3b02f8557e311d7cc3c0599b7a85ae86ae89a8665a3539719ea66bee232"


def driver() -> str:
    source = (HERE / "frame_budget.c").read_text()
    reference = (HERE / "fixtures/jewel_lighting.reference.c").read_text()
    assert hashlib.sha256(reference.encode()).hexdigest() == REFERENCE_SHA
    hooks = r'''
#include <assert.h>
#include <malloc.h>
static unsigned allocationCall, failAllocation, liveAllocations;
static void *assetMalloc(size_t n) { if(++allocationCall==failAllocation)return NULL;void *p=malloc(n);if(p)liveAllocations++;return p; }
static void *assetCalloc(size_t n,size_t b) { if(++allocationCall==failAllocation)return NULL;void *p=calloc(n,b);if(p)liveAllocations++;return p; }
static void *assetMemalign(size_t a,size_t n) { if(++allocationCall==failAllocation)return NULL;void *p=memalign(a,n);if(p)liveAllocations++;return p; }
static void *assetRealloc(void *old,size_t n) { if(++allocationCall==failAllocation)return NULL;void *p=realloc(old,n);if(p&&!old)liveAllocations++;return p; }
static void assetFree(void *p) { if(p){assert(liveAllocations);liveAllocations--;}free(p); }
#define malloc assetMalloc
#define calloc assetCalloc
#define realloc assetRealloc
#define memalign assetMemalign
#define free assetFree
'''
    source = source.replace('#include "ui_jewel_case.c"', hooks + '\n#include "ui_jewel_case.c"\n#undef malloc\n#undef calloc\n#undef realloc\n#undef memalign\n#undef free\n' + reference, 1)
    source = source.replace("int main(void)", "int frameBudgetMain(void)", 1)
    return source + r'''
static void cleanTest(void) {
 releaseAll();prepared=recorded=recordFailed=false;
 assert(!liveAllocations&&!lightingIndices&&!lightingCount&&!roomTable&&!studioTable);
}
static void synthetic(void) {
 assert(!roomTable&&!studioTable&&!lightingIndices);
 roomTable=assetMalloc(ROOM_ENTRIES*sizeof(v3));studioTable=assetMalloc(ROOM_ENTRIES*sizeof(v3));
 assert(roomTable&&studioTable);
 u32 words[8][3]={{0}};
 for(unsigned i=0;i<8;i++)words[i][0]=(i+1)<<16;
 /* Several distinct vectors collide in the actual 65536-slot hash. */
 for(unsigned i=1;i<8;i++)assert((lightingHash((const v3 *)words[i])&65535)==(lightingHash((const v3 *)words[0])&65535));
 words[0][0]=0;words[1][0]=0x80000000u;words[2][0]=0x7fc00001u;words[3][0]=0x7fc00002u;
 for(unsigned i=0;i<ROOM_ENTRIES;i++) {
  memcpy(roomTable+i,words[(i*3)%8],12);memcpy(studioTable+i,words[(i+5)%8],12);
 }
}
static void compareTables(bool lookups) {
 v3 *room=malloc(ROOM_ENTRIES*sizeof(v3)),*studioRef=malloc(ROOM_ENTRIES*sizeof(v3));
 assert(room&&studioRef);memcpy(room,roomTable,ROOM_ENTRIES*sizeof(v3));memcpy(studioRef,studioTable,ROOM_ENTRIES*sizeof(v3));
 assert(packLighting());assert(lightingIndices&&roomTable==studioTable&&lightingCount<2*ROOM_ENTRIES);
 for(unsigned i=0;i<ROOM_ENTRIES;i++) {
  assert(lightingIndices[i]<lightingCount&&lightingIndices[i+ROOM_ENTRIES]<lightingCount);
  assert(!memcmp(room+i,roomTable+lightingIndices[i],sizeof(v3)));
  assert(!memcmp(studioRef+i,studioTable+lightingIndices[i+ROOM_ENTRIES],sizeof(v3)));
 }
 unsigned before=allocationCall;
 if(lookups) {
  const v3 edge[]={{0,0,0},{NAN,NAN,NAN},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,1,1},{-1,1,1},{1,-1,-1},{1e-20f,-1e-20f,1e-20f}};
  for(unsigned i=0;i<4096+sizeof(edge)/sizeof(edge[0]);i++) {
   v3 d=i<4096?V3(((int)(i%127)-63)/17.0f,((int)((i*7)%139)-69)/19.0f,((int)((i*13)%149)-74)/23.0f):edge[i-4096];
   v3 a=roomT(d),b=referenceLightingLookup(room,d);assert(!memcmp(&a,&b,sizeof(a)));
   a=studio(d);b=referenceLightingLookup(studioRef,d);assert(!memcmp(&a,&b,sizeof(a)));
  }
 }
 assert(allocationCall==before);cleanTest();free(room);free(studioRef);
}
int main(void) {
 synthetic();compareTables(false);
 synthetic();allocationCall=0;assert(packLighting());unsigned packCalls=allocationCall;assert(packCalls==3);cleanTest();
 for(unsigned f=1;f<=packCalls;f++) {
  allocationCall=failAllocation=0;synthetic();v3 *a=roomTable,*b=studioTable;
  allocationCall=0;failAllocation=f;assert(!packLighting());
  assert(roomTable==a&&studioTable==b&&!lightingIndices&&!lightingCount&&liveAllocations==2);
  allocationCall=failAllocation=0;compareTables(false);
 }
 allocationCall=failAllocation=0;assert(buildRoomTable());compareTables(true);
 allocationCall=failAllocation=0;assert(UIJewelCase_Prepare()&&UIJewelCase_Ready());
 unsigned allCalls=allocationCall;assert(allCalls>40&&allCalls<90);cleanTest();
 for(unsigned f=1;f<=allCalls;f++) {
  allocationCall=0;failAllocation=f;assert(!UIJewelCase_Prepare());assert(!UIJewelCase_Ready()&&!liveAllocations);
  allocationCall=failAllocation=0;assert(UIJewelCase_Prepare()&&UIJewelCase_Ready());cleanTest();
 }
 printf("{\"scene\":\"jewel-lighting\",\"pack_failures\":%u,\"prepare_failures\":%u,\"lookups\":8216}\n",packCalls,allCalls);
 return 0;
}
'''


if __name__ == "__main__":
    if "--sanitize" in sys.argv:
        os.environ["CC"] = os.environ.get("CC", "cc") + " -fsanitize=address,undefined -fno-omit-frame-pointer"
        if sys.platform.startswith("linux"):
            os.environ["CC"] += " -no-pie"
    row = measure(driver_source=driver())["jewel-lighting"]
    print(f"lighting: exact vectors, {row['lookups']} exact lookups, {row['pack_failures']} pack and {row['prepare_failures']} preparation failures/retries passed")
