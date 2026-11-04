#include "refine.h"
#include <algorithm>
#include <cmath>
namespace {
using namespace polypitch;
constexpr double GATE_RATIO=.0316; // 30 dB
constexpr double STITCH_SECONDS=.08,STITCH_REACH=.35;
constexpr double NOTE_SECONDS=.17,BRIEF_SECONDS=.1,LASTING_SECONDS=.3;
// Against the whole track's level, since in a frame with nothing else even noise is the loudest thing.
constexpr double NOTE_LEVEL=.1,BRIEF_LEVEL=.4,LASTING_LEVEL=.2;
constexpr double HARMONIC_REACH=.2,HARMONIC_SHARE=.7,HARMONIC_SPREAD=.09,HARMONIC_RATIO=.8;
constexpr double LEVEL_POWER=.6,MAX_LIFT=4;
// A consonant or breath shows up across the spectrum at once, as many ridges in one frame.
constexpr double CROWD_LEVEL=.25;constexpr int CROWDED=5;
// Too weak for the live ranking. These only count if they have a spectral peak of their own which
// isn't a multiple of anything louder, because one voice alone makes no such peak.
constexpr double ALONGSIDE_CORE=.5,OWN_PEAK=.25,PEAK_REACH=.25,OWN_PEAK_SHARE=.6,SERIES_REACH=.2,WEAK_HAIR_REACH=.6;
// Even then it might be a faint voice or something else in the recording, so it's shown as a line
// but plainly dimmer than the notes we're sure of, and it has no say in the key.
constexpr double WEAK_SHOWN=.3,KEY_CORE=.35;
constexpr int SERIES_TOP=8;
constexpr double PI=3.14159265358979323846;
constexpr int HELD_FRAMES=12,JOIN_PASSES=3;
// A faint voice breaks into pieces that each look like noise. What gives it away is that they keep
// coming back to one pitch while another voice is singing, where a consonant scatters and is gone.
constexpr double RETURN_GAP=.2,RETURN_REACH=.4,RETURN_SECONDS=.3,RETURN_DENSITY=.5,ALONGSIDE_SHARE=.7,ALONGSIDE_LEVEL=.3,HAIR_REACH=.3,RECOVERED_LEVEL=.35;
constexpr double REST_WEIGHT=1,JOIN_REACH=.5,CLOSE_CALL=.1;
struct Point{int frame,index;};
bool hasOwnPeak(const Ridge& r,const std::vector<Ridge>& frame){
 if(r.peak<OWN_PEAK)return false;
 for(const Ridge& q:frame){
  if(q.w<=r.w||q.m>=r.m)continue;
  for(int n=2;n<=SERIES_TOP;n++)if(std::fabs(r.m-q.m-12*std::log2((double)n))<=SERIES_REACH)return false;
 }
 return true;
}
struct Track{std::vector<Point> points;int group=-1;};
double median(std::vector<double> v){if(v.empty())return 0;const size_t k=v.size()/2;std::nth_element(v.begin(),v.begin()+k,v.end());return v[k];}
struct Group{std::vector<Point> points;double medianW=0,medianCore=0,reference=0,overall=0;int span=0;bool keep=true,harmonic=false,recovered=false;};
// A faint harmonic follows the note below it exactly. A sung note wanders on its own.
bool isHarmonicOf(const Group& g,const std::vector<std::vector<Ridge>>& frames){
 // Negative n looks upwards, for the phantom that can appear an octave or a twelfth below a note.
 for(int n:{2,3,4,5,6,-2,-3}){
  const double interval=(n>0?12:-12)*std::log2((double)std::abs(n));
  int supported=0;std::vector<double> gaps;double lowerSum=0;
  for(const Point& p:g.points){
   const Ridge& r=frames[p.frame][p.index];const Ridge* best=nullptr;
   for(const Ridge& q:frames[p.frame])if(std::fabs(r.m-q.m-interval)<=HARMONIC_REACH&&q.w>r.w&&(!best||q.w>best->w))best=&q;
   if(best){supported++;gaps.push_back(r.m-best->m-interval);lowerSum+=best->w;}
  }
  if(supported<g.points.size()*HARMONIC_SHARE)continue;
  double mean=0;for(double x:gaps)mean+=x;mean/=gaps.size();
  double spread=0;for(double x:gaps)spread+=(x-mean)*(x-mean);spread=std::sqrt(spread/gaps.size());
  if(spread<=HARMONIC_SPREAD&&g.medianW<lowerSum/supported*HARMONIC_RATIO)return true;
 }
 return false;
}
// Follows each ridge from frame to frame, and gathers the pieces of a note that dips out for a moment.
void buildGroups(const std::vector<std::vector<Ridge>>& frames,double fps,double stitchSeconds,double stitchReach,std::vector<Track>& tracks,std::vector<std::vector<int>>& trackOf,std::vector<Group>& groups){
 const int frameCount=(int)frames.size();
 trackOf.assign(frameCount,{});
 // When several ridges continue one, the nearest does.
 for(int k=0;k<frameCount;k++){
  trackOf[k].assign(frames[k].size(),-1);
  std::vector<int> child(k>0?frames[k-1].size():0,-1);
  for(int i=0;i<(int)frames[k].size();i++){
   const int from=frames[k][i].from;if(from<0||k==0||from>=(int)child.size())continue;
   const double d=std::fabs(frames[k][i].m-frames[k-1][from].m);
   if(child[from]<0||d<std::fabs(frames[k][child[from]].m-frames[k-1][from].m))child[from]=i;
  }
  for(int from=0;from<(int)child.size();from++)if(child[from]>=0){
   const int t=trackOf[k-1][from];trackOf[k][child[from]]=t;tracks[t].points.push_back({k,child[from]});
  }
  for(int i=0;i<(int)frames[k].size();i++)if(trackOf[k][i]<0){trackOf[k][i]=(int)tracks.size();tracks.push_back({});tracks.back().points.push_back({k,i});}
 }
 const int stitchFrames=std::max(1,(int)std::lround(stitchSeconds*fps));
 std::vector<std::vector<int>> startingAt(frameCount);
 for(int t=0;t<(int)tracks.size();t++)startingAt[tracks[t].points.front().frame].push_back(t);
 for(int t=0;t<(int)tracks.size();t++){
  if(tracks[t].group>=0)continue;
  const int g=(int)groups.size();groups.push_back({});
  for(int at=t;at>=0;){
   tracks[at].group=g;
   groups[g].points.insert(groups[g].points.end(),tracks[at].points.begin(),tracks[at].points.end());
   const Point end=tracks[at].points.back();const double m=frames[end.frame][end.index].m;
   int next=-1;double nearest=stitchReach;
   for(int f=end.frame+1;f<=std::min(frameCount-1,end.frame+stitchFrames)&&next<0;f++)for(int u:startingAt[f])if(tracks[u].group<0){
    const Point st=tracks[u].points.front();const double d=std::fabs(frames[st.frame][st.index].m-m);
    if(d<=nearest){nearest=d;next=u;}
   }
   at=next;
  }
 }
}
// A voice too faint for the live ranking may still be there. Added to the finished trace, only
// where nothing confident already is, so the lines we're sure of aren't disturbed.
void addFaintVoices(std::vector<std::vector<Ridge>>& out,const std::vector<std::vector<Ridge>>& candidates,double fps,double overall,RefineStats& stats){
 std::vector<Track> tracks;std::vector<std::vector<int>> trackOf;std::vector<Group> groups;
 buildGroups(candidates,fps,RETURN_GAP,RETURN_REACH,tracks,trackOf,groups);
 const int frameCount=(int)candidates.size(),lasting=(int)std::lround(LASTING_SECONDS*fps);
 std::vector<int> order(groups.size());
 for(int i=0;i<(int)groups.size();i++)order[i]=i;
 std::sort(order.begin(),order.end(),[&](int a,int b){return groups[a].points.size()>groups[b].points.size();});
 std::vector<std::vector<int>> added(frameCount);
 for(int k=0;k<frameCount;k++)added[k].assign(candidates[k].size(),-1);
 std::vector<std::vector<int>> groupAt(frameCount);
 for(int index:order){
  Group& g=groups[index];
  std::vector<double> w;std::vector<int> seen;int own=0,hairs=0,alongside=0;
  for(const Point& p:g.points){
   const Ridge& r=candidates[p.frame][p.index];
   w.push_back(r.w);seen.push_back(p.frame);
   if(hasOwnPeak(r,candidates[p.frame]))own++;
   bool near=false,far=false;
   for(const Ridge& q:out[p.frame]){if(std::fabs(q.m-r.m)<=WEAK_HAIR_REACH)near=true;else if(std::fabs(q.m-r.m)>1&&q.core>=ALONGSIDE_CORE)far=true;}
   if(near)hairs++;if(far)alongside++;
  }
  std::sort(seen.begin(),seen.end());seen.erase(std::unique(seen.begin(),seen.end()),seen.end());
  const int present=(int)seen.size(),length=seen.empty()?0:seen.back()-seen.front()+1;
  g.span=(int)g.points.size();g.medianW=median(w);
  if(present<lasting||present<length*RETURN_DENSITY||g.medianW<overall*NOTE_LEVEL)continue;
  if(own<g.span*OWN_PEAK_SHARE||hairs*2>g.span||alongside<g.span*ALONGSIDE_SHARE)continue;
  if(isHarmonicOf(g,candidates))continue;
  stats.faintVoices++;
  for(const Point& p:g.points){
   Ridge r=candidates[p.frame][p.index];
   bool close=false;for(const Ridge& q:out[p.frame])if(std::fabs(q.m-r.m)<=HAIR_REACH)close=true;
   if(close)continue;
   r.core=WEAK_SHOWN*std::min(1.0,std::sqrt(r.w/g.medianW));
   r.hasPrev=false;r.hasPrev2=false;r.age=RIDGE_AGE_FULL;
   added[p.frame][p.index]=(int)out[p.frame].size();out[p.frame].push_back(r);
  }
 }
 for(int k=0;k<frameCount;k++)for(int i=0;i<(int)candidates[k].size();i++){
  const int mine=added[k][i];if(mine<0)continue;
  Ridge& r=out[k][mine];
  const int from=candidates[k][i].from;
  const int parent=k>0&&from>=0&&from<(int)added[k-1].size()?added[k-1][from]:-1;
  r.from=parent;
  if(parent>=0){
   const Ridge& q=out[k-1][parent];r.age=q.age+1;
   if(r.core>=RIDGE_STRONG&&q.core>=RIDGE_STRONG){r.prev=q.m;r.hasPrev=true;r.prev2=q.prev;r.hasPrev2=q.hasPrev;}
  }
 }
}
}
namespace polypitch {
void markPeaks(std::vector<Ridge>& ridges,const float* peakMidi,const float* peakStrength,int peakCount){
 for(Ridge& r:ridges){r.peak=0;for(int i=0;i<peakCount;i++)if(std::fabs(peakMidi[i]-r.m)<=PEAK_REACH)r.peak=std::max(r.peak,(double)peakStrength[i]);}
}
RefineStats refineTrace(std::vector<std::vector<Ridge>>& frames,double fps,int targets,std::vector<RemovedGroup>* removed,const std::vector<std::vector<Ridge>>* candidates,const std::vector<double>* levels){
 RefineStats stats;
 const int frameCount=(int)frames.size();
 // Well below the track's playing level there's nothing worth reading, however clear it looks.
 std::vector<std::vector<Ridge>> gatedCandidates;
 if(levels&&(int)levels->size()==frameCount){
  std::vector<double> sorted(*levels);std::sort(sorted.begin(),sorted.end());
  const double floorLevel=sorted.empty()?0:sorted[(size_t)(sorted.size()*.9)]*GATE_RATIO;
  if(candidates){gatedCandidates=*candidates;candidates=&gatedCandidates;}
  for(int k=0;k<frameCount;k++)if((*levels)[k]<floorLevel){frames[k].clear();if(candidates)gatedCandidates[k].clear();stats.gatedFrames++;}
 }
 std::vector<Track> tracks;
 std::vector<std::vector<int>> trackOf;
 std::vector<Group> groups;
 buildGroups(frames,fps,STITCH_SECONDS,STITCH_REACH,tracks,trackOf,groups);
 stats.tracks=(int)tracks.size();
 const int stitchFrames=std::max(1,(int)std::lround(STITCH_SECONDS*fps));
 // How loud things are around each moment, and overall.
 std::vector<double> loudest(frameCount,0.0),voiced;
 std::vector<int> crowd(frameCount,0);
 for(int k=0;k<frameCount;k++){
  for(const Ridge& r:frames[k])loudest[k]=std::max(loudest[k],r.w);
  for(const Ridge& r:frames[k])if(r.w>=loudest[k]*CROWD_LEVEL)crowd[k]++;
  if(loudest[k]>0)voiced.push_back(loudest[k]);
 }
 std::sort(voiced.begin(),voiced.end());
 const double overall=voiced.empty()?0:voiced[(size_t)(voiced.size()*.9)];
 const int noteFrames=(int)std::lround(NOTE_SECONDS*fps),briefFrames=(int)std::lround(BRIEF_SECONDS*fps),lastingFrames=(int)std::lround(LASTING_SECONDS*fps);
 for(Group& g:groups){
  std::vector<double> w,ref,cores,crowds;
  for(const Point& p:g.points){w.push_back(frames[p.frame][p.index].w);ref.push_back(loudest[p.frame]);cores.push_back(frames[p.frame][p.index].core);crowds.push_back(crowd[p.frame]);}
  g.medianW=median(w);g.medianCore=median(cores);g.reference=median(ref);g.overall=overall;g.span=(int)g.points.size();
  const double toAll=overall>0?g.medianW/overall:0;
  // A real note lasts, or if it's brief, is as strong as the singing around it.
  if(g.span<briefFrames||(g.span<noteFrames&&toAll<BRIEF_LEVEL)){g.keep=false;stats.tooShort++;}
  else if(toAll<NOTE_LEVEL){g.keep=false;stats.tooFaint++;}
  else if(g.span<lastingFrames&&median(crowds)>=CROWDED&&g.medianW<g.reference*.9){g.keep=false;stats.tooShort++;}
 }
 for(Group& g:groups)if(g.keep&&isHarmonicOf(g,frames)){g.keep=false;g.harmonic=true;stats.harmonics++;}
 // Put together any faint voice that has broken into pieces.
 {
  const auto keptNear=[&](int k,double m,double reach,bool wantFar){
   // Is a surviving ridge here, within reach of this pitch, or a strong one well away from it?
   for(int i=0;i<(int)frames[k].size();i++){
    const Group& other=groups[tracks[trackOf[k][i]].group];if(!other.keep)continue;
    const Ridge& r=frames[k][i];
    if(wantFar?(std::fabs(r.m-m)>1&&r.w>=overall*ALONGSIDE_LEVEL):std::fabs(r.m-m)<=reach)return true;
   }
   return false;
  };
  struct Piece{int group,first,last;double midi;bool used;};
  std::vector<Piece> pieces;
  for(int i=0;i<(int)groups.size();i++){
   const Group& g=groups[i];if(g.keep||g.harmonic||g.span<2)continue;
   std::vector<double> ms;int hairs=0;
   for(const Point& p:g.points){const double m=frames[p.frame][p.index].m;ms.push_back(m);if(keptNear(p.frame,m,HAIR_REACH,false))hairs++;}
   // Beside a note that's staying, this is only that note's fringe.
   if(hairs*2>g.span)continue;
   pieces.push_back({i,g.points.front().frame,g.points.back().frame,median(ms),false});
  }
  std::sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.first<b.first;});
  const int returnGap=(int)std::lround(RETURN_GAP*fps),returnFrames=(int)std::lround(RETURN_SECONDS*fps);
  for(size_t a=0;a<pieces.size();a++){
   if(pieces[a].used)continue;
   std::vector<size_t> chain{a};int end=pieces[a].last;double pitch=pieces[a].midi;
   for(size_t b=a+1;b<pieces.size()&&pieces[b].first<=end+returnGap;b++){
    if(pieces[b].used||std::fabs(pieces[b].midi-pitch)>RETURN_REACH)continue;
    chain.push_back(b);end=std::max(end,pieces[b].last);pitch=pitch*.7+pieces[b].midi*.3;
   }
   Group joined;std::vector<int> seen;
   for(size_t c:chain)for(const Point& p:groups[pieces[c].group].points){joined.points.push_back(p);seen.push_back(p.frame);}
   std::sort(seen.begin(),seen.end());seen.erase(std::unique(seen.begin(),seen.end()),seen.end());
   const int present=(int)seen.size(),length=end-pieces[a].first+1;
   if(present<returnFrames||present<length*RETURN_DENSITY)continue;
   int alongside=0;for(int k:seen)if(keptNear(k,pitch,0,true))alongside++;
   if(alongside<present*ALONGSIDE_SHARE)continue;
   // A chain that shadows a louder note is still only its harmonic.
   std::vector<double> w,ref,cores;
   for(const Point& p:joined.points){w.push_back(frames[p.frame][p.index].w);ref.push_back(loudest[p.frame]);cores.push_back(frames[p.frame][p.index].core);}
   joined.medianW=median(w);joined.reference=median(ref);joined.medianCore=median(cores);joined.overall=overall;joined.span=(int)joined.points.size();
   if(isHarmonicOf(joined,frames))continue;
   joined.keep=true;joined.recovered=true;
   const int home=pieces[a].group;
   for(size_t c:chain){
    const int from=pieces[c].group;pieces[c].used=true;
    for(const Point& p:groups[from].points)tracks[trackOf[p.frame][p.index]].group=home;
    if(from!=home){groups[from]=Group{};groups[from].keep=false;}
   }
   groups[home]=joined;
   stats.recovered++;
  }
 }
 // A scoop into a note or a slide between two is too brief to stand alone, but it joins on to
 // something that can. Line by line, what connects to a note is part of it.
 for(int pass=0;pass<JOIN_PASSES;pass++){
  std::vector<std::vector<int>> endsAt(frameCount);
  for(int i=0;i<(int)groups.size();i++)if(groups[i].keep){endsAt[groups[i].points.front().frame].push_back(i);endsAt[groups[i].points.back().frame].push_back(i);}
  const auto touches=[&](const Point& p){
   const double m=frames[p.frame][p.index].m;
   for(int f=std::max(0,p.frame-stitchFrames);f<=std::min(frameCount-1,p.frame+stitchFrames);f++)for(int other:endsAt[f])for(const Point& q:{groups[other].points.front(),groups[other].points.back()})
    if(q.frame==f&&std::fabs(frames[q.frame][q.index].m-m)<=JOIN_REACH)return true;
   return false;
  };
  int added=0;
  for(Group& g:groups){
   if(g.keep||g.harmonic||g.points.empty()||g.medianW<overall*NOTE_LEVEL||g.span<2)continue;
   if(touches(g.points.front())||touches(g.points.back())){g.keep=true;added++;stats.joined++;}
  }
  if(added==0)break;
 }
 if(removed)for(const Group& g:groups)if(g.recovered||(!g.keep&&!g.points.empty())){
  std::vector<double> ms;for(const Point& p:g.points)ms.push_back(frames[p.frame][p.index].m);
  removed->push_back({g.points.front().frame,g.span,median(ms),overall>0?g.medianW/overall:0,g.recovered?"RECOVERED":g.harmonic?"harmonic":g.medianW<overall*NOTE_LEVEL?"faint":"short"});
 }
 // Rebuild the frames from what's left. Brightness now comes from the note's own level, less
 // harshly than a single moment's ranking, while its shape within the note is kept.
 std::vector<std::vector<Ridge>> out(frameCount);
 std::vector<std::vector<int>> newIndex(frameCount);
 for(int k=0;k<frameCount;k++){
  newIndex[k].assign(frames[k].size(),-1);
  for(int i=0;i<(int)frames[k].size();i++){
   const Track& t=tracks[trackOf[k][i]];const Group& g=groups[t.group];if(!g.keep)continue;
   Ridge r=frames[k][i];
   if(g.recovered){
    // It's real but faint, so it shows at a steady, modest level of its own.
    const double level=std::max(RECOVERED_LEVEL,g.reference>0?std::pow(std::min(1.0,g.medianW/g.reference),LEVEL_POWER):0);
    r.core=std::max(r.core,level*std::min(1.0,std::sqrt(r.w/g.medianW)));
   }
   else if(g.span>=lastingFrames&&g.medianW>=g.overall*LASTING_LEVEL){
    // Lifts the whole note, so what's weak within it stays weak.
    const double level=g.reference>0?std::pow(std::min(1.0,g.medianW/g.reference),LEVEL_POWER):0;
    if(g.medianCore>0&&g.medianCore<level)r.core=std::min(1.0,r.core*std::min(MAX_LIFT,level/g.medianCore));
   }
   newIndex[k][i]=(int)out[k].size();out[k].push_back(r);
  }
 }
 for(int k=0;k<frameCount;k++)for(int i=0;i<(int)frames[k].size();i++){
  const int mine=newIndex[k][i];if(mine<0)continue;
  Ridge& r=out[k][mine];
  // Joined to whatever it continued from, if that's still here.
  const int from=frames[k][i].from;
  const int parent=k>0&&from>=0&&from<(int)newIndex[k-1].size()?newIndex[k-1][from]:-1;
  r.from=parent;r.hasPrev=false;r.hasPrev2=false;
  // A note that has proved itself needs no time to earn its brightness.
  const Group& g=groups[tracks[trackOf[k][i]].group];
  r.age=parent>=0?out[k-1][parent].age+1:(g.span>=noteFrames?RIDGE_AGE_FULL:0);
  if(parent>=0&&r.core>=RIDGE_STRONG&&out[k-1][parent].core>=RIDGE_STRONG){
   const Ridge& q=out[k-1][parent];r.prev=q.m;r.hasPrev=true;r.prev2=q.prev;r.hasPrev2=q.hasPrev;
  }
 }
 if(candidates&&(int)candidates->size()==frameCount)addFaintVoices(out,*candidates,fps,overall,stats);
 for(int k=0;k<frameCount;k++)tintRidges(out[k],k>0?&out[k-1]:nullptr,targets);
 frames.swap(out);
 for(const Group& g:groups)if(g.keep)stats.kept++;
 return stats;
}
KeyGuess guessKey(const std::vector<std::vector<Ridge>>& frames){
 KeyGuess guess;
 // The track's own tuning, from where its settled notes sit relative to the semitones.
 double sn=0,cs=0,settled=0;
 for(const auto& rs:frames)for(const Ridge& r:rs){
  if(r.core<.5||r.age<RIDGE_AGE_FULL*2)continue;
  const double a=2*PI*(r.centre-std::floor(r.centre));sn+=r.core*std::sin(a);cs+=r.core*std::cos(a);settled+=r.core;
 }
 if(settled<=0)return guess;
 guess.tuningCents=std::atan2(sn,cs)/(2*PI)*100;
 // Near one when the notes all sit the same way, and near nothing when they're all over.
 guess.tuningFocus=std::hypot(sn,cs)/settled;
 const double offset=guess.tuningCents/100;
 // Where held notes come to rest says more about home than how long anything sounds.
 double finals[12]={0},finalTotal=0;
 for(size_t k=0;k+1<frames.size();k++){
  std::vector<bool> continues(frames[k].size(),false);
  for(const Ridge& r:frames[k+1])if(r.from>=0&&r.from<(int)continues.size())continues[r.from]=true;
  for(size_t i=0;i<frames[k].size();i++){
   const Ridge& r=frames[k][i];if(continues[i]||r.age<HELD_FRAMES||r.core<KEY_CORE)continue;
   const double m=r.centre-guess.tuningCents/100,near=std::floor(m+.5);if(std::fabs(m-near)>.35)continue;
   const double held=std::min(1.0,r.age/(double)(HELD_FRAMES*4));finals[(((int)near%12)+12)%12]+=held;finalTotal+=held;
  }
 }
 // How long each pitch class sounds.
 double weight[12]={0},total=0;
 for(const auto& rs:frames)for(const Ridge& r:rs){
  if(r.core<KEY_CORE)continue;
  const double m=r.centre-offset,near=std::floor(m+.5);
  if(std::fabs(m-near)>.35)continue;
  const int pc=(((int)near%12)+12)%12;weight[pc]+=r.core;total+=r.core;
 }
 for(int i=0;i<12;i++){guess.weight[i]=total>0?weight[i]/total:0;guess.finals[i]=finalTotal>0?finals[i]/finalTotal:0;}
 if(total<=0)return guess;
 // Krumhansl and Kessler's key profiles.
 static constexpr double major[12]={6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
 static constexpr double minor[12]={6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};
 const auto correlate=[&](const double* profile,int root){
  double ma=0,mb=0;for(int i=0;i<12;i++){ma+=profile[i];mb+=weight[i];}ma/=12;mb/=12;
  double ab=0,aa=0,bb=0;
  for(int i=0;i<12;i++){const double a=profile[i]-ma,b=weight[(root+i)%12]-mb;ab+=a*b;aa+=a*a;bb+=b*b;}
  return aa>0&&bb>0?ab/std::sqrt(aa*bb):0.0;
 };
 // A melody can dwell on any note of its scale, which can fool a profile into a neighbouring
 // key. So find the notes first: the seven that account for most of what was sung.
 static constexpr int majorSteps[7]={0,2,4,5,7,9,11};
 int scaleRoot=0;double most=-1;
 for(int root=0;root<12;root++){
  double inside=0;for(int step:majorSteps)inside+=guess.weight[(root+step)%12];
  if(inside>most){most=inside;scaleRoot=root;}
 }
 // Those notes make a major key or its relative minor. The shape of the singing, and where its
 // phrases come to rest, say which feels like home.
 const auto homeScore=[&](const double* profile,int root){return correlate(profile,root)+REST_WEIGHT*(guess.finals[root]+.5*guess.finals[(root+7)%12]);};
 const int minorRoot=(scaleRoot+9)%12;
 const double asMajor=homeScore(major,scaleRoot),asMinor=homeScore(minor,minorRoot);
 // When it's this close they're much the same answer, and major is the commoner home.
 guess.minor=asMinor>asMajor+CLOSE_CALL;
 guess.root=guess.minor?minorRoot:scaleRoot;
 guess.nextMinor=!guess.minor;
 guess.nextRoot=guess.minor?scaleRoot:minorRoot;
 guess.score=std::max(asMajor,asMinor);guess.nextScore=std::min(asMajor,asMinor);
 guess.found=true;
 return guess;
}
}
