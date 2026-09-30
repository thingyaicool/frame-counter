#include "../src/logic.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); exit(1);} }while(0)
using namespace fc;
int main() {
    // bucket boundaries
    CHECK(bucketFor(15)==B_10_15); CHECK(bucketFor(10)==B_10_15); CHECK(bucketFor(11)==B_10_15);
    CHECK(bucketFor(9)==B_7_9);  CHECK(bucketFor(7)==B_7_9);
    CHECK(bucketFor(6)==B_5_6);  CHECK(bucketFor(5)==B_5_6);
    CHECK(bucketFor(4)==B_4); CHECK(bucketFor(3)==B_3); CHECK(bucketFor(2)==B_2); CHECK(bucketFor(1)==B_1);
    CHECK(bucketFor(16)==B_OTHER); CHECK(bucketFor(200)==B_OTHER);
    CHECK(bucketFor(0.83)==B_CBS); CHECK(bucketFor(0.92)==B_CBS); CHECK(bucketFor(0.55)==B_CBS);
    CHECK(bucketFor(0)==-1);

    // integer frames, no timestamps: click at 100, click at 110 -> 10-15 +1
    { Tracker t; double dt=1.0/240; 
      t.onInput(100,100*dt,std::nullopt,true,true);
      auto r=t.onInput(110,110*dt,std::nullopt,false,true);
      CHECK(r.counted && r.bucket==B_10_15 && r.isHold);
      CHECK(t.counts[B_10_15]==1 && t.total==1);
      t.onInput(113,113*dt,std::nullopt,true,true); CHECK(t.counts[B_3]==1);
      t.onInput(114,114*dt,std::nullopt,false,true); CHECK(t.counts[B_1]==1);
      t.onInput(120,120*dt,std::nullopt,true,true); CHECK(t.counts[B_5_6]==1);
      t.onInput(128,128*dt,std::nullopt,false,true); CHECK(t.counts[B_7_9]==1);
      t.onInput(160,160*dt,std::nullopt,true,true); CHECK(t.counts[B_OTHER]==1);
    }
    // dual mode mirror counted once
    { Tracker t; double dt=1.0/240;
      t.onInput(10,10*dt,std::nullopt,true,true); t.onInput(10,10*dt,std::nullopt,true,false);
      t.onInput(14,14*dt,std::nullopt,false,true); t.onInput(14,14*dt,std::nullopt,false,false);
      CHECK(t.total==1 && t.counts[B_4]==1);
    }
    // CBS with timestamps in seconds: 0.83 frame apart, same step
    { Tracker t; double dt=1.0/240;
      t.onInput(50,50*dt,0.500,true,true);
      auto r=t.onInput(50,50*dt,0.500+0.83*dt,false,true);
      CHECK(r.counted && r.bucket==B_CBS); CHECK(std::abs(r.gap-0.83)<1e-6);
      CHECK(t.tightest<0.84);
    }
    // CBS straddling a step boundary: steps 50 -> 51 but true gap 0.55
    { Tracker t; double dt=1.0/240;
      t.onInput(50,50*dt,1.0,true,true);
      auto r=t.onInput(51,51*dt,1.0+0.55*dt,false,true);
      CHECK(r.bucket==B_CBS);
    }
    // same-step no timestamp -> CBS
    { Tracker t; t.onInput(5,5/240.0,std::nullopt,true,true);
      auto r=t.onInput(5,5/240.0,std::nullopt,false,true); CHECK(r.bucket==B_CBS); }
    // microsecond timestamps auto-detected
    { Tracker t; double dt=1.0/240;
      t.onInput(0,0,0.0,true,true);
      t.onInput(10,10*dt,10*dt*1e6,false,true); // long gap teaches unit
      auto r=t.onInput(10,10*dt,10*dt*1e6+0.7*dt*1e6,true,true);
      CHECK(r.bucket==B_CBS);
    }
    // TPS calibration (360 tps)
    { Tracker t; double dt=1.0/360;
      t.onInput(0,0,std::nullopt,true,true); t.onInput(6,6*dt,std::nullopt,false,true);
      CHECK(std::abs(t.tps()-360)<1); CHECK(t.counts[B_5_6]==1); }
    // reset per attempt, session accumulates
    { Tracker t; double dt=1.0/240;
      t.onInput(1,dt,std::nullopt,true,true); t.onInput(3,3*dt,std::nullopt,false,true);
      t.resetAttempt(); CHECK(t.total==0 && t.counts[B_2]==0 && t.session[B_2]==1 && t.sessionTotal==1);
      // no gap across attempts
      auto r=t.onInput(1,dt,std::nullopt,true,true); CHECK(!r.counted);
    }
    printf("ALL TESTS PASSED\n");
}
