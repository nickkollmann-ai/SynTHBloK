#pragma once
#include "rack.hpp"

using namespace rack;


// -------------------------------------------------------------

extern Plugin* pluginInstance;

extern Model* modelSynTHBloK;
extern Model* modelSynTHBloKPoly;
extern Model* modelSynTHBloKStereo;
extern Model* modelSynTHBloKFX;
extern Model* modelSynTHBloKMix24;
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
extern Model* modelSynTHBloKDuo;
extern Model* modelSynTHBloKMix12;
extern Model* modelSynTHBloKMix8;
extern Model* modelSynTHBloKEcho;

// VCV-size jack (23.7 px, the same as Rack's PJ301M): black body with a silver ring.
// Used on every module but the JAM and MIX-24.
struct BlackJack : app::SvgPort {
    BlackJack() { setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/JackBlackSilver.svg"))); }
};

// Small version of BlackJack (5.2 mm), for the JAM and MIX-24 modules.
struct SmallJack : app::SvgPort {
    SmallJack() { setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/JackSmall.svg"))); }
};

struct MainToPoly {
    int   chainId;
    bool reset = false;
    float spread = 0.f;
    float cutoff = 0.f;  
    float env = 0.f;
    float vca = 0.f;
    float resonance;
    float drive;
    float filterMode; 
};

struct PolyToMain {
    float pitch[8];
    float env[8];
    float vca[8];
    float flt[8];
    int voices;
};

struct MainToStereo {
    float env;
    float accent;
    float postVCA;
};
// -------------------------------------------------------------
// INTERNAL 6‑STEM STEREO BUS (GLOBAL SHARED MIX BUS)
// -------------------------------------------------------------
struct SynTHBloKInternalBus {
    float L[6];
    float R[6];
};

