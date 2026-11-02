#pragma once
#include <cstddef>
#include <vector>
namespace polypitch {
struct Config { int sampleRate=48000; int fftSize=8192; int hop=1024; float minMidi=26.f,maxMidi=100.f,laneStep=.10f; };
class Engine {
public:
 explicit Engine(const Config&);
 void analyse(const float*,std::size_t);
 const float* laneMidi()const{return midi.data();}
 const float* laneEvidence()const{return evidence.data();}
 const float* lanePitch()const{return pitch.data();}
 int laneCount()const{return (int)midi.size();}
 const float* peakMidi()const{return peakMidiV.data();}
 const float* peakStrength()const{return peakStrengthV.data();}
 int peakCount()const{return (int)peakMidiV.size();}
 float noiseFloor()const{return noise;}
 int frameCount()const{return analysedFrames;}
 float inputRms()const{return debugRms;}
 float spectralCentroid()const{return debugCentroid;}
 float backgroundMean()const{return debugBgMean;}
 float residualMean()const{return debugResidualMean;}
 const float* debugTopHz()const{return debugHz;}
 const float* debugTopDb()const{return debugDb;}
 int debugTopCount()const{return 6;}
private:
 Config c;
 std::vector<float> re,im,spec,midi,evidence,pitch,peakMidiV,peakStrengthV,bgDb;
 float noise=-105.f; int analysedFrames=0,bgFrames=2; bool bgReady=false;
 std::vector<float> steadyDb; std::vector<int> steadyFrames;
 float debugRms=0.f,debugCentroid=0.f,debugBgMean=0.f,debugResidualMean=0.f;
 float debugHz[6]={0},debugDb[6]={-140,-140,-140,-140,-140,-140};
 void fft(); float at(float)const; static float hz(float);
};
}
