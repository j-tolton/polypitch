#include "polypitch.h"
#include "tracker.h"
#include <emscripten/emscripten.h>
#include <limits>
#include <memory>
#include <vector>
static std::unique_ptr<polypitch::Engine> e; static std::vector<float> in;
static polypitch::Tracker tracker; static std::vector<double> ridgeOut,retintRecords;
// Retinting passes each ridge as: centre, age, from, tint, inTune. Earlier frame first.
static constexpr int RETINT_FIELDS=5;
static std::vector<polypitch::Ridge> readRetint(int first,int count){std::vector<polypitch::Ridge> rs(count);for(int i=0;i<count;i++){const double* q=&retintRecords[(first+i)*RETINT_FIELDS];rs[i].centre=q[0];rs[i].age=(int)q[1];rs[i].from=(int)q[2];rs[i].tint=q[3];rs[i].inTune=q[4]!=0;}return rs;}
extern "C" {
EMSCRIPTEN_KEEPALIVE int pp_init(int sr,int n,int hop){polypitch::Config c;c.sampleRate=sr;c.fftSize=n;c.hop=hop;e=std::make_unique<polypitch::Engine>(c);in.resize(n);tracker.reset();return n;}
EMSCRIPTEN_KEEPALIVE float* pp_input(){return in.data();}
EMSCRIPTEN_KEEPALIVE void pp_analyse(){if(e)e->analyse(in.data(),in.size());}
// Links this frame's lanes into ridges. Each gives: m, core, centre, tint, alpha, prev, prev2, age, from, inTune. No prev is NaN.
EMSCRIPTEN_KEEPALIVE int pp_track(int canBridge,int targets){if(!e)return 0;const auto& rs=tracker.track(e->laneEvidence(),e->lanePitch(),e->laneCount(),canBridge!=0,targets);const double none=std::numeric_limits<double>::quiet_NaN();ridgeOut.clear();for(const auto& r:rs)ridgeOut.insert(ridgeOut.end(),{r.m,r.core,r.centre,r.tint,polypitch::ridgeAlpha(r),r.hasPrev?r.prev:none,r.hasPrev2?r.prev2:none,(double)r.age,(double)r.from,r.inTune?1.0:0.0});return (int)rs.size();}
EMSCRIPTEN_KEEPALIVE const double* pp_ridges(){return ridgeOut.data();}
EMSCRIPTEN_KEEPALIVE double* pp_retint_records(int count){retintRecords.resize((size_t)count*RETINT_FIELDS);return retintRecords.data();}
EMSCRIPTEN_KEEPALIVE void pp_retint(int prevCount,int count,int targets){auto prev=readRetint(0,prevCount);auto cur=readRetint(prevCount,count);polypitch::tintRidges(cur,prevCount?&prev:nullptr,targets);for(int i=0;i<count;i++){double* q=&retintRecords[(prevCount+i)*RETINT_FIELDS];q[3]=cur[i].tint;q[4]=cur[i].inTune?1:0;}}
EMSCRIPTEN_KEEPALIVE void pp_adopt_tints(int prevCount,int count){tracker.adoptTints(readRetint(prevCount,count));}
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
