
#ifndef ANIMATION_H
#define ANIMATION_H

#include "modeltype.h"
#include "n64compat.h"
#include "rotation.h"
#include "sprite.h"
#include "vec3d.h"

// max num bones per character
// on n64 we allocate this many transform matrices per character
#define MAX_ANIM_MESH_PARTS 12

// data for one frame of an animation
// for character models, this is the data for one model part/bone
typedef struct AnimationFrame {
  int frame;
  int object;
  Vec3d position;
  EulerDegrees rotation;

} AnimationFrame;

typedef struct AnimationRange {
  int start;
  int end;
} AnimationRange;

typedef struct AnimationBoneAttachment {
  int boneIndex;
  ModelType modelType;
  Vec3d offset;
  EulerDegrees rotation;
} AnimationBoneAttachment;

typedef struct AnimationBoneSpriteAttachment {
  int boneIndex;
  SpriteType spriteType;
  int startTick;
  Vec3d offset;
} AnimationBoneSpriteAttachment;

typedef struct AnimationState {
  int state;
  float progress;
  // for each bone, used for the n64 renderer
  Mtx animMeshTransform[MAX_ANIM_MESH_PARTS];
  // same but for attachment (there can only be one)
  Mtx attachmentTransform;
  Mtx attachmentSpriteTransform;

  AnimationBoneAttachment attachment;
  AnimationBoneSpriteAttachment spriteAttachment;
} AnimationState;

typedef struct AnimationInterpolation {
  int currentFrame;
  int nextFrame;
  float t;
} AnimationInterpolation;

void AnimationState_init(AnimationState* self);

void AnimationBoneAttachment_init(AnimationBoneAttachment* self);

void AnimationBoneSpriteAttachment_init(AnimationBoneSpriteAttachment* self);

void AnimationInterpolation_calc(AnimationInterpolation* self,
                                 AnimationState* state,
                                 AnimationRange* animRange);

void AnimationFrame_get(
    AnimationInterpolation* interp,  // result of AnimationInterpolation_calc()
    AnimationFrame* animData,        // pointer to start of AnimationFrame list
                                     // exported for some rig
    int animDataNumBones,            // num bones in rig used by animData
    int boneIdx,            // index of bone in rig to produce transform for
    AnimationFrame* result  // the resultant   animation frame
);

void AnimationFrame_lerp(
    AnimationInterpolation* interp,  // result of AnimationInterpolation_calc()
    AnimationFrame* animData,        // pointer to start of AnimationFrame list
                                     // exported for some rig
    int animDataNumBones,            // num bones in rig used by animData
    int boneIdx,            // index of bone in rig to produce transform for
    AnimationFrame* result  // the resultant interpolated animation frame
);

#endif /* !ANIMATION_H */
