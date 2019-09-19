/*
 * Copyright (c) 2018, The Regents of the University of California (Regents).
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
// ILQR problem.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef META_PLANNER_PLANNING_ILQR_PROBLEM_H
#define META_PLANNER_PLANNING_ILQR_PROBLEM_H

#include <ilqgames/cost/final_time_cost.h>
#include <ilqgames/cost/quadratic_cost.h>
#include <ilqgames/dynamics/concatenated_dynamical_system.h>
#include <ilqgames/solver/ilq_solver.h>
#include <ilqgames/solver/linesearching_ilq_solver.h>
#include <ilqgames/solver/problem.h>
#include <ilqgames/utils/types.h>
#include <meta_planner/planning/obstacle_cost_3d.h>

#include <fastrack_srvs/TrackingBoundBox.h>
#include <fastrack_srvs/TrackingBoundBoxResponse.h>
#include <meta_planner/environment/balls_in_box.h>

#include <glog/logging.h>
#include <ros/ros.h>

namespace meta_planner {
namespace planning {

using namespace ilqgames;
using Eigen::Vector3f;

namespace {
// Time.
static constexpr Time kTimeStep = 0.25;    // s
static constexpr Time kTimeHorizon = 5.0;  // s
static constexpr size_t kNumTimeSteps =
    static_cast<size_t>(kTimeHorizon / kTimeStep);

// Flag for one-sided costs.
static constexpr bool kOrientedRight = true;
}  // anonymous namespace

template <typename D>
class ILQRProblem : public Problem {
 public:
  ~ILQRProblem() {}
  ILQRProblem(const ros::NodeHandle& n);

  // Reset costs. Include new goal location at the given coordinates.
  void SetUpCosts(const VectorXf& start, float goal_x, float goal_y,
                  float goal_z, Time start_time);

  // Check collision avoidance.
  bool IsValid(const Vector3d& position) const {
    return env_.IsValid(position, bound_);
  }

  // Accessors.
  const ConcatenatedDynamicalSystem& Dynamics() const { return *dynamics_; }

 private:
  // Load parameters.
  void LoadParameters(const ros::NodeHandle& n);

  // Environment and range to pad obstacles.
  meta_planner::environment::BallsInBox env_;
  fastrack::bound::Box bound_;
  float max_tracking_error_;

  // Dynamics.
  std::shared_ptr<ConcatenatedDynamicalSystem> dynamics_;

  // Cost weights.
  float goal_cost_weight_;
  float control_effort_cost_weight_;
  float obstacle_cost_weight_;
};  //\class ILQR

// ----------------------------- IMPLEMENTATION ----------------------------- //

template <typename D>
ILQRProblem<D>::ILQRProblem(const ros::NodeHandle& n) {
  LoadParameters(n);
  CHECK(env_.Initialize(n));

  // Create dynamics.
  dynamics_.reset(new ConcatenatedDynamicalSystem({
      std::make_shared<D>(),
  }));

  CHECK_EQ(dynamics_->NumPlayers(), 1);
  CHECK_EQ(dynamics_->XDim(), D::kNumXDims);
  CHECK_EQ(dynamics_->TotalUDim(), D::kNumUDims);
  CHECK_EQ(D::kNumXDims, 6);
  CHECK_EQ(D::kNumUDims, 3);

  // Set up initial state.
  // NOTE: this will get overwritten before the solver is actually called.
  x0_ = VectorXf::Constant(D::kNumXDims, 0.0);

  CHECK_EQ(x0_.size(), D::kNumXDims);

  // Set up costs for all players.
  // NOTE: initialize with dummy goal location, since this will get overwritten
  // immediately.
  SetUpCosts(x0_, 1.0, 1.0, 1.0, ros::Time::now().toSec());
}

template <typename D>
void ILQRProblem<D>::SetUpCosts(const VectorXf& start, float goal_x,
                                float goal_y, float goal_z, Time start_time) {
  CHECK_EQ(start.size(), D::kNumXDims);

  // Set up initial strategies.
  strategies_.reset(new std::vector<Strategy>(
      1, Strategy(kNumTimeSteps, D::kNumXDims, D::kNumUDims)));

  // Set up initial operating point to be a straight line.
  operating_point_.reset(
      new OperatingPoint(kNumTimeSteps, 1, start_time, dynamics_));

  x0_ = start;
  for (size_t kk = 0; kk < kNumTimeSteps; kk++) {
    const float frac =
        static_cast<float>(kk) / static_cast<float>(kNumTimeSteps);
    const float px = (1.0 - frac) * start(D::kPxIdx) + frac * goal_x;
    const float py = (1.0 - frac) * start(D::kPyIdx) + frac * goal_y;
    const float pz = (1.0 - frac) * start(D::kPzIdx) + frac * goal_z;

    operating_point_->xs[kk](D::kPxIdx) = px;
    operating_point_->xs[kk](D::kPyIdx) = py;
    operating_point_->xs[kk](D::kPzIdx) = pz;

    // HACK! Assuming D format.
    CHECK_EQ(operating_point_->us[kk].size(), 1);
    auto& vel = (D::kNumXDims == 6) ? operating_point_->xs[kk]
                                    : operating_point_->us[kk][0];

    vel(D::kVxIdx) = (goal_x - start(D::kPxIdx)) / kTimeHorizon;
    vel(D::kVyIdx) = (goal_y - start(D::kPyIdx)) / kTimeHorizon;
    vel(D::kVzIdx) = (goal_z - start(D::kPzIdx)) / kTimeHorizon;
  }

  // Penalize control effort.
  PlayerCost p1_cost;
  const auto p1_u_cost = std::make_shared<QuadraticCost>(
      control_effort_cost_weight_, -1, 0.0, "Control Effort");
  p1_cost.AddControlCost(0, p1_u_cost);

  // Goal costs.
  constexpr float kFinalTimeWindow = 0.5;  // 0.75 * kTimeHorizon;  // s
  const auto p1_goalx_cost = std::make_shared<FinalTimeCost>(
      std::make_shared<QuadraticCost>(goal_cost_weight_, D::kPxIdx, goal_x),
      kTimeHorizon - kFinalTimeWindow, "GoalX");
  const auto p1_goaly_cost = std::make_shared<FinalTimeCost>(
      std::make_shared<QuadraticCost>(goal_cost_weight_, D::kPyIdx, goal_y),
      kTimeHorizon - kFinalTimeWindow, "GoalY");
  const auto p1_goalz_cost = std::make_shared<FinalTimeCost>(
      std::make_shared<QuadraticCost>(goal_cost_weight_, D::kPzIdx, goal_z),
      kTimeHorizon - kFinalTimeWindow, "GoalZ");
  p1_cost.AddStateCost(p1_goalx_cost);
  p1_cost.AddStateCost(p1_goaly_cost);
  p1_cost.AddStateCost(p1_goalz_cost);

  // Obstacle costs.
  const auto& centers = env_.Centers();
  const auto& radii = env_.Radii();

  for (size_t ii = 0; ii < env_.NumObstacles(); ii++) {
    // HACK! Set avoidance threshold to twice radius.
    const std::shared_ptr<ObstacleCost3D> obstacle_cost(new ObstacleCost3D(
        obstacle_cost_weight_, std::tuple<Dimension, Dimension, Dimension>(
                                   D::kPxIdx, D::kPyIdx, D::kPzIdx),
        centers[ii].cast<float>(), 2.0 * (radii[ii] + max_tracking_error_),
        "Obstacle" + std::to_string(ii)));
    p1_cost.AddStateCost(obstacle_cost);
  }

  // Create the corresponding solver.
  solver_.reset(new ILQSolver(dynamics_, {p1_cost}, kTimeHorizon, kTimeStep));
}

template <typename D>
void ILQRProblem<D>::LoadParameters(const ros::NodeHandle& n) {
  ros::NodeHandle nl(n);

  // Set bound by calling service provided by tracker.
  std::string bound_srv_name;
  CHECK(nl.getParam("srv/bound", bound_srv_name));

  ros::service::waitForService(bound_srv_name);
  ros::ServiceClient bound_srv =
      nl.serviceClient<fastrack_srvs::TrackingBoundBox>(bound_srv_name.c_str(),
                                                        true);

  fastrack_srvs::TrackingBoundBox b;
  CHECK(bound_srv);
  CHECK(bound_srv.call(b));

  bound_.FromRos(b.response);
  max_tracking_error_ = std::sqrt(bound_.x * bound_.x + bound_.y * bound_.y +
                                  bound_.z * bound_.z);

  // Cost weights.
  CHECK(nl.getParam("weight/obstacle", obstacle_cost_weight_));
  CHECK(nl.getParam("weight/goal", goal_cost_weight_));
  CHECK(nl.getParam("weight/control_effort", control_effort_cost_weight_));
}

}  //\namespace planning
}  //\namespace fastrack

#endif
