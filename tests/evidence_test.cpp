#include "polypitch.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
using polypitch::Engine; using polypitch::Config;
static constexpr double PI=3.14159265358979323846;
static float hz(float m){return 440.f*std::pow(2.f,(m-69.f)/12.f);} 
static float midi(float f){return 69.f+12.f*std::log2(f/440.f);} 
static std::vector<float> tone(int sr,int n,float f,const std::vector<float>& h,float phase=0){std::vector<float>x(n);for(int i=0;i<n;i++){double t=(double)i/sr;for(size_t k=0;k<h.size();k++)x[i]+=h[k]*std::sin(2*PI*f*(k+1)*t+phase);}return x;}
static float best(Engine&e){auto m=e.laneMidi(),v=e.laneEvidence();int n=e.laneCount(),bi=0;for(int i=1;i<n;i++)if(v[i]>v[bi])bi=i;return m[bi];}
static void calibrate(Engine&e,int n){std::vector<float>z(n);unsigned u=1;for(int j=0;j<35;j++){for(int i=0;i<n;i++){u=1664525u*u+1013904223u;z[i]=((int)(u>>8)/8388608.f-1.f)*0.0008f;}e.analyse(z.data(),z.size());}}
static bool near(float a,float b,float cents){return std::fabs(a-b)*100<=cents;}
int main(){Config c;c.sampleRate=48000;c.fftSize=8192;c.minMidi=28;c.maxMidi=100;c.laneStep=.05f;int fails=0;
 auto run=[&](const char*label,float f,std::vector<float>h,float tol){Engine e(c);calibrate(e,c.fftSize);auto x=tone(c.sampleRate,c.fftSize,f,h);for(int i=0;i<3;i++)e.analyse(x.data(),x.size());float got=best(e),want=midi(f);printf("%-24s want %7.2f got %7.2f err %+6.1fc\n",label,want,got,(got-want)*100);if(!near(got,want,tol))fails++;};
 run("sine 98Hz",98,{1},35);run("rich 98Hz",98,{.25f,1,.65f,.42f,.25f,.16f},35);run("weak fundamental 98",98,{.06f,1,.65f,.42f,.25f,.16f},45);run("rich 65.4Hz",65.406,{.18f,1,.7f,.45f,.3f,.18f},45);
 // gliss frames: fresh engine, calibration then sequential independent windows
 {Engine e(c);calibrate(e,c.fftSize);float fs[]={150,175,200,225,250,225,200,175,150};for(float f:fs){auto x=tone(c.sampleRate,c.fftSize,f,{.3f,1,.55f,.3f});e.analyse(x.data(),x.size());float got=best(e),want=midi(f);printf("gliss %6.1fHz           want %7.2f got %7.2f err %+6.1fc\n",f,want,got,(got-want)*100);if(!near(got,want,55))fails++;}}
 // two simultaneous harmonic-rich notes: both fundamentals should be strong local evidence peaks
 {Engine e(c);calibrate(e,c.fftSize);auto a=tone(c.sampleRate,c.fftSize,98,{.22f,1,.6f,.35f,.2f});auto b=tone(c.sampleRate,c.fftSize,146.832,{.25f,1,.55f,.3f,.18f});for(int i=0;i<c.fftSize;i++)a[i]=.55f*a[i]+.55f*b[i];for(int k=0;k<3;k++)e.analyse(a.data(),a.size());auto lm=e.laneMidi(),ev=e.laneEvidence();float e1=0,e2=0;for(int i=0;i<e.laneCount();i++){if(std::fabs(lm[i]-midi(98))<.25)e1=std::max(e1,ev[i]);if(std::fabs(lm[i]-midi(146.832))<.25)e2=std::max(e2,ev[i]);}printf("double 98+146.8Hz       evidence %.3f %.3f\n",e1,e2);if(e1<.45f||e2<.45f)fails++;}

 // long hold then descending real-voice-like timbre: regression for live latch/disappearance
 {Engine e(c);calibrate(e,c.fftSize);int frame=0;float fs[]={220,210,200,190,180,170,160,150,140};
  for(int k=0;k<120;k++){float wob=220.f*std::pow(2.f,(4.f*std::sin(k*.19f))/1200.f);auto x=tone(c.sampleRate,c.fftSize,wob,{.10f,.75f,1.f,.28f,.65f,.12f,.35f});e.analyse(x.data(),x.size());}
  for(float f:fs){for(int rep=0;rep<3;rep++){float amp=.20f+.80f*((rep+1)/3.f);auto x=tone(c.sampleRate,c.fftSize,f,{.04f,.55f,1.f,.18f,.72f,.10f,.30f},rep*.7f);for(float &q:x)q*=amp;e.analyse(x.data(),x.size());}float got=best(e),want=midi(f);printf("post-hold descend %6.1fHz want %7.2f got %7.2f err %+6.1fc\n",f,want,got,(got-want)*100);if(!near(got,want,65))fails++;}}

 // intra-window chirps: pitch changes during the 170ms FFT window, as a real hum does.
 {Engine e(c);calibrate(e,c.fftSize);float pairs[][2]={{150,170},{170,195},{195,220},{220,195},{195,170},{170,150}};
  for(auto &pr:pairs){std::vector<float>x(c.fftSize);double ph[7]={0};for(int i=0;i<c.fftSize;i++){float u=(float)i/(c.fftSize-1),f=pr[0]+(pr[1]-pr[0])*u;float hs[7]={.05f,.62f,1.f,.18f,.68f,.10f,.34f};for(int h=1;h<=7;h++){ph[h-1]+=2*PI*f*h/c.sampleRate;x[i]+=hs[h-1]*sin(ph[h-1]);}}e.analyse(x.data(),x.size());float mid=.5f*(pr[0]+pr[1]),got=best(e),want=midi(mid);printf("chirp %3.0f->%3.0fHz          want %7.2f got %7.2f err %+6.1fc\n",pr[0],pr[1],want,got,(got-want)*100);if(!near(got,want,90))fails++;}}

 // harmonic-authority regression: rich 98Hz should favour the observed fundamental over its 2f/3f harmonics.
 {Engine e(c);calibrate(e,c.fftSize);auto x=tone(c.sampleRate,c.fftSize,98,{.12f,1.f,.72f,.48f,.32f,.20f});for(int k=0;k<3;k++)e.analyse(x.data(),x.size());auto lm=e.laneMidi(),ev=e.laneEvidence();float ef=0,e2=0,e3=0;for(int i=0;i<e.laneCount();i++){if(std::fabs(lm[i]-midi(98))<.20)ef=std::max(ef,ev[i]);if(std::fabs(lm[i]-midi(196))<.20)e2=std::max(e2,ev[i]);if(std::fabs(lm[i]-midi(294))<.20)e3=std::max(e3,ev[i]);}printf("harmonic authority 98Hz   fundamental %.3f 2f %.3f 3f %.3f\n",ef,e2,e3);if(ef<e2*1.15f||ef<e3*1.15f)fails++;}

 // octave double-stop guard: an independently present octave must survive soft harmonic suppression.
 {Engine e(c);calibrate(e,c.fftSize);auto a=tone(c.sampleRate,c.fftSize,98,{.28f,.75f,.40f,.22f});auto b=tone(c.sampleRate,c.fftSize,196,{.55f,.65f,.32f,.18f});for(int i=0;i<c.fftSize;i++)a[i]=.55f*a[i]+.55f*b[i];for(int k=0;k<3;k++)e.analyse(a.data(),a.size());auto lm=e.laneMidi(),ev=e.laneEvidence();float e1=0,e2=0;for(int i=0;i<e.laneCount();i++){if(std::fabs(lm[i]-midi(98))<.25)e1=std::max(e1,ev[i]);if(std::fabs(lm[i]-midi(196))<.25)e2=std::max(e2,ev[i]);}printf("octave double 98+196Hz   evidence %.3f %.3f\n",e1,e2);if(e1<.20f||e2<.12f)fails++;}

 // octave trap: a tone whose actual fundamental is 196Hz must not be pulled to 98Hz merely by even-series alignment.
 {Engine e(c);calibrate(e,c.fftSize);auto x=tone(c.sampleRate,c.fftSize,196,{.18f,1.f,.62f,.38f,.22f});for(int k=0;k<3;k++)e.analyse(x.data(),x.size());float got=best(e),want=midi(196);printf("octave trap 196Hz        want %7.2f got %7.2f err %+6.1fc\n",want,got,(got-want)*100);if(!near(got,want,55))fails++;}
 // pitch accuracy at the app's real 10-cent lanes: the strongest lane must report the played pitch, and lanes strong enough to light a ridge (about 0.7 of the strongest) must agree on it.
 {Config pc;for(float root:{28.f,33.f,36.f,43.f,50.f,57.f,69.f,76.f})for(float ct:{-9.f,0.f,3.f,5.f,7.f,13.f}){Engine e(pc);calibrate(e,pc.fftSize);float want=root+ct/100.f;auto x=tone(pc.sampleRate,pc.fftSize,hz(want),{.5f,1,.6f,.35f,.2f,.1f});for(float &q:x)q*=.2f;for(int k=0;k<3;k++)e.analyse(x.data(),x.size());auto ev=e.laneEvidence();auto lp=e.lanePitch();int bi=0;for(int i=1;i<e.laneCount();i++)if(ev[i]>ev[bi])bi=i;float lo=lp[bi],hi=lp[bi];for(int i=0;i<e.laneCount();i++)if(std::fabs(e.laneMidi()[i]-want)<.5f&&ev[i]>ev[bi]*.7f){lo=std::min(lo,lp[i]);hi=std::max(hi,lp[i]);}printf("accuracy %5.2f            got %7.2f err %+6.1fc spread %4.1fc\n",want,lp[bi],(lp[bi]-want)*100,(hi-lo)*100);if(!near(lp[bi],want,4)||(hi-lo)*100>4)fails++;}}
 // no calibration wait: a note sounding from the very first frame must be seen at once, including after digital silence, and must stay seen.
 for(int silent:{0,8}){Config pc;Engine e(pc);std::vector<float>z(pc.fftSize);for(int k=0;k<silent;k++)e.analyse(z.data(),z.size());auto x=tone(pc.sampleRate,pc.fftSize,220,{.5f,1,.6f,.35f,.2f,.1f});for(float &q:x)q*=.2f;e.analyse(x.data(),x.size());auto ev=e.laneEvidence();auto lp=e.lanePitch();int bi=0;for(int i=1;i<e.laneCount();i++)if(ev[i]>ev[bi])bi=i;printf("first frame after %d silent  got %7.2f err %+6.1fc evidence %.3f\n",silent,lp[bi],(lp[bi]-57)*100,ev[bi]);if(!near(lp[bi],57,4)||ev[bi]<.5f)fails++;for(int k=0;k<120;k++)e.analyse(x.data(),x.size());float held=0;for(int i=0;i<e.laneCount();i++)if(std::fabs(e.laneMidi()[i]-57)<.25f)held=std::max(held,ev[i]);printf("held 5 s from first frame    evidence %.3f\n",held);if(held<.5f)fails++;}
 // room noise alone from the first frame must not look like a note.
 {Config pc;Engine e(pc);std::vector<float>z(pc.fftSize);unsigned u=7;float worst=0;for(int j=0;j<60;j++){for(int i=0;i<pc.fftSize;i++){u=1664525u*u+1013904223u;z[i]=((int)(u>>8)/8388608.f-1.f)*.002f;}e.analyse(z.data(),z.size());for(int i=0;i<e.laneCount();i++)worst=std::max(worst,e.laneEvidence()[i]);}printf("noise only from first frame  worst evidence %.3f\n",worst);if(worst>.5f)fails++;}
 printf("FAILURES %d\n",fails);return fails?1:0;}
