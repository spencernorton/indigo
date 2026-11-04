#!/usr/bin/env python3
"""Exercise actual cube pipeline and symbol transform, including vertical poses."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
from test_cheats_gx_stream import extract_function

GUI = Path(__file__).resolve().parents[3] / 'cube/swiss/source/gui'
PRELUDE = r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_scene.h"
#include "ui_cube_motif.h"
typedef float Mtx[3][4];
typedef float Mtx44[4][4];
typedef struct { float x,y,z; } guVector;
static Mtx loaded;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"pose assertion line %d\n",__LINE__); exit(71); } } while(0)
static void guMtxIdentity(Mtx m) { memset(m,0,sizeof(Mtx)); for(int i=0;i<3;i++) m[i][i]=1; }
static void guMtxCopy(Mtx a,Mtx b) { memcpy(b,a,sizeof(Mtx)); }
static void guMtxRotAxisRad(Mtx m,const guVector *a,float r) {
 float c=cosf(r),s=sinf(r); guMtxIdentity(m);
 if(a->x==1) { m[1][1]=m[2][2]=c; m[1][2]=-s; m[2][1]=s; }
 else { m[0][0]=m[2][2]=c; m[0][2]=s; m[2][0]=-s; }
}
static void guMtxConcat(Mtx a,Mtx b,Mtx out) {
 Mtx n={0}; for(int i=0;i<3;i++) { for(int j=0;j<3;j++) for(int k=0;k<3;k++) n[i][j]+=a[i][k]*b[k][j];
 n[i][3]=a[i][3]; for(int k=0;k<3;k++) n[i][3]+=a[i][k]*b[k][3]; } guMtxCopy(n,out);
}
static void guMtxScaleApply(Mtx a,Mtx out,float x,float y,float z) {
 float scale[3]={x,y,z}; for(int i=0;i<3;i++) for(int j=0;j<4;j++) out[i][j]=a[i][j]*scale[i];
}
static void guMtxTransApply(Mtx a,Mtx out,float x,float y,float z) {
 guMtxCopy(a,out); out[0][3]+=x; out[1][3]+=y; out[2][3]+=z;
}
static void guPerspective(Mtx44 p,float fov,float aspect,float near,float far) {
 (void)near;(void)far; memset(p,0,sizeof(Mtx44));p[1][1]=1/tanf(fov*3.14159265359f/360);p[0][0]=p[1][1]/aspect;
}
/* The 4:3 stage; test_ui_stage.c covers widescreen. */
static void UIStage_Project(Mtx44 p) { (void)p; }
#define GX_LoadPosMtxImm(m,i) guMtxCopy(m,loaded)
'''
MAIN = r'''
static void frameFor(uiSceneFrame_t *frame,const uiHomeState_t *home) {
 memset(frame,0,sizeof(*frame)); frame->cubeScale=1; frame->cubePitch=-0.09f;
 UIHome_OrientationMatrix(&home->orientation,frame->homeOrientation);
 uiCubeMotifBasis_t basis; UICubeMotif_Build(home,&basis);
 memcpy(frame->homeMotifBasis,basis.face,sizeof(basis.face));
 for(int f=0;f<UI_HOME_FACE_COUNT;++f) frame->homeMotifAlpha[f]=basis.shown[f]?0.7f:0.0f;
}
static guVector transformed(guVector p) {
 guVector q={loaded[0][0]*p.x+loaded[0][1]*p.y+loaded[0][2]*p.z,
 loaded[1][0]*p.x+loaded[1][1]*p.y+loaded[1][2]*p.z,
 loaded[2][0]*p.x+loaded[2][1]*p.y+loaded[2][2]*p.z}; return q;
}
int main(void) {
 uiHomeOrientation_t orientations[24]; UIHome_OrientationInit(&orientations[0]);int count=1;
 for(int i=0;i<count;i++) for(int axis=UI_HOME_TURN_HORIZONTAL;axis<=UI_HOME_TURN_VERTICAL;axis++) {
  uiHomeOrientation_t next=orientations[i]; UIHome_OrientationTurn(&next,(uiHomeTurnAxis_t)axis,1);
  int found=0;for(int j=0;j<count;j++) if(memcmp(&next,&orientations[j],sizeof(next))==0) found=1;
  if(!found) { CHECK(count<24);orientations[count++]=next; }
 }
 CHECK(count==24);
 for(int i=0;i<count;i++) for(int face=0;face<4;face++) for(int axis=1;axis<=2;axis++) {
  uiHomeState_t home;UIHome_Init(&home,(uiHomeCapabilities_t){.hasSource=true});home.orientation=orientations[i];home.face=(uiHomeFace_t)face;home.turnAxis=(uiHomeTurnAxis_t)axis;
  uiSceneFrame_t frame;frameFor(&frame,&home);cubeRasterTransform_t raster;memset(&raster,0,sizeof(raster));setupCubePipeline(&frame,0,false,&raster);
  /* Each face carries its own glyph opacity; Apps, outside the ring, none. */
  for(int f=0;f<UI_HOME_FACE_COUNT;f++) CHECK(fabsf(raster.motifAlpha[f]-(f<4?.7f:0.0f))<.0001f);
  for(int r=0;r<3;r++) for(int c=0;c<3;c++) CHECK(fabsf(loaded[r][c]-(float)home.orientation.m[r][c])<.0001f);
  guVector right=transformed(semanticFacePoint(&raster,face,1,0,0));
  guVector up=transformed(semanticFacePoint(&raster,face,0,1,0));
  guVector normal=transformed(semanticFacePoint(&raster,face,0,0,1));
  CHECK(fabsf(right.x-1)<.0001f && fabsf(right.y)<.0001f && fabsf(right.z)<.0001f);
  CHECK(fabsf(up.x)<.0001f && fabsf(up.y-1)<.0001f && fabsf(up.z)<.0001f);
  CHECK(fabsf(normal.x)<.0001f && fabsf(normal.y)<.0001f && fabsf(normal.z-1)<.0001f);
 }
 /* Classic: every face's glyph keeps a side of its own, and the reducer's
    turn from Library to the face brings that side to the front, the glyph
    upright, for all five faces, every one of them shown. */
 {
  static const uiHomeInput_t toward[UI_HOME_FACE_COUNT]={UI_HOME_INPUT_NONE,UI_HOME_INPUT_UP,UI_HOME_INPUT_LEFT,UI_HOME_INPUT_RIGHT,UI_HOME_INPUT_DOWN};
  uiHomeCapabilities_t caps={.hasSource=true,.hasApps=true,.style=UI_HOME_CUBE_CLASSIC};
  for(int face=0;face<=UI_HOME_FACE_APPS;face++) {
   uiHomeState_t home;UIHome_Init(&home,caps);
   if(toward[face]!=UI_HOME_INPUT_NONE) UIHome_Apply(&home,toward[face],caps);
   CHECK(home.face==(uiHomeFace_t)face);
   uiSceneFrame_t frame;frameFor(&frame,&home);cubeRasterTransform_t raster;memset(&raster,0,sizeof(raster));setupCubePipeline(&frame,0,false,&raster);
   /* Memory Cards and Emulators are on no side by default. */
   for(int f=0;f<UI_HOME_FACE_COUNT;f++) CHECK(fabsf(raster.motifAlpha[f]-(f<=UI_HOME_FACE_APPS?.7f:0.0f))<.0001f);
   guVector right=transformed(semanticFacePoint(&raster,face,1,0,0));
   guVector up=transformed(semanticFacePoint(&raster,face,0,1,0));
   guVector normal=transformed(semanticFacePoint(&raster,face,0,0,1));
   CHECK(fabsf(right.x-1)<.0001f && fabsf(right.y)<.0001f && fabsf(right.z)<.0001f);
   CHECK(fabsf(up.x)<.0001f && fabsf(up.y-1)<.0001f && fabsf(up.z)<.0001f);
   CHECK(fabsf(normal.x)<.0001f && fabsf(normal.y)<.0001f && fabsf(normal.z-1)<.0001f);
  }
  /* File Browser in place of each side's face: the same turn brings its
     glyph to the front, upright, and the face it replaced draws nowhere. */
  for(int side=0;side<UI_HOME_SIDE_COUNT;side++) {
   uiHomeCapabilities_t files=caps;files.customSides=true;
   for(int s=0;s<UI_HOME_SIDE_COUNT;s++) files.sides[s]=(uint8_t)(s+1);
   files.sides[side]=UI_HOME_FACE_FILES;
   uiHomeState_t home;UIHome_Init(&home,files);UIHome_Apply(&home,toward[side+1],files);
   CHECK(home.face==UI_HOME_FACE_FILES);
   uiSceneFrame_t frame;frameFor(&frame,&home);cubeRasterTransform_t raster;memset(&raster,0,sizeof(raster));setupCubePipeline(&frame,0,false,&raster);
   for(int f=0;f<UI_HOME_FACE_COUNT;f++) CHECK(fabsf(raster.motifAlpha[f]-((f<=UI_HOME_FACE_APPS && f!=side+1) || f==UI_HOME_FACE_FILES?.7f:0.0f))<.0001f);
   guVector right=transformed(semanticFacePoint(&raster,UI_HOME_FACE_FILES,1,0,0));
   guVector up=transformed(semanticFacePoint(&raster,UI_HOME_FACE_FILES,0,1,0));
   guVector normal=transformed(semanticFacePoint(&raster,UI_HOME_FACE_FILES,0,0,1));
   CHECK(fabsf(right.x-1)<.0001f && fabsf(right.y)<.0001f && fabsf(right.z)<.0001f);
   CHECK(fabsf(up.x)<.0001f && fabsf(up.y-1)<.0001f && fabsf(up.z)<.0001f);
   CHECK(fabsf(normal.x)<.0001f && fabsf(normal.y)<.0001f && fabsf(normal.z-1)<.0001f);
  }
 }
 /* An actual half-completed vertical turn moves the front normal vertically,
    while an equivalent horizontal turn moves it sideways. */
 for(int vertical=0;vertical<2;vertical++) for(int sign=-1;sign<=1;sign+=2) {
  uiHomeState_t home; UIHome_Init(&home,(uiHomeCapabilities_t){.hasSource=true});uiSceneFrame_t frame;frameFor(&frame,&home);
  Mtx turn;guVector axis=vertical?(guVector){1,0,0}:(guVector){0,1,0};guMtxRotAxisRad(turn,&axis,(float)sign*0.7853981634f);
  for(int r=0;r<3;r++) for(int c=0;c<3;c++) frame.homeOrientation[r][c]=turn[r][c];
  cubeRasterTransform_t raster;setupCubePipeline(&frame,0,false,&raster);guVector normal=transformed((guVector){0,0,1});
  CHECK(fabsf(normal.z-.70710678f)<.0001f);
  CHECK(vertical?(fabsf(normal.x)<.0001f && fabsf(normal.y+(float)sign*.70710678f)<.0001f):(fabsf(normal.y)<.0001f && fabsf(normal.x-(float)sign*.70710678f)<.0001f));
 }
 /* The boot fly-in: the cube starts farther off along the camera axis,
    spun about its vertical axis and then tumbled about its horizontal one. */
 {
  uiHomeState_t home;UIHome_Init(&home,(uiHomeCapabilities_t){.hasSource=true});uiSceneFrame_t frame;frameFor(&frame,&home);
  frame.introDistance=12.0f;frame.introSpin=0.7f;
  cubeRasterTransform_t raster;setupCubePipeline(&frame,0,false,&raster);
  CHECK(fabsf(loaded[0][3])<.0001f && fabsf(loaded[1][3])<.0001f && fabsf(loaded[2][3]-(CUBE_CAMERA_Z-12.0f))<.0001f);
  float cy=cosf(0.7f),sy=sinf(0.7f),cx=cosf(0.7f*CUBE_INTRO_TUMBLE),sx=sinf(0.7f*CUBE_INTRO_TUMBLE);
  float expect[3][3]={{cy,sy*sx,sy*cx},{0,cx,-sx},{-sy,cy*sx,cy*cx}};
  for(int r=0;r<3;r++) for(int c=0;c<3;c++) CHECK(fabsf(loaded[r][c]-expect[r][c])<.0001f);
 }
 puts("cube render pose: 192 settled bindings, five Classic faces, File Browser on each side, four directional midpoints and the boot fly-in passed"); return 0;
}
'''

# Idle Animation: Home's pose turned 0.28 to the right, the Library face in
# front. The model's yaw about the vertical axis, whatever its pitch.
SWAY_MAIN = MAIN[:MAIN.index('int main(void) {')] + r'''
static float yaw(void) { return atan2f(-loaded[2][0], loaded[0][0]); }
int main(void) {
 (void)semanticFacePoint; (void)transformed;  /* the face checks' helpers */
 uiHomeState_t home; UIHome_Init(&home,(uiHomeCapabilities_t){.hasSource=true});
 uiSceneFrame_t frame; frameFor(&frame,&home); frame.cubeYaw=0.28f; frame.homeIdleBlend=1.0f;
 cubeRasterTransform_t raster; memset(&raster,0,sizeof(raster));
 const float start=100.0f, half=3.14159265f/CUBE_SWAY_RATE;
 float low=1.0f, high=-1.0f, at;
 IndigoBackground_SetIdleSway(true);
 /* Sway starts from Home's own pose; a frame's two passes step it once. */
 setupCubePipeline(&frame,start,true,&raster); CHECK(fabsf(yaw()-0.28f)<0.001f);
 setupCubePipeline(&frame,start,true,&raster); CHECK(fabsf(yaw()-0.28f)<0.001f);
 /* Half a swing of frames: the mirror image, never past it. */
 for(int i=1;i<=(int)(half*60.0f);i++) {
  setupCubePipeline(&frame,start+i/60.0f,true,&raster); low=fminf(low,yaw()); high=fmaxf(high,yaw());
 }
 CHECK(fabsf(yaw()+0.28f)<0.01f); CHECK(low>=-0.2801f && high<=0.2801f);
 /* A frame five seconds long holds the swing rather than jumping it. */
 at=yaw(); setupCubePipeline(&frame,start+half+5.0f,true,&raster); CHECK(fabsf(yaw()-at)<0.05f);
 /* Home stops resting, then rests again: the next swing starts from the pose. */
 frame.homeIdleBlend=0.0f; setupCubePipeline(&frame,start+half+5.1f,true,&raster); CHECK(fabsf(yaw()-0.28f)<0.001f);
 frame.homeIdleBlend=1.0f; setupCubePipeline(&frame,start+half+5.2f,true,&raster); CHECK(fabsf(yaw()-0.28f)<0.002f);
 /* Motion off keeps the pose. */
 setupCubePipeline(&frame,start+half+8.0f,false,&raster); CHECK(fabsf(yaw()-0.28f)<0.0001f);
 /* Calm, the default, stays within its slight drift of the pose. */
 IndigoBackground_SetIdleSway(false);
 for(int i=0;i<1200;i++) { setupCubePipeline(&frame,start+i/20.0f,true,&raster); CHECK(fabsf(yaw()-0.28f)<=CUBE_IDLE_SWAY_RADIANS+0.0001f); }
 puts("cube idle: Sway reaches the mirror of Home's pose and no further, starts from it, steps once a frame, holds over a long frame; Calm keeps its drift"); return 0;
}
'''

# Sway through a turn and a shallow dip. The turn: Home's own idle blend,
# from the real scene's springs, as the cube turns a face at the far end of a
# swing (the pose stays Home's here, so the yaw is the swing alone). The dip: a
# spring of its own dropped for 0.3 s mid-swing, where the swing moves
# fastest, shallow enough that the blend stays above the restart level.
SWAY_TURN_MAIN = SWAY_MAIN[:SWAY_MAIN.index('int main(void) {')] + r'''
static uiSceneFrame_t frame; static cubeRasterTransform_t raster; static float now=100.0f;
static const float dt=1.0f/60.0f, reach=CUBE_SWAY_RADIANS;
static float drawAt(float blend) { now+=dt; frame.homeIdleBlend=blend; setupCubePipeline(&frame,now,true,&raster); return yaw(); }
static float swing(float seconds) { return 0.28f-reach*(1.0f-cosf(seconds*CUBE_SWAY_RATE)); }
int main(void) {
 (void)semanticFacePoint; (void)transformed;
 uiHomeCapabilities_t caps={.hasSource=true,.hasRecent=true,.hasApps=true};
 uiHomeState_t home; UIHome_Init(&home,caps); frameFor(&frame,&home); frame.cubeYaw=0.28f;
 memset(&raster,0,sizeof(raster)); IndigoBackground_SetIdleSway(true);
 UIScene_Reset(); UIScene_RequestHome(&home); UIScene_Activate();
 float y=0.0f, last, blend=0.0f;
 /* Home rests until the swing is at its far end, the mirror of the pose. */
 for(int i=0;i<60*40 && !(blend>=1.0f && y<-0.27f);i++) { UIScene_Update(dt,UI_MOTION_FULL); blend=UIScene_Frame()->homeIdleBlend; y=drawAt(blend); }
 CHECK(blend>=1.0f && y<-0.27f);
 /* A turn: the blend all but goes and comes back, as Home comes to rest. */
 UIHome_Apply(&home,UI_HOME_INPUT_RIGHT,caps); UIScene_RequestHome(&home);
 float blends[300], yaws[300]; int low=0;
 for(int i=0;i<300;i++) {
  UIScene_Update(dt,UI_MOTION_FULL); blends[i]=UIScene_Frame()->homeIdleBlend; yaws[i]=drawAt(blends[i]);
  if(blends[i]<blends[low]) low=i;
 }
 CHECK(blends[low]<0.01f && blends[low]>0.0f && blends[299]>=1.0f);
 last=yaws[0];
 for(int i=1;i<300;i++) {
  /* Never a jump; from the blend's lowest the swing starts again from the
     pose and goes no faster than a swing does. */
  CHECK(fabsf(yaws[i]-last)<0.03f); last=yaws[i];
  if(i>low) {
   CHECK(yaws[i]<=0.28f+0.0001f && yaws[i]>=swing((i-low)*dt)-0.003f);
   CHECK(fabsf(yaws[i]-yaws[i-1])/dt<=reach*CUBE_SWAY_RATE*1.2f);
  }
 }
 CHECK(yaws[299]<0.2f);  /* and it is swinging */
 /* A shallow dip mid-swing: the swing waits while the blend falls, takes
    no step, and goes on from where it was. */
 drawAt(0.0f);
 int rest=(int)(3.14159265f/2.0f/CUBE_SWAY_RATE*60.0f), steps=rest;
 for(int i=0;i<rest;i++) y=drawAt(1.0f);
 uiMotionSpring_t dip; UIMotion_SpringInit(&dip,1.0f,7.0f);
 UIMotion_SpringRetarget(&dip,0.0f,UI_MOTION_FULL);
 last=y; blend=1.0f;
 for(int i=0;i<60*3;i++) {
  if(i==18) UIMotion_SpringRetarget(&dip,1.0f,UI_MOTION_FULL);
  float b=UIMotion_SpringUpdate(&dip,dt,UI_MOTION_FULL);
  if(b>=blend) steps++;
  blend=b; y=drawAt(b);
  CHECK(fabsf(y-last)<0.03f); last=y;
 }
 CHECK(blend>=1.0f && fabsf(y-swing(steps*dt))<0.004f);
 puts("cube idle: through a turn Sway starts again from Home's pose without a jump; a shallow dip picks the swing up where it was"); return 0;
}
'''

class CubeRenderPose(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=(GUI/'indigo_background.c').read_text()
        # The pipeline loads its projection through loadCubeProjection().
        # Idle Animation's Sway: its clock and the switch the frame sets.
        sway=re.search(r'^static bool idleSway;\nstatic float swayLastSeconds = -1\.0f, swayLastBlend, swaySeconds;$',source,re.M).group()
        sway='#include "ui_anim.h"\n'+sway+'\n'+extract_function(source,'void IndigoBackground_SetIdleSway(')+'\n'+extract_function(source,'static float swayClock(')
        cls.setup='static Mtx44 cubeProjection;\n'+sway+'\n'+extract_function(source,'static void loadCubeProjection(')+'\n'+extract_function(source,'static void setupCubePipeline(')
        cls.point=extract_function(source,'static guVector semanticFacePoint(')
        raster=re.search(r'typedef struct cubeRasterTransform \{.*?\} cubeRasterTransform_t;',source,re.S).group()
        constants='\n'.join(line for line in source.splitlines() if line.startswith('#define CUBE_'))
        names=set(re.findall(r'\bGX_[A-Za-z0-9_]+',cls.setup))
        calls=set(re.findall(r'\b(GX_[A-Za-z0-9_]+)\(',cls.setup))
        macros='\n'.join(f'#define {n}(...) ((void)0)' if n in calls else f'#define {n} 0' for n in sorted(names) if n!='GX_LoadPosMtxImm')
        cls.prefix=PRELUDE+'\n'+raster+'\n'+constants+'\n'+macros+'\n'
    def execute(self,setup,point,main=MAIN):
        with tempfile.TemporaryDirectory(prefix='swiss-cube-pose-') as folder:
            source=Path(folder)/'test.c'; binary=Path(folder)/'test'
            source.write_text(self.prefix+setup+'\n'+point+'\n'+main)
            subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-I'+str(GUI),str(source),str(GUI/'ui_home.c'),str(GUI/'ui_cube_motif.c'),str(GUI/'ui_scene.c'),str(GUI/'ui_motion.c'),str(GUI/'ui_stage.c'),'-lm','-o',str(binary)],check=True,capture_output=True,text=True)
            return subprocess.run([str(binary)],capture_output=True,text=True,timeout=5)
    def test_actual_pipeline_and_face_binding(self):
        result=self.execute(self.setup,self.point)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_ignoring_navigation_is_rejected(self):
        mutant=self.setup.replace('guMtxConcat(rotation, navigation, rotation);','(void)navigation;')
        self.assertNotEqual(mutant,self.setup)
        self.assertNotEqual(self.execute(mutant,self.point).returncode,0)
    def test_flying_in_from_nowhere_is_rejected(self):
        mutant=self.setup.replace('CUBE_CAMERA_Z - scene->introDistance','CUBE_CAMERA_Z + 0.0f * scene->introDistance')
        self.assertNotEqual(mutant,self.setup)
        self.assertNotEqual(self.execute(mutant,self.point).returncode,0)
    def test_spinning_without_the_tumble_is_rejected(self):
        mutant=self.setup.replace('scene->introSpin * CUBE_INTRO_TUMBLE','0.0f * scene->introSpin')
        self.assertNotEqual(mutant,self.setup)
        self.assertNotEqual(self.execute(mutant,self.point).returncode,0)
    def test_wrong_face_basis_is_rejected(self):
        mutant=self.point.replace('semanticFaces[face]','semanticFaces[(face + 1) % UI_HOME_FACE_COUNT]')
        self.assertNotEqual(mutant,self.point)
        self.assertNotEqual(self.execute(self.setup,mutant).returncode,0)
    def test_sway_turns_to_the_mirror_of_home_and_back(self):
        result=self.execute(self.setup,self.point,SWAY_MAIN)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_sway_that_never_starts_over_is_rejected(self):
        mutant=self.setup.replace('swaySeconds = 0.0f;','swaySeconds += 0.0f;')
        self.assertNotEqual(mutant,self.setup)
        self.assertNotEqual(self.execute(mutant,self.point,SWAY_MAIN).returncode,0)
    def test_sway_through_a_turn_and_a_shallow_dip(self):
        result=self.execute(self.setup,self.point,SWAY_TURN_MAIN)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_sway_that_starts_over_late_or_never_waits_is_rejected(self):
        for name,old,new in (
            # Only once the blend is exactly 0, which a turn's never reaches.
            ('starts over only at 0','idleBlend < CUBE_SWAY_RESTART_BLEND','idleBlend <= 0.0f'),
            # Running on while the blend falls: a dip moves the swing on.
            ('never waits','else if(idleBlend >= swayLastBlend) {','else {'),
            # Starting over as the blend rises again: a dip drops the yaw.
            ('starts over rising','else if(idleBlend >= swayLastBlend) {',
             'else if(idleBlend > swayLastBlend && idleBlend < 1.0f) {\n\t\tswaySeconds = 0.0f;\n\t}\n\telse if(idleBlend >= swayLastBlend) {'),
        ):
            with self.subTest(name):
                mutant=self.setup.replace(old,new)
                self.assertNotEqual(mutant,self.setup)
                self.assertNotEqual(self.execute(mutant,self.point,SWAY_TURN_MAIN).returncode,0)
    def test_sway_that_jumps_over_a_long_frame_is_rejected(self):
        mutant=self.setup.replace('fminf(step, 0.1f)','step')
        self.assertNotEqual(mutant,self.setup)
        self.assertNotEqual(self.execute(mutant,self.point,SWAY_MAIN).returncode,0)
if __name__=='__main__': unittest.main()
