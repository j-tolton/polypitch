#include "tracker.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace { constexpr double RIDGE_GAP=.04,RIDGE_REACH=.45,RIDGE_FULL_WEIGHT=.15,DOT_ALPHA=.34,GREEN_GAIN_CENTS=9,GREEN_KEEP_CENTS=15,TINT_EASE=.3; constexpr int TINT_AGE=5; }
namespace polypitch {
double targetCents(double m,int targets){
 // Cents to the nearest target note: any note, or only the selected scale's notes.
 const int n=(int)std::floor(m+.5);if(targets==ANY_NOTE)return std::fabs(m-n)*100;
 double best=std::numeric_limits<double>::infinity();
 for(int k=n-2;k<=n+2;k++)if(targets>>((k%12+12)%12)&1)best=std::min(best,std::fabs(m-k));
 return best*100;
}
double inTuneAmount(double cents){
 // Colour starts at 15 cents, is obvious by about 9, strong at 5 and full by 3.
 static constexpr double stops[4][2]={{3,1},{5,.82},{9,.5},{15,0}};
 if(cents<=3)return 1;
 for(int i=1;i<4;i++)if(cents<=stops[i][0]){const double c0=stops[i-1][0],t0=stops[i-1][1],c1=stops[i][0],t1=stops[i][1];return t0+(t1-t0)*(cents-c0)/(c1-c0);}
 return 0;
}
double ridgeAlpha(const Ridge& r){
 // A ridge earns its brightness by lasting: a note is full within a moment, a flicker never is.
 return DOT_ALPHA*r.core*std::min(1.0,(r.age+1)/(double)RIDGE_AGE_FULL);
}
void tintRidges(std::vector<Ridge>& rs,const std::vector<Ridge>* prev,int targets,double offset){
 // Green is easier to keep than to win, so a note wobbling around its target holds its colour.
 // A ridge must last a moment first, so clicks and bumps stay neutral. The tint then eases
 // towards its goal, and how close the note is still sets how vivid the green is.
 for(Ridge& r:rs){
  const Ridge* before=r.from>=0&&prev?&(*prev)[r.from]:nullptr;
  const double cents=targetCents(r.centre-offset,targets);
  r.inTune=r.age>=TINT_AGE&&cents<=(before&&before->inTune?GREEN_KEEP_CENTS:GREEN_GAIN_CENTS);
  const double amount=inTuneAmount(cents),goal=r.inTune?std::max(.65,amount):std::min(.2,amount),from=before?before->tint:0;
  r.tint=from+(goal-from)*TINT_EASE;
 }
}
void Tracker::adoptTints(const std::vector<Ridge>& latest){for(size_t i=0;i<last.size()&&i<latest.size();i++){last[i].tint=latest[i].tint;last[i].inTune=latest[i].inTune;}}
void Tracker::reset(){last.clear();lastTrails.clear();}
void Tracker::makeVotes(const float* field,const float* measured,int laneCount){
 // Sample lanes in proportion to their weight; each keeps what that lane measured.
 weights.resize(laneCount);votes.clear();double total=0;
 for(int i=0;i<laneCount;i++){const double x=std::max(0.0,(double)field[i]-.035),w=std::pow(x,2.15);weights[i]=(float)w;total+=w;}
 if(total<.012)return;
 const double step=total/maxVotes;double target=step*.5,acc=0;
 for(int i=0;i<laneCount&&(int)votes.size()<maxVotes;i++){acc+=weights[i];while(acc>=target&&(int)votes.size()<maxVotes){votes.push_back({(double)measured[i],(double)weights[i]});target+=step;}}
}
void Tracker::findRidges(){
 // Votes agreeing on a pitch form one ridge. Its authority is its strongest lane, not
 // its vote count: low notes reach more lanes, which must not outrank an upper note.
 std::stable_sort(votes.begin(),votes.end(),[](const Vote& a,const Vote& b){return a.m<b.m;});
 found.clear();double sum=0,weight=0,w=0;
 for(size_t k=0;k<votes.size();k++){
  const Vote& v=votes[k];sum+=v.m*v.w;weight+=v.w;w=std::max(w,v.w);
  if(k+1==votes.size()||votes[k+1].m-v.m>RIDGE_GAP){Ridge r;r.m=sum/weight;r.w=w;found.push_back(r);sum=0;weight=0;w=0;}
 }
}
void Tracker::linkRidges(bool canBridge){
 // Ridges rank against the frame's strongest, and the frame's own strength scales them
 // all, so a faint noise peak can't shine just because nothing else is sounding.
 double wmax=0;for(const Ridge& r:found)if(r.w>wmax)wmax=r.w;
 const double presence=std::min(1.0,wmax/RIDGE_FULL_WEIGHT);
 next.clear();nextTrails.clear();
 for(Ridge r:found){
  const double rank=std::pow(r.w/wmax,3.0);if(rank<=floorRank)continue;
  r.core=rank*presence;
  // Any ridge may continue another, however faint. That history sets how much it has
  // earned (age) and where a wobbling note is centred (trail).
  const Ridge* link=nullptr;
  if(canBridge)for(size_t q=0;q<last.size();q++)if(std::fabs(last[q].m-r.m)<=RIDGE_REACH&&(!link||std::fabs(last[q].m-r.m)<std::fabs(link->m-r.m))){link=&last[q];r.from=(int)q;}
  r.age=link?link->age+1:0;
  Trail trail;
  if(link){const Trail& t=lastTrails[r.from];const int keep=std::min(t.count,RIDGE_FRAMES_CENTRE-1);for(int i=0;i<keep;i++)trail.m[i]=t.m[t.count-keep+i];trail.count=keep;}
  trail.m[trail.count++]=r.m;
  double sum=0;for(int i=0;i<trail.count;i++)sum+=trail.m[i];r.centre=sum/trail.count;
  // Only strong ridges that continue from the last frame join up. Noise stays as dots.
  if(canBridge&&r.core>=RIDGE_STRONG){
   const Ridge* from=nullptr;
   for(const Ridge& q:last)if(q.core>=RIDGE_STRONG&&std::fabs(q.m-r.m)<=RIDGE_REACH&&(!from||std::fabs(q.m-r.m)<std::fabs(from->m-r.m)))from=&q;
   if(from){r.prev=from->m;r.hasPrev=true;r.prev2=from->prev;r.hasPrev2=from->hasPrev;}
  }
  next.push_back(r);nextTrails.push_back(trail);
 }
}
const std::vector<Ridge>& Tracker::track(const float* field,const float* measured,int laneCount,bool canBridge,int targets,double offset){
 makeVotes(field,measured,laneCount);findRidges();linkRidges(canBridge);
 tintRidges(next,&last,targets,offset);
 last.swap(next);lastTrails.swap(nextTrails);
 return last;
}
}
