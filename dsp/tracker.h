#pragma once
#include <array>
#include <vector>
namespace polypitch {
constexpr int RIDGE_FRAMES_CENTRE=16,RIDGE_AGE_FULL=5;
constexpr double RIDGE_STRONG=.25;
// Target notes as a bitmask of pitch classes, C is bit 0. ANY_NOTE makes every note a target.
constexpr int ANY_NOTE=-1;
struct Ridge {
 double m=0,w=0,core=0,centre=0,tint=0,prev=0,prev2=0;
 double peak=0; // Strength of a spectral peak at this pitch, when asked for: see markPeaks.
 int age=0,from=-1;
 bool hasPrev=false,hasPrev2=false,inTune=false;
};
double targetCents(double m,int targets);
double inTuneAmount(double cents);
double ridgeAlpha(const Ridge&);
// Offset is in semitones: how far the reference pitch is from A=440.
void tintRidges(std::vector<Ridge>& rs,const std::vector<Ridge>* prev,int targets,double offset=0);
// Turns each frame of the engine's lanes into ridges linked to the frame before.
class Tracker {
public:
 // Live, a frame's weak ridges are dropped at once. With the whole recording to judge them
 // against, more votes and a lower floor keep them as candidates for refineTrace.
 explicit Tracker(int voteCount=128,double rankFloor=.08):maxVotes(voteCount),floorRank(rankFloor){}
 const std::vector<Ridge>& track(const float* field,const float* measured,int laneCount,bool canBridge,int targets,double offset=0);
 // After the frames have been retinted, the next frame must continue from the new colours.
 void adoptTints(const std::vector<Ridge>& latest);
 void reset();
private:
 struct Vote{double m,w;};
 struct Trail{std::array<double,RIDGE_FRAMES_CENTRE> m;int count=0;};
 int maxVotes;
 double floorRank;
 std::vector<float> weights;
 std::vector<Vote> votes;
 std::vector<Ridge> found,last,next;
 std::vector<Trail> lastTrails,nextTrails;
 void makeVotes(const float* field,const float* measured,int laneCount);
 void findRidges();
 void linkRidges(bool canBridge);
};
}
