 /* \copyright This work was completed by Robert Leishman while performing official duties as 
  * a federal government employee with the Air Force Research Laboratory and is therefore in the 
  * public domain (see 17 USC § 105). Public domain software can be used by anyone for any purpose,
  * and cannot be released under a copyright license
  */

/*! \file constants.h
  * \author Robert Leishman
  * \date June 2012
  *
  * \brief The constants.h file is the header for the Constants class, which contains all the switches and constants.
  * It also contains the typedefs for the messages
  *
*/

#ifndef CONSTANTS_H
#define CONSTANTS_H




#include <math.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <geometry_msgs/TransformStamped.h>
#include <sensor_msgs/Imu.h>
#include <kinect_vo/kinect_vo_message.h>
#include "mikro_serial/mikoImu.h"
#include "rel_estimator/vodata.h"
#include "sensor_msgs/Imu.h"
#include "evart_bridge/transform_plus.h"
#include <visualization_msgs/Marker.h>


//
/// Decide whether or not to include the calibration parameters in the state, if not, the defaults below will be used
#define STATE_LENGTH 15 /// (22 or 15) \note Use this to switch between estimating the calibration parameters or not
#define COVAR_LENGTH 14 /// (20 or 14) \note This should be switched with the above: 20 = yes calib, 14 = no calib

/*!
 *  \typedef mikro_serial::mikoIMU is replaced with Hex_Message
 *  It is located here so that it is easily used throughout the project, as all these constants are
*/
typedef mikro_serial::mikoImu Hex_message;

/*!
 *  \typedef sensor_msgs::IMU is changed to IMU_message for convienience
*/
typedef sensor_msgs::Imu IMU_message;

///*!
// *  \typedef sensor_msgs/IMU is redefined as IMU_message
// *  This is assuming that the IMU data is from Caleb's UM6 IMU
//*/
//typedef sensor_msgs::Imu IMU_message;


/*!
 *  \typedef kinect_vo::kinect_visual_odometry is replaced with k_message
*/
typedef kinect_vo::kinect_vo_message k_message;


/// \typedef VOData is replaced with VO_message
typedef VOData VO_message;


/*!
 *  \typedef A switch for the Truth Data:
*/
typedef evart_bridge::transform_plus TRUTH_message;



/*!
 * \def LASER
 * Define this for using the laser
*/
//#define LASER

/*!
 * \def DETECT
 * Define this for using the fault detection stuff(only if LASER is defined)
*/
//#define DETECT

#ifdef LASER
#ifdef DETECT
/*!
 *  \typedef visualization_msgs::Marker is replaced with Status_message
*/
typedef visualization_msgs::Marker Status_message;
#endif
#endif


/*!
 *  \class Constants constants.h "include/rel_estimator/constants.h"
 *  \brief The constants class provides many constants to the whole project.  These constants are declared here and
 *  defined in the .cpp file.  These constants are used in many different places in the code, so placing them in one
 *  class enables changing the value one time and not all over the code.
*/
class Constants
{
public:

  const static double pi = 3.1415926535897932384626;

#ifndef LASER // THE ALTIMETER IS BEING USED
  // ESTIMATOR CONSTANTS:
  const static double acc_z_switch = -8.8; // -9.7;/// -8.90665;    //!< Once acc_z drops below this level, the estimator is turned on
  const static double acc_y_inflate = 15.0;//8.0;// 15.0;//   //!< Use this to inflate the R for acc_y, to smooth estimates
  const static double acc_x_inflate = 15.0;// 15.0;//   //!< for smoothing, inflate the R for x accelerometer
  const static double alt_inflate = 1.5; //!< for smoothing the altimeter measurements
  const static double camera_x_inflate = 1.0; //1.255;////!< inflate the camera x portion of R
  const static double camera_qx_inflate = 5000.0;//10.0; //!< and for camera qx
  const static double gamma = 1.0;//0.8; //!< Constant for tuning filter - between 0 and 1, used for scaling the input  //0.5;
                  //!< covariance B*G*B' in prediction, - decreasing the weight (trusting more) the inputs in prediction
//  const static double phi_bias = 0.0; /// -2.1 * 3.14159265358 / 180.0;    //!< There is a bias in the hardware platform in phi
//  const static double Q_w = 0.00250; //!< for setting the Q (process noise) for w (down velocity)
  const static double lambda = 100.d; //!< postitive gain used in the prediction step for the quaterion, to keep the magnitude = 1 //10.d; ///

  // Hardware parameters:
  const static double mass = 3.65;//4.05;//3.95; //!< mass of aircraft (kg); //2.66
  const static double lam1x = 0.0015;//0.0015;// 0.0036;//0.0036;//0.0025; //0.002; //!< body x-axis coefficient for force/(forward velocity*angular velocity of motor)
  const static double kF = 0.00033; //!< coefficient for thrust/(angular velocity motor)^2  0.000175;
  const static double g = 9.80665; //!< Gravity (m/s^2)
  const static double yaw_camera_body = 0.d; //!< the yaw angle difference between the body-fixed coord. frame and the camera coord frame

  // Calibration Constants: the static transformation from the camera coordinate frame to the body-fixed coordinate frame.
  Eigen::Quaterniond q_camera_to_body; //!< Rotation portion of the calibration (initialized with the below parameters)
  const static double qx = -0.558;//-0.5;
  const static double qy = -0.5239;//-0.5;
  const static double qz = -0.4736;//-0.5;
  const static double qw = 0.435;//0.5;
  Eigen::Vector3d T_camera_to_body; //!< the translation portion of the calibration (initialized with the below paramters)
  const static double cx =  0.1346;// 0.091;////!< body x axis coordinate of the left camera focal point
  const static double cy =  -0.0417;// 0.002;// //!< body y axis coordinate
  const static double cz =  0.08898;// 0.1161;// //!< body z axis

  // Noise Parameters:
  const static double accelx_bias = 0.23; //!< initial bias in body x accelerometer
  const static double accely_bias = -0.015; //!< bias for body y accel
  const static double accel_x_std = 0.32; //!< x accelerometer standard deviation
  const static double accel_y_std = 0.30; //!< y accel std. dev.
  //const double accel_z_std = .33; //1.0;//  //z accel variance
  /// The following values were estimated from data
  /// taken from the hexakopter while it was hovering in the air
  const static double gyrox_bias = 0.0005;//0.00000305;  //!< initial gyro bias
  const static double gyroy_bias = 8.82e-5;//0.0005;//0.00000305;
  const static double gyroz_bias = 0.00000305;
  const static double gyrox_std = 0.030;   //!< measured body x gyro noise standard deviation
  const static double gyroy_std = 0.033;
  const static double gyroz_std = 0.030;


  //General Estimator Constants:
  // Number of "babysteps" for Prediction step: - not really used with quaternions...
  const static int normal_steps = 1;     //!< number of steps used when doing normal prediction (not processing delayed updates)
  const static int catchup_steps = 1;     //!< number of steps used when doing the repropogation (processing delayed updates)

  /// Constants for Initializing P, the covariance
  const static double P_5mm = 0.000025;     // 0.000025 represents 5mm of uncertainty
  const static double P_05ms = 0.0025;       // 0.0025 represents 0.05 m/s of uncertainty
  const static double P_1deg = 0.0003;       // 0.0003 represents about 1 degree of uncertainty
  const static double P_001 = 0.000001;     // 0.000001 represents a std of 100% of the initialized value, 0.001;
  const static double P_1 = 0.01;      // .1*.1 = .01

  /// Altitude Noise Characteristics
  const static double alt_std = 0.03;  //!< st. dev. for the altimeter
  const static double alt_w_std = 0.20; //!< st. dev. for the altimeter w measurement

  /// Laser Specific Constants
  const static double delta_x_las = 0.315; //!< x offset of laser from frontmost Cortex dot (abs. val.)
  const static double delta_z_las = 0.1500; //!< z offset of laser from frontmost Cortex dot (abs. val.)
  const static double las_bias = 0.0950; //!< bias in the laser range measurements
  const static double failed_return_distance = 4.0; //!< distance returned by laser for a failed return (needs verification)
  const static double prob_false_allow = 0.05; //!< Allowed missed detection rate for faults in the laser rangefinder
  const static int window_size = 5; //!< number of measurements to consider when performing fault detection for laser

//  /// Quaternion Noise
//  const static double quat_std = 0.001; //!< st. dev. for the quaternion psuedo-measurement

//  /// Camera Noise Characteristics:
//  const static double cam_x_std = 0.033;   //!< camera x std. dev.
//  const static double cam_y_std = 0.081;
//  const static double cam_z_std = 0.101;
//  const static double cam_psi_std = 0.017;

//  /// St. Dev. terms for the static transformation between the camera and the IMU
//  const static double trans_x_std = 0.01;
//  const static double trans_y_std = 0.01;
//  const static double trans_z_std = 0.01;

#else  // THE LASER IS BEING USED
  // ESTIMATOR CONSTANTS:
  const static double acc_z_switch = -8.8; // -9.7;/// -8.90665;    //!< Once acc_z drops below this level, the estimator is turned on
  const static double acc_y_inflate = 8.0;// 15.0;//   //!< Use this to inflate the R for acc_y, to smooth estimates
  const static double acc_x_inflate = 10.0;// 15.0;//   //!< for smoothing, inflate the R for x accelerometer
  const static double alt_inflate = 2.5; //!< for smoothing the altimeter measurements
  const static double camera_x_inflate = 1.0; //1.255;////!< inflate the camera x portion of R
  const static double camera_qx_inflate = 1000.0;//10.0; //!< and for camera qx
  const static double gamma = 1.0;//0.8; //!< Constant for tuning filter - between 0 and 1, used for scaling the input  //0.5;
                  //!< covariance B*G*B' in prediction, - decreasing the weight (trusting more) the inputs in prediction
//  const static double phi_bias = 0.0; /// -2.1 * 3.14159265358 / 180.0;    //!< There is a bias in the hardware platform in phi
//  const static double Q_w = 0.00250; //!< for setting the Q (process noise) for w (down velocity)
  const static double lambda = 100.d; //!< postitive gain used in the prediction step for the quaterion, to keep the magnitude = 1 //10.d; ///

  // Hardware parameters:
  const static double mass = 4.05;//3.95; //!< mass of aircraft (kg); //2.66
  const static double lam1x = 0.0015;//0.0015;// 0.0036;//0.0036;//0.0025; //0.002; //!< body x-axis coefficient for force/(forward velocity*angular velocity of motor)
  const static double kF = 0.00033; //!< coefficient for thrust/(angular velocity motor)^2  0.000175;
  const static double g = 9.80665; //!< Gravity (m/s^2)
  const static double yaw_camera_body = 0.d; //!< the yaw angle difference between the body-fixed coord. frame and the camera coord frame

  // Calibration Constants: the static transformation from the camera coordinate frame to the body-fixed coordinate frame.
  Eigen::Quaterniond q_camera_to_body; //!< Rotation portion of the calibration (initialized with the below parameters)
  const static double qx = -0.5;
  const static double qy = -0.5;
  const static double qz = -0.5;
  const static double qw = 0.5;
  Eigen::Vector3d T_camera_to_body; //!< the translation portion of the calibration (initialized with the below paramters)
  const static double cx =  0.114;// 0.091;////!< body x axis coordinate of the left camera focal point
  const static double cy =  -0.019;// 0.002;// //!< body y axis coordinate
  const static double cz =  0.089;// 0.1161;// //!< body z axis

  // Noise Parameters:
  const static double accelx_bias = 0.23; //!< initial bias in body x accelerometer
  const static double accely_bias = -0.015; //!< bias for body y accel
  const static double accel_x_std = 0.32; //!< x accelerometer standard deviation
  const static double accel_y_std = 0.30; //!< y accel std. dev.
  //const double accel_z_std = .33; //1.0;//  //z accel variance
  /// The following values were estimated from data
  /// taken from the hexakopter while it was hovering in the air
  const static double gyrox_bias = 0.0005;//0.00000305;  //!< initial gyro bias
  const static double gyroy_bias = 8.82e-5;//0.0005;//0.00000305;
  const static double gyroz_bias = 0.00000305;
  const static double gyrox_std = 0.030;   //!< measured body x gyro noise standard deviation
  const static double gyroy_std = 0.033;
  const static double gyroz_std = 0.030;


  //General Estimator Constants:
  // Number of "babysteps" for Prediction step: - not really used with quaternions...
  const static int normal_steps = 1;     //!< number of steps used when doing normal prediction (not processing delayed updates)
  const static int catchup_steps = 1;     //!< number of steps used when doing the repropogation (processing delayed updates)

  /// Constants for Initializing P, the covariance
  const static double P_5mm = 0.000025;     // 0.000025 represents 5mm of uncertainty
  const static double P_05ms = 0.0025;       // 0.0025 represents 0.05 m/s of uncertainty
  const static double P_1deg = 0.0003;       // 0.0003 represents about 1 degree of uncertainty
  const static double P_001 = 0.000001;     // 0.000001 represents a std of 100% of the initialized value, 0.001;
  const static double P_1 = 0.01;      // .1*.1 = .01

  /// Altitude Noise Characteristics
  const static double alt_std = 0.04;//!< st. dev. for the altimeter
  const static double alt_w_std = 0.20; //!< st. dev. for the altimeter w measurement

  /// Laser Specific Constants
  const static double delta_x_las = 0.20;//0.315; //!< x offset of laser from frontmost Cortex dot (abs. val.)
  const static double delta_z_las = 0.075;//0.1500; //!< z offset of laser from frontmost Cortex dot (abs. val.)
  const static double las_bias = 0.029;//0.0950; //!< bias in the laser range measurements
  const static double failed_return_distance = 4.0; //!< distance returned by laser for a failed return (needs verification)
  const static double prob_false_allow = 0.05; //!< Allowed missed detection rate for faults in the laser rangefinder
  const static int window_size = 8; //!< number of measurements to consider when performing fault detection for laser
  const static double threshold_covariance_inflate = 0.115;
  const static double threshold_mean_inflate = 1.20;

#endif

  Constants();

//  Eigen::Matrix3d cameraToBodyFixedRotation(double yaw);

};

#endif // CONSTANTS_H
