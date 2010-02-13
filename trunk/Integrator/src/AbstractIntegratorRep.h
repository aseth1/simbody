#ifndef SimTK_SIMMATH_ABSTRACT_INTEGRATOR_REP_H_
#define SimTK_SIMMATH_ABSTRACT_INTEGRATOR_REP_H_

/* -------------------------------------------------------------------------- *
 *                      SimTK Core: SimTK Simmath(tm)                         *
 * -------------------------------------------------------------------------- *
 * This is part of the SimTK Core biosimulation toolkit originating from      *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2007-2010 Stanford University and the Authors.      *
 * Authors: Peter Eastman                                                     *
 * Contributors: Michael Sherman                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "SimTKcommon.h"

#include "simmath/internal/common.h"
#include "simmath/Integrator.h"

#include "IntegratorRep.h"

namespace SimTK {

/**
 * This class implements most of the generic functionality needed for an 
 * Integrator, leaving only the actual integration method to be implemented 
 * by the subclass. This is the parent class of several different integrators.
 *
 * There are default implementations for everything but the ODE formula.
 */

class AbstractIntegratorRep : public IntegratorRep {
public:
    AbstractIntegratorRep(Integrator* handle, const System& sys, 
                          int minOrder, int maxOrder, 
                          const std::string& methodName, 
                          bool hasErrorControl);

    void methodInitialize(const State&);

    Integrator::SuccessfulStepStatus 
        stepTo(Real reportTime, Real scheduledEventTime);

    Real getActualInitialStepSizeTaken() const;
    Real getPreviousStepSizeTaken() const;
    Real getPredictedNextStepSize() const;
    long getNumStepsAttempted() const;
    long getNumStepsTaken() const;
    long getNumErrorTestFailures() const;
    long getNumConvergenceTestFailures() const;
    long getNumConvergentIterations() const;
    long getNumDivergentIterations() const;
    long getNumIterations() const;
    void resetMethodStatistics();
    const char* getMethodName() const;
    int getMethodMinOrder() const;
    int getMethodMaxOrder() const;
    bool methodHasErrorControl() const;

protected:
    /**
     * Given initial values for all the continuous variables y=(q,u,z) and 
     * their derivatives (not necessarily what's in advancedState currently), 
     * take a trial step of size h=(t1-t0), optimistically storing the result 
     * in advancedState. Also estimate the absolute error in each element of y,
     * and store them in yErrEst. Returns true if the step converged (always 
     * true for non-iterative methods), false otherwise. The number of internal
     * iterations just for this step is return in numIterations, which should 
     * always be 1 for non-iterative methods.
     *
     * This is a DAE step, meaning that coordinate projections should be done
     * (including their effect on the error estimate) prior to returning.
     * The default implementation calls the "raw" ODE integrator and then
     * handles the necessary projections; if that's OK for your method then
     * you only have to implement attemptODEStep(). Otherwise, you should
     * override the attemptDAEStep() and deal carefully with the DAE-specific
     * issues yourself. 
     *
     * The return value is true if the step converged; that tells the caller
     * to look at the error estimate. If the step doesn't converge, the error
     * estimate is meaningless and the step will be rejected.
     */
    virtual bool attemptDAEStep
       (Real t0, Real t1, 
        const Vector& q0, const Vector& qdot0, const Vector& qdotdot0, 
        const Vector& u0, const Vector& udot0, 
        const Vector& z0, const Vector& zdot0, 
        Vector& yErrEst, int& errOrder, int& numIterations)
    {
        bool ODEconverged = false;
        try {
            numIterations = 1; // so non-iterative ODEs can forget about this
            ODEconverged = attemptODEStep(t0,t1,q0,qdot0,qdotdot0,
                                          u0,udot0,z0,zdot0,
                                          yErrEst, errOrder, numIterations);
        } catch (...) {return false;}

        if (!ODEconverged)
            return false;

        // The ODE step did not throw an exception and says it converged,
        // meaning its error estimate is worth a look.
        Real rmsErr = calcWeightedRMSNorm(yErrEst, getDynamicSystemWeights());

        // If the estimated error is extremely bad, don't attempt the 
        // projection. If we're near the edge, though, the projection may
        // clean up the error estimate enough to allow the step to be
        // accepted. We'll define "near the edge" to mean that a half-step
        // would have succeeded where this one failed. If the current error
        // norm is eStep then a half step would have given us an error of
        // eHalf = eStep/(2^p). We want to try the projection as long as 
        // eHalf <= accuracy, i.e., eStep <= 2^p * accuracy.
        if (rmsErr > std::pow(Real(2),errOrder)*getAccuracyInUse())
            return true; // this step converged, but isn't worth projecting

        // The ODE error estimate is good enough or at least worth trying
        // to salvage via projection. If the constraint violation is 
        // extreme, however, we must not attempt to project it. The goal
        // here is to ensure that the Newton iteration in projection is
        // well behaved, running near its quadratic convergence regime.
        // Thus we'll consider failure to reach sqrt(consTol) to be extreme. 
        // To guard against numerically large values of consTol, we'll 
        // always permit projection if we come within 2X of consTol. Examples:
        //      consTol        projectionLimit
        //        1e-12             1e-6
        //        1e-4              1e-2
        //        0.01              0.1
        //        0.1               0.316
        //        0.5               1
        //        1                 2
        const Real projectionLimit = 
            std::max(2*getConstraintToleranceInUse(), 
                     std::sqrt(getConstraintToleranceInUse()));

        const Real consErrAfterODE = 
            calcWeightedRMSNorm(getAdvancedState().getYErr(),
                                getDynamicSystemOneOverTolerances());

        if (consErrAfterODE > projectionLimit)
            return false; // "convergence" failure; caller can't use error est.

        // Now we'll project if the constraints aren't already satisifed,
        // or if the user said we have to project every step regardless.
        if (   userProjectEveryStep == 1 
            || consErrAfterODE > getConstraintToleranceInUse())
        {
            try {   
                projectStateAndErrorEstimate(updAdvancedState(), yErrEst);
            } catch (...) {
                return false; // projection failed
            }
        }

        // ODE step and projection (if any) were successful, although 
        // the accuracy requirement may not have been met.
        return true;
    }

    // Any integrator that doesn't override the above attemptDAEStep() method
    // must override at least the ODE part here. The method must take an ODE
    // step modifying y in advancedState, return false for failure to converge,
    // or return true and an estimate of the absolute error in each element of 
    // the advancedState y variables. The integrator should not attempt to 
    // evaluate derivatives at the final y value because we want to project
    // onto the position and velocity constraint manifolds first so the
    // derivative calculation would have been wasted.
    virtual bool attemptODEStep
       (Real t0, Real t1, 
        const Vector& q0, const Vector& qdot0, const Vector& qdotdot0, 
        const Vector& u0, const Vector& udot0, 
        const Vector& z0, const Vector& zdot0, 
        Vector& yErrEst, int& errOrder, int& numIterations) 
    {
        SimTK_ERRCHK_ALWAYS(!"Unimplemented virtual function", 
            "AbstractIntegratorRep::attemptODEStep()",
            "This virtual function was called but wasn't defined. Every"
            " concrete integrator must override attemptODEStep() or override"
            " attemptDAEStep() which calls it.");
        return false;
    }


    /**
     * Evaluate the error that occurred in the step we just attempted, and 
     * select a new step size accordingly.  The default implementation should 
     * work well for most integrators.
     * 
     * @param   err     
     *      The error estimate from the step that was just attempted.
     * @param   errOrder     
     *      The order of the error estimator so we know what the effect of a
     *      step size change would be on the error we see next time.
     * @param   hWasArtificiallyLimited   
     *      Tells whether the step size was artificially reduced due to a 
     *      scheduled event time. If this is true, we will never attempt to 
     *      increase the step size.
     * @return 
     *      True if the step should be accepted, false if it should be rejected
     *      and retried with a smaller step size.
     */
    virtual bool adjustStepSize(Real err, int errOrder, 
                                bool hWasArtificiallyLimited);
    /**
     * Create an interpolated state at time t, which is between the previous 
     * and advanced times. The default implementation uses third order 
     * Hermite spline interpolation.
     */
    virtual void createInterpolatedState(Real t);
    /**
     * Interpolate the advanced state back to an earlier part of the interval,
     * forgetting about the rest of the interval. This is necessary, for 
     * example, after we have localized an event trigger to an interval 
     * tLow:tHigh where tHigh < tAdvanced.  The default implementation uses 
     * third order Hermite spline interpolation.
     */
    virtual void backUpAdvancedStateByInterpolation(Real t);
    long statsStepsTaken, statsStepsAttempted, statsErrorTestFailures, statsConvergenceTestFailures;

    // Iterative methods should count iterations and then classify them as 
    // iterations that led to successful convergence and those that didn't.
    long statsConvergentIterations, statsDivergentIterations;
private:
    bool takeOneStep(Real tMax, Real tReport);
    bool initialized, hasErrorControl;
    Real currentStepSize, lastStepSize, actualInitialStepSizeTaken;
    int minOrder, maxOrder;
    std::string methodName;
};

} // namespace SimTK

#endif // SimTK_SIMMATH_ABSTRACT_INTEGRATOR_REP_H_
