#include "ccny_rgbd_calibrate/depth_calibrator.h"

namespace ccny_rgbd {

DepthCalibrator::DepthCalibrator(ros::NodeHandle nh, ros::NodeHandle nh_private):
  nh_(nh), nh_private_(nh_private)
{  
  std::string home_path = getenv("HOME");
  path_ = home_path + "/ros/ccny-ros-pkg/ccny_rgbd_data/images/ext_calib_01/";
  
  // parameters
  square_size_ = 100.0;
  n_cols_ = 9;
  n_rows_ = 6;
  
  patternsize_ = cv::Size(n_cols_, n_rows_);
  
  // input   
  calib_rgb_filename_  = path_ + "rgb.yml";
  calib_ir_filename_   = path_ + "depth.yml";
  calib_extr_filename_ = path_ + "extr.yml";
  
  //output
  cloud_filename1_ = path_ + "cloud_1.pcd";
  cloud_filename2_ = path_ + "cloud_2.pcd";
  
  calibrate();
}

DepthCalibrator::~DepthCalibrator()
{
 
}

void DepthCalibrator::build3dCornerVector()
{
  // fill in 3d points
  ROS_INFO("Creating vector of 3D corners...");

  for(int j = 0; j < n_rows_; ++j) // the order matters here
  for(int i = 0; i < n_cols_; ++i)
  {
    float x = i * square_size_;
    float y = j * square_size_; 
    corners_3d_.push_back(cv::Point3f(x, y, 0.0));
  }
}

bool DepthCalibrator::loadCalibrationImagePair(
  int idx,
  cv::Mat& rgb_img,
  cv::Mat& depth_img)
{
  // construct the filename image index
  std::stringstream ss_filename;
  ss_filename << std::setw(4) << std::setfill('0') << idx << ".png";

  // construct the rgb and ir file paths
  std::string rgb_filename   = path_ + "train_depth/rgb/"   + ss_filename.str();
  std::string depth_filename = path_ + "train_depth/depth/" + ss_filename.str();

  ROS_INFO("Trying images \n\t%s\n\t%s", rgb_filename.c_str(), depth_filename.c_str());
  
  // check that both exist
  if (!boost::filesystem::exists(rgb_filename))
  {
    return false;
  }
  if (!boost::filesystem::exists(depth_filename))
  {
    ROS_INFO("%s does not exist", depth_filename.c_str());
    return false;
  }
  
  // load the images 
  rgb_img   = cv::imread(rgb_filename);
  depth_img = cv::imread(depth_filename, -1);

  return true;
}

bool DepthCalibrator::loadCameraParams()
{
  // check that both exist
  if (!boost::filesystem::exists(calib_rgb_filename_))
  {
    ROS_ERROR("Could not open %s", calib_rgb_filename_.c_str());
    return false;
  }
  if (!boost::filesystem::exists(calib_ir_filename_))
  {
    ROS_ERROR("Could not open %s", calib_ir_filename_.c_str());
    return false;
  }
  if (!boost::filesystem::exists(calib_extr_filename_))
  {
    ROS_ERROR("Could not open %s", calib_extr_filename_.c_str());
    return false;
  }

  // **** load intrinsics and distortion coefficients
  ROS_INFO("Reading camera info...");
  cv::FileStorage fs_rgb (calib_rgb_filename_,  cv::FileStorage::READ);
  cv::FileStorage fs_ir  (calib_ir_filename_,   cv::FileStorage::READ);
  cv::FileStorage fs_extr(calib_extr_filename_, cv::FileStorage::READ);
    
  fs_rgb["camera_matrix"]           >> intr_rgb_;
  fs_rgb["distortion_coefficients"] >> dist_rgb_;
  fs_ir ["camera_matrix"]           >> intr_ir_;
  fs_ir ["distortion_coefficients"] >> dist_ir_;
  
  fs_extr["rgb2ir"] >> rgb2ir_;
  
  return true;
}

void DepthCalibrator::buildRectMaps()
{
  // determine new matrix
  cv::Size size_rgb(640, 480);
  cv::Size size_ir (640, 480);
  double alpha = 0.0;
  
  cv::Size size_rgb_rect(640, 480);
  cv::Size size_ir_rect (640, 480);
  
  intr_rect_rgb_ = getOptimalNewCameraMatrix(
    intr_rgb_, dist_rgb_, size_rgb, alpha, size_rgb_rect);

  intr_rect_ir_ = getOptimalNewCameraMatrix(
    intr_ir_, dist_ir_, size_ir, alpha, size_ir_rect);
     
  // determine undistortion maps
  initUndistortRectifyMap(
    intr_rgb_, dist_rgb_, cv::Mat(), intr_rect_rgb_, 
    size_rgb_rect, CV_16SC2, map_rgb_1_, map_rgb_2_);
  
  initUndistortRectifyMap(
    intr_ir_, dist_ir_, cv::Mat(), intr_rect_ir_, 
    size_ir_rect, CV_16SC2, map_ir_1_, map_ir_2_);
}

void DepthCalibrator::calibrate()
{
  if (!loadCameraParams()) return;
  buildRectMaps();
  build3dCornerVector();
  
  // read in calibration images
  int img_idx = 1;
  cv::Mat rgb_img, depth_img;
  bool load_result = loadCalibrationImagePair(img_idx, rgb_img, depth_img);  
  if (!load_result)
  {
    ROS_INFO("Images not found - reached end of image sequence.");
    // break
    return;
  }
  
  // rectify
  cv::Mat rgb_img_rect, depth_img_rect; // rectified images 
  cv::remap(rgb_img,   rgb_img_rect,   map_rgb_1_, map_rgb_2_, cv::INTER_LINEAR);
  cv::remap(depth_img, depth_img_rect, map_ir_1_,  map_ir_2_,  cv::INTER_NEAREST);

  // detect corners
  ROS_INFO("Detecting 2D corners...");
  std::vector<cv::Point2f> corners_2d_rgb;

  bool corner_result_rgb = getCorners(rgb_img_rect, patternsize_, corners_2d_rgb);

  if (!corner_result_rgb)
  {
    ROS_WARN("Corner detection failed. Skipping image pair %d.", img_idx);
    return;
    //img_idx++;
    //continue;
  }
  
  // show images with corners
  showCornersImage(rgb_img_rect, patternsize_, corners_2d_rgb, 
                   corner_result_rgb, "RGB Corners");
  cv::waitKey(0);
  
  // get pose from RGB camera to checkerboard
  cv::Mat rvec, tvec;
  bool pnp_result = solvePnP(corners_3d_, corners_2d_rgb, 
    intr_rect_rgb_, cv::Mat(), rvec, tvec);

  std::cout << "rvec" << std::endl << rvec << std::endl; 
  std::cout << "tvec" << std::endl << tvec << std::endl;

  cv::Mat rgb_to_board;
  matrixFromRvecTvec(rvec, tvec, rgb_to_board);
  
  // **** reproject
  cv::Mat depth_img_rect_reg;
  
  buildRegisteredDepthImage(
    intr_rect_ir_, intr_rect_rgb_, rgb2ir_,
    depth_img_rect, depth_img_rect_reg);
  
  // **** visualize 
  //showBlendedImage(depth_img_rect,     rgb_img_rect, "Unregistered");
  //showBlendedImage(depth_img_rect_reg, rgb_img_rect, "Registered");  
  //cv::waitKey(0);
    
  // **** build point cloud 
  PointCloudT cloud;
  buildPointCloud(depth_img_rect_reg, rgb_img_rect, intr_rect_rgb_, cloud);
  pcl::io::savePCDFileBinary<PointT>(cloud_filename1_, cloud);
  
  // **** build detected checkerboard cloud
  PointCloudT d_cloud;
 
  for (unsigned int cn_idx = 0; cn_idx < corners_3d_.size(); ++cn_idx)
  {
    const cv::Point3f& corner = corners_3d_[cn_idx];
    cv::Mat corner_4d = cv::Mat(4,1, CV_64FC1);
    corner_4d.at<double>(0,0) = corner.x;
    corner_4d.at<double>(1,0) = corner.y;
    corner_4d.at<double>(2,0) = corner.z;
    corner_4d.at<double>(3,0) = 1.0;  
    
    cv::Mat corner_tf = rgb_to_board * corner_4d;
    
    PointT pt;
    pt.x = corner_tf.at<double>(0,0); 
    pt.y = corner_tf.at<double>(1,0);
    pt.z = corner_tf.at<double>(2,0);
                  
    d_cloud.push_back(pt);
  }
  
  pcl::io::savePCDFileBinary<PointT>(cloud_filename2_, d_cloud);
}

void DepthCalibrator::testPlaneDetection(
  const cv::Mat& rgb_img_rect,
  const cv::Mat& depth_img_rect,
  const cv::Mat& rvec,
  const cv::Mat& tvec)
{

}

} //namespace ccny_rgbd