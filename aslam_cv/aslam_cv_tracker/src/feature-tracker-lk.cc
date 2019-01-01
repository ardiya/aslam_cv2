#include <aslam/common/statistics/statistics.h>
#include <aslam/common/timer.h>
#include <aslam/frames/visual-frame.h>
#include <aslam/matcher/match-helpers.h>
#include <aslam/tracker/feature-tracker-lk.h>
#include <aslam/tracker/tracking-helpers.h>
#include <brisk/brisk.h>
#include <gflags/gflags.h>
#include <opencv/highgui.h>
#include <opencv2/nonfree/nonfree.hpp>

DEFINE_bool(lk_show_detection_mask, false, "Draw the detection mask.");
DEFINE_string(lk_detector_type, "ocvbrisk", "Keypoint detector type.");
DEFINE_int32(lk_ocv_brisk_detector_threshold, 20, "Threshold on difference between "
    "intensity of the central pixel and pixels of a circle around this pixel.");
DEFINE_int32(lk_ocv_brisk_detector_octaves, 0,
             "Detection octaves. Use 0 to do single scale.");
DEFINE_double(lk_ocv_brisk_detector_patternScale, 1.0,
            "Scale applied to the pattern used for "
            "sampling the neighbourhood of a keypoint.");
DEFINE_uint64(lk_brisk_octaves, 1, "Brisk detector number of octaves.");
DEFINE_uint64(lk_brisk_uniformity_radius_px, 0, "Brisk detector uniformity radius.");
DEFINE_uint64(lk_brisk_absolute_threshold, 45, "Brisk detector absolute threshold.");
DEFINE_double(lk_min_distance_between_features_px, 5.0, "Minimal image space distance between "
              "nearest features in pixels.");
DEFINE_uint64(lk_max_feature_count, 750, "Max. number of features to track.");
DEFINE_uint64(lk_min_feature_count, 500, "Min. number of tracked features before a redetection is"
              "performed.");
DEFINE_double(lk_min_eigen_threshold, 0.001, "Minimum eigen value of a 2x2 normal matrix of the"
              "optical flow equations.");
DEFINE_uint64(lk_max_pyramid_level, 3, "Maximal pyramid level number for the Lk-tracking.");
DEFINE_uint64(lk_window_size, 21, "Size of the search window at each pyramid level.");
DEFINE_double(lk_max_tracking_error, 1.0, "blabla");
DEFINE_double(lk_keypoint_search_radius_pixels_squared, 3.0, "blala");

DEFINE_int32(ocv_surf_detector_hessian_threshold, 400, "");
DEFINE_int32(ocv_surf_detector_octaves, 4, "");
DEFINE_int32(ocv_surf_detector_octave_layers, 2, "");
DEFINE_bool(ocv_surf_extended, true, "");
DEFINE_bool(ocv_surf_upright, false, "");

namespace aslam {

LkTrackerSettings::LkTrackerSettings()
    : detector_type(convertStringToDetectorType(FLAGS_lk_detector_type)),
      ocv_brisk_detector_octaves(FLAGS_lk_ocv_brisk_detector_octaves),
      ocv_brisk_detector_patternScale(FLAGS_lk_ocv_brisk_detector_patternScale),
      ocv_brisk_detector_threshold(FLAGS_lk_ocv_brisk_detector_threshold),
      brisk_detector_octaves(FLAGS_lk_brisk_octaves),
      brisk_detector_uniformity_radius_px(FLAGS_lk_brisk_uniformity_radius_px),
      brisk_detector_absolute_threshold(FLAGS_lk_brisk_absolute_threshold),
      ocv_surf_detector_hessian_threshold(FLAGS_ocv_surf_detector_hessian_threshold),
      ocv_surf_detector_octaves(FLAGS_ocv_surf_detector_octaves),
      ocv_surf_detector_octave_layers(FLAGS_ocv_surf_detector_octave_layers),
      ocv_surf_extended(FLAGS_ocv_surf_extended),
      ocv_surf_upright(FLAGS_ocv_surf_upright),
      min_distance_between_features_px(FLAGS_lk_min_distance_between_features_px),
      max_feature_count(FLAGS_lk_max_feature_count),
      min_feature_count(FLAGS_lk_min_feature_count),
      lk_min_eigen_threshold(FLAGS_lk_min_eigen_threshold),
      lk_max_pyramid_level(FLAGS_lk_max_pyramid_level),
      lk_window_size(FLAGS_lk_window_size) {
  CHECK_GE(ocv_brisk_detector_octaves, 0);
  CHECK_GT(ocv_brisk_detector_patternScale, 0.0f);
  CHECK_GE(ocv_brisk_detector_threshold, 0);
  CHECK_GE(brisk_detector_octaves, 0);
  CHECK_GT(min_distance_between_features_px, 1.0);
  CHECK_GT(min_feature_count, 0u);
  CHECK_GT(max_feature_count, min_feature_count);
  CHECK_GT(lk_min_eigen_threshold, 0.0);
  CHECK_GT(lk_window_size, 0u);
}

LkTrackerSettings::DetectorType LkTrackerSettings::convertStringToDetectorType(
    const std::string& detector_string) {
  if (detector_string == "brisk") {
    return DetectorType::kBriskDetector;
  } else if (detector_string == "ocvgfft") {
    return DetectorType::kOcvGfft;
  } else if (detector_string == "ocvbrisk") {
    return DetectorType::kOcvBrisk;
  } else if (detector_string == "ocvsurf") {
    return DetectorType::kOcvSurf;
  }
  LOG(FATAL) << "Unknown detector type: " << FLAGS_lk_detector_type;
}

FeatureTrackerLk::FeatureTrackerLk(const aslam::Camera& camera, const LkTrackerSettings& settings)
    : lk_window_size_(settings.lk_window_size, settings.lk_window_size),
      camera_(camera),
      settings_(settings) {
  initialize(camera);
}

void FeatureTrackerLk::initialize(const aslam::Camera& camera) {
  // Create a detection mask that prevents detecting new keypoints close to the image border as
  // no descriptors can be calculated in this region.
  CHECK_LT(2 * kMinDistanceToImageBorderPx, camera.imageWidth());
  CHECK_LT(2 * kMinDistanceToImageBorderPx, camera.imageHeight());
  if (camera.hasMask()) {
    VLOG(5) << "Loading camera mask.";
    detection_mask_image_border_ = camera.getMask();
    CHECK_EQ(detection_mask_image_border_.rows, camera.imageHeight());
    CHECK_EQ(detection_mask_image_border_.cols, camera.imageWidth());
    VLOG(5) << "Mask has dimensions: " << detection_mask_image_border_.rows << 'x'
        << detection_mask_image_border_.cols;
  } else {
    detection_mask_image_border_ = cv::Mat::zeros(
        camera.imageHeight(), camera.imageWidth(), CV_8UC1);
    cv::Mat region_of_interest(
        detection_mask_image_border_, cv::Rect(
            kMinDistanceToImageBorderPx + 1,kMinDistanceToImageBorderPx + 1,
            camera.imageWidth() - 2 * kMinDistanceToImageBorderPx - 1,
            camera.imageHeight() - 2 * kMinDistanceToImageBorderPx - 1));
    region_of_interest = cv::Scalar(255);
    VLOG(5) << "Created mask with dimension: " << detection_mask_image_border_.rows << 'x'
        << detection_mask_image_border_.cols;
  }

  switch (settings_.detector_type) {
    case LkTrackerSettings::DetectorType::kOcvBrisk:
      detector_ = new cv::BRISK(settings_.ocv_brisk_detector_threshold,
                                settings_.ocv_brisk_detector_octaves,
                                settings_.ocv_brisk_detector_patternScale);
      break;
    case LkTrackerSettings::DetectorType::kOcvSurf:
      detector_ = new cv::SURF(
          settings_.ocv_surf_detector_hessian_threshold, settings_.ocv_surf_detector_octaves,
          settings_.ocv_surf_detector_octave_layers, settings_.ocv_surf_extended,
          settings_.ocv_surf_upright);
  }
}

void FeatureTrackerLk::initializeKeypointsInEmptyVisualFrame(
    aslam::VisualFrame* frame) const {
  CHECK_NOTNULL(frame);
  CHECK(!frame->hasKeypointMeasurements() || frame->getNumKeypointMeasurements() == 0u);

  VLOG(5) << "Creating occ grid with the following parameters: "
      << camera_.imageHeight() << ", "
      << camera_.imageWidth() << ", "
      << settings_.min_distance_between_features_px;

  OccupancyGrid occupancy_grid(camera_.imageHeight(), camera_.imageWidth(),
                               settings_.min_distance_between_features_px,
                               settings_.min_distance_between_features_px);

  detectNewKeypointsInVisualFrame(*frame, detection_mask_image_border_,
                                  &occupancy_grid);

  VLOG(10) << "Detected new keypoints in visual frame.";

  OccupancyGrid::PointList keypoints;
  occupancy_grid.getAllPointsInGrid(&keypoints);

  VLOG(5) << "Inserting " << keypoints.size()
           << " occupancy grid filtered keypoints into the frame.";
  //Vector2dList new_keypoints_occupancy_grid_filtered;
  std::vector<cv::KeyPoint> new_keypoints_occupancy_grid_filtered;

  new_keypoints_occupancy_grid_filtered.reserve(keypoints.size());
  for (const WeightedKeypoint& point : keypoints) {
    new_keypoints_occupancy_grid_filtered.emplace_back(point.point);
    //v_cols,
    //                                                   point.u_rows);
  }
  // Insert the new keypoints into the frame. No special care wrt. ordering
  // necessary, since we only have new keypoints here.
  insertCvKeypointsIntoEmptyVisualFrame(
      new_keypoints_occupancy_grid_filtered, kKeypointUncertaintyPx, frame);

  //insertAdditionalKeypointsToVisualFrame(new_keypoints_occupancy_grid_filtered,
  //                                       kKeypointUncertaintyPx, frame);

  CHECK(frame->hasKeypointMeasurements());
}

void FeatureTrackerLk::detectNewKeypointsInVisualFrame(
    const aslam::VisualFrame& frame, const cv::Mat& detection_mask,
    OccupancyGrid* occupancy_grid) const {
  CHECK_NOTNULL(occupancy_grid);
  timing::Timer keypoint_detection_timer(
      "FeatureTrackerLk::detectNewKeypointsInVisualFrame_keypoint_detection");

  CHECK(frame.hasRawImage())
      << "Can only detect keypoints if the frame has a raw image";

  const size_t num_keypoints_to_detect =
      settings_.max_feature_count - occupancy_grid->getNumPoints();

  std::vector<cv::KeyPoint> new_keypoints;
  //Vector2dList new_keypoints;
  //std::vector<double> new_keypoints_scores;
  detectNewKeypoints(frame.getRawImage(), num_keypoints_to_detect,
                     detection_mask, &new_keypoints);  //&new_keypoints_scores
  VLOG(5) << "Detected " << new_keypoints.size() << " out of a desired "
           << num_keypoints_to_detect << " keypoints.";

  keypoint_detection_timer.Stop();

  // Drop keypoint if it moved too close to the image border as we can't compute a descriptor.
  std::vector<cv::KeyPoint>::iterator keypoint_iterator = new_keypoints.begin();
  while (keypoint_iterator != new_keypoints.end()) {
    const cv::Point2f& point = keypoint_iterator->pt;
    //const Eigen::Vector2d& point = *keypoint_iterator;
    if (point.x < kMinDistanceToImageBorderPx ||
        point.x >= (camera_.imageWidth() - kMinDistanceToImageBorderPx) ||
        point.y < kMinDistanceToImageBorderPx ||
        point.y >= (camera_.imageHeight() - kMinDistanceToImageBorderPx)) {
      // Erase this point.
      keypoint_iterator = new_keypoints.erase(keypoint_iterator);
    } else {
      ++keypoint_iterator;
    }
  }

  // Add the new points to the occupancy grid. If a keypoint is inserted too
  // close to an existing point in the grid, the point with the higher score
  // will be kept. The grid stores an id for each point that corresponds to the
  // keypoint index in the previous frame for tracked keypoints. If it is a
  // newly detected keypoint the index -1 is set.
  timing::Timer keypoints_grid_insertion_timer(
      "FeatureTrackerLk::detectNewKeypointsInVisualFrame_keypoint_grid_"
      "insertion");
  const int kKeypointMatchIndexPreviousFrame = -1;
  const size_t num_tracked_keypoints = occupancy_grid->getNumPoints();
  for (size_t idx = 0u; idx < new_keypoints.size(); ++idx) {
    occupancy_grid->addPointOrReplaceWeakestNearestPoints(
        WeightedKeypoint(new_keypoints[idx], new_keypoints[idx].pt.y, new_keypoints[idx].pt.x,
                         new_keypoints[idx].response,
                         /*new_keypoints_scores[idx],*/
                         kKeypointMatchIndexPreviousFrame),
        settings_.min_distance_between_features_px);
    VLOG(100) << "After adding new keypoint " << idx << " with score: "
        << new_keypoints[idx].response
        << ", have " << occupancy_grid->getNumPoints() << " points in the grid.";
  }
  keypoints_grid_insertion_timer.Stop();

  aslam::statistics::StatsCollector("lk-tracker: num detected keypoints").
      AddSample(new_keypoints.size());
  const size_t num_added_detections =
      occupancy_grid->getNumPoints() - num_tracked_keypoints;
  aslam::statistics::StatsCollector("lk-tracker: num detected keypoints add").
      AddSample(num_added_detections);
  const size_t num_rejected_detections =
      new_keypoints.size() - num_added_detections;
  aslam::statistics::StatsCollector("lk-tracker: num detected keypoints rejected").
      AddSample(num_rejected_detections);
}

void FeatureTrackerLk::track(
    const Quaternion& q_Ckp1_Ck, const VisualFrame& frame_k, VisualFrame* frame_kp1,
    FrameToFrameMatchesWithScore* matches_with_score_kp1_k) {
  aslam::timing::Timer timer_tracking("FeatureTrackerLk: track");
  CHECK_NOTNULL(frame_kp1);
  CHECK_NOTNULL(matches_with_score_kp1_k)->clear();
  CHECK_EQ(camera_.getId(), CHECK_NOTNULL(frame_k.getCameraGeometry().get())->getId());
  CHECK_EQ(camera_.getId(), CHECK_NOTNULL(frame_kp1->getCameraGeometry().get())->getId());

  // Make sure, frame_k has keypoint measurements (at least the channel).
  LOG_IF(WARNING, !frame_k.hasKeypointMeasurements())
      << "The frame k does not have keypoint measurements. The track function "
      << "will not track anything between the frame_k and frame_kp1 "
      << "and only initialize new keypoints in frame kp1."
      << "Call FeatureTrackerLk::initializeKeypointsInVisualFrame(...) with "
      << "frame_k beforehand if you want to track keypoints between frame_k "
      << "and frame_kp1.";

  // Make sure the frame_kp1 does not yet contain keypoint/tracking information.
  CHECK(frame_kp1->hasRawImage());
  CHECK(!frame_kp1->hasKeypointMeasurements() || frame_kp1->getNumKeypointMeasurements() == 0u);
  CHECK(!frame_kp1->hasTrackIds() || frame_kp1->getTrackIds().rows() == 0);

  // Make sure the externally set list of keypoints to abort corresponds to this frame.
  if (!keypoint_indices_to_abort_.empty() && abort_keypoints_wrt_frame_id_ != frame_k.getId()) {
    LOG(FATAL) << "Keypoints to abort do not match the processed frame.";
  }

  // Track existing keypoints from frame (k) to frame (k+1).
  //Vector2dList tracked_keypoints_kp1;
  std::vector<cv::Point2f> tracked_points_kp1;
  std::vector<unsigned char> tracking_success;
  std::vector<float> tracking_errors;
  VLOG(5) << "Tracking keypoints with q_Ckp1_Ck: " << q_Ckp1_Ck.toImplementation().coeffs().transpose();
  trackKeypoints(q_Ckp1_Ck, frame_k, frame_kp1->getRawImage(), &tracked_points_kp1,
                 &tracking_success, &tracking_errors);
  VLOG(5) << "Tracked " << tracked_points_kp1.size()
           << " keypoints from frame k to kp1.";
  CHECK_EQ(frame_k.getNumKeypointMeasurements(), tracked_points_kp1.size());

  std::vector<cv::KeyPoint> tracked_keypoints_kp1;
  getCvKeypointsFromFrame(frame_k, &tracked_keypoints_kp1);
  CHECK_EQ(tracked_keypoints_kp1.size(), tracked_points_kp1.size());

  for (size_t idx = 0u; idx < tracked_keypoints_kp1.size(); ++idx) {
    tracked_keypoints_kp1[idx].pt = tracked_points_kp1[idx];
  }

  // Detect BRISK.
  //Vector2dList keypoints_kp1_match;
  std::vector<cv::KeyPoint> keypoints_kp1_match;
  detectNewKeypoints(frame_kp1->getRawImage(), settings_.max_feature_count,
                     detection_mask_image_border_, &keypoints_kp1_match);
  VLOG(5) << "Detected " << keypoints_kp1_match.size() << " new keypoints.";

  // Reject tracked keypoints that meet one of the following criteria:
  //   - tracking was unsuccessful
  //   - marked for abortion by the external 2pt-ransac
  //   - too close to the image border
  //   - too close to other tracked point. In case of conflicts the keypoint
  //     with the lowest tracking error will be kept.
  // Reject features that got too close to each other (probably by object occlusion), are outside
  // the image, tracking failed or were marked for abortion by external logic. Keep the features
  // with the lower tracking error in case of conflicts.
  aslam::timing::Timer timer_selection("FeatureTrackerLk: track - feature selection");

  OccupancyGrid occupancy_grid(camera_.imageHeight(),camera_.imageWidth(),
                               settings_.min_distance_between_features_px,
                               settings_.min_distance_between_features_px);

  size_t num_failed_tracking = 0u;
  size_t num_outside_image = 0u;
  size_t num_external_abort = 0u;
  size_t num_too_high_tracking_error = 0u;
  size_t num_skipped_because_no_keypoint = 0u;
  size_t num_points_added = 0u;
  for (size_t keypoint_idx_k = 0u; keypoint_idx_k < tracked_keypoints_kp1.size();
      ++keypoint_idx_k) {
    // Drop keypoint if the tracking was unsuccessful.
    if (!tracking_success[keypoint_idx_k]) {
      ++num_failed_tracking;
      //VLOG(5) << "Failed tracking abort.";
      continue;
    }

    // Drop keypoint if it moved too close to the image border as we can't compute a descriptor.
    Eigen::Vector2d point(
        tracked_keypoints_kp1[keypoint_idx_k].pt.x, tracked_keypoints_kp1[keypoint_idx_k].pt.y);
    if (point(0) < kMinDistanceToImageBorderPx ||
        point(0) >= (camera_.imageWidth() - kMinDistanceToImageBorderPx) ||
        point(1) < kMinDistanceToImageBorderPx ||
        point(1) >= (camera_.imageHeight() - kMinDistanceToImageBorderPx)) {
      ++num_outside_image;
      //VLOG(5) << "Outside image abort.";
      continue;
    }

    // Drop keypoint if it is marked for abortion.
    if (keypoint_indices_to_abort_.count(keypoint_idx_k) >= 1u) {
      ++num_external_abort;
      //VLOG(5) << "External abort.";
      continue;
    }

    double min_distance = std::numeric_limits<double>::infinity();
    size_t min_index = 0u;
    const size_t num_keypoints_kp1 = keypoints_kp1_match.size();
    for (size_t keypoint_kp1_idx = 0u; keypoint_kp1_idx < num_keypoints_kp1; ++keypoint_kp1_idx) {
      const Eigen::Vector2d keypoint_kp1(
          keypoints_kp1_match[keypoint_kp1_idx].pt.x, keypoints_kp1_match[keypoint_kp1_idx].pt.y);
      const double distance_pixels_squared = (keypoint_kp1 - point).squaredNorm();
      if (distance_pixels_squared < min_distance) {
        min_distance = distance_pixels_squared;
        min_index = keypoint_kp1_idx;
      }
    }

    VLOG(6) << "min_distance: " << min_distance << " ("
        << FLAGS_lk_keypoint_search_radius_pixels_squared << ")"; //>" with score " << keypoints_kp1_scores[min_index];
    if (min_distance < FLAGS_lk_keypoint_search_radius_pixels_squared) {
      point = Eigen::Vector2d(
          keypoints_kp1_match[min_index].pt.x, keypoints_kp1_match[min_index].pt.y);
    } else {
      const float tracking_error = tracking_errors[keypoint_idx_k];
      //VLOG(5) << "Tracking error: " << tracking_error << " (" << FLAGS_lk_max_tracking_error << ").";
      if (tracking_error > FLAGS_lk_max_tracking_error) {
        //VLOG(5) << "Skipped with tracking error of " << tracking_errors[keypoint_idx_k];
        ++num_too_high_tracking_error;
        continue;
      }
    }

    ++num_points_added;
    // Drop keypoints that have moved too close to another tracked keypoint.
    occupancy_grid.addPointOrReplaceWeakestNearestPoints(
        WeightedKeypoint(
            tracked_keypoints_kp1[keypoint_idx_k], point(1), point(0),
            -tracking_errors[keypoint_idx_k], keypoint_idx_k),
        settings_.min_distance_between_features_px);

    //LOG(ERROR) << "Num points: " << occupancy_grid.getNumPoints(); // << ", tracing error: "
        //<< tracking_errors[keypoint_idx_k];
  }
  timer_selection.Stop();
  //LOG(WARNING) << "Num points added: " << num_points_added;

  // Keep some statistics about tracking failures.
  aslam::statistics::StatsCollector("lk-tracker: num tried track keypoints").
      AddSample(tracked_keypoints_kp1.size());
  aslam::statistics::StatsCollector("lk-tracker: num tracking successful").
      AddSample(occupancy_grid.getNumPoints());
  aslam::statistics::StatsCollector("lk-tracker: num failed tracking error").
      AddSample(num_failed_tracking);
  aslam::statistics::StatsCollector("lk-tracker: num failed outside image").
      AddSample(num_outside_image);
  aslam::statistics::StatsCollector("lk-tracker: num failed external abort").
      AddSample(num_external_abort);
  aslam::statistics::StatsCollector too_high_tracking_error("lk-tracker: num too high tracking-error abort");
  too_high_tracking_error.AddSample(num_too_high_tracking_error);
  aslam::statistics::StatsCollector no_keypoint("lk-tracker: num no keypoint match abort");
  no_keypoint.AddSample(num_skipped_because_no_keypoint);
  const size_t num_failed_occupancy_grid = tracked_keypoints_kp1.size() - num_failed_tracking
      - num_outside_image - num_external_abort - occupancy_grid.getNumPoints();
  aslam::statistics::StatsCollector("lk-tracker: num failed occupancy grid").
      AddSample(num_failed_occupancy_grid);

  // Set an infinite weight for all tracked keypoints in the occupancy grid to avoid replacing them
  // with new detected keypoints.
  occupancy_grid.setConstantWeightForAllPointsInGrid(
      std::numeric_limits<OccupancyGrid::WeightType>::max());

  // Detect new keypoints if the number of keypoints drops below the specified threshold.
  if (occupancy_grid.getNumPoints() < settings_.min_feature_count) {
    aslam::statistics::StatsCollector lk_redetection("lk-tracker: redetection");
    lk_redetection.AddSample(1);

    VLOG(5) << "Below min feature count. Spawning new ones.";
    // Create the detection mask consisting of the mask that prevents detecting points too close to
    // the image border and the mask of the current points in the occupancy grid.
    const size_t kMaxNumberOfKeypointPerCell = std::numeric_limits<size_t>::max();
    const cv::Mat detection_mask_occupancy_grid = occupancy_grid.getOccupancyMask(
        settings_.min_distance_between_features_px, kMaxNumberOfKeypointPerCell);
    cv::Mat detection_mask;
    cv::bitwise_and(detection_mask_image_border_, detection_mask_occupancy_grid, detection_mask);
    if (FLAGS_lk_show_detection_mask) {
      cv::namedWindow("detection mask");
      cv::imshow("detection mask", detection_mask);
      cv::waitKey(0);
    }

    // Detect new points.
    CHECK_LT(occupancy_grid.getNumPoints(), settings_.max_feature_count);
    detectNewKeypointsInVisualFrame(*frame_kp1, detection_mask, &occupancy_grid);
  }

  // Write the keypoints to the frame (k+1) in the following order [tracked, new keypoints]. Also
  // Extract the index-pairs between matching keypoints between frame (k) and (k+1).
  OccupancyGrid::PointList keypoints_kp1;
  occupancy_grid.getAllPointsInGrid(&keypoints_kp1);

  //Vector2dList new_keypoints_kp1;
  std::vector<cv::KeyPoint> new_keypoints_kp1;
  new_keypoints_kp1.reserve(keypoints_kp1.size());
  size_t keypoint_idx_kp1 = 0u;
  VLOG(5) << "Going over " << keypoints_kp1.size() << " weighted keypoints.";
  for (const WeightedKeypoint& point : keypoints_kp1) {    // Register a match if the point was successfully tracked from the previous frame. An id of -1
    // marks a new detected point.
    if (point.id >= 0) {
      const int keypoint_idx_k = point.id;
      matches_with_score_kp1_k->emplace_back(keypoint_idx_kp1, keypoint_idx_k, point.weight);
    }

    new_keypoints_kp1.emplace_back(point.point); //point.v_cols, point.u_rows);
    ++keypoint_idx_kp1;
  }

  insertCvKeypointsIntoEmptyVisualFrame(new_keypoints_kp1, kKeypointUncertaintyPx, frame_kp1);
  //insertAdditionalKeypointsToVisualFrame(new_keypoints_kp1, kKeypointUncertaintyPx, frame_kp1);

  // Reset the list of keypoints to abort tracking.
  keypoint_indices_to_abort_.clear();
  abort_keypoints_wrt_frame_id_.setInvalid();
}

void FeatureTrackerLk::trackKeypoints(const Quaternion& q_Ckp1_Ck, const VisualFrame& frame_k,
                                      const cv::Mat& image_frame_kp1,
                                      std::vector<cv::Point2f>* tracked_keypoints_kp1,
                                      std::vector<unsigned char>* tracking_success,
                                      std::vector<float>* tracking_errors) const {
  aslam::timing::Timer timer_tracking("FeatureTrackerLk: track - trackKeypoints");
  CHECK_NOTNULL(tracked_keypoints_kp1)->clear();
  CHECK_NOTNULL(tracking_success)->clear();
  CHECK_NOTNULL(tracking_errors)->clear();

  // Early exit if the frame k does not contain any keypoints.
  if (!frame_k.hasKeypointMeasurements() || frame_k.getNumKeypointMeasurements() == 0u) {
    VLOG(3) << "Aborting tracking of keypoints because frame_k does not have "
            << "any.";
    return;
  }

  // Predict the keypoint locations from the frame (k) to the frame (k+1) using the rotation prior.
  // The initial keypoint location is kept if the prediction failed.
  Eigen::Matrix2Xd predicted_keypoints_kp1;
  std::vector<unsigned char> prediction_success;
  predictKeypointsByRotation(frame_k, q_Ckp1_Ck, &predicted_keypoints_kp1, &prediction_success);
  size_t num_prediction_success = 0u;
  size_t num_prediction_failure = 0u;
  for (const unsigned char success : prediction_success) {
    if (success) {
      ++num_prediction_success;
    } else {
      ++num_prediction_failure;
    }
  }
  VLOG(3) << "Num prediction success/failures: " << num_prediction_success
      << '/' << num_prediction_failure;

  // Convert the keypoint type to OpenCV.
  std::vector<cv::Point2f> keypoints_k, keypoints_kp1;
  convertKeypointVectorToCvPointList(frame_k.getKeypointMeasurements(), &keypoints_k);
  convertKeypointVectorToCvPointList(predicted_keypoints_kp1, &keypoints_kp1);

  // Find the keypoint location in the frame (k+1) starting from the predicted positions using
  // optical flow. If the flow wasn’t found, then the error is not defined. Use the
  // tracking_success parameter to find such cases.
  cv::calcOpticalFlowPyrLK(frame_k.getRawImage(),
                           image_frame_kp1,
                           keypoints_k,
                           keypoints_kp1,
                           *tracking_success,
                           *tracking_errors,
                           lk_window_size_,
                           settings_.lk_max_pyramid_level,
                           kTerminationCriteria,
                           kOperationFlag,
                           settings_.lk_min_eigen_threshold);

  VLOG(5) << "Calculated optical flow with " << std::endl
      << keypoints_k.size() << " keypoints_k" << std::endl
      << keypoints_kp1.size() << " keypoints kp1" << std::endl
      << "lk_window_size_: " << lk_window_size_ << std::endl
      << "lk_max_pyramic_level: " << settings_.lk_max_pyramid_level << std::endl
      << "termination criteria epsilon: " << kTerminationCriteria.epsilon << std::endl
      << "termination criteria max count: " << kTerminationCriteria.maxCount << std::endl
      << "termination criteria type: " << kTerminationCriteria.type << std::endl
      << "operation flag: " << kOperationFlag << std::endl
      << "lk_min_eigen_threshold: " << settings_.lk_min_eigen_threshold;

  CHECK_EQ(keypoints_kp1.size(), keypoints_k.size());
  *tracked_keypoints_kp1 = keypoints_kp1;
  /*
  tracked_keypoints_kp1->reserve(keypoints_kp1.size());
  for (const cv::Point2f& tracked_point : keypoints_kp1) {
    cv::KeyPoint keypoint;
    keypoint.pt = tracked_point;
    keypoint
    tracked_keypoints_kp1->emplace_back(tracked_point.x, tracked_point.y);
  }*/

  CHECK_EQ(keypoints_k.size(), keypoints_kp1.size());
  CHECK_EQ(tracking_success->size(), keypoints_kp1.size());
  CHECK_EQ(tracking_errors->size(), keypoints_kp1.size());
  CHECK_EQ(tracked_keypoints_kp1->size(), keypoints_kp1.size());
}

void FeatureTrackerLk::detectNewKeypoints(const cv::Mat& image_kp1,
                                          size_t num_keypoints_to_detect,
                                          const cv::Mat& detection_mask,
                                          std::vector<cv::KeyPoint>* out_keypoints) const {
  aslam::timing::Timer timer_detection("FeatureTrackerLk: detectNewKeypoints");
  CHECK_NOTNULL(out_keypoints)->clear();

  // Early exit if no keypoints need to be detected.
  if (num_keypoints_to_detect == 0u) {
    return;
  }

  std::vector<cv::KeyPoint> keypoints_cv;
  switch (settings_.detector_type) {
    case LkTrackerSettings::DetectorType::kOcvSurf: {
      detector_->detect(image_kp1, keypoints_cv, detection_mask);
      cv::KeyPointsFilter::retainBest(keypoints_cv, num_keypoints_to_detect);
      break;
    }
    case LkTrackerSettings::DetectorType::kOcvBrisk: {
      //VLOG(5) << "OCV BRISK";
      detector_->detect(image_kp1, keypoints_cv, detection_mask);
      cv::KeyPointsFilter::retainBest(keypoints_cv, num_keypoints_to_detect);
      break;
    }
    case LkTrackerSettings::DetectorType::kBriskDetector: {
      //VLOG(5) << "BRISK";
      // The detector needs to be reconstructed in each iteration as brisk doesn't provide an
      // interface to change the number of detected keypoints.
      brisk::ScaleSpaceFeatureDetector<brisk::HarrisScoreCalculator> detector(
          settings_.brisk_detector_octaves,
          settings_.brisk_detector_uniformity_radius_px,
          settings_.brisk_detector_absolute_threshold, num_keypoints_to_detect);

      // Detect new keypoints in the unmasked image area.
      keypoints_cv.reserve(num_keypoints_to_detect);
      detector.detect(image_kp1, keypoints_cv, detection_mask);

      VLOG(5) << "Detected: " << keypoints_cv.size();
      /*
      cv::Mat image_with_kps;
      cv::namedWindow("D");
      cv::drawKeypoints(image_kp1, keypoints_cv, image_with_kps);
      cv::imshow("D", image_with_kps);
      cv::waitKey(0);*/

      // Since the Brisk detector ignores the detection mask, we apply it manually.
      std::vector<cv::KeyPoint>::iterator new_keypoints_iterator = keypoints_cv.begin();
      while (new_keypoints_iterator != keypoints_cv.end()) {
        if (detection_mask.at<unsigned char>(new_keypoints_iterator->pt) > 0u) {
          ++new_keypoints_iterator;
        } else {
          new_keypoints_iterator = keypoints_cv.erase(new_keypoints_iterator);
        }
      }
      break;
    }
    case LkTrackerSettings::DetectorType::kOcvGfft: {
      static constexpr double kGoodFeaturesToTrackQualityLevel = 0.001;
      const cv::Size kSubPixelWinSize = cv::Size(10, 10);
      const cv::Size kSubPixelZeroZone = cv::Size(-1, -1);

      std::vector<cv::Point2f> points_cv;
      cv::goodFeaturesToTrack(image_kp1, points_cv, num_keypoints_to_detect,
                              kGoodFeaturesToTrackQualityLevel,
                              settings_.min_distance_between_features_px, detection_mask);

      aslam::timing::Timer timer_subpix("FeatureTrackerLk: detection - cornerSubPix");
      cv::cornerSubPix(image_kp1, points_cv, kSubPixelWinSize, kSubPixelZeroZone,
                       kTerminationCriteria);
      timer_subpix.Stop();

      // Convert to Keypoint datatype and set a constant score as the gfft detector does not
      // provide any score but the keypoints are sorted by descending detector response.
      cv::KeyPoint::convert(points_cv, keypoints_cv);
      double score = 1.0;
      for (cv::KeyPoint& keypoint : keypoints_cv) {
        keypoint.response = score;
        score -= 1.0;
      }
      break;
    }
    default: {
      LOG(FATAL) << "Unhandled detector type.";
    }
  }

  // Convert the data types to the output structures.
  /*
  keypoints->reserve(keypoints_cv.size());
  keypoint_scores->reserve(keypoints_cv.size());
  keypoint_scales->reserve(keypoints_cv.size());*/
  for (size_t idx = 0u; idx < keypoints_cv.size(); ++idx) {
    if (keypoints_cv[idx].pt.x < kMinDistanceToImageBorderPx ||
        keypoints_cv[idx].pt.x >= (camera_.imageWidth() - kMinDistanceToImageBorderPx) ||
        keypoints_cv[idx].pt.y < kMinDistanceToImageBorderPx ||
        keypoints_cv[idx].pt.y >= (camera_.imageHeight() - kMinDistanceToImageBorderPx)) {
      continue;
    }

    out_keypoints->emplace_back(keypoints_cv[idx]);
    /*

    keypoints->emplace_back(keypoints_cv[idx].pt.x, keypoints_cv[idx].pt.y);
    keypoint_scores->emplace_back(keypoints_cv[idx].response);
    keypoint_scales->emplace_back(keypoints_cv[idx].size);
    keypoint_angles->emplace_back(keypoints_cv[idx].angle);*/
  }
}

void FeatureTrackerLk::swapKeypointIndicesToAbort(
    const FrameId& frame_id, std::unordered_set<size_t>* keypoint_indices_to_abort) {
  CHECK_NOTNULL(keypoint_indices_to_abort);
  CHECK(frame_id.isValid());
  keypoint_indices_to_abort_.swap(*keypoint_indices_to_abort);
  abort_keypoints_wrt_frame_id_ = frame_id;
}

}  // namespace aslam
