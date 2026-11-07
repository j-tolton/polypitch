#include "polypitch.h"
#include <algorithm>
#include <cmath>
namespace { constexpr float PI=3.14159265358979323846f; constexpr float STEADY_DB=1.5f; constexpr float STEADY_SECONDS=15.f; constexpr float RELATIVE_FULL_DB=20.f; constexpr float RELATIVE_ZERO_DB=50.f; }
namespace polypitch {
float Engine::hz(float m){return 440.f*std::pow(2.f,(m-69.f)/12.f);} Engine::Engine(const Config& x):c(x){re.resize(c.fftSize);im.resize(c.fftSize);spec.resize(c.fftSize/2+1);bgDb.assign(c.fftSize/2+1,-140.f);for(float m=c.minMidi;m<=c.maxMidi+.001f;m+=c.laneStep)midi.push_back(m);evidence.resize(midi.size());pitch=midi;steadyFrames.assign(c.fftSize/2+1,0);}
void Engine::fft(){int n=c.fftSize;for(int i=1,j=0;i<n;i++){int b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){std::swap(re[i],re[j]);std::swap(im[i],im[j]);}}for(int l=2;l<=n;l<<=1){float a=-2*PI/l,cr=cos(a),ci=sin(a);for(int i=0;i<n;i+=l){float wr=1,wi=0;for(int j=0;j<l/2;j++){int k=i+j+l/2;float vr=re[k]*wr-im[k]*wi,vi=re[k]*wi+im[k]*wr,ur=re[i+j],ui=im[i+j];re[i+j]=ur+vr;im[i+j]=ui+vi;re[k]=ur-vr;im[k]=ui-vi;float q=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=q;}}}}
float Engine::at(float f)const{float b=f*c.fftSize/c.sampleRate;int i=(int)floor(b);float t=b-i;if(i<0||i+1>=(int)spec.size())return -140;float r0=spec[i]-bgDb[i]-3.5f,r1=spec[i+1]-bgDb[i+1]-3.5f;return r0*(1-t)+r1*t;}
void Engine::analyse(const float* p,std::size_t n){if(!p||n<(size_t)c.fftSize)return;
 double rmsAcc=0;for(int i=0;i<c.fftSize;i++)rmsAcc+=(double)p[i]*p[i];debugRms=std::sqrt(rmsAcc/c.fftSize);
 for(int i=0;i<c.fftSize;i++){float w=.5f-.5f*cos(2*PI*i/(c.fftSize-1));re[i]=p[i]*w;im[i]=0;}fft();
for(int i=0;i<=c.fftSize/2;i++)spec[i]=20*log10(hypot(re[i],im[i])/(c.fftSize*.5f)+1e-12f);
 // Diagnostics from the raw FFT, before room subtraction.
 double cNum=0,cDen=0;for(int i=1;i<c.fftSize/2;i++){float f=(float)i*c.sampleRate/c.fftSize;if(f<30||f>6000)continue;double a=std::pow(10.0,spec[i]/20.0);cNum+=f*a;cDen+=a;}debugCentroid=cDen>0?(float)(cNum/cDen):0.f;
 for(int j=0;j<6;j++){debugHz[j]=0;debugDb[j]=-140;}
 for(int i=2;i<c.fftSize/2-1;i++){if(spec[i]<=spec[i-1]||spec[i]<spec[i+1])continue;float f=(float)i*c.sampleRate/c.fftSize;if(f<30||f>6000)continue;for(int j=0;j<6;j++)if(spec[i]>debugDb[j]){for(int q=5;q>j;q--){debugDb[q]=debugDb[q-1];debugHz[q]=debugHz[q-1];}debugDb[j]=spec[i];debugHz[j]=f;break;}}

analysedFrames++;
// A microphone can deliver digital silence while it opens. That says nothing about the room.
if(!bgReady&&debugRms<1e-6f){
 std::fill(evidence.begin(),evidence.end(),0.f);
 peakMidiV.clear(); peakStrengthV.clear();
 return;
}
// Adaptive per-bin background model, learned on the fly with no quiet period. It starts
// from the first frame's low spectral floor, so a note already sounding stands out.
// Stationary room tone is then followed slowly, while rises are protected so a sustained
// played note is not learned away.
const bool firstFrame=!bgReady;
// Learning rates below are per 2048-sample hop, so they keep their real-time speed.
const float perHop=c.hop/2048.f;
const int steadyLimit=(int)(STEADY_SECONDS*c.sampleRate/c.hop);
if(firstFrame){
 // The floor is taken from each bin's neighbourhood, because rooms are far louder in the
 // bass, and never from the bin alone, which could start in a deep momentary dip or on a
 // note. The 28th percentile of noise sits about 5 dB below its mean level.
 const int blockBins=16,reach=32,top=c.fftSize/2-1;
 std::vector<float> near;
 for(int start=1;start<=top;start+=blockBins){
  int from=std::max(1,start-reach),to=std::min(top,start+reach);
  near.assign(spec.begin()+from,spec.begin()+to+1);
  size_t nk=(size_t)(near.size()*.28f);std::nth_element(near.begin(),near.begin()+nk,near.end());
  for(int i=start;i<std::min(top+1,start+blockBins);i++)bgDb[i]=near[nk]+5.f;
 }
 steadyDb=spec;
 bgReady=true;
}else for(int i=1;i<c.fftSize/2;i++){
 float x=spec[i];
 // A rise across the whole spectrum is the room changing, not a note, so only a rise
 // beyond the current broad noise level counts as protected.
 float rise=x-bgDb[i]-std::max(0.f,noise);
 float hzBin=(float)i*c.sampleRate/c.fftSize;
 // Bass fundamentals can rise only a few dB above persistent low-frequency
 // webcam/room energy. Protect those rises much earlier than upper-band noise.
 float protectRise = hzBin < 260.f ? 2.5f : 6.f;
 // Unprotected bins average quickly at first, so the room is known within moments.
 float rate = rise > protectRise ? (hzBin < 260.f ? .0002f : .0008f)*perHop : std::max(.010f*perHop,1.f/bgFrames);
 // Mains hum and fans hold their level exactly; a played note keeps moving. A level
 // unchanged for this long is room tone, however far it stands above the background.
 if(std::fabs(x-steadyDb[i])<=STEADY_DB){steadyDb[i]+=.02f*perHop*(x-steadyDb[i]);steadyFrames[i]++;}
 else{steadyDb[i]=x;steadyFrames[i]=0;}
 if(steadyFrames[i]>=steadyLimit)rate=std::max(rate,.02f*perHop);
 // A sound that has stopped leaves the background quickly.
 if(x<bgDb[i]-6.f)rate=.05f*perHop;
 bgDb[i] += rate*(x-bgDb[i]);
}
bgFrames++;
double bgSum=0,resSum=0;int dbgN=0;for(int i=1;i<c.fftSize/2;i++){float f=(float)i*c.sampleRate/c.fftSize;if(f>=30&&f<=6000){bgSum+=bgDb[i];resSum+=spec[i]-bgDb[i]-3.5f;dbgN++;}}debugBgMean=dbgN?(float)(bgSum/dbgN):0;debugResidualMean=dbgN?(float)(resSum/dbgN):0;
// Work in residual dB above learned background. A small margin makes stable
// webcam/fan/electrical components disappear once learned.
std::vector<float> residual(spec.size(),-140.f);
for(size_t i=0;i<spec.size();i++) residual[i]=spec[i]-bgDb[i]-3.5f;

std::vector<float> v;int a=ceil(30.f*c.fftSize/c.sampleRate),b=std::min(c.fftSize/2,(int)floor(6000.f*c.fftSize/c.sampleRate));for(int i=a;i<=b;i++)v.push_back(residual[i]);size_t k=(size_t)(v.size()*.28f);std::nth_element(v.begin(),v.begin()+k,v.end());noise=firstFrame?v[k]:noise+(v[k]-noise)*.035f*perHop;float floorDb=std::clamp(noise+5.f,-12.f,12.f),ceilDb=floorDb+42.f;
// Keep a second, absolute spectral floor for low-frequency rescue. This is the
// useful part of the 0.3 detector: real bass energy is allowed to survive even
// when per-bin room subtraction leaves only a modest residual rise.
std::vector<float> rawV;rawV.reserve(b-a+1);for(int i=a;i<=b;i++)rawV.push_back(spec[i]);size_t rk=(size_t)(rawV.size()*.28f);std::nth_element(rawV.begin(),rawV.begin()+rk,rawV.end());float rawFloor=rawV[rk]+8.f;
auto amp=[&](float f){return std::clamp((at(f)-floorDb)/(ceilDb-floorDb),0.f,1.f);};for(size_t i=0;i<midi.size();i++){float f=hz(midi[i]),d=0;for(float ct:{-5.f,-2.5f,0.f,2.5f,5.f})d=std::max(d,amp(f*pow(2.f,ct/1200.f)));float sl=amp(f*pow(2.f,-18.f/1200.f)),sh=amp(f*pow(2.f,18.f/1200.f));float contrast=std::max(0.f,d-(sl+sh)*.5f);evidence[i]=pow(d,1.55f)*(.70f+.30f*std::min(1.f,contrast/.18f));}

// Extract actual local spectral maxima. These are observations, not inferred notes.
// Quadratic interpolation gives a sub-bin frequency estimate.
peakMidiV.clear(); peakStrengthV.clear();
// Start a bin below the lowest lane so a note at the bottom of the range keeps its peak.
int pLo=std::max(2,(int)std::floor(hz(c.minMidi)*c.fftSize/c.sampleRate)-1);
std::vector<float> peakDb; std::vector<int> peakBin;
int pHi=std::min(c.fftSize/2-2,(int)std::floor(hz(c.maxMidi)*c.fftSize/c.sampleRate));
for(int kbin=pLo;kbin<=pHi;kbin++){
 float binHz=(float)kbin*c.sampleRate/c.fftSize;
 float r0=residual[kbin-1], r1=residual[kbin], r2=residual[kbin+1];
 float s0=spec[kbin-1], s1=spec[kbin], s2=spec[kbin+1];
 float above=r1-floorDb;
 // A raw maximum counts too: a note learned into the background moments ago can flatten
 // the residual's shape, which must not stop the same note being heard again.
 bool residualPeak=((r1>r0 && r1>=r2) || (s1>s0 && s1>=s2)) && above>=7.f;
 // Below 260 Hz, also accept an observed raw spectral maximum when it is well
 // above the absolute floor AND has risen above its learned room baseline. The
 // latter condition prevents steady fan/mains/room components from returning.
 float rawAbove=s1-rawFloor;
 bool bassRescue=(binHz<260.f && s1>s0 && s1>=s2 && rawAbove>=7.f && r1>=1.25f);
 if(!residualPeak && !bassRescue) continue;
 // Locate the peak on the raw spectrum: subtracting the per-bin background
 // distorts the peak's shape and so biases the estimate.
 bool rawPeak=(s1>=s0 && s1>=s2);
 float y0=rawPeak?s0:r0, y1=rawPeak?s1:r1, y2=rawPeak?s2:r2;
 float den=(y0-2.f*y1+y2);
 float delta=std::fabs(den)>1e-6f ? .5f*(y0-y2)/den : 0.f;
 delta=std::clamp(delta,-.5f,.5f);
 float f=(kbin+delta)*c.sampleRate/c.fftSize;
 if(f<=0.f) continue;
 float m=69.f+12.f*std::log2(f/440.f);
 float residualStrength=std::clamp((above-7.f)/45.f,0.f,1.f);
 float rescueStrength=std::clamp((rawAbove-7.f)/38.f,0.f,1.f)*std::clamp((r1-1.25f)/7.f,0.f,1.f);
 float strength=std::max(residualStrength,rescueStrength);
 peakMidiV.push_back(m); peakStrengthV.push_back(strength); peakDb.push_back(s1); peakBin.push_back(kbin);
}
// A much quieter peak right beside a loud one is that peak's window sidelobe, not a sound.
{
 std::vector<bool> sidelobe(peakDb.size(),false);
 for(size_t j=0;j<peakDb.size();j++)for(size_t q=0;q<peakDb.size();q++)
  if(std::abs(peakBin[j]-peakBin[q])<=5&&peakDb[q]-peakDb[j]>=25.f){sidelobe[j]=true;break;}
 size_t kept=0;
 for(size_t j=0;j<peakDb.size();j++)if(!sidelobe[j]){peakMidiV[kept]=peakMidiV[j];peakStrengthV[kept]=peakStrengthV[j];peakDb[kept]=peakDb[j];kept++;}
 peakMidiV.resize(kept);peakStrengthV.resize(kept);peakDb.resize(kept);
}
// Height above the background says nothing once the background is very low: a clean
// recording would rate every faint peak as highly as the note. Strength is also capped by
// how far a peak sits below the frame's loudest. Learned room tone is no longer a peak here,
// so it can't set this reference.
{
 float loudest=-1e9f;for(float d:peakDb)loudest=std::max(loudest,d);
 for(size_t j=0;j<peakDb.size();j++)peakStrengthV[j]=std::min(peakStrengthV[j],std::clamp((peakDb[j]-loudest+RELATIVE_ZERO_DB)/(RELATIVE_ZERO_DB-RELATIVE_FULL_DB),0.f,1.f));
}

// Continuous pitch-evidence field.  Spectral peaks are observations; each pitch
// lane asks how well a harmonic family rooted at that pitch explains them.
// Odd-partial support is deliberately important: it prevents a strong 2f/4f/6f
// pattern from being misread as an unobserved octave-lower fundamental.
std::fill(evidence.begin(),evidence.end(),0.f);
auto peakNear=[&](float targetMidi,float tol,float& at){
 float best=0.f,loudest=-1e9f;
 for(size_t j=0;j<peakMidiV.size();j++){
   float d=std::fabs(peakMidiV[j]-targetMidi);
   if(d>tol)continue;
   float q=1.f-d/tol;
   best=std::max(best,peakStrengthV[j]*(.35f+.65f*q*q));
   // Position comes from the loudest peak in reach: a nearer, weaker one is often
   // just a window sidelobe of a strong partial.
   if(peakDb[j]>loudest){loudest=peakDb[j];at=peakMidiV[j];}
 }
 return best;
};
for(size_t i=0;i<midi.size();i++){
 float root=midi[i], f=hz(root);
 // FFT resolution expressed in semitones.  Interpolated peaks are much better
 // than a raw bin, but low notes still need a wider association window.
 float binHz=(float)c.sampleRate/c.fftSize;
 float tol=std::clamp(12.f*std::log2((f+binHz*.45f)/f),.16f,.72f);
 float h[8]={0},implied[8]={0};
 int top=-1;
 for(int n=1;n<=8;n++){
   float tf=f*n;if(tf>c.sampleRate*.48f)break;
   float tm=69.f+12.f*std::log2(tf/440.f),at=tm;
   h[n-1]=peakNear(tm,std::max(.14f,tol),at);
   implied[n-1]=at-(tm-root);
   if(h[n-1]>0.f&&(top<0||h[n-1]>h[top]))top=n-1;
 }
 // Lanes only score candidates. The pitch a lane reports comes from the peaks that
 // support it: the root each implies, averaged over those agreeing with the strongest.
 pitch[i]=root;
 if(top>=0){
  float sum=0.f,weight=0.f;
  for(int n=0;n<8;n++)if(h[n]>0.f&&std::fabs(implied[n]-implied[top])<=.15f){sum+=h[n]*implied[n];weight+=h[n];}
  pitch[i]=sum/weight;
 }
 float direct=h[0];
 float odd=.75f*h[2]+.48f*h[4]+.30f*h[6];
 float even=.70f*h[1]+.40f*h[3]+.24f*h[5]+.16f*h[7];
 float coverage=0.f; int covered=0;
 for(int n=1;n<=6;n++)if(h[n-1]>.035f){coverage+=1.f/(float)n;covered++;}
 // A real weak fundamental can be rescued by 2f plus odd harmonics.  A proposed
 // subharmonic supported only by even partials receives very little confidence.
 float identity=direct + odd*.82f;
 // Even partials alone say nothing: they could all belong to a note an octave above. Without
 // an odd partial they don't count, so faint energy below a note can't borrow its strength.
 float evenTrust=std::clamp(odd/.1f,0.f,1.f);
 float family=direct*1.15f + odd + even*.62f*evenTrust;
 float oddGate=std::clamp((identity-.018f)/.16f,0.f,1.f);
 float covGate=std::clamp((coverage-.20f)/1.15f,0.f,1.f);
 float score=family*(.22f+.78f*std::max(oddGate,covGate*.72f)) + direct*1.10f;
 if(direct<.012f && odd<.035f)score*=.12f;
 if(covered<2)score*=.55f;
 evidence[i]=std::max(0.f,score);
}
// Normalize first, then reduce upper candidates that are already well explained
// by a stronger observed lower harmonic family. This is deliberately a soft
// redistribution: harmonics remain visible, and a genuinely independent upper
// note can still retain substantial evidence.
for(float &x:evidence)x=std::clamp(x/3.5f,0.f,1.f);
std::vector<float> baseEvidence=evidence;
for(size_t i=0;i<midi.size();i++){
  float upper=midi[i], shadow=0.f;
  for(int n=2;n<=6;n++){
    float lower=upper-12.f*std::log2((float)n);
    if(lower<c.minMidi)continue;
    int j=(int)std::lround((lower-c.minMidi)/c.laneStep);
    if(j<0||j>=(int)baseEvidence.size())continue;
    float lo=baseEvidence[j], hi=baseEvidence[i];
    if(lo<.08f||lo<=hi*.72f)continue;
    // Strong lower-family evidence explains an upper harmonic increasingly well,
    // but never erases it. Exact octaves get a slightly gentler reduction so
    // octave double-stops remain possible.
    float dominance=std::clamp((lo-hi*.72f)/(.32f+hi),0.f,1.f);
    float explain=(n==2?.48f:.62f)*dominance;
    shadow=std::max(shadow,explain);
  }
  evidence[i]=baseEvidence[i]*(1.f-shadow);
}
}
}
