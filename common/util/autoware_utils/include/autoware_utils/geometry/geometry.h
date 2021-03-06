/*
 * Copyright 2020 TierIV. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>

#define EIGEN_MPL2_ONLY
#include <Eigen/Core>
#include <Eigen/Geometry>

#include <autoware_planning_msgs/Path.h>
#include <autoware_planning_msgs/Trajectory.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/TransformStamped.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>

#include <autoware_utils/geometry/boost_geometry.h>

namespace autoware_utils
{
inline geometry_msgs::Point getPoint(const geometry_msgs::Point & p) { return p; }

inline geometry_msgs::Point getPoint(const geometry_msgs::Pose & p) { return p.position; }

inline geometry_msgs::Point getPoint(const geometry_msgs::PoseStamped & p)
{
  return p.pose.position;
}

inline geometry_msgs::Point getPoint(const autoware_planning_msgs::PathPoint & p)
{
  return p.pose.position;
}
inline geometry_msgs::Point getPoint(const autoware_planning_msgs::TrajectoryPoint & p)
{
  return p.pose.position;
}

inline geometry_msgs::Point createPoint(const double x, const double y, const double z)
{
  geometry_msgs::Point p;
  p.x = x;
  p.y = y;
  p.z = z;
  return p;
}

// Revival of tf::createQuaternionFromRPY
// https://answers.ros.org/question/304397/recommended-way-to-construct-quaternion-from-rollpitchyaw-with-tf2/
inline tf2::Quaternion createQuaternionFromRPY(
  const double roll, const double pitch, const double yaw)
{
  tf2::Quaternion q;
  q.setRPY(roll, pitch, yaw);
  return q;
}

template <class Point1, class Point2>
double calcDistance2d(const Point1 & point1, const Point2 & point2)
{
  const auto p1 = getPoint(point1);
  const auto p2 = getPoint(point2);
  return std::hypot(p1.x - p2.x, p1.y - p2.y);
}

template <class Point1, class Point2>
double calcDistance3d(const Point1 & point1, const Point2 & point2)
{
  const auto p1 = getPoint(point1);
  const auto p2 = getPoint(point2);
  // To be replaced by std::hypot(dx, dy, dz) in C++17
  return std::hypot(std::hypot(p1.x - p2.x, p1.y - p2.y), p1.z - p2.z);
}

inline geometry_msgs::Pose transform2pose(const geometry_msgs::Transform & transform)
{
  geometry_msgs::Pose pose;
  pose.position.x = transform.translation.x;
  pose.position.y = transform.translation.y;
  pose.position.z = transform.translation.z;
  pose.orientation = transform.rotation;
  return pose;
}

inline geometry_msgs::PoseStamped transform2pose(const geometry_msgs::TransformStamped & transform)
{
  geometry_msgs::PoseStamped pose;
  pose.header = transform.header;
  pose.pose = transform2pose(transform.transform);
  return pose;
}

inline geometry_msgs::Transform pose2transform(const geometry_msgs::Pose & pose)
{
  geometry_msgs::Transform transform;
  transform.translation.x = pose.position.x;
  transform.translation.y = pose.position.y;
  transform.translation.z = pose.position.z;
  transform.rotation = pose.orientation;
  return transform;
}

inline geometry_msgs::TransformStamped pose2transform(const geometry_msgs::PoseStamped & pose)
{
  geometry_msgs::TransformStamped transform;
  transform.header = pose.header;
  transform.transform = pose2transform(pose.pose);
  return transform;
}

inline Point3d transformPoint(const Point3d & point, const geometry_msgs::Transform & transform)
{
  const auto & translation = transform.translation;
  const auto & rotation = transform.rotation;

  const Eigen::Translation3d T(translation.x, translation.y, translation.z);
  const Eigen::Quaterniond R(rotation.w, rotation.x, rotation.y, rotation.z);

  const Eigen::Vector3d transformed(T * R * point);

  return Point3d{transformed.x(), transformed.y(), transformed.z()};
}

inline Point2d transformPoint(const Point2d & point, const geometry_msgs::Transform & transform)
{
  Point3d point_3d{point.x(), point.y(), 0};
  const auto transformed = transformPoint(point_3d, transform);
  return Point2d{transformed.x(), transformed.y()};
}

template <class T>
T transformVector(const T & points, const geometry_msgs::Transform & transform)
{
  T transformed;
  for (const auto & point : points) {
    transformed.push_back(transformPoint(point, transform));
  }
  return transformed;
}
}  // namespace autoware_utils
