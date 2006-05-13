/* Copyright (c) 2006 Stanford University and Michael Sherman.
 * Contributors:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including 
 * without limitation the rights to use, copy, modify, merge, publish, 
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included 
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "simbody/internal/common.h"
#include "simbody/internal/DecorativeGeometry.h"
#include "simbody/internal/AnalyticGeometry.h"

#include <cmath>

namespace SimTK {

static const Real Pi = std::acos(Real(-1));

class AnalyticGeometryRep {
public:
    AnalyticGeometryRep() : myHandle(0) { }

    virtual DecorativeGeometry generateDecorativeGeometry() const = 0;


    virtual ~AnalyticGeometryRep() {clearMyHandle();}

    AnalyticGeometryRep* clone() const {
        AnalyticGeometryRep* dup = cloneAnalyticGeometryRep();
        dup->clearMyHandle();
        return dup;
    }
    virtual AnalyticGeometryRep* cloneAnalyticGeometryRep() const = 0;
    void setMyHandle(AnalyticGeometry& h) {myHandle = &h;}
    void clearMyHandle() {myHandle=0;}

private:
    friend class AnalyticGeometry;
    AnalyticGeometry* myHandle;     // the owner of this rep
};

class AnalyticCurveRep : public AnalyticGeometryRep {
public:
    // default destructor, copy, copy assign
    AnalyticCurveRep() { }

    virtual Real calcArcLength() const = 0;
    virtual Vec3 calcStationFromArcLength(const Real& s) const = 0;
    virtual bool isClosed() const {return false;}

    SimTK_DOWNCAST(AnalyticCurveRep, AnalyticGeometryRep);
};

class AnalyticSurfaceRep : public AnalyticGeometryRep {
public:
    // default destructor, copy, copy assign
    AnalyticSurfaceRep() { }

    virtual Real calcArea() const = 0;

    SimTK_DOWNCAST(AnalyticSurfaceRep, AnalyticGeometryRep);
};

class AnalyticVolumeRep : public AnalyticGeometryRep {
public:
    // default destructor, copy, copy assign
    AnalyticVolumeRep() { }

    virtual Real calcVolume() const = 0;
    virtual bool isPointInside(const Vec3&) const = 0;

    SimTK_DOWNCAST(AnalyticVolumeRep, AnalyticGeometryRep);
};

class AnalyticLineRep : public AnalyticCurveRep {
public:
    AnalyticLineRep() : length(CNT<Real>::getNaN()) { }
    AnalyticLineRep(const Real& l) : length(l) {
        assert(l > 0); // TODO
    }

    // virtuals from AnalyticGeometryRep
    DecorativeGeometry generateDecorativeGeometry() const {
        return DecorativeLine(length);
    }

    AnalyticGeometryRep* cloneAnalyticGeometryRep() const {
        return new AnalyticLineRep(*this);
    }

    // virtuals from AnalyticCurveRep
    Real calcArcLength() const {return length;}
    Vec3 calcStationFromArcLength(const Real& s) const {
        assert(-length/2 <= s && s <= length/2); // TODO
        return Vec3(s,0,0);
    }

    SimTK_DOWNCAST(AnalyticLineRep, AnalyticCurveRep);
private:
    Real length;
};

class AnalyticCircleRep : public AnalyticCurveRep {
public:
    AnalyticCircleRep() : r(CNT<Real>::getNaN()) { }
    AnalyticCircleRep(const Real& rad) : r(rad) {
        assert(r > 0); // TODO
    }

    const Real& getRadius() const {return r;}

    // virtuals from AnalyticGeometryRep
    DecorativeGeometry generateDecorativeGeometry() const {
        return DecorativeCircle(r);
    }
    AnalyticGeometryRep* cloneAnalyticGeometryRep() const {
        return new AnalyticCircleRep(*this);
    }

    // virtuals from AnalyticCurveRep
    Real calcArcLength() const {return 2*Pi*r;}
    Vec3 calcStationFromArcLength(const Real& s) const {
        assert(0 <= s && s <= 2*Pi*r); // TODO
        const Real theta = s/r; // 0 to 2pi
        return Vec3(r*std::cos(theta), r*std::sin(theta), 0);
    }
    bool isClosed() const {return true;}

    SimTK_DOWNCAST(AnalyticCircleRep, AnalyticCurveRep);
private:
    Real r;
};

class AnalyticSphereRep : public AnalyticVolumeRep {
public:
    AnalyticSphereRep() : r(CNT<Real>::getNaN()) { }
    AnalyticSphereRep(const Real& rad) : r(r) {
        assert(r > 0); // TODO
    }

    const Real& getRadius() const {return r;}

    // virtuals from AnalyticGeometryRep
    DecorativeGeometry generateDecorativeGeometry() const {
        return DecorativeSphere(r);
    }
    AnalyticGeometryRep* cloneAnalyticGeometryRep() const {
        return new AnalyticSphereRep(*this);
    }

    // virtuals from AnalyticVolumeRep

    Real calcVolume() const {return (Real(4)/3)*Pi*r*r*r;}

    // The point is in the sphere's local frame. Note that exactly
    // *on* the surface is NOT inside.
    bool isPointInside(const Vec3& p) const {
        return p.normSqr() < r*r;
    }

    SimTK_DOWNCAST(AnalyticSphereRep, AnalyticVolumeRep);
private:
    Real r;
};


} // namespace SimTK

