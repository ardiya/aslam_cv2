#ifndef ASLAM_FRAMES_VISUAL_FRAME_H_
#define ASLAM_FRAMES_VISUAL_FRAME_H_

#include <memory>
#include <unordered_map>
#include <cstdint>

#include <aslam/cameras/camera.h>
#include <aslam/common/channel.h>
#include <aslam/common/channel-declaration.h>
#include <aslam/common/macros.h>
#include <aslam/common/unique-id.h>
#include <Eigen/Dense>

namespace aslam {
class Camera;

/// \class VisualFrame
/// \brief An image and keypoints from a single camera.
///
/// This class stores data from an image and keypoints taken from a single
/// camera. It stores a pointer to the camera's intrinsic calibration,
/// an id that uniquely identifies this frame, and a measurement timestamp.
///
/// The class also stores a ChannelGroup object that can be used to hold
/// keypoint data, the raw image, and other associated information.
///
/// The camera geometry stored in the frame and accessible by getCameraGeometry()
/// may refer to a transformed geometry that includes downsampling and undistortion.
/// However, we recommend to always store the raw image with the frame so that
/// no information is lost. In order to be able to plot on these raw images, we must
/// also store the original camera geometry object (accessible with
/// getRawCameraGeometry()). To plot transformed keypoints on the raw image, one
/// may use toRawImageCoordinates() or toRawImageCoordinatesVectorized() to recover
/// the raw image coordinates of any keypoint.
class VisualFrame  {
 public:
  /// \brief The descriptor matrix stores descriptors in columns, i.e. the descriptor matrix
  ///        has num_bytes_per_descriptor rows and num_descriptors columns.
  typedef Eigen::Matrix<unsigned char, Eigen::Dynamic, Eigen::Dynamic> DescriptorsT;
  typedef Eigen::Matrix<unsigned char, Eigen::Dynamic, Eigen::Dynamic> LidarDescriptorsT;
  typedef Eigen::VectorXd KeypointScoresT;

  ASLAM_POINTER_TYPEDEFS(VisualFrame);
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  //////////////////////////////////////////////////////////////
  /// \name Constructors/destructors and operators
  /// @{
  VisualFrame();
  virtual ~VisualFrame() {};

  /// Copy constructor for clone operation. (Cameras are not cloned!)
  VisualFrame(const VisualFrame& other);
  VisualFrame& operator=(const VisualFrame& other);

  virtual bool operator==(const VisualFrame& other) const;
  /// @}

  virtual bool compareWithoutCameraGeometry(const VisualFrame& other) const;

  template<typename CHANNEL_DATA_TYPE>
  void addChannel(const std::string& channel) {
    aslam::channels::addChannel<CHANNEL_DATA_TYPE>(channel, &channels_);
  }

  /// Are there keypoint measurements stored in this frame?
  bool hasKeypointMeasurements() const;

  /// Are there keypoint measurement uncertainties stored in this frame?
  bool hasKeypointMeasurementUncertainties() const;

  /// Are there keypoint orientations stored in this frame?
  bool hasKeypointOrientations() const;

  /// Are there keypoint scores stored in this frame?
  bool hasKeypointScores() const;

  /// Are there keypoint scales stored in this frame?
  bool hasKeypointScales() const;

  /// Are there descriptors stored in this frame?
  bool hasDescriptors() const;

  /// Are there track ids stored in this frame?
  bool hasTrackIds() const;

  /// Is there a raw image stored in this frame?
  bool hasRawImage() const;

  /// Is a certain channel stored in this frame?
  bool hasChannel(const std::string& channel) const {
    return aslam::channels::hasChannel(channel, channels_);
  }

  /// Clears the following channels: KeypointMeasurements, KeypointMeasurementUncertainties,
  /// KeypointOrientations, KeypointScores, KeypointScales, Descriptors, TrackIds
  void clearKeypointChannels();

  /// The keypoint measurements stored in a frame.
  const Eigen::Matrix2Xd& getKeypointMeasurements() const;

  /// Get the number of keypoint measurements stored in this frame.
  inline size_t getNumKeypointMeasurements() const {
    return hasKeypointMeasurements() ? getKeypointMeasurements().cols() : 0u;
  }

  /// Get the number of LiDAR keypoint measurements stored in this frame.
  inline size_t getNumLidarKeypointMeasurements() const {
    return hasLidarKeypoint3DMeasurements() ? getLidarKeypoint3DMeasurements().cols() : 0u;
  }

  /// Get the total number of keypoint measurements stored in this frame.
  inline size_t getTotalNumKeypointMeasurements() const {
    return getNumKeypointMeasurements() + getNumLidarKeypointMeasurements();
  }

  /// The keypoint measurement uncertainties stored in a frame.
  const Eigen::VectorXd& getKeypointMeasurementUncertainties() const;

  /// The keypoint orientations stored in a frame.
  const Eigen::VectorXd& getKeypointOrientations() const;

  /// The keypoint scores stored in a frame.
  const Eigen::VectorXd& getKeypointScores() const;

  /// The keypoint scales stored in a frame.
  const Eigen::VectorXd& getKeypointScales() const;

  /// The descriptors stored in a frame.
  const DescriptorsT& getDescriptors() const;

  /// The track ids stored in this frame.
  const Eigen::VectorXi& getTrackIds() const;

  /// The raw image stored in a frame.
  const cv::Mat& getRawImage() const;

  /// Release the raw image. Only if the cv::Mat reference count is 1 the memory will be freed.
  void releaseRawImage();

  template<typename CHANNEL_DATA_TYPE>
  const CHANNEL_DATA_TYPE& getChannelData(const std::string& channel) const {
    return aslam::channels::getChannelData<CHANNEL_DATA_TYPE>(channel, channels_);
  }

  /// A pointer to the keypoint measurements, can be used to swap in new data.
  Eigen::Matrix2Xd* getKeypointMeasurementsMutable();

  /// A pointer to the keypoint measurement uncertainties, can be used to swap in new data.
  Eigen::VectorXd* getKeypointMeasurementUncertaintiesMutable();

  /// A pointer to the keypoint orientations, can be used to swap in new data.
  Eigen::VectorXd* getKeypointOrientationsMutable();

  /// A pointer to the keypoint scores, can be used to swap in new data.
  Eigen::VectorXd* getKeypointScoresMutable();

  /// A pointer to the keypoint scales, can be used to swap in new data.
  Eigen::VectorXd* getKeypointScalesMutable();

  /// A pointer to the descriptors, can be used to swap in new data.
  DescriptorsT* getDescriptorsMutable();

  /// A pointer to the track ids, can be used to swap in new data.
  Eigen::VectorXi* getTrackIdsMutable();

  /// A pointer to the raw image, can be used to swap in new data.
  cv::Mat* getRawImageMutable();

  template<typename CHANNEL_DATA_TYPE>
  CHANNEL_DATA_TYPE* getChannelDataMutable(const std::string& channel) const {
    CHANNEL_DATA_TYPE& data =
        aslam::channels::getChannelData<CHANNEL_DATA_TYPE>(channel,
                                                           channels_);
    return &data;
  }

  /// Return block expression of the keypoint measurement pointed to by index.
  const Eigen::Block<Eigen::Matrix2Xd, 2, 1> getKeypointMeasurement(size_t index) const;

  /// Return the keypoint measurement uncertainty at index.
  double getKeypointMeasurementUncertainty(size_t index) const;

  /// Return the keypoint orientation at index.
  double getKeypointOrientation(size_t index) const;

  /// Return the keypoint score at index.
  double getKeypointScore(size_t index) const;

  /// Return the keypoint scale at index.
  double getKeypointScale(size_t index) const;

  /// Return pointer location of the descriptor pointed to by index.
  const unsigned char* getDescriptor(size_t index) const;

  /// Return the track id at index. (-1: not tracked)
  int getTrackId(size_t index) const;

  /// Replace (copy) the internal keypoint measurements by the passed ones.
  void setKeypointMeasurements(const Eigen::Matrix2Xd& keypoints);

  /// Replace (copy) the internal keypoint measurement uncertainties
  ///        by the passed ones.
  void setKeypointMeasurementUncertainties(const Eigen::VectorXd& uncertainties);

  /// Replace (copy) the internal keypoint orientations by the passed ones.
  void setKeypointOrientations(const Eigen::VectorXd& orientations);

  /// Replace (copy) the internal keypoint scores by the passed ones.
  void setKeypointScores(const Eigen::VectorXd& scores);

  /// Replace (copy) the internal keypoint orientations by the passed ones.
  void setKeypointScales(const Eigen::VectorXd& scales);

  /// Replace (copy) the internal descriptors by the passed ones.
  void setDescriptors(const DescriptorsT& descriptors);

  /// Replace (copy) the internal descriptors by the passed ones.
  void setDescriptors(const Eigen::Map<const DescriptorsT>& descriptors);

  /// Replace (copy) the internal track ids by the passed ones.
  void setTrackIds(const Eigen::VectorXi& track_ids);

  /// Replace (copy) the internal raw image by the passed ones.
  ///        This is a shallow copy by default. Please clone the image if it
  ///        should be owned by the VisualFrame.
  void setRawImage(const cv::Mat& image);

  template<typename CHANNEL_DATA_TYPE>
  void setChannelData(const std::string& channel,
                      const CHANNEL_DATA_TYPE& data_new) {
    if (!aslam::channels::hasChannel(channel, channels_)) {
      aslam::channels::addChannel<CHANNEL_DATA_TYPE>(channel, &channels_);
    }
    CHANNEL_DATA_TYPE& data =
        aslam::channels::getChannelData<CHANNEL_DATA_TYPE>(channel, channels_);
    data = data_new;
  }

  /// Replace (swap) the internal keypoint measurements by the passed ones.
  /// This method creates the channel if it doesn't exist
  void swapKeypointMeasurements(Eigen::Matrix2Xd* keypoints);

  /// Replace (swap) the internal keypoint measurement uncertainties
  /// by the passed ones.
  void swapKeypointMeasurementUncertainties(Eigen::VectorXd* uncertainties);

  /// Replace (swap) the internal keypoint orientations by the passed ones.
  void swapKeypointOrientations(Eigen::VectorXd* orientations);

  /// Replace (swap) the internal keypoint scores by the passed ones.
  void swapKeypointScores(Eigen::VectorXd* scores);

  /// Replace (swap) the internal keypoint orientations by the passed ones.
  void swapKeypointScales(Eigen::VectorXd* scales);

  /// Replace (swap) the internal descriptors by the passed ones.
  void swapDescriptors(DescriptorsT* descriptors);

  /// Replace (swap) the internal track ids by the passed ones.
  void swapTrackIds(Eigen::VectorXi* track_ids);

  /// Swap channel data with the data passed in. This will only work
  /// if the channel data type has a swap() method.
  template<typename CHANNEL_DATA_TYPE>
  void swapChannelData(const std::string& channel,
                       CHANNEL_DATA_TYPE* data_new) {
    CHECK_NOTNULL(data_new);
    if (!aslam::channels::hasChannel(channel, channels_)) {
      aslam::channels::addChannel<CHANNEL_DATA_TYPE>(channel, &channels_);
    }
    CHANNEL_DATA_TYPE& data =
        aslam::channels::getChannelData<CHANNEL_DATA_TYPE>(channel, channels_);
    data.swap(*data_new);
  }

  /// The camera geometry.
  const Camera::ConstPtr getCameraGeometry() const;

  /// Set the camera geometry.
  void setCameraGeometry(const Camera::ConstPtr& camera);

  /// The camera geometry.
  const Camera::ConstPtr getRawCameraGeometry() const;

  /// Set the camera geometry.
  void setRawCameraGeometry(const Camera::ConstPtr& camera);

  /// \brief Return keypoint measurement pointed to by index in raw image coordinates.
  /// \param[in] keypoint_idx              Index of the keypoint.
  /// \param[out] keypoint_raw_coordinates The keypoint in raw image coordinates.
  /// \return Was the projection successful?
  aslam::ProjectionResult getKeypointInRawImageCoordinates(size_t keypoint_idx,
      Eigen::Vector2d* keypoint_raw_coordinates) const;

  /// \brief Convert keypoint coordinates to raw image coordinates.
  ///
  /// \param[in] keypoint               The keypoint coordinates with respect to
  ///                                   the camera calibration available from
  ///                                   getCameraGeometry().
  /// \param[out] out_image_coordinates The image coordinates with respect to the
  ///                                   camera calibration available from
  ///                                   getRawCameraGeometry().
  aslam::ProjectionResult toRawImageCoordinates(const Eigen::Vector2d& keypoint,
                                                Eigen::Vector2d* out_image_coordinates) const;

  /// \brief Convert keypoint coordinates to raw image coordinates.
  ///
  /// \param[in] keypoints              The keypoint coordinates with respect to
  ///                                   the camera calibration available from
  ///                                   getCameraGeometry().
  /// \param[out] out_image_coordinates The image coordinates with respect to the
  ///                                   camera calibration available from
  ///                                   getRawCameraGeometry().
  /// \param[out] results               One result for each keypoint.
  void toRawImageCoordinatesVectorized(const Eigen::Matrix2Xd& keypoints,
                                       Eigen::Matrix2Xd* out_image_coordinates,
                                       std::vector<aslam::ProjectionResult>* results) const;

  /// Return a list of normalized bearing vectors for the specified keypoint indices.
  Eigen::Matrix3Xd getNormalizedBearingVectors(
      const std::vector<size_t>& keypoint_indices,
      std::vector<unsigned char>* backprojection_success) const;

  /// Get the frame id.
  inline const aslam::FrameId& getId() const { return id_; }

  /// Set the frame id.
  inline void setId(const aslam::FrameId& id) { id_ = id; }

  /// Get the timestamp.
  inline int64_t getTimestampNanoseconds() const { return timestamp_nanoseconds_; }

  /// Set the timestamp.
  inline void setTimestampNanoseconds(int64_t timestamp_nanoseconds) {
    timestamp_nanoseconds_ = timestamp_nanoseconds;
  }

  /// Set the size of the descriptor in bytes.
  size_t getDescriptorSizeBytes() const;

  /// Set the validity flag to true.
  void validate() { is_valid_ = true; }
  /// Set the validity flag to false.
  void invalidate(){ is_valid_ = false; }
  /// Check the validity flag.
  bool isValid() const { return is_valid_; }
  /// Set the validity flag.
  void setValid(bool is_valid) { is_valid_ = is_valid; }

  /// Print out a human-readable version of this frame
  void print(std::ostream& out, const std::string& label) const;

  /// \brief Creates an empty frame. The following channels are added without any data attached:
  ///        {KeypointMeasurements, KeypointMeasurementUncertainties, Descriptors}
  /// @param[in]  camera                  Camera which will be assigned to the frame.
  /// @param[in]  timestamp_nanoseconds   Timestamp of the frame. [ns]
  /// @return Pointer to the created frame.
  static VisualFrame::Ptr createEmptyTestVisualFrame(const aslam::Camera::ConstPtr& camera,
                                                     int64_t timestamp_nanoseconds);

  void discardUntrackedObservations(std::vector<size_t>* discarded_indices);

  /* Lidar feature point channels and operations. */

  /// Are there Lidar track ids in this frame?
  bool hasLidarTrackIds() const;

  /// Are there Lidar 2D measurements in this frame?
  bool hasLidarKeypoint3DMeasurements() const;

  /// Are there Lidar 2D measurements in this frame?
  bool hasLidarKeypoint2DMeasurements() const;

  /// Are there Lidar descriptors in this frame?
  bool hasLidarDescriptors() const;

  /// Are there Lidar keypoint measurement uncertainties stored in this frame?
  bool hasLidarKeypoint2DMeasurementUncertainties() const;

  /// The Lidar track ids stored in this frame.
  const Eigen::VectorXi& getLidarTrackIds() const;

  /// The Lidar 3D measurements stored in a lidar frame.
  const Eigen::Matrix3Xd& getLidarKeypoint3DMeasurements() const;

  /// The Lidar 2D measurements stored in a frame.
  const Eigen::Matrix2Xd& getLidarKeypoint2DMeasurements() const;

  /// The Lidar descriptors stored in a frame.
  const LidarDescriptorsT& getLidarDescriptors() const;

  /// The Lidar keypoint measurement uncertainties stored in a frame.
  const Eigen::VectorXd& getLidarKeypoint2DMeasurementUncertainties() const;

  /// A pointer to the Lidar track ids, can be used to swap in new data.
  Eigen::VectorXi* getLidarTrackIdsMutable();

  /// A pointer to the Lidar 3D measurements, can be used to swap in new data.
  Eigen::Matrix3Xd* getLidarKeypoint3DMeasurementsMutable();

  /// A pointer to the Lidar 2D measurements, can be used to swap in new data.
  Eigen::Matrix2Xd* getLidarKeypoint2DMeasurementsMutable();

  /// A pointer to the Lidar descriptors, can be used to swap in new data.
  LidarDescriptorsT* getLidarDescriptorsMutable();

  /// A pointer to the Lidar keypoint measurement uncertainties, can be used to swap in new data.
  Eigen::VectorXd* getLidarKeypoint2DMeasurementUncertaintiesMutable();

  /// Return block expression of the Lidar 3D measurement pointed to by index.
  const Eigen::Block<Eigen::Matrix3Xd, 3, 1> getLidarKeypoint3DMeasurement(
      const std::size_t index) const;

  /// Return block expression of the Lidar 2D measurement pointed to by index.
  const Eigen::Block<Eigen::Matrix2Xd, 2, 1> getLidarKeypoint2DMeasurement(
      const std::size_t index) const;

  /// Return pointer location of the Lidar descriptor pointed to by index.
  const unsigned char* getLidarDescriptor(const std::size_t index) const;

  /// Return the Lidar keypoint measurement uncertainty at index.
  double getLidarKeypoint2DMeasurementUncertainty(const std::size_t index) const;

  /// Replace (copy) the internal Lidar track ids by the passed ones.
  void setLidarTrackIds(const Eigen::VectorXi& track_ids);

  /// Replace (copy) the internal Lidar 3D measurements by the passed ones.
  void setLidarKeypoint3DMeasurements(
      const Eigen::Matrix3Xd& lidar_3d_measurements);

  /// Replace (copy) the internal Lidar 2D measurments by the passed ones.
  void setLidarKeypoint2DMeasurements(
      const Eigen::Matrix2Xd& lidar_2d_measurements);

  /// Replace (copy) the internal Lidar descriptors by the passed ones.
  void setLidarDescriptors(const LidarDescriptorsT& descriptors);

  /// Replace (copy) the internal Lidar descriptors by the passed ones.
  void setLidarDescriptors(
      const Eigen::Map<const LidarDescriptorsT>& descriptors);

  /// Replace (copy) the internal Lidar keypoint measurement uncertainties
  ///        by the passed ones.
  void setLidarKeypoint2DMeasurementUncertainties(
      const Eigen::VectorXd& uncertainties);

  /// Replace (swap) the internal Lidar track ids by the passed ones.
  void swapLidarTrackIds(Eigen::VectorXi* track_ids);

  /// Replace (swap) the internal Lidar 3D measurements by the passed ones.
  void swapLidarKeypoint3DMeasurements(Eigen::Matrix3Xd* vectors);

  /// Replace (swap) the internal Lidar 2D measurements by the passed ones.
  void swapLidarKeypoint2DMeasurements(Eigen::Matrix2Xd* vectors);

  /// Replace (swap) the internal Lidar descriptors by the passed ones.
  void swapLidarDescriptors(LidarDescriptorsT* descriptors);

  /// Replace (swap) the internal Lidar keypoint measurement uncertainties
  /// by the passed ones.
  void swapLidarKeypoint2DMeasurementUncertainties(
      Eigen::VectorXd* uncertainties);

  void discardUntrackedLidarObservations(
      std::vector<size_t>* discarded_indices);

  /* External feature point channels and operations.
     The functionalities mirror the standard visual keypoint functions with a
     few exceptions to be able to handle floating point based descriptors. */

  bool hasExternalKeypointMeasurements() const;
  bool hasExternalKeypointMeasurementUncertainties() const;
  bool hasExternalKeypointOrientations() const;
  bool hasExternalKeypointScores() const;
  bool hasExternalKeypointScales() const;
  bool hasExternalDescriptors() const;
  bool hasExternalTrackIds() const;

  const Eigen::Matrix2Xd& getExternalKeypointMeasurements() const;
  const Eigen::VectorXd& getExternalKeypointMeasurementUncertainties() const;
  const Eigen::VectorXd& getExternalKeypointScales() const;
  const Eigen::VectorXd& getExternalKeypointOrientations() const;
  const Eigen::VectorXd& getExternalKeypointScores() const;
  const DescriptorsT& getExternalDescriptors() const;
  const Eigen::VectorXi& getExternalTrackIds() const;

  Eigen::Matrix2Xd* getExternalKeypointMeasurementsMutable();
  Eigen::VectorXd* getExternalKeypointMeasurementUncertaintiesMutable();
  Eigen::VectorXd* getExternalKeypointScalesMutable();
  Eigen::VectorXd* getExternalKeypointOrientationsMutable();
  Eigen::VectorXd* getExternalKeypointScoresMutable();
  DescriptorsT* getExternalDescriptorsMutable();
  Eigen::VectorXi* getExternalTrackIdsMutable();

  const Eigen::Block<Eigen::Matrix2Xd, 2, 1> getExternalKeypointMeasurement(size_t index) const;
  double getExternalKeypointMeasurementUncertainty(size_t index) const;
  double getExternalKeypointScale(size_t index) const;
  double getExternalKeypointOrientation(size_t index) const;
  double getExternalKeypointScore(size_t index) const;
  // const unsigned char* getExternalDescriptor(size_t index) const;
  int getExternalTrackId(size_t index) const;

  void setExternalKeypointMeasurements(const Eigen::Matrix2Xd& keypoints_new);
  void setExternalKeypointMeasurementUncertainties(const Eigen::VectorXd& uncertainties_new);
  void setExternalKeypointScales(const Eigen::VectorXd& scales_new);
  void setExternalKeypointOrientations(const Eigen::VectorXd& orientations_new);
  void setExternalKeypointScores(const Eigen::VectorXd& scores_new);
  void setExternalDescriptors(const DescriptorsT& descriptors_new);
  void setExternalDescriptors(const Eigen::Map<const DescriptorsT>& descriptors_new);
  void setExternalTrackIds(const Eigen::VectorXi& track_ids_new);

  void swapExternalKeypointMeasurements(Eigen::Matrix2Xd* keypoints_new);
  void swapExternalKeypointMeasurementUncertainties(Eigen::VectorXd* uncertainties_new);
  void swapExternalKeypointScales(Eigen::VectorXd* scales_new);
  void swapExternalKeypointOrientations(Eigen::VectorXd* orientations_new);
  void swapExternalKeypointScores(Eigen::VectorXd* scores_new);
  void swapExternalDescriptors(DescriptorsT* descriptors_new);
  void swapExternalTrackIds(Eigen::VectorXi* track_ids_new);

  void clearExternalKeypointChannels();

 private:
  /// Timestamp in nanoseconds.
  int64_t timestamp_nanoseconds_;

  aslam::FrameId id_;
  aslam::channels::ChannelGroup channels_;
  Camera::ConstPtr camera_geometry_;
  Camera::ConstPtr raw_camera_geometry_;

  /// Validity flag: can be used by an external algorithm to flag frames that should
  /// be excluded/included when processing a list of frames. Does not have any internal
  /// effect on the frame.
  bool is_valid_;
};

inline std::ostream& operator<<(std::ostream& out, const VisualFrame& rhs) {
  rhs.print(out, "");
  return out;
}
}  // namespace aslam
#endif  // ASLAM_FRAMES_VISUAL_FRAME_H_
