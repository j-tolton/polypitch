#pragma once
#include "tracker.h"
#include <vector>
namespace polypitch {
// With the whole recording in hand, each ridge can be judged by the note it belongs to rather
// than the single moment it was heard in.
struct RefineStats { int tracks=0,kept=0,tooFaint=0,tooShort=0,harmonics=0,joined=0,recovered=0,faintVoices=0,gatedFrames=0; };
// Removes what isn't a note, and lets a quieter voice show at its own level. Links stay valid
// and colours are worked out again.
// For tuning: one line per group that didn't survive.
struct RemovedGroup { int firstFrame=0,frames=0; double midi=0,level=0; const char* why=""; };
// Candidates are the same frames from a generous tracker (see refineTracker), marked with markPeaks.
// They can add a faint voice where the frames show none. Levels are each frame's RMS, because the
// engine rates a pitch against its moment, so near-silence can otherwise look like a strong note.
RefineStats refineTrace(std::vector<std::vector<Ridge>>& frames,double framesPerSecond,int targets,std::vector<RemovedGroup>* removed=nullptr,const std::vector<std::vector<Ridge>>* candidates=nullptr,const std::vector<double>* levels=nullptr);
// Notes which ridges have a spectral peak of their own, from the engine's peaks for that frame.
void markPeaks(std::vector<Ridge>& ridges,const float* peakMidi,const float* peakStrength,int peakCount);
inline Tracker refineTracker(){return Tracker(512,.001);}
struct KeyGuess { bool found=false; int root=0; bool minor=false; double tuningCents=0,score=0,nextScore=0; double weight[12]={0},finals[12]={0}; int nextRoot=0; bool nextMinor=false; double tuningFocus=0; };
KeyGuess guessKey(const std::vector<std::vector<Ridge>>& frames);
}
