#include "plugin.hpp"

Plugin* pluginInstance;

// Declare all module models
extern Model* modelSynTHBloK;
extern Model* modelSynTHBloKFX;
extern Model* modelSynTHBloKMix24;
extern Model* modelSynTHBloKPoly;
extern Model* modelSynTHBloKStereo;
extern Model* modelNoizeeFlagshipFilter;
extern Model* modelSynTHBloKFilter;
extern Model* modelBloKMaster;
extern Model* modelSynTHBloKPlayer;
extern Model* modelSynTHBloKKick;
extern Model* modelSynTHBloKPlayerView;
extern Model* modelSynTHBloKSnareHat;
extern Model* modelSynTHBloKEuclid;
extern Model* modelSynTHBloKJamProbe;
extern Model* modelSynTHBloKJam;
extern Model* modelSynTHBloKMatrix8;
extern Model* modelSynTHBloKGrainForge;
void init(Plugin* p) {
    pluginInstance = p;

    p->addModel(modelSynTHBloK);
    p->addModel(modelSynTHBloKFX);
    p->addModel(modelSynTHBloKMix24);
    p->addModel(modelSynTHBloKPoly);
    p->addModel(modelSynTHBloKStereo);
    p->addModel(modelNoizeeFlagshipFilter);
    p->addModel(modelSynTHBloKFilter);
    p->addModel(modelBloKMaster);
    p->addModel(modelSynTHBloKPlayer);
    p->addModel(modelSynTHBloKKick);
    p->addModel(modelSynTHBloKPlayerView);
    p->addModel(modelSynTHBloKSnareHat);
    p->addModel(modelSynTHBloKEuclid);
    p->addModel(modelSynTHBloKJamProbe);
    p->addModel(modelSynTHBloKJam);
    p->addModel(modelSynTHBloKMatrix8);
    p->addModel(modelSynTHBloKGrainForge);
}

