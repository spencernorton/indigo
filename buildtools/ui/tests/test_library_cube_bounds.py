#!/usr/bin/env python3
"""The parked Home cube emits no geometry in screen-filling Library views."""
import subprocess
import sys

from test_frame_budget import HERE, measure


def driver():
    source = (HERE / "frame_budget.c").read_text()
    source = source.replace("int main(void)", "int originalFrameBudgetMain(void)", 1)
    return source + r'''
#include <assert.h>
int main(void)
{
    const uiGameflowLayout_t hidden[] = {
        UI_GAMEFLOW_LAYOUT_GRID, UI_GAMEFLOW_LAYOUT_SPOTLIGHT,
        UI_GAMEFLOW_LAYOUT_JEWEL_CASE
    };
    const float phases[] = {0,7,23,60,130};
    unsigned scenarios = 0;
    for(unsigned mode = 0; mode < UI_MOTION_COUNT; mode++) {
        for(unsigned layout = 0; layout < sizeof(hidden)/sizeof(hidden[0]); layout++) {
            for(unsigned wide = 0; wide < 2; wide++) {
                /* A width change while the Library is already open must also
                 * retarget it, preserving smooth transitions and Off snapping. */
                UIStage_SetWide(wide == 0);
                startScene();
                UIScene_RequestLibraryLayout(hidden[layout]);
                UIScene_Request(UI_SCENE_LIBRARY);
                for(int i = 0; i < 600; i++) UIScene_Update(DT, (uiMotionMode_t)mode);
                UIStage_SetWide(wide != 0);
                for(int i = 0; i < 600; i++) UIScene_Update(DT, (uiMotionMode_t)mode);
                for(unsigned phase = 0; phase < sizeof(phases)/sizeof(phases[0]); phase++) {
                    memset(&cost, 0, sizeof(cost));
                    drawCube(UIScene_Frame(), phases[phase], true, &clock, &pad, icons, true);
                    if(cost.vertices != 0) {
                        fprintf(stderr,"parked cube visible: layout %u wide %u mode %u phase %.0f vertices %ld\n",
                                layout,wide,mode,phases[phase],cost.vertices);
                        return 1;
                    }
                    scenarios++;
                }
                /* Returning Home must still draw the cube in both shapes. */
                UIScene_Request(UI_SCENE_HOME);
                for(int i = 0; i < 600; i++) UIScene_Update(DT, (uiMotionMode_t)mode);
                memset(&cost, 0, sizeof(cost));
                drawCube(UIScene_Frame(),0,false,&clock,&pad,icons,true);
                assert(cost.vertices > 0);
            }
        }
    }
    UIStage_SetWide(false);
    printf("{\"scene\":\"library-cube-bounds\",\"scenarios\":%u}\n",scenarios);
    return 0;
}
'''


if __name__ == "__main__":
    try:
        result = measure(driver_source=driver())["library-cube-bounds"]
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr, file=sys.stderr, end="")
        raise
    assert result["scenarios"] == 90
    print("Library cube bounds: 90 hidden-render cases and Home returns passed")
