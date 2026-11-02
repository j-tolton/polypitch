#include "polypitch.h"
#include <emscripten/emscripten.h>
#include <memory>
#include <vector>
static std::unique_ptr<polypitch::Engine> e; static std::vector<float> in;
extern "C" {
EMSCRIPTEN_KEEPALIVE int pp_init(int sr,int n,int hop){polypitch::Config c;c.sampleRate=sr;c.fftSize=n;c.hop=hop;e=std::make_unique<polypitch::Engine>(c);in.resize(n);return n;}
EMSCRIPTEN_KEEPALIVE float* pp_input(){return in.data();}
EMSCRIPTEN_KEEPALIVE void pp_analyse(){if(e)e->analyse(in.data(),in.size());}
EMSCRIPTEN_KEEPALIVE const float* pp_lane_midi(){return e?e->laneMidi():nullptr;}
EMSCRIPTEN_KEEPALIVE const float* pp_lane_evidence(){return e?e->laneEvidence():nullptr;}
EMSCRIPTEN_KEEPALIVE const float* pp_lane_pitch(){return e?e->lanePitch():nullptr;}
EMSCRIPTEN_KEEPALIVE int pp_lane_count(){return e?e->laneCount():0;}
EMSCRIPTEN_KEEPALIVE const float* pp_peak_midi(){return e?e->peakMidi():nullptr;}
EMSCRIPTEN_KEEPALIVE const float* pp_peak_strength(){return e?e->peakStrength():nullptr;}
EMSCRIPTEN_KEEPALIVE int pp_peak_count(){return e?e->peakCount():0;}
EMSCRIPTEN_KEEPALIVE float pp_noise_floor(){return e?e->noiseFloor():-120.f;}

EMSCRIPTEN_KEEPALIVE int pp_frame_count(){return e?e->frameCount():0;}
EMSCRIPTEN_KEEPALIVE float pp_input_rms(){return e?e->inputRms():0.f;}
EMSCRIPTEN_KEEPALIVE float pp_spectral_centroid(){return e?e->spectralCentroid():0.f;}
EMSCRIPTEN_KEEPALIVE float pp_background_mean(){return e?e->backgroundMean():0.f;}
EMSCRIPTEN_KEEPALIVE float pp_residual_mean(){return e?e->residualMean():0.f;}
EMSCRIPTEN_KEEPALIVE const float* pp_debug_top_hz(){return e?e->debugTopHz():nullptr;}
EMSCRIPTEN_KEEPALIVE const float* pp_debug_top_db(){return e?e->debugTopDb():nullptr;}
EMSCRIPTEN_KEEPALIVE int pp_debug_top_count(){return e?e->debugTopCount():0;}
}
