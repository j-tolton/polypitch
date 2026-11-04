#pragma once
#include "polypitch.h"
#include <vector>
namespace polypitch {
// With no need to keep up with live sound there's time to analyse twice. A long window is needed
// to tell low notes apart, while a short one follows quick notes and separates voices better
// higher up. Both are centred on the same moment.
class DualEngine {
public:
 static constexpr int LONG_WINDOW=8192,SHORT_WINDOW=4096;
 static constexpr float SPLIT_MIDI=52.f;
 explicit DualEngine(int sampleRate,int hop=1024):longEngine(make(sampleRate,LONG_WINDOW,hop)),shortEngine(make(sampleRate,SHORT_WINDOW,hop)){
  evidence.resize(longEngine.laneCount());pitch.resize(longEngine.laneCount());
 }
 // Takes LONG_WINDOW samples.
 void analyse(const float* window){
  longEngine.analyse(window,LONG_WINDOW);
  shortEngine.analyse(window+(LONG_WINDOW-SHORT_WINDOW)/2,SHORT_WINDOW);
  const float* midi=longEngine.laneMidi();
  for(int i=0;i<laneCount();i++){
   const Engine& from=midi[i]<SPLIT_MIDI?longEngine:shortEngine;
   evidence[i]=from.laneEvidence()[i];pitch[i]=from.lanePitch()[i];
  }
  peakM.clear();peakS.clear();
  for(int i=0;i<longEngine.peakCount();i++)if(longEngine.peakMidi()[i]<SPLIT_MIDI){peakM.push_back(longEngine.peakMidi()[i]);peakS.push_back(longEngine.peakStrength()[i]);}
  for(int i=0;i<shortEngine.peakCount();i++)if(shortEngine.peakMidi()[i]>=SPLIT_MIDI){peakM.push_back(shortEngine.peakMidi()[i]);peakS.push_back(shortEngine.peakStrength()[i]);}
 }
 const float* laneEvidence()const{return evidence.data();}
 const float* lanePitch()const{return pitch.data();}
 const float* laneMidi()const{return longEngine.laneMidi();}
 int laneCount()const{return longEngine.laneCount();}
 const float* peakMidi()const{return peakM.data();}
 const float* peakStrength()const{return peakS.data();}
 int peakCount()const{return (int)peakM.size();}
 float inputRms()const{return longEngine.inputRms();}
private:
 static Config make(int sampleRate,int window,int hop){Config c;c.sampleRate=sampleRate;c.fftSize=window;c.hop=hop;return c;}
 Engine longEngine,shortEngine;
 std::vector<float> evidence,pitch,peakM,peakS;
};
}
