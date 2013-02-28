#ifndef CCNY_RGBD_CALIBRATE_AVG_LOGGER_H
#define CCNY_RGBD_CALIBRATE_AVG_LOGGER_H

#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <boost/filesystem.hpp>

#include <ccny_rgbd/structures/rgbd_frame.h>
#include <ccny_rgbd/types.h>

namespace ccny_rgbd
{

class AvgLogger 
{
  public:

    AvgLogger(ros::NodeHandle nh, ros::NodeHandle nh_private);
    virtual ~AvgLogger();

    void RGBDCallback(const ImageMsg::ConstPtr& depth_msg,
                      const ImageMsg::ConstPtr& rgb_msg,
                      const CameraInfoMsg::ConstPtr& info_msg);  

  private:

    ros::NodeHandle nh_;
    ros::NodeHandle nh_private_;

    boost::shared_ptr<RGBDSynchronizer3> sync_;
       
    ImageSubFilter      sub_depth_;
    ImageSubFilter      sub_rgb_;
    CameraInfoSubFilter sub_info_;

    std::stringstream ss_rgb_path_;
    std::stringstream ss_depth_path_;
    std::stringstream ss_stdev_path_;

    boost::thread input_thread_;
    
    // state variables
    int count_;
    bool logging_;
    bool rgb_saved_;
    
    cv::Mat c_img_;   // uint16t
    cv::Mat m_img_;   // double
    cv::Mat s_img_;   // double
        
    // parameters   
    int n_depth_;
    int id_;   
    int n_cols_;
    int n_rows_;
    
    std::string sequence_;

    void prepareDirectories();
    
    void keyboardThread();
    
    uint16_t getMean(int v, int u);
    uint16_t getStDev(int v, int u);
};

} // namespace ccny_rgbd

#endif // CCNY_RGBD_CALIBRATE_AVG_LOGGER_H