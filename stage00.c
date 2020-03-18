/*
   stage00.c

  the main game file
*/

#include <assert.h>
#include <nusys.h>
// #include <stdlib.h>

#include <malloc.h>
#include <math.h>

// game
#include "animation.h"
#include "constants.h"
#include "game.h"
#include "gameobject.h"
#include "graphic.h"
#include "input.h"
#include "main.h"
#include "modeltype.h"
#include "pathfinding.h"
#include "physics.h"
#include "renderer.h"
#include "trace.h"
#include "vec2d.h"
#include "vec3d.h"

// models
#include "book.h"
#include "bush.h"
#include "characterrig.h"
#include "flagpole.h"
#include "gooserig.h"
#include "planter.h"
#include "testingCube.h"
#include "university_bldg.h"
#include "university_floor.h"
#include "wall.h"

// map
#include "university_map.h"
#include "university_map_collision.h"
#include "university_map_graph.h"
// anim data
#include "character_anim.h"
#include "goose_anim.h"

#include "ed64io_everdrive.h"
#include "ed64io_usb.h"

#define CONSOLE_EVERDRIVE_DEBUG 0
#define CONSOLE_SHOW_PROFILING 1
#define CONSOLE_SHOW_RCP_TASKS 1
#define LOG_TRACES 1
#define CONTROLLER_DEAD_ZONE 0.1

typedef enum RenderMode {
  ToonFlatShadingRenderMode,
  TextureAndLightingRenderMode,
  TextureNoLightingRenderMode,
  NoTextureNoLightingRenderMode,
  LightingNoTextureRenderMode,
  MAX_RENDER_MODE
} RenderMode;

static Vec3d viewPos;
static Vec3d viewRot;
static Input input;

static u16 perspNorm;
static u32 nearPlane; /* Near Plane */
static u32 farPlane;  /* Far Plane */

/* frame counter */
float frameCounterLastTime;
int frameCounterCurFrames;
int frameCounterLastFrames;

/* profiling */
float profAvgCharacters;
float profAvgPhysics;
float profAvgDraw;
float profAvgPath;
float lastFrameTime;

static int usbEnabled;
static int usbResult;
static UsbLoggerState usbLoggerState;

static int logTraceStartOffset = 0;
static int loggingTrace = FALSE;

static float cycleMode;
static RenderMode renderModeSetting;
static GameObject* sortedObjects[MAX_WORLD_OBJECTS];
PhysWorldData physWorldData = {
    university_map_collision_collision_mesh, UNIVERSITY_MAP_COLLISION_LENGTH,
    &university_map_collision_collision_mesh_hash,
    /*gravity*/ -9.8 * N64_SCALE_FACTOR, /*viscosity*/ 0.05};

void drawWorldObjects(Dynamic* dynamicp);

#define OBJ_START_VAL 1000

Lights1 sun_light = gdSPDefLights1(120,
                                   120,
                                   120, /* weak ambient light */
                                   255,
                                   255,
                                   255, /* white light */
                                   80,
                                   80,
                                   0);

Lights0 amb_light = gdSPDefLights0(200, 200, 200 /*  ambient light */);

/* The initialization of stage 0 */
void initStage00() {
  Game* game;
  GameObject* obj;
  int i;

  usbEnabled = TRUE;
  usbResult = 0;

  frameCounterLastTime = 0;
  frameCounterCurFrames = 0;
  profAvgCharacters = 0;
  profAvgPhysics = 0;
  profAvgDraw = 0;
  profAvgPath = 0;

  loggingTrace = FALSE;

  cycleMode = 1.0;
  renderModeSetting = ToonFlatShadingRenderMode;
  nearPlane = 10;
  farPlane = 10000;
  Vec3d_init(&viewPos, 0.0F, 0.0F, -400.0F);
  Vec3d_init(&viewRot, 0.0F, 0.0F, 0.0F);
  Input_init(&input);

  Game_init(university_map_data, UNIVERSITY_MAP_COUNT, &physWorldData);

  assert(UNIVERSITY_MAP_COUNT <= MAX_WORLD_OBJECTS);

  game = Game_get();

  game->pathfindingGraph = &university_map_graph;
  game->pathfindingState = &university_map_graph_pathfinding_state;

  // init sortedObjects pointer array, used for painter's algorithm rendering
  for (i = 0, obj = game->worldObjects; i < game->worldObjectsCount;
       i++, obj++) {
    sortedObjects[i] = obj;
  }

  lastFrameTime = CUR_TIME_MS();
  evd_init();

  debugPrintf("good morning\n");
}

void debugPrintVec3d(int x, int y, char* label, Vec3d* vec) {
  char conbuf[100];
  nuDebConTextPos(0, x, y);
  sprintf(conbuf, "%s {%5.0f,%5.0f,%5.0f}", label, vec->x, vec->y, vec->z);
  nuDebConCPuts(0, conbuf);
}

void debugPrintFloat(int x, int y, char* format, float value) {
  char conbuf[100];
  nuDebConTextPos(0, x, y);
  sprintf(conbuf, format, value);
  nuDebConCPuts(0, conbuf);
}

void traceRCP() {
  int i;
  // s64 retraceTime;

  // retraceTime = nuDebTaskPerfPtr->retraceTime;
  // debugPrintf("rt=%f ", retraceTime / (1000000.0));
  for (i = 0; i < nuDebTaskPerfPtr->gfxTaskCnt; i++) {
    // debugPrintf(
    //     "[t%d: st=%f rsp=%f,rdp=%f] ", i,
    //     (nuDebTaskPerfPtr->gfxTaskTime[i].rspStart ) / 1000.0,
    //     (nuDebTaskPerfPtr->gfxTaskTime[i].rspEnd -
    //      nuDebTaskPerfPtr->gfxTaskTime[i].rspStart) /
    //         1000.0,
    //     (nuDebTaskPerfPtr->gfxTaskTime[i].rdpEnd -
    //      nuDebTaskPerfPtr->gfxTaskTime[i].rspStart) /
    //         1000.0);

    Trace_addEvent(RSPTaskTraceEvent,
                   nuDebTaskPerfPtr->gfxTaskTime[i].rspStart / 1000.0f,
                   nuDebTaskPerfPtr->gfxTaskTime[i].rspEnd / 1000.0f);

    Trace_addEvent(RDPTaskTraceEvent,
                   nuDebTaskPerfPtr->gfxTaskTime[i].rspStart / 1000.0f,
                   nuDebTaskPerfPtr->gfxTaskTime[i].rdpEnd / 1000.0f);
  }
  // debugPrintf("\n");
}

/* Make the display list and activate the task */
void makeDL00() {
  Game* game;
  Dynamic* dynamicp;
  int consoleOffset;
  float curTime;
  float profStartDraw, profEndDraw, profStartDebugDraw;
#if CONSOLE
  char conbuf[100];
#endif

  game = Game_get();
  consoleOffset = 20;

  curTime = CUR_TIME_MS();
  frameCounterCurFrames++;

  profStartDraw = curTime;

  Trace_addEvent(FrameTraceEvent, curTime, curTime + 16);

  if (curTime - frameCounterLastTime >= 1000.0) {
    frameCounterLastFrames = frameCounterCurFrames;
    frameCounterCurFrames = 0;
    frameCounterLastTime += 1000.0;
  }

  /* Specify the display list buffer */
  dynamicp = &gfx_dynamic[gfx_gtask_no];
  glistp = &gfx_glist[gfx_gtask_no][0];

  /* Initialize RCP */
  gfxRCPInit();

  /* Clear the frame and Z-buffer */
  gfxClearCfb();

  /* projection, viewing, modeling matrix set */
  guPerspective(&dynamicp->projection, &perspNorm, 45,
                (f32)SCREEN_WD / (f32)SCREEN_HT, nearPlane, farPlane, 1.0);
  gSPPerspNormalize(glistp++, perspNorm);

  if (game->freeView) {
    guPosition(&dynamicp->camera,
               viewRot.x,  // roll
               viewRot.y,  // pitch
               viewRot.z,  // yaw
               1.0F,       // scale
               viewPos.x, viewPos.y, viewPos.z);
  } else {
    guLookAt(&dynamicp->camera, game->viewPos.x, game->viewPos.y,
             game->viewPos.z, game->viewTarget.x, game->viewTarget.y,
             game->viewTarget.z, 0.0f, 1.0f, 0.0f);
  }

  drawWorldObjects(dynamicp);

  gDPFullSync(glistp++);
  gSPEndDisplayList(glistp++);

  assert((glistp - gfx_glist[gfx_gtask_no]) < GFX_GLIST_LEN);

  /* Activate the task and (maybe)
     switch display buffers */
  nuGfxTaskStart(&gfx_glist[gfx_gtask_no][0],
                 (s32)(glistp - gfx_glist[gfx_gtask_no]) * sizeof(Gfx),
                 NU_GFX_UCODE_F3DEX,
#if CONSOLE || NU_PERF_BAR
                 NU_SC_NOSWAPBUFFER
#else
                 NU_SC_SWAPBUFFER
#endif
  );

  /* Switch display list buffers */
  gfx_gtask_no ^= 1;

  profEndDraw = CUR_TIME_MS();
  Trace_addEvent(DrawTraceEvent, profStartDraw, profEndDraw);
  game->profTimeDraw += profEndDraw - profStartDraw;

  profStartDebugDraw = CUR_TIME_MS();

  traceRCP();

#if NU_PERF_BAR
  nuDebTaskPerfBar1(1, 200, NU_SC_SWAPBUFFER);
#endif

// debug text overlay
#if CONSOLE

#if NU_PERF_BAR
#error "can't enable NU_PERF_BAR and CONSOLE at the same time"
#endif

  if (contPattern & 0x1) {
    nuDebConClear(0);
    consoleOffset = 21;

    debugPrintFloat(4, consoleOffset++, "frame=%3.2fms",
                    1000.0 / frameCounterLastFrames);

    if (game->freeView) {
      debugPrintVec3d(4, consoleOffset++, "viewPos", &viewPos);
    } else {
#if CONSOLE_SHOW_PROFILING
      if (game->tick % 60 == 0) {
        profAvgCharacters = game->profTimeCharacters / 60.0f;
        game->profTimeCharacters = 0.0f;
        profAvgPhysics = game->profTimePhysics / 60.0f;
        game->profTimePhysics = 0.0f;
        profAvgDraw = game->profTimeDraw / 60.0f;
        game->profTimeDraw = 0.0f;
        profAvgPath = game->profTimePath / 60.0f;
        game->profTimePath = 0.0f;
      }
      // debugPrintFloat(4, consoleOffset++, "char=%3.2fms", profAvgCharacters);
      // debugPrintFloat(4, consoleOffset++, "phys=%3.2fms", profAvgPhysics);
      // debugPrintFloat(4, consoleOffset++, "draw=%3.2fms", profAvgDraw);
      // debugPrintFloat(4, consoleOffset++, "path=%3.2fms", profAvgPath);
      nuDebConTextPos(0, 4, consoleOffset++);
      sprintf(conbuf, "trace rec=%d,log=%d,evs=%d,lgd=%d", Trace_isTracing(),
              loggingTrace, Trace_getEventsCount(), logTraceStartOffset);
      nuDebConCPuts(0, conbuf);
#endif
#if CONSOLE_SHOW_RCP_TASKS
      {
        int tskIdx;
        for (tskIdx = 0; tskIdx < nuDebTaskPerfPtr->gfxTaskCnt; tskIdx++) {
          nuDebConTextPos(0, 4, consoleOffset++);
          sprintf(conbuf, "[t%d:  rsp=%.2f,rdp=%.2f] ", tskIdx,
                  (nuDebTaskPerfPtr->gfxTaskTime[tskIdx].rspEnd -
                   nuDebTaskPerfPtr->gfxTaskTime[tskIdx].rspStart) /
                      1000.0,
                  (nuDebTaskPerfPtr->gfxTaskTime[tskIdx].rdpEnd -
                   nuDebTaskPerfPtr->gfxTaskTime[tskIdx].rspStart) /
                      1000.0);
          nuDebConCPuts(0, conbuf);
        }
      }
#endif

      // debugPrintVec3d(4, consoleOffset++, "viewPos", &game->viewPos);
      // debugPrintVec3d(4, consoleOffset++, "viewTarget", &game->viewTarget);
      nuDebConTextPos(0, 4, consoleOffset++);
      sprintf(conbuf, "retrace=%lu", nuScRetraceCounter);
      nuDebConCPuts(0, conbuf);

#if CONSOLE_EVERDRIVE_DEBUG
      usbLoggerGetState(&usbLoggerState);
      nuDebConTextPos(0, 4, consoleOffset++);
      sprintf(conbuf, "usb=%d,res=%d,st=%d,id=%d,mqsz=%d", usbEnabled,
              usbResult, usbLoggerState.fifoWriteState, usbLoggerState.msgID,
              usbLoggerState.msgQSize);
      nuDebConCPuts(0, conbuf);
      nuDebConTextPos(0, 4, consoleOffset++);
      sprintf(conbuf, "off=%4d,flu=%d,ovf=%d,don=%d,err=%d",
              usbLoggerState.usbLoggerOffset, usbLoggerState.usbLoggerFlushing,
              usbLoggerState.usbLoggerOverflow, usbLoggerState.countDone,
              usbLoggerState.writeError);
      nuDebConCPuts(0, conbuf);
#endif
    }
    debugPrintVec3d(4, consoleOffset++, "pos", &game->player.goose->position);
  } else {
    nuDebConTextPos(0, 9, 24);
    nuDebConCPuts(0, "Controller1 not connect");
  }

  /* Display characters on the frame buffer */
  nuDebConDisp(NU_SC_SWAPBUFFER);
#endif  // #if CONSOLE

  Trace_addEvent(DebugDrawTraceEvent, profStartDebugDraw, CUR_TIME_MS());
}

void checkDebugControls(Game* game) {
  /* Change the display position by stick data */
  viewRot.x = contdata->stick_y;  // rot around x
  viewRot.y = contdata->stick_x;  // rot around y

  /* The reverse rotation by the A button */
  if (contdata[0].trigger & A_BUTTON) {
    cycleMode = -cycleMode;
  }
  if (contdata[0].trigger & B_BUTTON) {
    renderModeSetting++;
    if (renderModeSetting >= MAX_RENDER_MODE) {
      renderModeSetting = 0;
    }
  }
  /* Change the moving speed with up/down buttons of controller */
  if (contdata[0].button & U_JPAD)
    viewPos.z += 10.0;
  if (contdata[0].button & D_JPAD)
    viewPos.z -= 10.0;

  if (viewPos.z < (600.0 - farPlane)) {
    /* It comes back near if it goes too far */
    viewPos.z = 600.0 - nearPlane;
  } else if (viewPos.z > (600.0 - nearPlane)) {
    /* It goes back far if it comes too near */
    viewPos.z = 600.0 - farPlane;
  }

  /* << XY axis shift process >> */
  /* Move left/right with left/right buttons of controller */
  if (contdata[0].button & L_JPAD)
    viewPos.x -= 1.0;
  if (contdata[0].button & R_JPAD)
    viewPos.x += 1.0;
  if (contdata[0].button & U_CBUTTONS)
    viewPos.y -= 30.0;
  if (contdata[0].button & D_CBUTTONS)
    viewPos.y += 30.0;
  if (contdata[0].button & L_CBUTTONS)
    viewPos.x -= 30.0;
  if (contdata[0].button & R_CBUTTONS)
    viewPos.x += 30.0;
}

void logTraceChunk() {
  int i;
  int printedFirstItem;

  printedFirstItem = FALSE;
  if (usbLoggerBufferRemaining() < 120) {
    return;
  }
  debugPrintf("TRACE=[");
  for (i = logTraceStartOffset; i < Trace_getEventsCount(); i++) {
    // check we have room for more data
    if (usbLoggerBufferRemaining() < 40) {
      break;
    }
    debugPrintf("%s[%.2f,%.2f,%d]", printedFirstItem ? "," : "",
                traceEvents[i].start, traceEvents[i].end, traceEvents[i].type);
    printedFirstItem = TRUE;
    logTraceStartOffset = i;
  }
  debugPrintf("]\n");

  if (logTraceStartOffset == Trace_getEventsCount() - 1) {
    // finished
    loggingTrace = FALSE;
    logTraceStartOffset = 0;
    Trace_clear();
  }
}

void startRecordingTrace() {
  Trace_clear();
  Trace_start();
}

void finishRecordingTrace() {
  Trace_stop();
  loggingTrace = TRUE;
}

/* The game progressing process for stage 0 */
void updateGame00(void) {
  Game* game;
  game = Game_get();

  Vec2d_origin(&input.direction);

  /* Data reading of controller 1 */
  nuContDataGetEx(contdata, 0);
  if (contdata[0].trigger & START_BUTTON) {
    renderModeSetting++;
    if (renderModeSetting >= MAX_RENDER_MODE) {
      renderModeSetting = 0;
    }
  }

  if (game->freeView) {
    checkDebugControls(game);
  } else {
    // normal controls
    if (contdata[0].button & A_BUTTON) {
      input.run = TRUE;
    }
    if (contdata[0].button & B_BUTTON) {
      input.pickup = TRUE;
    }
    if (contdata[0].button & Z_TRIG) {
      input.zoomIn = TRUE;
    }
    if (contdata[0].button & L_TRIG) {
      input.zoomIn = TRUE;
    }
    if (contdata[0].button & R_TRIG) {
      input.zoomOut = TRUE;
    }

    if (contdata[0].trigger & U_CBUTTONS) {
      if (!loggingTrace) {
        if (!Trace_isTracing()) {
          startRecordingTrace();
        } else {
          finishRecordingTrace();
        }
      }
    }
    // if (contdata[0].button & U_CBUTTONS) {
    //   farPlane += 100.0;
    // }

    // if (contdata[0].button & D_CBUTTONS) {
    //   farPlane -= 100.0;
    // }
    input.direction.x = -contdata->stick_x / 61.0F;
    input.direction.y = contdata->stick_y / 63.0F;
    if (fabsf(input.direction.x) < CONTROLLER_DEAD_ZONE)
      input.direction.x = 0;
    if (fabsf(input.direction.y) < CONTROLLER_DEAD_ZONE)
      input.direction.y = 0;
    if (Vec2d_length(&input.direction) > 1.0F) {
      Vec2d_normalise(&input.direction);
    }
  }

  if (Trace_getEventsCount() == TRACE_EVENT_BUFFER_SIZE) {
    finishRecordingTrace();
  }

  if (usbEnabled) {
#if LOG_TRACES
    if (loggingTrace) {
      logTraceChunk();
      usbResult = usbLoggerFlush();
    }
#endif
  }

  Game_update(&input);

  // if (contdata[0].trigger & R_CBUTTONS) {
  //   usbEnabled = !usbEnabled;
  // }

  if (usbEnabled) {
#if LOG_TRACES
    if (loggingTrace) {
      logTraceChunk();
      usbResult = usbLoggerFlush();
    }
#else
    usbResult = usbLoggerFlush();
#endif
  }
}

typedef enum LightingType {
  SunLighting,
  OnlyAmbientLighting,
  MAX_LIGHTING_TYPE
} LightingType;

LightingType getLightingType(GameObject* obj) {
  switch (obj->modelType) {
    case UniFloorModel:
      return OnlyAmbientLighting;
    default:
      return SunLighting;
  }
}

// map the mesh type enum (used by animation frames) to the mesh displaylists
Gfx* GooseMeshList[] = {
    Wtx_gooserig_goosebody_goosebodymesh,      // goosebody_goosebodymesh
    Wtx_gooserig_goosehead_gooseheadmesh,      // goosehead_gooseheadmesh
    Wtx_gooserig_gooseleg_l_gooseleg_lmesh,    // gooseleg_l_gooseleg_lmesh
    Wtx_gooserig_goosefoot_l_goosefoot_lmesh,  // goosefoot_l_goosefoot_lmesh
    Wtx_gooserig_gooseleg_r_gooseleg_rmesh,    // gooseleg_r_gooseleg_rmesh
    Wtx_gooserig_goosefoot_r_goosefoot_rmesh,  // goosefoot_r_goosefoot_rmesh
    Wtx_gooserig_gooseneck_gooseneckmesh,      // gooseneck_gooseneckmesh
};

Gfx* CharacterMeshList[] = {
    Wtx_characterrig_gkbicep_l_gkbicep_lrmesh,  // characterbicep_l_characterbicep_lmesh
    Wtx_characterrig_gkbicep_r_gkbicep_rmesh,  // characterbicep_r_characterbicep_rmesh
    Wtx_characterrig_gkfoot_l_gkfoot_lrmesh,  // characterfoot_l_characterfoot_lmesh
    Wtx_characterrig_gkfoot_r_gkfoot_rmesh,  // characterfoot_r_characterfoot_rmesh
    Wtx_characterrig_gkforearm_l_gkforearm_lrmesh,  // characterforearm_l_characterforearm_lmesh
    Wtx_characterrig_gkforearm_r_gkforearm_rmesh,  // characterforearm_r_characterforearm_rmesh
    Wtx_characterrig_gkhead_gkheadmesh,      // characterhead_characterheadmesh
    Wtx_characterrig_gkshin_l_gkshin_lmesh,  // charactershin_l_charactershin_lmesh
    Wtx_characterrig_gkshin_r_gkshin_rmesh,  // charactershin_r_charactershin_rmesh
    Wtx_characterrig_gktorso_gktorsomesh,  // charactertorso_charactertorsomesh
    Wtx_characterrig_gkthigh_l_gkthigh_lmesh,  // characterthigh_l_characterthigh_lmesh
    Wtx_characterrig_gkthigh_r_gkthigh_rmesh,  // characterthigh_r_characterthigh_rmesh
};

int getAnimationNumModelMeshParts(ModelType modelType) {
  switch (modelType) {
    case GooseModel:
      return MAX_GOOSE_MESH_TYPE;
    default:
      return MAX_CHARACTER_MESH_TYPE;
  }
}

int shouldLerpAnimation(ModelType modelType) {
  switch (modelType) {
    case GooseModel:
      return TRUE;
    default:
      return FALSE;
  }
}

AnimationRange* getCurrentAnimationRange(GameObject* obj) {
  if (obj->modelType == GooseModel) {
    return &goose_anim_ranges[(GooseAnimType)obj->animState->state];
  } else {
    return &character_anim_ranges[(CharacterAnimType)obj->animState->state];
  }
}

Gfx* getMeshDisplayListForModelMeshPart(ModelType modelType, int meshPart) {
  switch (modelType) {
    case GooseModel:
      return GooseMeshList[meshPart];
    default:
      return CharacterMeshList[meshPart];
  }
}

AnimationFrame* getAnimData(ModelType modelType) {
  switch (modelType) {
    case GooseModel:
      return goose_anim_data;
    default:
      return character_anim_data;
  }
}

Gfx* getModelDisplayList(ModelType modelType) {
  switch (modelType) {
    case BushModel:
      return Wtx_bush;
    case BookItemModel:
      return Wtx_book;
    case UniBldgModel:
      return Wtx_university_bldg;
    case UniFloorModel:
      return Wtx_university_floor;
    case FlagpoleModel:
      return Wtx_flagpole;
    case WallModel:
      return Wtx_wall;
    case PlanterModel:
      return Wtx_planter;
    default:
      return Wtx_testingCube;
  }
}

void drawWorldObjects(Dynamic* dynamicp) {
  Game* game;
  GameObject* obj;
  int i, useZBuffering;
  int modelMeshIdx;
  int modelMeshParts;
  Gfx* modelDisplayList;
  AnimationFrame animFrame;
  AnimationInterpolation animInterp;
  AnimationRange* curAnimRange;
  AnimationBoneAttachment* attachment;
  float profStartSort, profStartIter, profStartAnim, profStartAnimLerp;
  RendererSortDistance* rendererSortDistance;

  game = Game_get();
  profStartSort = CUR_TIME_MS();
  rendererSortDistance = (RendererSortDistance*)malloc(
      game->worldObjectsCount * sizeof(RendererSortDistance));
  if (!rendererSortDistance) {
    debugPrintf("failed to alloc rendererSortDistance");
  }

  Renderer_sortWorldObjects(game->worldObjects, rendererSortDistance,
                            game->worldObjectsCount);
  Trace_addEvent(DrawSortTraceEvent, profStartSort, CUR_TIME_MS());

  gSPClearGeometryMode(glistp++, 0xFFFFFFFF);
  gDPSetCycleType(glistp++, cycleMode > 0.0F ? G_CYC_1CYCLE : G_CYC_2CYCLE);
  useZBuffering = TRUE;  // Renderer_isZBufferedGameObject(obj);
  if (useZBuffering) {
    gDPSetRenderMode(glistp++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPSetGeometryMode(glistp++, G_ZBUFFER);
  } else {
    gDPSetRenderMode(glistp++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    gSPClearGeometryMode(glistp++, G_ZBUFFER);
  }

  gSPSetLights0(glistp++, amb_light);

  // setup view
  gSPMatrix(glistp++, OS_K0_TO_PHYSICAL(&(dynamicp->projection)),
            G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
  gSPMatrix(glistp++, OS_K0_TO_PHYSICAL(&(dynamicp->camera)),
            G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);

  profStartIter = CUR_TIME_MS();
  // render world objects
  for (i = 0; i < game->worldObjectsCount; i++) {
    obj = (rendererSortDistance + i)->obj;
    if (obj->modelType == NoneModel || !obj->visible) {
      continue;
    }

    // render textured models
    gSPTexture(glistp++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureFilter(glistp++, G_TF_BILERP);
    gDPSetTexturePersp(glistp++, G_TP_PERSP);

    switch (renderModeSetting) {
      case TextureAndLightingRenderMode:
      case LightingNoTextureRenderMode:
        if (getLightingType(obj) == OnlyAmbientLighting) {
          gSPSetLights0(glistp++, amb_light);

        } else {
          gSPSetLights1(glistp++, sun_light);
        }
        break;
      default:
        break;
    }

    switch (renderModeSetting) {
      case ToonFlatShadingRenderMode:
        // if (Renderer_isLitGameObject(obj)) {
        //   gSPSetGeometryMode(glistp++, G_SHADE | G_LIGHTING | G_CULL_BACK);
        // } else {
        //   gSPSetGeometryMode(glistp++, G_SHADE | G_CULL_BACK);
        // }

        gSPSetGeometryMode(glistp++, G_SHADE | G_CULL_BACK);
        gDPSetCombineMode(glistp++, G_CC_DECALRGB, G_CC_DECALRGB);
        break;
      case TextureNoLightingRenderMode:
        gSPSetGeometryMode(glistp++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        gDPSetCombineMode(glistp++, G_CC_DECALRGB, G_CC_DECALRGB);
        break;
      case TextureAndLightingRenderMode:
        gSPSetGeometryMode(
            glistp++, G_SHADE | G_SHADING_SMOOTH | G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(glistp++, G_CC_MODULATERGB, G_CC_MODULATERGB);
        break;
      case LightingNoTextureRenderMode:
        gSPSetGeometryMode(
            glistp++, G_SHADE | G_SHADING_SMOOTH | G_LIGHTING | G_CULL_BACK);
        gDPSetCombineMode(glistp++, G_CC_SHADE, G_CC_SHADE);
        break;
      default:  // NoTextureNoLightingRenderMode
        gDPSetPrimColor(glistp++, 0, 0, /*r*/ 180, /*g*/ 180, /*b*/ 180,
                        /*a*/ 255);
        gSPSetGeometryMode(glistp++, G_CULL_BACK);
        gDPSetCombineMode(glistp++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    }

    // set the transform in world space for the gameobject to render
    guPosition(&obj->objTransform,
               0.0F,             // rot x
               obj->rotation.y,  // rot y
               0.0F,             // rot z
               1.0F,             // scale
               obj->position.x,  // pos x
               obj->position.y,  // pos y
               obj->position.z   // pos z
    );
    gSPMatrix(
        glistp++, OS_K0_TO_PHYSICAL(&(obj->objTransform)),
        G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);  // gameobject mtx start

    if (Renderer_isAnimatedGameObject(obj)) {
      // case for multi-part objects using rigid body animation
      profStartAnim = CUR_TIME_MS();

      modelMeshParts = getAnimationNumModelMeshParts(obj->modelType);
      curAnimRange = getCurrentAnimationRange(obj);
      AnimationInterpolation_calc(&animInterp, obj->animState, curAnimRange);

      for (modelMeshIdx = 0; modelMeshIdx < modelMeshParts; ++modelMeshIdx) {
        profStartAnimLerp = CUR_TIME_MS();
        // lerping takes about 0.2ms per bone
        if (shouldLerpAnimation(obj->modelType)) {
          AnimationFrame_lerp(
              &animInterp,  // result of AnimationInterpolation_calc()
              getAnimData(
                  obj->modelType),  // pointer to start of AnimationFrame list
              modelMeshParts,       // num bones in rig used by animData
              modelMeshIdx,  // index of bone in rig to produce transform for
              &animFrame     // the resultant interpolated animation frame
          );
        } else {
          AnimationFrame_get(
              &animInterp,  // result of AnimationInterpolation_calc()
              getAnimData(
                  obj->modelType),  // pointer to start of AnimationFrame list
              modelMeshParts,       // num bones in rig used by animData
              modelMeshIdx,  // index of bone in rig to produce transform for
              &animFrame     // the resultant interpolated animation frame
          );
        }
        Trace_addEvent(AnimLerpTraceEvent, profStartAnimLerp, CUR_TIME_MS());

        // push matrix with the blender to n64 coord rotation, then mulitply
        // it by the model's rotation and offset

        // rotate from z-up (blender) to y-up (opengl) coords
        // TODO: move as many of these transformations as possible to
        // be precomputed in animation data
        guRotate(&dynamicp->zUpToYUpCoordinatesRotation, -90.0f, 1, 0, 0);
        gSPMatrix(glistp++,
                  OS_K0_TO_PHYSICAL(&(dynamicp->zUpToYUpCoordinatesRotation)),
                  G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);

        guPosition(&obj->animState->animMeshTransform[modelMeshIdx],
                   animFrame.rotation.x,  // roll
                   animFrame.rotation.y,  // pitch
                   animFrame.rotation.z,  // yaw
                   1.0F,                  // scale
                   animFrame.position.x,  // pos x
                   animFrame.position.y,  // pos y
                   animFrame.position.z   // pos z
        );
        gSPMatrix(glistp++,
                  OS_K0_TO_PHYSICAL(
                      &(obj->animState->animMeshTransform[modelMeshIdx])),
                  G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);

        gSPDisplayList(glistp++, getMeshDisplayListForModelMeshPart(
                                     obj->modelType, animFrame.object));

        attachment = &obj->animState->attachment;
        if (attachment->modelType != NoneModel &&
            attachment->boneIndex == modelMeshIdx) {
          guPosition(&obj->animState->attachmentTransform,
                     attachment->rotation.x,  // roll
                     attachment->rotation.y,  // pitch
                     attachment->rotation.z,  // yaw
                     1.0F,                    // scale
                     attachment->offset.x,    // pos x
                     attachment->offset.y,    // pos y
                     attachment->offset.z     // pos z
          );
          gSPMatrix(glistp++,
                    OS_K0_TO_PHYSICAL(&(obj->animState->attachmentTransform)),
                    G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
          modelDisplayList = getModelDisplayList(attachment->modelType);
          gSPDisplayList(glistp++, modelDisplayList);
          gSPPopMatrix(glistp++, G_MTX_MODELVIEW);
        }

        gSPPopMatrix(glistp++, G_MTX_MODELVIEW);
      }
      Trace_addEvent(DrawAnimTraceEvent, profStartAnim, CUR_TIME_MS());
    } else {
      // case for simple gameobjects with no moving sub-parts
      modelDisplayList = getModelDisplayList(obj->modelType);

      gSPDisplayList(glistp++, modelDisplayList);
    }

    gSPPopMatrix(glistp++, G_MTX_MODELVIEW);  // gameobject mtx end

    gDPPipeSync(glistp++);
  }

  free(rendererSortDistance);

  Trace_addEvent(DrawIterTraceEvent, profStartIter, CUR_TIME_MS());
}
