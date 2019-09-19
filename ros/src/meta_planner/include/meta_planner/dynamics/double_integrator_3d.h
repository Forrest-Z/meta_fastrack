/*
 * Copyright (c) 2019, The Regents of the University of California (Regents).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *    3. Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Please contact the author(s) of this library if you have any questions.
 * Authors: David Fridovich-Keil   ( dfk@eecs.berkeley.edu )
 */

///////////////////////////////////////////////////////////////////////////////
//
// Single player dynamics modeling a point with direct velocity control.
// State is [x, y, z, vx, vy, vz], control is [ax, ay, az], and dynamics are:
//                     \dot px = vx
//                     \dot py = vy
//                     \dot pz = vz
//                     \dot vx = ax
//                     \dot vy = ay
//                     \dot vz = az
//
///////////////////////////////////////////////////////////////////////////////

#ifndef META_PLANNER_DYNAMICS_DOUBLE_INTEGRATOR_3D_H
#define META_PLANNER_DYNAMICS_DOUBLE_INTEGRATOR_3D_H

#include <ilqgames/dynamics/single_player_dynamical_system.h>
#include <ilqgames/utils/types.h>

#include <glog/logging.h>

namespace ilqgames {

class DoubleIntegrator3D : public SinglePlayerDynamicalSystem {
 public:
  ~DoubleIntegrator3D() {}
  DoubleIntegrator3D() : SinglePlayerDynamicalSystem(kNumXDims, kNumUDims) {}

  // Compute time derivative of state.
  VectorXf Evaluate(Time t, const VectorXf& x, const VectorXf& u) const;

  // Compute a discrete-time Jacobian linearization.
  void Linearize(Time t, Time time_step, const VectorXf& x, const VectorXf& u,
                 Eigen::Ref<MatrixXf> A, Eigen::Ref<MatrixXf> B) const;

  // Constants for state indices.
  static const Dimension kNumXDims;
  static const Dimension kPxIdx;
  static const Dimension kPyIdx;
  static const Dimension kPzIdx;
  static const Dimension kVxIdx;
  static const Dimension kVyIdx;
  static const Dimension kVzIdx;

  // Constants for control indices.
  static const Dimension kNumUDims;
  static const Dimension kAxIdx;
  static const Dimension kAyIdx;
  static const Dimension kAzIdx;
};  //\class DoubleIntegrator3D

// ----------------------------- IMPLEMENTATION ----------------------------- //

inline VectorXf DoubleIntegrator3D::Evaluate(Time t, const VectorXf& x,
                                             const VectorXf& u) const {
  VectorXf xdot(kNumXDims);
  xdot(kPxIdx) = x(kVxIdx);
  xdot(kPyIdx) = x(kVyIdx);
  xdot(kPzIdx) = x(kVzIdx);
  xdot(kVxIdx) = u(kAxIdx);
  xdot(kVyIdx) = u(kAyIdx);
  xdot(kVzIdx) = u(kAzIdx);

  return xdot;
}

inline void DoubleIntegrator3D::Linearize(Time t, Time time_step,
                                          const VectorXf& x, const VectorXf& u,
                                          Eigen::Ref<MatrixXf> A,
                                          Eigen::Ref<MatrixXf> B) const {
  A(kPxIdx, kVxIdx) += time_step;
  A(kPyIdx, kVyIdx) += time_step;
  A(kPzIdx, kVzIdx) += time_step;

  B(kVxIdx, kAxIdx) = time_step;
  B(kVyIdx, kAyIdx) = time_step;
  B(kVzIdx, kAzIdx) = time_step;
}

}  // namespace ilqgames

#endif