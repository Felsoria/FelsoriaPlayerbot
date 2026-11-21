#ifndef FELSORIA_AV_SAFETY_H
#define FELSORIA_AV_SAFETY_H

#include <algorithm>
#include <cmath>

namespace FelsoriaAVSafety
{
    struct Point { float x, y, z; };

    inline bool Finite(Point const& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    // The closed gate is the forward boundary; the cave is longer than
    // the generic 70-yard starting circle. Keep an explicit escape check.
    inline bool InsideStartingCave(Point const& spawn, Point const& gate, Point const& p)
    {
        if (!Finite(spawn) || !Finite(gate) || !Finite(p)) return false;
        float dx=gate.x-spawn.x, dy=gate.y-spawn.y;
        float length=std::sqrt(dx*dx+dy*dy);
        if (length<1.0f) return false;
        float along=((p.x-spawn.x)*dx+(p.y-spawn.y)*dy)/length;
        float across=std::abs((p.x-spawn.x)*dy-(p.y-spawn.y)*dx)/length;
        return along>=-100.0f && along<=length+3.0f && across<=75.0f &&
            std::abs(p.z-spawn.z)<=40.0f;
    }

    inline unsigned ScatterSeed(unsigned value)
    {
        value^=value>>16; value*=0x7feb352du;
        value^=value>>15; value*=0x846ca68bu;
        return value^(value>>16);
    }

    inline Point ReadyPoint(Point const& spawn, Point const& gate, unsigned seed)
    {
        float dx=gate.x-spawn.x, dy=gate.y-spawn.y;
        float length=std::sqrt(dx*dx+dy*dy);
        if (!Finite(spawn) || !Finite(gate) || length<1.0f) return spawn;
        dx/=length; dy/=length;
        // A continuous, match-seeded scatter through the cave, not repeating
        // ranks near the gate. The caller retains a successfully reached spot.
        float along=float(ScatterSeed(seed)&0xffffffu)/16777216.0f;
        float across=float(ScatterSeed(seed^0x9e3779b9u)&0xffffffu)/16777216.0f;
        float behind=2.5f+along*(std::max(10.0f,std::min(60.0f,length*.9f))-2.5f);
        float side=(across*2.0f-1.0f)*6.5f;
        float z=gate.z+(spawn.z-gate.z)*std::min(1.0f,behind/length);
        return {gate.x-dx*behind-dy*side, gate.y-dy*behind+dx*side, z};
    }

    inline bool SafeStep(Point const& a, Point const& b)
    {
        if (!Finite(a) || !Finite(b)) return false;
        float dx=b.x-a.x, dy=b.y-a.y, dz=std::abs(b.z-a.z);
        float horizontal=std::sqrt(dx*dx+dy*dy);
        return horizontal<=12.0f && dz<=6.0f && dz<=1.5f*horizontal+1.0f;
    }

    // Choose the local walking floor. Raw outdoor terrain above a cave is
    // not a replacement for its nearby WMO floor; never lift onto another level.
    inline bool LocalFloor(float z, float nearFloor, float upperFloor, float& floor)
    {
        bool a=std::isfinite(nearFloor)&&nearFloor>-99999.0f&&std::abs(nearFloor-z)<=2.5f;
        bool b=std::isfinite(upperFloor)&&upperFloor>-99999.0f&&std::abs(upperFloor-z)<=2.5f;
        if(!a&&!b)return false;
        floor=a&&(!b||std::abs(nearFloor-z)<=std::abs(upperFloor-z))?nearFloor:upperFloor;
        return true;
    }
}
#endif
