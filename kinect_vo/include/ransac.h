
 /* \copyright This work was completed by Robert Leishman while performing official duties as 
  * a federal government employee with the Air Force Research Laboratory and is therefore in the 
  * public domain (see 17 USC § 105). Public domain software can be used by anyone for any purpose,
  * and cannot be released under a copyright license
  */
/*!
 *  \package kinect_visual_odometry
 *  \file ransac.h
 *  \author Robert Leishman
 *  \date March 2012
 *
 *  \brief Provides the header for the RANSAC class.
 *
*/

#ifndef RANSAC_H
#define RANSAC_H

#include <sensor_msgs/CameraInfo.h>
#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <math.h>
#include <complex>
#include <stdlib.h>
#include <stdint.h>
#include <QList>
#include <QSet>
#include <QHash>
#include <ros/assert.h>
#include <iostream>



/*!
 *  \class RANSAC ransac.h "include\ransac.h"
 *  \brief The RANSAC class provides the random sample consensus algorithm for the visual odometry.
 *
 *  It is a simple class, based on the RANSAC described by <b>Multiple View Geometry</b> by Hartley and Zisserman 2nd Ed. (pg 118).
 *  It samples from the provided 3D points, constructs a transformation estimate using the method
 *  outlined by K.S. Arun, T.S. Huang and S.D. Blostein in <b>Least-Squares Fitting of Two 3-D Point Sets</b>
 *  (IEEE Trans. on Pattern Analysis and Machine Intelligence Vol. PAMI-9 No. 5 Sept 1987).  The transformation estimate then
 *  is checked for inliers by reprojecting the reference features onto the current image plane and finding distance to
 *  the corresponding current image 2D feature locations.  The estimate with the most inliers over the iterations is selected
*/
class RANSAC
{

public:

  /*!
   *  \brief The constructor for RANSAC
   *
   *  \param iters is the number of iterations for the instance
   *  \param inlier_thres is the distance threshold for a feature to be declared an inlier
   *  \param consensus_thres is a percentage of inliers needed to break the iterations and declare a transformation
   *  \param camera_params are the RGB camera parameters for bringing 3D points onto the image plane
   *  \param optimize_enabled tells RANSAC if the solution will be optimized or not afterward, if so, it reduces some steps
  */
  RANSAC(int iters, int inlier_thres, double consensus_thres, sensor_msgs::CameraInfo &camera_params, bool optimize_enabled);


  /*!
   *  \brief The destructor
   *  \todo document anything that ends up in here...
  */
  ~RANSAC();



  /*!
   *  \brief The method to run RANSAC and return the solution
   *
   *  The solution expresses 3D points in the reference camera coordinate frame and expresses them in the current camera
   *  coordinate frame, i.e. p^c = R p^r + T, where R rotates from the reference frame into the current frame and T is
   *  expressed in the current frame.  The direction is important to remember.
   *  \attention The points coming in the algroithm must matched, i.e. reference3d[1] is matched to current3D[1] and current2D[1]
   *
   *  \param reference3D the reference 3D feature location (ordered by the matches with current2D)
   *  \param current3D the current frame 3D feature locations
   *  \param current2D the current feature locations on the image plane
   *  \param final_rotation is the rotation portion of the transformation estimated by the method and returned.
   *  \param final_translation is the translation portion of the transformation returned by the method.
   *  \param inliers is the number of inliers in the RANSAC solution.  This is returned.
   *  \param inlier_list is a vector of the inlier positions in reference3D, current3D, & current2D
   *  \param solution_list is the vector of indicies for the 3 points from the best solution (for covariance estimation)
   *  \param svd_D,U,V are the matricies from the SVD of the best solution (for covariance estimation)
   */
  void runRANSAC(std::vector<cv::Point3d> &reference3D,
                 std::vector<cv::Point3d> &current3D,
                 std::vector<cv::Point2f> &current2D,
                 cv::Mat *final_rotation,
                 cv::Mat *final_translation,
                 int *inliers,
                 std::vector<int> *inlier_list,
                 std::vector<int> *solution_list,
                 cv::Mat *svd_D, cv::Mat *svd_U, cv::Mat *svd_V);


  /*!
   *  \brief this function finds the average of the points (inpoints3D) and returns the average and the centered (mean is
   *  subtracted) points
   *
   *  \param inpoints3D the points to be centered
   *  \param centered3D the centered points (inpoints  - average)
   *  \param centroid is the centroid of inpoints3D (in matrix form to facilitate further processing)
  */
  void findCentroid(std::vector<cv::Point3d> &inpoints3D,
                    std::vector<cv::Point3d> *centered3D,
                    cv::Matx31d *centroid);


protected:
  //variables 
  int iterations_; //!< the number of iterations to perform
  double inlier_threshold_; //!< is used to determine when a feature is an inlier for the proposed transformation
  double consensus_threshold_; //!< for determining an exit criteria if a sufficient percentage of inliers are found
  cv::Mat rgb_camera_intrinsics; //!< for bringing the reference features onto the image plane (needs to be the RBG camera parameters)
  cv::Mat rgb_distortion; //!< copy the distortion parameters of the camera
  cv::RNG *rnd_gen; //!< randomn number generator for sampling from a uniform distribution
  bool optimizer_enabled_; //!< flag for when optimization is enabled and the solution will be optimized after RANSAC

  //methods
  /*!
   *  \brief computeErrorModel projects the reference 3D points onto the current image frame to find inliers.
   *
   *  \param reference3D are the 3D points from the reference image features
   *  \param current2D are the 2D feature locations from the curreent image
   *  \param rotation is the rotation matrix predicted from the random three points
   *  \param translation is the translation between the random three points.
   *  \param error is the vector containing the reprojection error returned by the method (same length as feature vectors)
   *  \param inliers is the vector containing the location of the inliers (the size of inliers is the number of inliers)
   *         i.e. [1,5,8,17,24] gives 5 inliers at positions 1,5,8,etc. in reference3D and current2D. This is also returned.
   *  \returns The method returns the sum of errors (these are squared errors)
  */
  double computeErrorModel(std::vector<cv::Point3d> &reference3D,
                           std::vector<cv::Point2f> &current2D,
                           cv::Mat &rotation,
                           cv::Mat &translation,
                           std::vector<double> *error,
                           std::vector<int> *inliers);



  /*!
   *  \brief computeSampleTransformation is the method that implements the Least Squares Fitting article cited above
   *
   *  \param reference3D are the 3D points from the reference image.
   *  \param current3D are the 3D points from the current image.
   *  \param rotation is the rotation matrix returned by the method, as part of the transformation.
   *  \param tanslation is the second part of the transformation.
   *  \param D,U,V are the matricies from the SVD (for covariance estimation) (If they are NULL, nothing will be returned)
  */
  void computeSampleTransformation(std::vector<cv::Point3d> &reference3D,
                                   std::vector<cv::Point3d> &current3D,
                                   cv::Mat *rotation,
                                   cv::Mat *translation,
                                   cv::Mat *D = NULL,
                                   cv::Mat *U = NULL,
                                   cv::Mat *V = NULL);



  /*!
   *  Copyright (c) 2011, Laurent Kneip, ETH Zurich
   *  All rights reserved.
   *
   *  \brief computeKneipP3P is a method for computing transformation between camera frames developed by Laurent Kneip
   *
   *  This is an implementation of the algorithm that Laurent Kneip wrote.  Source code is from
   *  http://www.laurentkneip.de/research.html/p3p_code_final.zip and it was published as "A Novel Parametrization of the
   *  P3P-Problem for a Direct Computation of Absolute Camera Position and Orientation" in IEEE Conference on Computer
   *  Vision and Pattern Recognition. Colorado Springs, USA. June 20-25, 2011
   *  I have simply modified it to used OpenCV types
   *
   *  \param reference3D are the 3D points from the reference image.
   *  \param current2D are the 2D points from the current image.
   *  \param rotation is the rotation matrix returned by the method, as part of the transformation.
   *  \param tanslation is the second part of the transformation.
  */
  int computeKneipP3P(std::vector<cv::Point3d> &reference3D,
                      std::vector<cv::Point2d> &current2D,
                      std::vector<cv::Mat> *rotation,
                      std::vector<cv::Mat> *translation);


  /*!
   *  \brief This function is just like computeErrorModel, but it takes in 4 solutions as returned by the P3P algorithm
   *
   *  \note The correct rotation and translation are returned by the algorithm
   *
   *  \returns the total error
  */
  double computeErrorModelP3P(std::vector<cv::Point3d> &reference3D,
                           std::vector<cv::Point2f> &current2D,
                           std::vector<cv::Mat> *rotation,
                           std::vector<cv::Mat> *translation,
                           std::vector<double> *error,
                           std::vector<int> *inliers);


  /*!
   *  \brief solveQuartic solves for roots of 4th order polynomials.  This is a reimplementation of Laurent Kneip's code
   *
   *  \param factors are the input from computeKneipP3P
   *  \param real_roots are the real versions of the roots solved for by the algorithm
  */
  void solveQuartic(cv::Mat_<double> factors, cv::Mat_<double> real_roots);


  /*!
   *  \brief This method samples from the reference and current points and returns vector of 3 points for each
   *
   *  It is called within runRANSAC.
   *
   *  \param reference_points are the 3D points from the reference image
   *  \param current_points are the 3D points from the current image
   *  \param current2D_points are the 2D feature points on the current image
   *  \param reference_sample are the 3 3D points from reference_points returned to be used the RANSAC iteration
   *  \param current_sample are the 3 3D points from current_points returned for use in the RANSAC iteration
   *  \param current2D_sample are the 3 2D feature points from the current image returned (for P3P)
   *  \param list is the list of indicies used for the sample (to pass on for covariance estimation)
  */
  inline void sample(std::vector<cv::Point3d> &reference_points,
              std::vector<cv::Point3d> &current_points,
              std::vector<cv::Point2f> &current2D_points,
              std::vector<cv::Point3d> *reference_sample,
              std::vector<cv::Point3d> *current_sample,
              std::vector<cv::Point2d> *current2D_sample,
              std::vector<int> *list)
  {
    //obtain samples for the indicies
    QList<int> sample_set;
    sample_set = uniformSampler(3, 0, reference_points.size()); // 3 is the minimal model parameterization.

    //make sure samples are empty
    if(!reference_sample->empty())
      reference_sample->clear();

    if(!current_sample->empty())
      current_sample->clear();

    //load the samples in
    for(int i = 0; i< sample_set.count(); i++)
    {
      reference_sample->push_back(reference_points[sample_set[i]]);
      current_sample->push_back(current_points[sample_set[i]]);
      current2D_sample->push_back(current2D_points[sample_set[i]]);
      list->push_back(sample_set[i]);
    }
  }



  /*!
   *  \brief Used by sample to find # indicies for the # samples.
   *
   *  This uses a hash set for speed.  OpenCV's RNG class is used to provide the uniform distribution.
   *
   *  \param number_samples is the number of samples needed (3) is the minimum
   *  \param min is the minimum value to sample from  (0);
   *  \param max is the maximum value to sample from (the length of the 3D features)
   *  \returns Qlist with the indicies of the samples
  */
  inline QList<int> uniformSampler(int number_samples, int min, int max)
  {
    QSet<int> sample;
    int value;
    while(sample.count() < number_samples)
    {
      value = 0;
      value = rnd_gen->uniform(min, max);
      if(!sample.contains(value))
        sample.insert(value);
    }
    return sample.toList();
  }


};

#endif
