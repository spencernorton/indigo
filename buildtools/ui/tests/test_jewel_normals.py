#!/usr/bin/env python3
"""Validate exact mesh packing, the real GX index stream and CPU glints."""
import hashlib
import os
import sys
from test_frame_budget import HERE, measure
REFERENCE_SHA = "6abfc95c933d76d8d165eee2abc113326ab815d58947f6bf2673742e5ddd25df"


def driver() -> str:
    source = (HERE / "frame_budget.c").read_text()
    reference = (HERE / "fixtures/jewel_glints.reference.c").read_text()
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
    source = source.replace('#include "ui_jewel_case.c"', hooks + '\n#include "ui_jewel_case.c"\n#undef malloc\n#undef calloc\n#undef realloc\n#undef memalign\n#undef free\n' + reference.replace("findGlints(", "referenceGlints(", 1), 1)
    source = source.replace("void GX_Position1x16(u16 index) { (void)index; emitted++; cost.vertices++; }", "void GX_Position1x16(u16 index) { checkPosition(index); emitted++; cost.vertices++; }", 1)
    source = source.replace("void GX_Normal1x16(u16 index) { (void)index; }", "void GX_Normal1x16(u16 index) { checkNormal(index); }", 1)
    callbacks = r'''
static const rodmesh_t *checked;
static const float *originalNormals, *originalPositions;
static const u16 *expectedIndices;
static unsigned expectedCount, cursor;
static u16 originalIndex;
static void checkPosition(u16 i) {
 if(!checked)return;
 assert(cursor<expectedCount);originalIndex=expectedIndices[cursor];
 assert(i<checked->positionCount);
 assert(memcmp(checked->pos+i*3,originalPositions+originalIndex*3,12)==0);
}
static void checkNormal(u16 i) {
 if(!checked)return;
 assert(cursor<expectedCount);assert(i<checked->normalCount);
 assert(memcmp(checked->nrm+i*3,originalNormals+originalIndex*3,12)==0);cursor++;
}
'''
    source = source.replace("/* ---- display lists:", callbacks + "\n/* ---- display lists:", 1)
    source = source.replace("int main(void)", "int frameBudgetMain(void)", 1)
    return source + r'''
static rodmesh_t synthetic(void) {
 rodmesh_t m={.count=16,.cap=16,.glintEnd=4};
 m.pos=assetCalloc(16,12);m.nrm=assetMalloc(16*12);m.prims=assetMalloc(18*2);m.primLen=m.primCap=18;
 assert(m.pos&&m.nrm&&m.prims);m.prims[0]=GX_TRIANGLESTRIP;m.prims[1]=16;
 u32 words[8][3];unsigned n=0;
 for(u32 k=1;n<8;k++){u32 v[3]={k,0,0};if((rodVectorHash((const float *)v)&31)==0)memcpy(words[n++],v,12);}
 /* Include signed zero and distinct NaN payloads in an exact-bit fixture. */
 words[0][0]=0;words[1][0]=0x80000000u;words[2][0]=0x7fc00001u;words[3][0]=0x7fc00002u;
 for(int i=0;i<16;i++){
  memcpy(m.nrm+i*3,words[i%8],12);memcpy(m.pos+i*3,words[(i*3)%8],12);m.prims[i+2]=i;
 }
 return m;
}
static void compareMesh(rodmesh_t *m) {
 int count=m->count,glints=m->glintEnd,primLen=m->primLen;
 float *normals=malloc(count*12),*positions=malloc(count*12);assert(normals&&positions);
 memcpy(normals,m->nrm,count*12);memcpy(positions,m->pos,count*12);
 u16 *indices=malloc(primLen*2);assert(indices);unsigned n=0;
 for(int p=0;p<primLen;p+=m->prims[p+1]+2)for(int i=0;i<m->prims[p+1];i++)indices[n++]=m->prims[p+2+i];
 assert(rodPack(m));assert(m->count==count&&m->normalCount<=count&&m->positionCount<=count);
 assert(((uintptr_t)m->pos&31)==0&&((uintptr_t)m->nrm&31)==0);
 for(int i=0;i<count;i++)assert(m->positionMap[i]<m->positionCount&&memcmp(m->pos+m->positionMap[i]*3,positions+i*3,12)==0);
 for(int i=0;i<count;i++)assert(m->normalMap[i]<m->normalCount&&memcmp(m->nrm+m->normalMap[i]*3,normals+i*3,12)==0);
 assert(memcmp(m->nrm,normals,glints*12)==0&&memcmp(m->pos,positions,glints*12)==0);
 for(int i=0;i<glints;i++)assert(m->normalMap[i]==i&&m->positionMap[i]==i);
 checked=m;originalNormals=normals;originalPositions=positions;expectedIndices=indices;expectedCount=n;cursor=0;
 unsigned before=allocationCall;assert(rodRecord(m));assert(allocationCall==before);assert(cursor==n);
 checked=NULL;assert(!m->normalMap&&!m->positionMap&&!m->prims);
 rodmesh_t reference=*m;reference.nrm=normals;reference.pos=positions;
 unsigned positive=0;
 for(int x=-8;x<=8;x++)for(int y=-8;y<=8;y++) {
  Mtx a,b,mv;guMtxRotRad(a,'x',x*.18f);guMtxRotRad(b,'y',y*.21f);guMtxConcat(a,b,mv);mv[2][3]=-500;
  star_t actual[MAX_STARS]={{0}},expected[MAX_STARS]={{0}};int na=0,ne=0;
  findGlints(m,mv,1,.7f,actual,&na);referenceGlints(&reference,mv,1,.7f,expected,&ne);
  assert(na==ne&&memcmp(actual,expected,sizeof(actual))==0);positive+=na;
 }
 if(glints>16)assert(positive>0);
 assert(allocationCall==before);
 rodFree(m);free(normals);free(positions);free(indices);assert(!liveAllocations);
}
int main(void) {
 buildGlow();
 rodmesh_t small=synthetic();compareMesh(&small);
 rodmesh_t tray={0},lid={0};buildTrayRods(&tray);compareMesh(&tray);buildLidRods(&lid);compareMesh(&lid);
 small=synthetic();allocationCall=0;assert(rodPack(&small));unsigned calls=allocationCall;rodFree(&small);assert(!liveAllocations);
 for(unsigned f=1;f<=calls;f++) {
  failAllocation=0;small=synthetic();allocationCall=0;failAllocation=f;
  assert(!rodPack(&small));rodFree(&small);assert(!liveAllocations);
 }
 failAllocation=allocationCall=0;assert(UIJewelCase_Prepare()&&UIJewelCase_Ready());
 unsigned allCalls=allocationCall;assert(allCalls>43&&allCalls<80);
 releaseAll();prepared=false;assert(!liveAllocations);
 for(unsigned f=1;f<=allCalls;f++) {
  allocationCall=0;failAllocation=f;assert(!UIJewelCase_Prepare());assert(!UIJewelCase_Ready()&&!liveAllocations);
  failAllocation=allocationCall=0;assert(UIJewelCase_Prepare()&&UIJewelCase_Ready());releaseAll();prepared=false;assert(!liveAllocations);
 }
 printf("{\"scene\":\"jewel-normals\",\"pack_failures\":%u,\"prepare_failures\":%u}\n",calls,allCalls);
 return 0;
}
'''


if __name__ == "__main__":
    if "--sanitize" in sys.argv:
        os.environ["CC"] = os.environ.get("CC", "cc") + " -fsanitize=address,undefined -fno-omit-frame-pointer"
        if sys.platform.startswith("linux"):
            os.environ["CC"] += " -no-pie"
    row = measure(driver_source=driver())["jewel-normals"]
    print(f"mesh packing: exact positions/normals, indices, glints, {row['pack_failures']} pack and {row['prepare_failures']} preparation failures/retries passed")
