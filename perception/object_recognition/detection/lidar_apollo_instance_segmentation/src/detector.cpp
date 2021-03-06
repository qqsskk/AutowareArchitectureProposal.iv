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

#include "lidar_apollo_instance_segmentation/detector.h"
#include <NvCaffeParser.h>
#include <NvInfer.h>
#include <boost/filesystem.hpp>
#include "lidar_apollo_instance_segmentation/feature_map.h"

LidarApolloInstanceSegmentation::LidarApolloInstanceSegmentation() : nh_(""), pnh_("~"), tf_listener_(tf_buffer_)
{
  int range, width, height;
  bool use_intensity_feature, use_constant_feature;
  std::string engine_file;
  std::string prototxt_file;
  std::string caffemodel_file;
  pnh_.param<float>("score_threshold", score_threshold_, 0.8);
  pnh_.param<int>("range", range, 60);
  pnh_.param<int>("width", width, 640);
  pnh_.param<int>("height", height, 640);
  pnh_.param<std::string>("engine_file", engine_file, "vls-128.engine");
  pnh_.param<std::string>("prototxt_file", prototxt_file, "vls-128.prototxt");
  pnh_.param<std::string>("caffemodel_file", caffemodel_file, "vls-128.caffemodel");
  pnh_.param<bool>("use_intensity_feature", use_intensity_feature, true);
  pnh_.param<bool>("use_constant_feature", use_constant_feature, true);
  pnh_.param<std::string>("target_frame", target_frame_, "base_link");
  pnh_.param<float>("z_offset", z_offset_, 2);

  // load weight file
  std::ifstream fs(engine_file);
  if (!fs.is_open()) {
    ROS_INFO(
      "Could not find %s. try making TensorRT engine from caffemodel and prototxt",
      engine_file.c_str());
    Tn::Logger logger;
    nvinfer1::IBuilder * builder = nvinfer1::createInferBuilder(logger);
    nvinfer1::INetworkDefinition * network = builder->createNetworkV2(0U);
    nvcaffeparser1::ICaffeParser * parser = nvcaffeparser1::createCaffeParser();
    nvinfer1::IBuilderConfig * config = builder->createBuilderConfig();
    const nvcaffeparser1::IBlobNameToTensor * blob_name2tensor = parser->parse(
      prototxt_file.c_str(), caffemodel_file.c_str(), *network, nvinfer1::DataType::kFLOAT);
    std::string output_node = "deconv0";
    auto output = blob_name2tensor->find(output_node.c_str());
    if (output == nullptr) ROS_ERROR("can not find output named %s", output_node.c_str());
    network->markOutput(*output);
    const int batch_size = 1;
    builder->setMaxBatchSize(batch_size);
    config->setMaxWorkspaceSize(1 << 30);
    nvinfer1::ICudaEngine * engine = builder->buildEngineWithConfig(*network, *config);
    nvinfer1::IHostMemory * trt_model_stream = engine->serialize();
    assert(trt_model_stream != nullptr);
    std::ofstream outfile(engine_file, std::ofstream::binary);
    assert(!outfile.fail());
    outfile.write(reinterpret_cast<char *>(trt_model_stream->data()), trt_model_stream->size());
    outfile.close();
    network->destroy();
    parser->destroy();
    builder->destroy();
    config->destroy();
  }
  net_ptr_.reset(new Tn::trtNet(engine_file));

  // feature map generator: pre process
  feature_generator_ = std::make_shared<FeatureGenerator>(
    width, height, range, use_intensity_feature, use_constant_feature);

  // cluster: post process
  cluster2d_ = std::make_shared<Cluster2D>(width, height, range);
}

bool LidarApolloInstanceSegmentation::transformCloud(
  const sensor_msgs::PointCloud2 & input,
  sensor_msgs::PointCloud2& transformed_cloud,
  float z_offset)
{
  // transform pointcloud to target_frame
  if (target_frame_ != input.header.frame_id) {
    try {
      geometry_msgs::TransformStamped transform_stamped;
      transform_stamped = tf_buffer_.lookupTransform(target_frame_, input.header.frame_id,
                                                     input.header.stamp, ros::Duration(0.5));
      Eigen::Matrix4f affine_matrix =
        tf2::transformToEigen(transform_stamped.transform).matrix().cast<float>();
      pcl_ros::transformPointCloud(affine_matrix, input, transformed_cloud);
      transformed_cloud.header.frame_id = target_frame_;
    } catch (tf2::TransformException &ex) {
      ROS_WARN("%s",ex.what());
      return false;
    }
  } else {
    transformed_cloud = input;
  }

  // move pointcloud z_offset in z axis
  sensor_msgs::PointCloud2 pointcloud_with_z_offset;
  Eigen::Affine3f z_up_translation(Eigen::Translation3f(0, 0, z_offset));
  Eigen::Matrix4f z_up_transform = z_up_translation.matrix();
  pcl_ros::transformPointCloud(z_up_transform, transformed_cloud, transformed_cloud);

  return true;
}

bool LidarApolloInstanceSegmentation::detectDynamicObjects(
  const sensor_msgs::PointCloud2 & input,
  autoware_perception_msgs::DynamicObjectWithFeatureArray & output)
{
  // move up pointcloud z_offset in z axis
  sensor_msgs::PointCloud2 transformed_cloud;
  transformCloud(input, transformed_cloud, z_offset_);

  // convert from ros to pcl
  pcl::PointCloud<pcl::PointXYZI>::Ptr pcl_pointcloud_raw_ptr(new pcl::PointCloud<pcl::PointXYZI>);
  pcl::fromROSMsg(transformed_cloud, *pcl_pointcloud_raw_ptr);

  // generate feature map
  std::shared_ptr<FeatureMapInterface> feature_map_ptr =
    feature_generator_->generate(pcl_pointcloud_raw_ptr);

  // inference
  std::shared_ptr<float> inferred_data(new float[net_ptr_->getOutputSize() / sizeof(float)]);
  net_ptr_->doInference(feature_map_ptr->map_data.data(), inferred_data.get());

  // post process
  const float objectness_thresh = 0.5;
  pcl::PointIndices valid_idx;
  valid_idx.indices.resize(pcl_pointcloud_raw_ptr->size());
  std::iota(valid_idx.indices.begin(), valid_idx.indices.end(), 0);
  cluster2d_->cluster(
    inferred_data, pcl_pointcloud_raw_ptr, valid_idx, objectness_thresh,
    true /*use all grids for clustering*/);
  const float height_thresh = 0.5;
  const int min_pts_num = 3;
  cluster2d_->getObjects(score_threshold_, height_thresh, min_pts_num, output, transformed_cloud.header);

  // move down pointcloud z_offset in z axis
  for (int i=0; i<output.feature_objects.size(); i++) {
    sensor_msgs::PointCloud2 transformed_cloud;
    transformCloud(output.feature_objects.at(i).feature.cluster, transformed_cloud, -z_offset_);
    output.feature_objects.at(i).feature.cluster = transformed_cloud;
  }

  output.header = transformed_cloud.header;
  return true;
}
