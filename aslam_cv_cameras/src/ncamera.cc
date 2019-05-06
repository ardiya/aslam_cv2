#include <string>
#include <utility>

#include <glog/logging.h>

#include <aslam/cameras/camera.h>
#include <aslam/cameras/camera-pinhole.h>
#include <aslam/cameras/distortion-radtan.h>
#include <aslam/cameras/ncamera.h>
#include <aslam/cameras/yaml/camera-yaml-serialization.h>
#include <aslam/common/pose-types.h>
#include <aslam/common/predicates.h>
#include <aslam/common/unique-id.h>

namespace aslam {

NCamera::NCamera() {}

NCamera::NCamera(
    const NCameraId& id, const TransformationVector& T_C_B,
    const std::vector<Camera::Ptr>& cameras, const std::string& label)
    : Sensor(id), T_C_B_(T_C_B), cameras_(cameras), label_(label) {
  CHECK(id.isValid());
  initInternal();
}

NCamera::NCamera(const sm::PropertyTree& /* propertyTree */) {
  // TODO(PTF): fill in
  CHECK(false) << "Not implemented";
}

NCamera::NCamera(const NCamera& other) :
    Sensor(other),
    T_C_B_(other.T_C_B_),
    label_(other.label_) {
  // Clone all contained cameras.
  for (size_t idx = 0u; idx < other.getNumCameras(); ++idx) {
    cameras_.emplace_back(other.getCamera(idx).clone());
  }
  initInternal();
}

bool NCamera::loadFromYamlNodeImpl(const YAML::Node& yaml_node) {
  CHECK(yaml_node.IsMap());

  // Parse the label.
  if (!YAML::safeGet(yaml_node, "label", &label_)) {
    LOG(ERROR) << "Unable to get the label for the ncamera.";
    return false;
  }

  // Parse the cameras.
  const YAML::Node& cameras_node = yaml_node["cameras"];
  if (!cameras_node.IsSequence()) {
    LOG(ERROR)
        << "Unable to parse the cameras because"
        << "the camera node is not a sequence.";
    return false;
  }

  size_t num_cameras = cameras_node.size();
  if (num_cameras == 0) {
    LOG(ERROR) << "Number of cameras is 0.";
    return false;
  }

  for (size_t camera_index = 0; camera_index < num_cameras; ++camera_index) {
    // Decode the camera
    const YAML::Node& camera_node = cameras_node[camera_index];
    if (!camera_node) {
      LOG(ERROR) << "Unable to get camera node for camera " << camera_index;
      return false;
    }

    if (!camera_node.IsMap()) {
      LOG(ERROR)
          << "Camera node for camera " << camera_index << " is not a map.";
      return false;
    }

    aslam::Camera::Ptr camera;
    if (!YAML::safeGet(camera_node, "camera", &camera)) {
      LOG(ERROR) << "Unable to retrieve camera " << camera_index;
      return false;
    }

    // Get the transformation matrix T_B_C (takes points from the frame C to frame B).
    Eigen::Matrix4d T_B_C_raw;
    if (!YAML::safeGet(camera_node, "T_B_C", &T_B_C_raw)) {
      LOG(ERROR)
          << "Unable to get extrinsic transformation T_B_C for camera "
          << camera_index;
      return false;
    }
    // This call will fail hard if the matrix is not a rotation matrix.
    aslam::Quaternion q_B_C = aslam::Quaternion(
        static_cast<Eigen::Matrix3d>(T_B_C_raw.block<3,3>(0,0)));
    aslam::Transformation T_B_C(q_B_C, T_B_C_raw.block<3,1>(0,3));

    // Fill in the data in the ncamera.
    cameras_.push_back(camera);
    T_C_B_.push_back(T_B_C.inverse());
  }

  initInternal();

  return true;
}

void NCamera::saveToYamlNodeImpl(YAML::Node* yaml_node) const {
  CHECK_NOTNULL(yaml_node);
  YAML::Node& node = *yaml_node;
  node["label"] = label_;

  YAML::Node cameras_node;
  size_t num_cameras = numCameras();
  for (size_t camera_index = 0; camera_index < num_cameras; ++camera_index) {
    YAML::Node camera_node;
    camera_node["camera"] = getCamera(camera_index);
    camera_node["T_B_C"] =
        get_T_C_B(camera_index).inverse().getTransformationMatrix();
    cameras_node.push_back(camera_node);
  }

  node["cameras"] = cameras_node;
}

void NCamera::initInternal() {
  CHECK_EQ(cameras_.size(), T_C_B_.size());
  id_to_index_.clear();
  for (size_t i = 0; i < cameras_.size(); ++i) {
    CHECK_NOTNULL(cameras_[i].get());
    CHECK(cameras_[i]->getId().isValid());
    id_to_index_[cameras_[i]->getId()] = i;
  }
}

size_t NCamera::getNumCameras() const {
  return cameras_.size();
}

const Transformation& NCamera::get_T_C_B(size_t camera_index) const {
  CHECK_LT(camera_index, cameras_.size());
  return T_C_B_[camera_index];
}

Transformation& NCamera::get_T_C_B_Mutable(size_t camera_index) {
  CHECK_LT(camera_index, cameras_.size());
  return T_C_B_[camera_index];
}

const Transformation& NCamera::get_T_C_B(const CameraId& camera_id) const {
  CHECK(camera_id.isValid());
  int camera_idx = getCameraIndex(camera_id);
  CHECK_GE(camera_idx, 0) << "Camera with ID " << camera_id
                          << " not in NCamera container!";
  return get_T_C_B(camera_idx);
}

Transformation& NCamera::get_T_C_B_Mutable(const CameraId& camera_id) {
  CHECK(camera_id.isValid());
  int camera_idx = getCameraIndex(camera_id);
  CHECK_GE(camera_idx, 0) << "Camera with ID " << camera_id
                          << " not in NCamera! container";
  return get_T_C_B_Mutable(camera_idx);
}

void NCamera::set_T_C_B(size_t camera_index, const Transformation& T_Ci_B) {
  CHECK_LT(camera_index, T_C_B_.size());
  T_C_B_[camera_index] = T_Ci_B;
}

const TransformationVector& NCamera::getTransformationVector() const {
  return T_C_B_;
}

const Camera& NCamera::getCamera(size_t camera_index) const {
  CHECK_LT(camera_index, cameras_.size());
  CHECK_NOTNULL(cameras_[camera_index].get());
  return *cameras_[camera_index];
}

Camera& NCamera::getCameraMutable(size_t camera_index) {
  CHECK_LT(camera_index, cameras_.size());
  CHECK_NOTNULL(cameras_[camera_index].get());
  return *cameras_[camera_index];
}

Camera::Ptr NCamera::getCameraShared(size_t camera_index) {
  CHECK_LT(camera_index, cameras_.size());
  return cameras_[camera_index];
}

Camera::ConstPtr NCamera::getCameraShared(size_t camera_index) const {
  CHECK_LT(camera_index, cameras_.size());
  return cameras_[camera_index];
}

void NCamera::setCamera(size_t camera_index, Camera::Ptr camera) {
  CHECK(camera);
  CHECK_LT(camera_index, cameras_.size());
  id_to_index_.erase(cameras_[camera_index]->getId());
  cameras_[camera_index] = camera;
  id_to_index_[camera->getId()] = camera_index;
}

size_t NCamera::numCameras() const {
  return cameras_.size();
}

const std::vector<Camera::Ptr>& NCamera::getCameraVector() const {
  return cameras_;
}

const CameraId& NCamera::getCameraId(size_t camera_index) const {
  CHECK_LT(camera_index, cameras_.size());
  return cameras_[camera_index]->getId();
}

bool NCamera::hasCameraWithId(const CameraId& id) const {
  CHECK(id.isValid());
  return id_to_index_.find(id) != id_to_index_.end();
}

int NCamera::getCameraIndex(const CameraId& id) const {
  CHECK(id.isValid());
  std::unordered_map<CameraId, size_t>::const_iterator it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return -1;
  } else {
    return it->second;
  }
}

NCamera::Ptr NCamera::createTestNCamera(size_t num_cameras) {
  std::vector<aslam::Camera::Ptr> cameras;
  Aligned<std::vector, aslam::Transformation> T_C_B_vector;

  for(size_t camera_idx = 0u; camera_idx < num_cameras; ++camera_idx) {
    cameras.push_back(aslam::PinholeCamera::createTestCamera<aslam::RadTanDistortion>());

    // Offset each camera 0.1 m in x direction and rotate it to face forward.
    Eigen::Vector3d position(0.1 * (camera_idx + 1), 0.0, 0.0);
    aslam::Quaternion q_C_B(0.5, 0.5, -0.5, 0.5);
    aslam::Transformation T_C_B(q_C_B, position);
    T_C_B_vector.push_back(T_C_B);
  }

  aslam::NCameraId rig_id;
  generateId(&rig_id);
  std::string label("Test camera rig");
  return aslam::NCamera::Ptr(new aslam::NCamera(rig_id, T_C_B_vector, cameras, label));
}

NCamera::Ptr NCamera::createSurroundViewTestNCamera() {
  std::vector<aslam::Camera::Ptr> cameras;
  cameras.push_back(aslam::PinholeCamera::createTestCamera());
  cameras.push_back(aslam::PinholeCamera::createTestCamera());
  cameras.push_back(aslam::PinholeCamera::createTestCamera());
  cameras.push_back(aslam::PinholeCamera::createTestCamera());
  aslam::NCameraId rig_id;
  generateId(&rig_id);
  // This defines an artificial camera system similar to the one on the V-Charge or JanETH car.
  aslam::Position3D t_B_C0(2.0, 0.0, 0.0);
  Eigen::Matrix3d R_B_C0 = Eigen::Matrix3d::Zero();
  R_B_C0(1, 0) = -1.0;
  R_B_C0(2, 1) = -1.0;
  R_B_C0(0, 2) = 1.0;
  aslam::Quaternion q_B_C0(R_B_C0);
  aslam::Position3D t_B_C1(0.0, 1.0, 0.0);
  Eigen::Matrix3d R_B_C1 = Eigen::Matrix3d::Zero();
  R_B_C1(0, 0) = 1.0;
  R_B_C1(2, 1) = -1.0;
  R_B_C1(1, 2) = 1.0;
  aslam::Quaternion q_B_C1(R_B_C1);
  aslam::Position3D t_B_C2(-1.0, 0.0, 0.0);
  Eigen::Matrix3d R_B_C2 = Eigen::Matrix3d::Zero();
  R_B_C2(1, 0) = 1.0;
  R_B_C2(2, 1) = -1.0;
  R_B_C2(0, 2) = -1.0;
  aslam::Quaternion q_B_C2(R_B_C2);
  aslam::Position3D t_B_C3(0.0, -1.0, 0.0);
  Eigen::Matrix3d R_B_C3 = Eigen::Matrix3d::Zero();
  R_B_C3(0, 0) = -1.0;
  R_B_C3(2, 1) = -1.0;
  R_B_C3(1, 2) = -1.0;
  aslam::Quaternion q_B_C3(R_B_C3);
  aslam::TransformationVector rig_transformations;
  rig_transformations.emplace_back(q_B_C0.inverse(), -t_B_C0);
  rig_transformations.emplace_back(q_B_C1.inverse(), -t_B_C1);
  rig_transformations.emplace_back(q_B_C2.inverse(), -t_B_C2);
  rig_transformations.emplace_back(q_B_C3.inverse(), -t_B_C3);
  std::string label = "Artificial Planar 4-Pinhole-Camera-Rig";
  return aligned_shared<aslam::NCamera>(
      rig_id, rig_transformations, cameras, label);
}

aslam::NCamera::Ptr NCamera::cloneRigWithoutDistortion() const {
  aslam::NCamera::Ptr rig_without_distortion(this->clone());
  // Remove distortion and assign new IDs to the rig and all cameras.
  for (Camera::Ptr& camera : rig_without_distortion->cameras_) {
    camera->removeDistortion();
    aslam::CameraId cam_id;
    generateId(&cam_id);
    camera->setId(cam_id);
  }

  aslam::NCameraId ncam_id;
  generateId(&ncam_id);
  rig_without_distortion->setId(ncam_id);
  return rig_without_distortion;
}

// TODO(smauq): Fix this with respect to base class
bool NCamera::operator==(const NCamera& other) const {
  bool same = true;
  same &= getNumCameras() == other.getNumCameras();
  same &= label_ == other.label_;
  same &= id_ == other.id_;
  if (same) {
    for (size_t i = 0; i < getNumCameras(); ++i) {
      same &= aslam::checkSharedEqual(cameras_[i], other.cameras_[i]);
      same &= ((T_C_B_[i].getTransformationMatrix() - other.T_C_B_[i].getTransformationMatrix())
          .cwiseAbs().maxCoeff() < common::macros::kEpsilon);
    }
  }
  return same;
}

std::string NCamera::getComparisonString(const NCamera& other) const {
  if (operator==(other)) {
    return "There is no difference between the given ncameras.\n";
  }

  std::ostringstream ss;

  if (id_ != other.id_) {
    ss << "The id is " << id_ << ", the other id is " << other.id_ << std::endl;
  }

  if (label_ != other.label_) {
    ss << "The label is " << label_ << ", the other label is " << other.label_
        << std::endl;
  }

  if (getNumCameras() != other.getNumCameras()) {
    ss << "The number of cameras is " << getNumCameras()
        << ", the other number of cameras is " << other.getNumCameras()
        << std::endl;
  } else {
    for (size_t i = 0; i < getNumCameras(); ++i) {
      double max_coeff_diff = (T_C_B_[i].getTransformationMatrix() -
            other.T_C_B_[i].getTransformationMatrix()).cwiseAbs().maxCoeff();
      if (max_coeff_diff >= common::macros::kEpsilon) {
        ss << "The maximum coefficient of camera transformation " << i
            << " differs by " << max_coeff_diff << std::endl;
        ss << "The transformation matrices are:\n" << T_C_B_[i] << "\nand\n"
            << other.T_C_B_[i] << std::endl;
      }
      if (!aslam::checkSharedEqual(cameras_[i], other.cameras_[i])) {
        ss << "Camera " << i << " differs" << std::endl;
      }
    }
  }

  if (id_to_index_ != other.id_to_index_) {
    ss << "The id to index map differs!" << std::endl;
  }

  return ss.str();
}

}  // namespace aslam
