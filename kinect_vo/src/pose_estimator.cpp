 /* \copyright This work was completed by Robert Leishman while performing official duties as 
  * a federal government employee with the Air Force Research Laboratory and is therefore in the 
  * public domain (see 17 USC § 105). Public domain software can be used by anyone for any purpose,
  * and cannot be released under a copyright license
  */

/*!
 *  \package kinect_visual_odometry
 *  \file pose_estimator.cpp
 *  \author Robert Leishman
 *  \date March 2012
 *
 *  \brief The details for the methods in pose_estimator.h
*/


#include "pose_estimator.h"
using namespace Eigen;
using namespace std;
//using namespace cv; //Can't use this because of Conflicts with names

//
//constructor
//
PoseEstimator::PoseEstimator(bool optimize, bool display):enable_optimizer_(optimize),enable_display_(display)
{
  //instatiate the image processing elements:
  if(enable_optimizer_)
  {
    num_features_ = 300;
    num_iterations_ = 100;
  }
  else
  {
    num_features_ = 750;
    num_iterations_ = 300;
  }
  feature_detector_ptr_ = cv::FeatureDetector::create("FAST"); //!< I need to place this text on param
  grid_detector_ = new cv::GridAdaptedFeatureDetector(feature_detector_ptr_, num_features_, 8, 6);  //!< need to place on param
  //300, 8, 6 - previous settings for grid_detector

  //cv::DescriptorExtractor::create("BRIEF");  //This only implements 32 bit descriptor, may need 64!
  descriptor_extractor_ = new cv::BriefDescriptorExtractor(64);  //64 is better
  //descriptor_matcher_ = cv::DescriptorMatcher::create("FlannBased");  //!< again, on the parameter server!
  matcher_forward_ = new cv::BFMatcher(cv::NORM_HAMMING);
  matcher_reverse_ = new cv::BFMatcher(cv::NORM_HAMMING);


  reference_set_ = false;

  if(enable_display_)
  {
    MATCHEDWINDOW = "Matched Image";
    cv::namedWindow(MATCHEDWINDOW);
    reference_window_ = "Reference Features";
    current_window_ = "Current Features";
    association_ = new ImageDisplay("Associated Features");
    counter_ = 1000;
  }

  //lsh_matcher_.setDimensions(10, 24, 2);  //! \note The params for LSH Matcher found in example.cpp in RBRIEF project.

  if(enable_optimizer_)
  {
//    //setup the optimizer:
//    optimizer_.setMethod(g2o::SparseOptimizer::LevenbergMarquardt);
//    optimizer_.setVerbose(false);
//    g2o::BlockSolver_6_3::LinearSolverType *lin_solver;
//    lin_solver = new g2o::LinearSolverCholmod<g2o::BlockSolver_6_3::PoseMatrixType>();
//    g2o::BlockSolver_6_3 *solver_ptr = new g2o::BlockSolver_6_3(&optimizer_,lin_solver);
//    optimizer_.setSolver(solver_ptr);
//    point_vertex_offset_ = 200; //This allows 200 pose verticies to be in place before the point verticies start
//    max_poses_ = 20; //Initialize the max number of poses in the optimizer
//    num_pose_verticies_ = 0;
  }

  //Tunable parameters:
  deltaI_.setIdentity();
  deltaI_(3,3) = 87;
  deltaI_(4,4) = 153;
  deltaI_(5,5) = 121;
  deltaI_(6,6) = 100;

  //Set the actual image noise (for the new covariance method):
  image_noise_.setZero();
  image_noise_(0,0) = 4; //pixel variance
  image_noise_(1,1) = 4; //pixel variance
  image_noise_(2,2) = 0.01*0.01; //meter std dev^2

  //Temp stuff for two covariances
  calc_hess_covariance_= false;
  hess_covariance_.setZero();
  
  pose_vertex_id_ = 0; //set to zero
}


//
// Destructor
//
PoseEstimator::~PoseEstimator()
{
  if(enable_display_)
    cv::destroyAllWindows();
  feature_detector_ptr_.delete_obj();
  grid_detector_->~FeatureDetector();
  //descriptor_extractor_.release();
  delete descriptor_extractor_;
  delete grid_detector_;
  delete association_;
}



//
//This method sets the reference information
//
void PoseEstimator::setReferenceView(cv::Mat &visual_image, cv::Mat &depth_image_float, cv::Mat &depth_image_CV8UC1)
{
  cv::Mat gray_image, smooth_gray; //temp images
  //convert the image to gray:
  cv::cvtColor(visual_image, gray_image, CV_RGB2GRAY);
  reference_img_RGB_ = visual_image.clone(); //set the color to the reference color, for later use
  reference_img_gray_ = gray_image.clone(); //set the gray to the reference image, for later use
  reference_img_depth_ = depth_image_float.clone();

//  ImageDisplay temp("Unitary_Depth");
//  temp.displayImage(depth_image_CV8UC1);
//  cv::waitKey(100);

  //Vision processing:
  //cv::ORB orb_detector;
  grid_detector_->detect(gray_image,reference2D_features_,depth_image_CV8UC1);  //the depth image is the mask, only find features w/ valid 3D
  //orb_detector(gray_image,depth_image_CV8UC1,reference2D_features_,reference_descriptors_,true);

  if(reference2D_features_.empty() || (int)reference2D_features_.size() < 200)
  {
    //No features detected - wait
    ROS_INFO_THROTTLE(1, "Reference Image was not set - a sufficient number of features were not detected!");
    return;
  }


  //smooth the image before extracting descriptors:
  cv::Size kernal(9,9);
  cv::GaussianBlur(gray_image, smooth_gray, kernal, 2, 2);
  descriptor_extractor_->compute(smooth_gray,reference2D_features_,reference_descriptors_);

  vector<cv::Point2f> feature_points;  //to convert Keypoints to 2D points  
  cv::KeyPoint::convert(reference2D_features_, feature_points);

  //Using the camera calibration, undistort the 2D feature points before you extract the 3D points
  vector<cv::Point2f> idealized_pts;
  try
  {
    cv::undistortPoints(feature_points, idealized_pts, rgb_camera_matrix_,rgb_camera_distortion_,cv::noArray(), rgb_camera_P_ );
    /*! \note You must place in the new P matrix to get out pixel coordinates, if not, undistortPoints returns normalized
        points that are not useful for everything else that we need to do.
    */
  }
  catch(cv::Exception e)
  {
    ROS_ERROR("OpenCV Exception caught while using undistortPoints: %s",e.what());
    return; //No points detected
  }

  reference2D_idealized_ = idealized_pts;

  //calc 3D points:
  calc3DPoints(depth_image_float, rgb_info_, &feature_points, &idealized_pts, &reference3D_features_);

  reference_set_ = true;
  vector<cv::Mat> temp_descriptors;
  temp_descriptors.push_back(reference_descriptors_);


  if(enable_optimizer_)
  {
//    //If the optimizer already has data, clear it, add new reference info:
//    if(optimizer_.vertices().size() > 0)
//      optimizer_.clear();

//    Vector3d trans(0.d,0.d,0.d);
//    Quaterniond q;
//    q.setIdentity();

//    g2o::SBACam ref_sba(q,trans);
//    ref_sba.setKcam(rgb_info_.K[0],rgb_info_.K[4],rgb_info_.K[2],rgb_info_.K[5], 0.d); //fx,fy,cx,cy

//    g2o::VertexCam *ref_vertex = new g2o::VertexCam();
//    ref_vertex->setId(0);
//    ref_vertex->setEstimate(ref_sba); // ->estimate() = ref_sba;
//    ref_vertex->setFixed(true);

//    optimizer_.addVertex(ref_vertex);
//    pose_vertex_id_ = 0; //set to zero
//    pose_vertex_id_++; //increment
//    num_pose_verticies_ = 1;

//    //add edges between this vertex and the 3D points (and verticies for the 3D points)
//    for(size_t i = 0; i< reference3D_features_.size(); i++)
//    {
//      g2o::VertexPointXYZ *v_p = new g2o::VertexPointXYZ();
//      v_p->setId(i+point_vertex_offset_); //the ID is the position in the vector + the offset
//      v_p->setMarginalized(true);
//      v_p->estimate() = Vector3d(reference3D_features_[i].x,reference3D_features_[i].y,reference3D_features_[i].z);


//      Vector2d pt(reference2D_idealized_[i].x, reference2D_idealized_[i].y);
//      g2o::EdgeProjectP2MC *e = new g2o::EdgeProjectP2MC();
//      e->vertices()[0] = dynamic_cast<g2o::OptimizableGraph::Vertex*>(v_p);
//      e->vertices()[1] = dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer_.vertices().find(0)->second);
//      e->measurement() = pt;
//      e->inverseMeasurement() = -pt;
//      e->information() = Matrix2d::Identity();
//      e->setRobustKernel(true);
//      e->setHuberWidth(0.01);
//      optimizer_.addEdge(e);
//      optimizer_.addVertex(v_p);
//    }
  }
  else
    ROS_WARN_ONCE("Optimizations are NOT enabled: sparse bundle adjustment refining of solution will not occur!");
}



//
//The method that sets the current view information and then proceeds forward with the estimating the pose transformation
//
int PoseEstimator::setCurrentAndFindTransform(cv::Mat &visual_cur_image, cv::Mat &depth_curr_image_float,
                                               cv::Mat &depth_curr_image_CV8UC1, Quaterniond *rotation,
                                               Vector3d *translation,Matrix<double,7,7> *covariance,
                                               int *inliers, int *corresponding, int *total,
                                               bool setAsReference, Quaterniond *rot_opt,
                                               Vector3d *tran_opt, cv::Mat rotation_guess)
{

  ROS_ASSERT(reference_set_); //If the reference has not been set, cannot proceed

  cv::Mat gray_image, smooth_gray; //temp images
  //current image member variables
  vector<cv::KeyPoint> current2D_features; // the current image 2D features
  vector<cv::Point3d> current3D_features; // current image 3D features locations
  cv::Mat current_descriptors;   // the current image descriptors found around the 2D features
  vector<cv::DMatch> final_matches; //the final matches
  vector<cv::DMatch> forward_matches;
  vector<cv::DMatch> reverse_matches;
  //std::vector<std::vector<cv::DMatch> > k_matches; //the top k matches for each descriptor
  //int k = 2; //number of matches to return in the knn match
  //double distance_fraction = 0.85;//0.75; //!< need to be set in param, the fraction for deciding uniqe matches. possible value 0.6

  //convert the image to gray:
  cv::cvtColor(visual_cur_image, gray_image, CV_RGB2GRAY);

  //Vision processing:
  //cv::ORB orb_detect;
  grid_detector_->detect(gray_image,current2D_features,depth_curr_image_CV8UC1);  //the depth image is the mask, only find features w/ valid 3D
  //orb_detect(gray_image,depth_curr_image_CV8UC1,current2D_features,current_descriptors,true);
  //smooth the image before extracting descriptors:
  if(current2D_features.empty())
  {
    ROS_WARN("No features were found on current image!");

    // Check to make sure, if we are supposed to set the current as reference, do so without an estimate of the current
    // transformation. (This enables the VO to continue, even if it breaks, although, the global pose estimates will be
    // bad).
    if(setAsReference)
    {
      /// \todo MAKE SURE THAT THIS setAsReference in this case was set by dropped frames, if it was, do a hard reset of
      /// the reference image.
      reference_set_ = false;
      ROS_WARN("ESTIMATOR: HARD RESET OF THE REFERENCE IMAGE IS OCCURING! No features were found and reference ");
    }

    return 0;
  }

  cv::Size kernal(9,9);
  cv::GaussianBlur(gray_image, smooth_gray, kernal, 2, 2);
  descriptor_extractor_->compute(smooth_gray,current2D_features,current_descriptors);

  //Using the camera calibration, undistort the 2D feature points before you extract the 3D points
  vector<cv::Point2f> feature_points_cur;  //to convert Keypoints to 2D points
  cv::KeyPoint::convert(current2D_features,feature_points_cur);
  vector<cv::Point2f> idealized_curr_pts;
  try
  {
    cv::undistortPoints(feature_points_cur, idealized_curr_pts, rgb_camera_matrix_,rgb_camera_distortion_,cv::noArray(), rgb_camera_P_ );
  }
  catch(cv::Exception e)
  {
    ROS_ERROR("OpenCV Exception caught while using undistortPoints: %s",e.what());
  }

  //calc 3D points:
  calc3DPoints(depth_curr_image_float, rgb_info_, &feature_points_cur ,&idealized_curr_pts, &current3D_features);

  //! \note beginning of transformation estimation
  /*!
   *  \attention This code could be converted to thread-safe code if the feature matcher was "cloned" and a Mutex was
   *  wrapped around the feature and descriptor finder instances.  Then Qfuture could be used in ROSRelay for mulitple threads
  */

  //create a mask for matching (TUNE THESE!!!)
  cv::Mat mask, mask_r;
  int wx = 300;//140; //horizontal element
  int wy = 200;//80; //vertical element

  //Use the predicted rotation to adjust the mask:  (when there's something in the rotation guess)
  if(rotation_guess.at<double>(0,0) >= 0.1 || rotation_guess.at<double>(0,0) >= 0.1 || rotation_guess.at<double>(0,0) >= 0.1)
  {
    double roll,pitch,yaw;
    extractAngles(rotation_guess, &roll, &pitch, &yaw);

    if( fabs(roll) > 0.2)
    {
      wx = 120;
      wy = 120;
    }
    else if( fabs(pitch) > 0.12)
    {
      //wx = 180;
      wy = 180;
    }
    else if( fabs(yaw) > 0.12)
    {
      wx = 250;
      //wy = 60;
    }
  }

  mask = cv::windowedMatchingMask(current2D_features, reference2D_features_, wx, wy);
  mask_r = cv::windowedMatchingMask(reference2D_features_,current2D_features,wx,wy);

  //add descriptors and find matches:
  //descriptor_matcher_.knnMatch(current_descriptors, reference_descriptors_, k_matches, k, mask);
  //lsh_matcher_.knnMatch(current_descriptors,k_matches,k);
  //lsh_matcher_.match(current_descriptors, reference_descriptors_,forward_matches,mask);
  //lsh_matcher_.match(reference_descriptors_,current_descriptors,reverse_matches, mask_r);

  //Match forward and backward - select mutual correspondences
  #pragma omp parallel sections
  {
    #pragma omp section
    matcher_forward_->match(current_descriptors, reference_descriptors_,forward_matches,mask);

    #pragma omp section
    matcher_reverse_->match(reference_descriptors_,current_descriptors,reverse_matches,mask_r);
  }

  //Find mutual matches:
  for(int i = 0; i < (int)forward_matches.size(); i++)
  {
    if(reverse_matches[forward_matches[i].trainIdx].trainIdx == i)
    {
      final_matches.push_back(cv::DMatch(i,forward_matches[i].trainIdx,0.f));
    }
  }

  cv::Mat reference_out, current_out;
  if(enable_display_)
  {
    //cv::drawKeypoints(reference_img_gray_,reference2D_features_,reference_out);
    //cv::drawKeypoints(gray_image,current2D_features,current_out);
    //reference_->displayImage(reference_out);
    //current_->displayImage(current_out);
    //cv::imshow(reference_window_, reference_out);
    //cv::imshow(current_window_, current_out);
    //cv::waitKey(100);
//    string filename = "current_";
//    string num = boost::lexical_cast<string>(counter_);
//    filename.append(num);
//    filename.append(".jpg");
//    bool result = cv::imwrite(filename,current_out);
//    counter_++;
  }

  //If there were not enough matches, toss the frame and try again
  if((int)final_matches.size() <= 3)
  {
    // Check to make sure, if we are supposed to set the current as reference, do so without an estimate of the current
    // transformation. (This enables the VO to continue, even if it breaks, although, the global pose estimates will be
    // bad).
    if(setAsReference)
    {
      //Rewrite the reference material:
      setCurrentAsReference(visual_cur_image,gray_image,depth_curr_image_float,current2D_features,idealized_curr_pts,
                            current3D_features,current_descriptors);

      ROS_WARN("Current image set as reference without a good transformation between the last reference and this image!!");
    }
    //set the number of corresponding matches:
    *corresponding = (int)final_matches.size();
    *total = (int)idealized_curr_pts.size();//(int)reference2D_idealized_.size();
    return 0;
  }


  //Make vectors containing the ordered pairs:
  vector<cv::Point3d> ordered_reference3D;
  vector<cv::Point2f> ordered_reference2D;
  vector<cv::Point3d> ordered_current3D;
  vector<cv::Point2f> ordered_current2D;

  /*
  //KNN: Check uniqueness of the matches and extract the final list of matches:
  for (unsigned int i = 0; i < k_matches.size(); i++)
 {
    if(k_matches[i].size() > 0)
    {
      cv::DMatch m1 = k_matches[i][0];  //closested neighbor
      cv::DMatch m2 = k_matches[i][1];  //second closest
      if (m1.distance < distance_fraction * m2.distance ||(m1.queryIdx == m2.queryIdx && m1.trainIdx == m2.trainIdx))
      {
          final_matches.push_back(m1);
         //order the vectors at the same time:
          ordered_reference3D.push_back(reference3D_features_[m1.trainIdx]);
          ordered_reference2D.push_back(reference2D_idealized_[m1.trainIdx]);
          ordered_current3D.push_back(current3D_features[m1.queryIdx]);
          ordered_current2D.push_back(idealized_curr_pts[m1.queryIdx]);
      }
    }
  }
  */

  for(unsigned int i = 0; i < final_matches.size(); i++)    
  {
    cv::DMatch m1 = final_matches[i];
    ordered_reference3D.push_back(reference3D_features_[m1.trainIdx]);
    ordered_reference2D.push_back(reference2D_idealized_[m1.trainIdx]);
    ordered_current3D.push_back(current3D_features[m1.queryIdx]);
    ordered_current2D.push_back(idealized_curr_pts[m1.queryIdx]);   
  }

  //set the number of corresponding matches:
  *corresponding = (int)final_matches.size();
  *total = (int)idealized_curr_pts.size(); //int)reference2D_idealized_.size();
  //ROS_INFO_THROTTLE(1,"Corresponding matches = %d out of %d features.", (int)final_matches.size(),(int)reference2D_idealized_.size());

  int iterations;
  int inlier_error;

//  if(enable_optimizer_)
//  {
//    //open things up a bit if will optimize afterwards
//    iterations = 150;//100;
    inlier_error = 20;//18;
//  }
//  else
//  {
//    //if no optimization after, close it up a little to improve accuracy
//    iterations = 100;
//    inlier_error = 18;//9;
//  }



//  struct timeval  tim;
//  gettimeofday(&tim,NULL);
//  double t1 = tim.tv_sec+(tim.tv_usec/1000000.0);


  //Compute the transformation using RANSAC:
  RANSAC *ransac = new RANSAC(num_iterations_, inlier_error, 0.95, rgb_info_,enable_optimizer_);

  vector<int> inlier_list;
  cv::Mat rotation_matrix, translation_matrix;
  rotation_matrix = cv::Mat::zeros(3,3,CV_64FC1);
  translation_matrix = cv::Mat::zeros(3,1,CV_64FC1);    
  vector<int> solution_list;
  cv::Mat svd_D, svd_U, svd_V;


  //******************************************************************
  /// Run RANSAC on the ordered features:
  ransac->runRANSAC(ordered_reference3D, ordered_current3D, ordered_current2D,
                    &rotation_matrix, &translation_matrix,inliers, &inlier_list,&solution_list,&svd_D,&svd_U,&svd_V);

//  gettimeofday(&tim,NULL);
//  double t2=tim.tv_sec+(tim.tv_usec/1000000.0);
//  printf("%.10lf seconds elapsed\n", t2-t1);

  Quaterniond rot_guess;

  if(rotation_matrix.size() != cv::Size(3,3))
  {
    // Check to make sure, if we are supposed to set the current as reference, do so without an estimate of the current
    // transformation. (This enables the VO to continue, even if it breaks, although, the global pose estimates will be
    // bad).
    if(setAsReference)
    {
      //Rewrite the reference material:
      setCurrentAsReference(visual_cur_image,gray_image,depth_curr_image_float,current2D_features,idealized_curr_pts,
                            current3D_features,current_descriptors);

      ROS_WARN("Current image set as reference without a good transformation between the last reference and this image!!");
    }
    //set the number of corresponding matches:
    *corresponding = (int)final_matches.size();
    *total = (int)reference2D_idealized_.size();

    return 0;    
  }

  convertRToQuaternion(rotation_matrix, &rot_guess);
  //rot_guess.normalize();

  Vector3d trans_guess(translation_matrix.at<double>(0,0),
                       translation_matrix.at<double>(1,0),
                       translation_matrix.at<double>(2,0));

  if(enable_display_)
  {
    // Display & save correspondence image
    cv::Mat output;
    cv::addWeighted(visual_cur_image,0.5,reference_img_RGB_,0.5,0,output, -1);
    drawFeatureAssociations(ordered_reference2D,ordered_current2D,inlier_list,output);
    association_->displayImage(output);
//    string filename = "feature_association_";
//    string num = boost::lexical_cast<string>(pose_vertex_id_);
//    filename.append(num);
//    filename.append(".jpg");
//    cv::imwrite(filename,output);
    pose_vertex_id_++; //increment
  }


  if(enable_optimizer_)
  {
//    //Eliminate a vertex to make room for another image if needed:
//    if(num_pose_verticies_ == max_poses_)
//    {
//      int elim_id = pose_vertex_id_ - (max_poses_ - 1);
//      if(elim_id <= 0)
//      {
//        //because we are wrapping the id (because if we don't, we run into the point id numbers), we need to do this:
//        elim_id = pose_vertex_id_; //we want to eliminate the node at the spot we are going to take
//      }
//      optimizer_.removeVertex(dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer_.vertices().find(elim_id)->second));
//      /*! \note When you remove a vertex, g2o automatically removes all the edges associated with that vertex (I found
//      this out after digging through source, it's not in the documentation)
//      */
//      num_pose_verticies_--;

//      //wrap the vertex id, so that it stays between 0 & max_poses-1 (because the id is zero-based)
//      if(pose_vertex_id_ > max_poses_ - 1)
//        pose_vertex_id_ = 1;  //zero is reserved for the reference
//    }

//    g2o::SBACam pose(rot_guess,trans_guess);
//    pose.setKcam(rgb_info_.K[0],rgb_info_.K[4],rgb_info_.K[2],rgb_info_.K[5],0.d); //fx,fy,cx,cy

//    g2o::VertexCam *v_cam = new g2o::VertexCam();
//    v_cam->setId(pose_vertex_id_);

//    v_cam->setEstimate(pose);//>estimate() = pose;

//    optimizer_.addVertex(v_cam);
//    num_pose_verticies_++;

//    //add edges between this pose and the reference 3D point verticies based on the inliers:
//    for(size_t i = 0; i <  inlier_list.size(); i++) //this line only admits the inliers to the optimizer
//    //for(size_t i = 0; i< final_matches.size(); i++) //this places all the correspondence in the optimizer
//    {
//      g2o::EdgeProjectP2MC *e = new g2o::EdgeProjectP2MC();

//      //use the unorderedmap to recover the original id from the inlier vector, add the offset, and use that id to find the vertex:
//      //e->vertices()[0] = dynamic_cast<g2o::OptimizableGraph::Vertex*>
//      //    (optimizer_.vertices().find(ordered_to_trueid.find(inlier_list[i])->second + point_vertex_offset_)->second);  //w/ inliers
//      e->vertices()[0] = dynamic_cast<g2o::OptimizableGraph::Vertex*>
//          (optimizer_.vertices().find(final_matches[i].trainIdx + point_vertex_offset_)->second); //w/ correspondence
//      e->vertices()[1] = dynamic_cast<g2o::OptimizableGraph::Vertex*>(v_cam);

//      //Vector2d pt(ordered_current2D[i].x, ordered_current2D[i].y); //for inliers
//      Vector2d pt(idealized_curr_pts[i].x,idealized_curr_pts[i].y); //for Correspondence

//      //measurement is the image location of the feature:
//      e->measurement() = pt;
//      e->inverseMeasurement() = -pt;
//      e->information() = Matrix2d::Identity();
//      e->setRobustKernel(true);
//      e->setHuberWidth(0.01);
//      optimizer_.addEdge(e);
//    }

//    optimizer_.initializeOptimization();
//    optimizer_.setVerbose(false); //Set to true for Optimizer info:

//    optimizer_.optimize(4,false); //

//    //extract the most recent camera pose, return it as the solution:
//    g2o::HyperGraph::VertexIDMap::iterator v_it = optimizer_.vertices().find(pose_vertex_id_);
//    g2o::SBACam new_pose;

//    g2o::VertexCam *v_c = dynamic_cast<g2o::VertexCam *>(v_it->second);
//    new_pose = v_c->estimate();

//    *rot_opt = new_pose.rotation();
//    *tran_opt = new_pose.translation();

//    //copy over the guess to the rotation and tranlation:
//    *rotation = rot_guess;
//    *translation = trans_guess;

//    //increment the pose up one
//    pose_vertex_id_++;
  }
  else
  {
    //copy over the guess to the rotation and tranlation:
    *rotation = rot_guess;
    *translation = trans_guess;
    rot_opt->setIdentity();
    tran_opt->setZero();
  }

//  std::vector<cv::Point2f> reference_image_pts;
//  std::vector<cv::Point2f> current_image_pts;
//  std::vector<cv::Point3d> reference_3D_pts;
//  std::vector<cv::Point3d> current_3D_pts;
//  std::vector<cv::Point3d> reference_cent_3D_pts;
//  std::vector<cv::Point3d> current_cent_3D_pts;
//  cv::Matx31d reference_centroid;
//  cv::Matx31d current_centroid_pt;

//  // Set the class variables need to calculate the covariance:
//  //pull out the solution points from the solution_list
//  for(int i = 0; i < (int)solution_list.size(); i++)
//  {
//    reference_3D_pts.push_back(ordered_reference3D[solution_list[i]]);
//    current_3D_pts.push_back(ordered_current3D[solution_list[i]]);
//    reference_image_pts.push_back(ordered_reference2D[solution_list[i]]);
//    current_image_pts.push_back(ordered_current2D[solution_list[i]]);
//  }
//  ransac->findCentroid(reference_3D_pts,&reference_cent_3D_pts,&reference_centroid);
//  ransac->findCentroid(current_3D_pts,&current_cent_3D_pts,&current_centroid_pt);
//  cv::Point3d reference_centroid_pt(reference_centroid(0,0),reference_centroid(1,1),reference_centroid(2,2));

//  Matrix3d U_svd,V_svd,D_svd;
//  cv2eigen(svd_D,D_svd);
//  cv2eigen(svd_U,U_svd);
//  cv2eigen(svd_V,V_svd);
//  Matrix3d rot;
//  cv2eigen(rotation_matrix,rot);

//  // Compute the Covariance

//  //Inverse Hessian Method
//  if(calc_hess_covariance_)
//  {
//    hess_covariance_ = calculateCovariance(*rotation,*translation,ordered_reference3D);
//  }

  //New method, using image & depth noise:
//  *covariance = calculateNewCovariance(reference_image_pts,current_image_pts,reference_3D_pts,current_3D_pts,
//                                       reference_cent_3D_pts,current_cent_3D_pts,reference_centroid_pt,U_svd,V_svd,
//                                       D_svd,rot,*translation);
  *covariance << 0.0015,0,0,0,0,0,0,
                 0,0.0015,0,0,0,0,0,
                 0,0,0.0012,0,0,0,0,
                 0,0,0,4.8e-5,0,0,0,
                 0,0,0,0,4.4e-4,0,0,
                 0,0,0,0,0,3.2e-5,0,
                 0,0,0,0,0,0,7.1e-8;

  //! \attention Setting the current as the reference must occur at the end of the function!
  if(setAsReference)
  {
    //Rewrite the reference material:
    setCurrentAsReference(visual_cur_image,gray_image,depth_curr_image_float,current2D_features,idealized_curr_pts,
                          current3D_features,current_descriptors);
  }



  delete ransac;
  return 1;
}


//
//  Calculate the approximate Covariance Matrix
//
Matrix<double,7,7> PoseEstimator::calculateCovariance(Quaterniond q, Vector3d t,
                                                      std::vector<cv::Point3d> reference3D)
{
  Matrix<double,7,7> H;
  H.setZero();
  double fy,fx,ox,oy;  //Camera parameters
  double xr,yr,zr,lam;  //reference camera feature points (3D), 1/zfinal = lambda
  double cx,cy,cz,qx,qy,qz,qw;
  cx = t(0);
  cy = t(1);
  cz = t(2);
  qx = q.x();
  qy = q.y();
  qz = q.z();
  qw = q.w();

  fx = rgb_info_.P[0];
  fy = rgb_info_.P[5];
  ox = rgb_info_.P[2];
  oy = rgb_info_.P[6];

  //For loop through each feature, sum up hessian matricies, Add a delta Identity, multiply by 1/noise, and invert
  for(int i=0; i< (int)reference3D.size(); i++)
  {
    xr = reference3D[i].x;
    yr = reference3D[i].y;
    zr = reference3D[i].z;
    lam = 1/(cz + zr*(qw*qw - qx*qx - qy*qy +qz*qz) + xr*(2*qw*qy
                + 2*qx*qz) - yr*2*(qw*qx - qy*qz));

    //H is symmetric - only do half now
    //derivatives w.r.t. cx
    H(0,0) = H(0,0) + 2*lam*lam*fx*fx;
    H(0,1) = H(0,1) + 0;
    H(0,2) = H(0,2) + 2*lam*lam*fx*ox/10.0;
    H(0,3) = H(0,3) + 4*lam*lam*fx*ox*qw*yr;
    H(0,4) = H(0,4) + 4*lam*lam*fx*qw*(fx*zr + ox*xr);
    H(0,5) = H(0,5) + 4*lam*lam*fx*fx*qw*yr;
    H(0,6) = H(0,6) + 4*lam*lam*fx*(fx*qz*yr + fx*qy*zr + ox*qy*xr + ox*qx*yr);

    // cy
    H(1,1) = H(1,1) + 2*lam*lam*fy*fy;
    H(1,2) = H(1,2) + 2*lam*lam*fy*oy/10.0;
    H(1,3) = H(1,3) + 4*lam*lam*fy*qw*(fy*zr+oy*yr);
    H(1,4) = H(1,4) + 4*lam*lam*fy*oy*qw*xr;
    H(1,5) = H(1,5) + 4*lam*lam*fy*fy*qw*xr;
    H(1,6) = H(1,6) + 4*lam*lam*fy*(fy*qz*xr + fy*qx*zr + oy*qy*xr + oy*qx*yr);

    //cz
    H(2,2) = H(2,2) + 2*lam*lam*ox*ox + 2*lam*lam*oy*oy + 2*lam*lam;
    H(2,3) = H(2,3) + 4*lam*lam*qw*(yr*ox*ox + yr*oy*oy + fy*zr*oy + yr);
    H(2,4) = H(2,4) + 4*lam*lam*qw*(xr*ox*ox + fx*zr*ox + xr*oy*oy + xr);
    H(2,5) = H(2,5) + 4*lam*lam*qw*(fy*oy*xr + fx*ox*yr);
    H(2,6) = H(2,6) + 4*lam*lam*(qy*xr + qx*yr + ox*ox*qy*xr + oy*oy*qy*xr + ox*ox*qx*yr +
                                 oy*oy*qx*yr + fy*oy*qz*xr + fx*ox*qz*yr + fx*ox*qy*zr + fy*oy*qx*zr);

    //qx
    H(3,3) = H(3,3) + 8*lam*lam*qw*qw*(fy*fy*zr*zr + 2*fy*oy*yr*zr + ox*ox*yr*yr + oy*oy*yr*yr + yr*yr);
    H(3,4) = H(3,4) + 8*lam*lam*qw*qw*(xr*yr*ox*ox + fx*yr*zr*ox + xr*yr*oy*oy + fy*xr*zr*oy + xr*yr);
    H(3,5) = H(3,5) + 8*lam*lam*qw*qw*(xr*zr*fy*fy + oy*xr*fy*yr + fx*ox*yr*yr);
    H(3,6) = H(3,6) + 4*lam*lam*(cy*fy*fy*zr + cz*ox*ox*yr + cz*oy*oy*yr + fy*oy*yr*yr + fy*oy*zr*zr + fy*fy*yr*zr +
                                 ox*ox*yr*zr + oy*oy*yr*zr + 4*fy*fy*qw*qx*zr*zr + cx*fx*ox*yr + cy*fy*oy*yr +
                                 cz*fy*oy*zr + 4*ox*ox*qw*qx*yr*yr + 4*oy*oy*qw*qx*yr*yr + fx*ox*xr*yr +
                                 4*fx*ox*qw*qz*yr*yr + 4*fy*fy*qw*qz*xr*zr + 4*ox*ox*qw*qy*xr*yr +
                                 oy*oy*qw*qy*xr*yr + 4*fy*oy*qw*qz*xr*yr + 4*fy*oy*qw*qy*xr*zr +
                                 4*fx*ox*qw*qy*yr*zr + 8*fy*oy*qw*qx*yr*zr);

    //qy
    H(4,4) = H(4,4) + 8*lam*lam*qw*qw*(fx*fx*zr*zr + 2*fx*ox*xr*zr + xr*xr);//ox*ox*xr*xr + oy*oy*xr*xr +
    H(4,5) = H(4,5) + 8*lam*lam*qw*qw*(yr*zr*fx*fx);
    H(4,6) = H(4,6) + 4*lam*lam*qw*qw*(cx*fx*fx*zr + cz*ox*ox*xr + cz*oy*oy*xr + fx*ox*xr*xr + fx*ox*zr*zr +
                                             fx*fx*xr*zr + ox*ox*xr*zr + oy*oy*xr*zr + cx*fx*ox*xr + cy*fy*oy*xr +
                                             4*fx*fx*qw*qy*zr*zr + cz*fx*ox*zr + 4*ox*ox*qw*qy*xr +
                                             4*oy*oy*qw*qy*xr*xr + fy*oy*xr*yr + 4*fy*oy*qw*qz*xr*xr +
                                             4*fx*fx*qw*qz*yr*zr + 4*ox*ox*qw*qx*xr*yr +
                                             4*oy*oy*qw*qx*xr*yr + 4*fx*ox*qw*qz*xr*yr + 8*fx*ox*qw*qy*xr*zr +
                                             4*fy*oy*qw*qx*xr*zr + 4*fx*ox*qw*qx*yr*zr);

    //qz
    H(5,5) = H(5,5) + 8*lam*lam*fx*fx*qw*qw*yr*yr + 8*lam*lam*fy*fy*qw*qw*xr*xr;
    H(5,6) = H(5,6) + 4*lam*lam*(cy*fy*fy*xr + cy*fx*fx*yr + fx*fx*xr*yr + fy*fy*xr*yr + 4*fy*fy*qw*qz*xr*xr +
                                 4*fx*fx*qw*qz*yr*yr + cy*fy*oy*xr + cz*fx*ox*yr + fy*oy*xr*zr + fx*ox*yr*zr +
                                 4*fy*oy*qw*qy*xr*xr + fx*ox*qw*qx*yr*yr + 4*fy*fy*qw*qx*xr*zr +
                                 4*fx*fx*qw*qy*yr*zr + 4*fx*ox*qw*qy*xr*yr + 4*fy*oy*qw*qx*xr*yr);

    //qw
    H(6,6) = H(6,6) + 8*lam*lam*((fy*qz*xr + fy*qx*zr + oy*qx*yr)*(fy*qz*xr + fy*qx*zr + oy*qx*yr) +
                                 (fx*qz*yr + fx*qy*zr + ox*qy*xr + ox*qx*yr)*
                                 (fx*qz*yr + fx*qy*zr + ox*qy*xr + ox*qx*yr)) + 8*(lam*qy*xr + lam*qx*yr)*
                                 (lam*qy*xr + lam*qx*yr);

  }
  H(1,0) = H(0,1);
  H(2,0) = H(0,2);
  H(3,0) = H(0,3);
  H(4,0) = H(0,4);
  H(5,0) = H(0,5);
  H(6,0) = H(0,6);
  H(2,1) = H(1,2);
  H(3,1) = H(1,3);
  H(4,1) = H(1,4);
  H(5,1) = H(1,5);
  H(6,1) = H(1,6);
  H(3,2) = H(2,3);
  H(4,2) = H(2,4);
  H(5,2) = H(2,5);
  H(6,2) = H(2,6);
  H(4,3) = H(3,4);
  H(5,3) = H(3,5);
  H(6,3) = H(3,6);
  H(5,4) = H(4,5);
  H(6,4) = H(4,6);
  H(6,5) = H(5,6);

  H += deltaI_;
  Matrix<double,7,7> idmap; //Used to remap some of the parameters in H
  idmap = Matrix<double,7,7>::Identity();
  idmap(4,4) = 0;
  idmap(5,5) = 0;
  idmap(5,4) = 1;
  idmap(4,5) = 1;

  H = idmap*H*idmap.transpose()*1/25;

  //H *= image_noise_;
//  H(0,0) *= image_noise_(0,0);
//  H(1,1) *= image_noise_(1,1);
//  H(2,2) *= image_noise_(2,2);
//  H(3,3) *= image_noise_(3,3);
//  H(4,4) *= image_noise_(4,4);
//  H(5,5) *= image_noise_(5,5);
//  H(6,6) *= image_noise_(6,6);

  //std::cout << H << std::endl;

  return H.inverse();
}


//
// Calculate the covariance with the new method that brings it from image and depth noise:
//
Matrix<double,7,7> PoseEstimator::calculateNewCovariance(std::vector<cv::Point2f> &reference_image_pts,
                                                         std::vector<cv::Point2f> &current_image_pts,
                                                         std::vector<cv::Point3d> &reference_3D_pts,
                                                         std::vector<cv::Point3d> &current_3D_pts,
                                                         std::vector<cv::Point3d> &reference_cent_3D_pts,
                                                         std::vector<cv::Point3d> &current_cent_3D_pts,
                                                         cv::Point3d &reference_centroid_pt,
                                                         Eigen::Matrix3d &svd_U,
                                                         Eigen::Matrix3d &svd_V,
                                                         Eigen::Matrix3d &svd_D,
                                                         Eigen::Matrix3d &R,
                                                         Eigen::Vector3d &T)
{
  /// \note:  I need to define EIGEN_NO_DEBUG somewhere so that range checking will be disabled (and it will run faster)

  //
  //There are 5 steps to producing the covariance and they build on one another:
  //

  //********************************************************
  //Step 1: Map uncertainty in pixels and depth to 3D points:
  //the outputs of this step: 6 covariances, 1 for each of the 6 3D points used in the solution
  int number = (int)reference_image_pts.size(); //# points used in solution
  std::vector<Matrix3d, Eigen::aligned_allocator<Matrix3d> > covariance_Ref_pts;
  std::vector<Matrix3d, Eigen::aligned_allocator<Matrix3d> > covariance_Cur_pts;
  Matrix3d JacobianR1;
  Matrix3d JacobianC1;
  JacobianR1.setZero();
  JacobianC1.setZero();
  double fx,fy,ox,oy;
  fx = rgb_camera_P_.at<double>(0,0);
  fy = rgb_camera_P_.at<double>(1,1);
  ox = rgb_camera_P_.at<double>(0,2);
  oy = rgb_camera_P_.at<double>(1,2);

  for (int i = 0; i < number; i++)
  {
    //Reference points:
    JacobianR1(0,0) = reference_3D_pts[i].z/fx;
    JacobianR1(0,2) = (reference_image_pts[i].x - ox)/fx;
    JacobianR1(1,1) = reference_3D_pts[i].z/fy;
    JacobianR1(1,2) = (reference_image_pts[i].y - oy)/fy;
    /// No terms yet for JacobianR1(2,0) and *(2,1) - which maps uncertainty in cx and cy to the Z direction
    JacobianR1(2,2) = 1.0;

    covariance_Ref_pts.push_back(JacobianR1*image_noise_*JacobianR1.transpose());

    //Current  points:
    JacobianC1(0,0) = current_3D_pts[i].z/fx;
    JacobianC1(0,2) = (current_image_pts[i].x - ox)/fx;
    JacobianC1(1,1) = current_3D_pts[i].z/fy;
    JacobianC1(1,2) = (current_image_pts[i].y - oy)/fy;
    /// No terms yet for JacobianC1(2,0) and *(2,1) - which maps uncertainty in cx and cy to the Z direction
    JacobianC1(2,2) = 1.0;

    covariance_Cur_pts.push_back(JacobianC1*image_noise_*JacobianC1.transpose());
  }

  //********************************************************
  //Step 2a: Map 3D point uncertainty to centered 3D point uncertainty (each point minus its centroid):
  std::vector<Matrix3d, Eigen::aligned_allocator<Matrix3d> > covariance_Ref_Cent;
  std::vector<Matrix3d, Eigen::aligned_allocator<Matrix3d> > covariance_Cur_Cent;
  Matrix3d covariance_corresopond;
  //covariance_corresopond << 0.1*0.1,0,0,0,0.1*0.1,0,0,0,0.1*0.1;
  covariance_corresopond.setZero();

  double n = (double)number;

  for(int i=0; i<number;i++)
  {
    Matrix3d temp_Ref, temp_Cur;
    temp_Ref.setZero();
    temp_Cur.setZero();
    for(int j=0; j<number;j++)
    {
      if(i == j)
      {
        temp_Ref += (1.0 - 1./n)*(1.0 - 1./n)*covariance_Ref_pts[j] + covariance_corresopond;
        temp_Cur += (1.0 - 1./n)*(1.0 - 1./n)*covariance_Cur_pts[j] + covariance_corresopond;
      }
      else
      {
        temp_Ref += (1./n)*(1./n)*covariance_Ref_pts[j] + covariance_corresopond;
        temp_Cur += (1./n)*(1./n)*covariance_Cur_pts[j] + covariance_corresopond;
      }
    }
    covariance_Ref_Cent.push_back(temp_Ref);
    covariance_Cur_Cent.push_back(temp_Cur);
  }
//  covariance_Ref_Cent.push_back(4./9.*covariance_Ref_pts[0] + 1./9.*covariance_Ref_pts[1] + 1./9.*covariance_Ref_pts[2]);
//  covariance_Ref_Cent.push_back(1./9.*covariance_Ref_pts[0] + 4./9.*covariance_Ref_pts[1] + 1./9.*covariance_Ref_pts[2]);
//  covariance_Ref_Cent.push_back(1./9.*covariance_Ref_pts[0] + 1./9.*covariance_Ref_pts[1] + 1./9.*covariance_Ref_pts[2]);

//  covariance_Cur_Cent.push_back(4./9.*covariance_Cur_pts[0] + 1./9.*covariance_Cur_pts[1] + 1./9.*covariance_Cur_pts[2]);
//  covariance_Cur_Cent.push_back(1./9.*covariance_Cur_pts[0] + 4./9.*covariance_Cur_pts[1] + 1./9.*covariance_Cur_pts[2]);
//  covariance_Cur_Cent.push_back(1./9.*covariance_Cur_pts[0] + 1./9.*covariance_Cur_pts[1] + 4./9.*covariance_Cur_pts[2]);

  //Step 2b: Map centered 3D point uncertainty to H uncertainty (H is the matrix composed of the 3D pts to find the SVD)
  Matrix<double,9,9> CovarianceH;
  Matrix<double,9,3> JacobianR2;
  Matrix<double,9,3> JacobianC2;
  CovarianceH.setZero();
  JacobianR2.setZero();
  JacobianC2.setZero();

  for (int i = 0; i<number; i++)
  {
    //Ref Jacobian uses Current Points and vice versa
    JacobianR2(0,0) = current_cent_3D_pts[i].x;
    JacobianR2(1,0) = current_cent_3D_pts[i].x;
    JacobianR2(2,0) = current_cent_3D_pts[i].x;
    JacobianR2(3,1) = current_cent_3D_pts[i].y;
    JacobianR2(4,1) = current_cent_3D_pts[i].y;
    JacobianR2(5,1) = current_cent_3D_pts[i].y;
    JacobianR2(6,2) = current_cent_3D_pts[i].z;
    JacobianR2(7,2) = current_cent_3D_pts[i].z;
    JacobianR2(8,2) = current_cent_3D_pts[i].z;

    JacobianC2(0,0) = reference_cent_3D_pts[i].x;
    JacobianC2(1,0) = reference_cent_3D_pts[i].y;
    JacobianC2(2,0) = reference_cent_3D_pts[i].z;
    JacobianC2(3,1) = reference_cent_3D_pts[i].x;
    JacobianC2(4,1) = reference_cent_3D_pts[i].y;
    JacobianC2(5,1) = reference_cent_3D_pts[i].z;
    JacobianC2(6,2) = reference_cent_3D_pts[i].x;
    JacobianC2(7,2) = reference_cent_3D_pts[i].y;
    JacobianC2(8,2) = reference_cent_3D_pts[i].z;

    CovarianceH += JacobianR2*covariance_Ref_Cent[i]*JacobianR2.transpose() +
                   JacobianC2*covariance_Cur_Cent[i]*JacobianC2.transpose();
  }

  //********************************************************
  //Step 3: Map covariance on H to the covariance on the Rotation R (Uses covariance on SVD from Papadopoulo & Lourakis)
  Matrix<double,9,9> Jacobian3,CovarianceR;
  Matrix3d omega_Uij,omega_Vij; //Matricies for the SVD jacobian
  Matrix3d JacobianVUt; //Jacobian of VU^T w.r.t element (i,j) of H
  double d1,d2,d3; //singular values
  Jacobian3.setZero();
  CovarianceR.setZero();
  d1 = svd_D(0,0);
  d2 = svd_D(1,0);
  d3 = svd_D(2,0);
  bool unique = true;
  bool unique12 = true;
  bool unique13 = true;
  bool unique23 = true;

  //Check to see if the singular values are unique:
  const double TOLERANCE = 0.01;
  if(fabs(d1-d2) <= TOLERANCE*maximum(1.0d, fabs(d1),fabs(d2))) //(from: http://realtimecollisiondetection.net/blog/?p=89)
  {
    unique12 = false;
  }
  else if(fabs(d1-d3) <= TOLERANCE*maximum(1.0d, fabs(d1),fabs(d3)))
  {
    unique13 = false;
  }
  else if(fabs(d2-d3) <= TOLERANCE*maximum(1.0d, fabs(d2),fabs(d3)))
  {
    unique23 = false;
  }
  if((!unique12 && !unique13) || (!unique12 && !unique23) || (!unique13 && !unique23))
  {
    unique = false;
  }

  if(!unique)
  {
    ROS_WARN("SVD Resulted in None-Uniqe singular values: minimum-norm solution used!");
  }

  int position = 0;

  //to handle when last column of V should be negated
  bool negateCol = false;
  Matrix3d temp;
  temp.setZero();
  temp = svd_V*svd_U.transpose();
  if(temp.determinant() < 0)
  {
    negateCol = true;
  }
  Matrix3d negColumn;
  negColumn.setIdentity();
  negColumn(2,2) = -1.0;


  //compute Jacobian for each element of H, gives a 9x9 Jacobian
  for(int j = 0; j < 3; j++)
  {
    for(int i = 0; i < 3; i++)
    {
      omega_Uij.setZero();
      omega_Vij.setZero();
      JacobianVUt.setZero();

//      if(unique && unique23)
//      {
      /// \note: This portion is ONLY valid when all the singular values are unique:
      //Here we are solving 3 2x2 systems of equations (I've done it analytically to try and save computations)
      //for (0,1) of Omega_U & _V
      omega_Uij(0,1) = 1.0*1.0/(d2*d2 - d1*d1)*(d2*(svd_U(i,0)*svd_V(j,1)) - d1*(-svd_U(i,1)*svd_V(j,0)));
      omega_Vij(0,1) = 1.0*1.0/(d2*d2 - d1*d1)*(-d1*(svd_U(i,0)*svd_V(j,1)) + d2*(-svd_U(i,1)*svd_V(j,0)));

      //for (0,2) of Omega_U & _V
      omega_Uij(0,2) = 1.0*1.0/(d3*d3 - d1*d1)*(d3*(svd_U(i,0)*svd_V(j,2)) - d1*(-svd_U(i,2)*svd_V(j,0)));
      omega_Vij(0,2) = 1.0*1.0/(d3*d3 - d1*d1)*(-d1*(svd_U(i,0)*svd_V(j,2)) + d3*(-svd_U(i,2)*svd_V(j,0)));

      //for (1,2) 0f Omega_U & _V
      omega_Uij(1,2) = (1.0/1.0)*1.0/(d3*d3 - d2*d2)*(d3*(svd_U(i,1)*svd_V(j,2)) - d2*(-svd_U(i,2)*svd_V(j,1)));
      omega_Vij(1,2) = (1.0/1.0)*1.0/(d3*d3 - d2*d2)*(-d2*(svd_U(i,1)*svd_V(j,2)) + d3*(-svd_U(i,2)*svd_V(j,1)));
//      }
//      else if(unique && !unique23)
//      {
//        //We can solve for the 1st two analytically, need minimum norm solution for the 3rd.
//        //for (0,1) of Omega_U & _V
//        omega_Uij(0,1) = 100.0*1.0/(d2*d2 - d1*d1)*(d2*(svd_U(i,0)*svd_V(j,1)) - d1*(-svd_U(i,1)*svd_V(j,0)));
//        omega_Vij(0,1) = 100.0*1.0/(d2*d2 - d1*d1)*(-d1*(svd_U(i,0)*svd_V(j,1)) + d2*(-svd_U(i,1)*svd_V(j,0)));

//        //for (0,2) of Omega_U & _V
//        omega_Uij(0,2) = 100.0*1.0/(d3*d3 - d1*d1)*(d3*(svd_U(i,0)*svd_V(j,2)) - d1*(-svd_U(i,2)*svd_V(j,0)));
//        omega_Vij(0,2) = 100.0*1.0/(d3*d3 - d1*d1)*(-d1*(svd_U(i,0)*svd_V(j,2)) + d3*(-svd_U(i,2)*svd_V(j,0)));

////        //for (1,2) 0f Omega_U & _V
////        double tempU = 1.0/(d3*d3 - d2*d2)*(d3*(svd_U(i,1)*svd_V(j,2)) - d2*(-svd_U(i,2)*svd_V(j,1)));
////        double tempV = 1.0/(d3*d3 - d2*d2)*(-d2*(svd_U(i,1)*svd_V(j,2)) + d3*(-svd_U(i,2)*svd_V(j,1)));

//        //Need to do least-squares to find the components (gives minimum-norm solution)
//        Matrix<double,2,2> d;
//        Matrix<double,2,1> omega;
//        Matrix<double,2,1> uv;
//        d.setZero();
//        omega.setZero();
//        uv.setZero();

//        d(0,0) = d3;
//        d(0,1) = d2;
//        d(1,0) = d2;
//        d(1,1) = d3;

//        uv(0,0) = svd_U(i,1)*svd_V(j,2);
//        uv(1,0) = -svd_U(i,2)*svd_V(j,1);

//        omega = d.jacobiSvd(ComputeFullU | ComputeFullV).solve(uv);

//        omega_Uij(1,2) = (1.0/100.0)*omega(0,0);
//        omega_Vij(1,2) = (1.0/100.0)*omega(1,0);
//      }


      //enter the other parts:
      omega_Uij(1,0) = omega_Uij(0,1);
      omega_Uij(2,0) = omega_Uij(0,2);
      omega_Uij(2,1) = omega_Uij(1,2);
      omega_Vij(1,0) = omega_Vij(0,1);
      omega_Vij(2,0) = omega_Vij(0,2);
      omega_Vij(2,1) = omega_Vij(1,2);

      if(!negateCol)
      {
        JacobianVUt = -svd_V*omega_Vij*svd_U.transpose() - svd_V*omega_Uij*svd_U.transpose();
      }
      else
      {
        JacobianVUt = -svd_V*omega_Vij*negColumn*svd_U.transpose() - svd_V*negateCol*omega_Uij*svd_U.transpose();
      }
      Jacobian3.block<3,1>(0,position) = JacobianVUt.block<3,1>(0,0);
      Jacobian3.block<3,1>(3,position) = JacobianVUt.block<3,1>(0,1);
      Jacobian3.block<3,1>(6,position) = JacobianVUt.block<3,1>(0,2);

      position++;
    }
  }

  CovarianceR = Jacobian3*CovarianceH*Jacobian3.transpose();

  //********************************************************
  //Step 4: Map uncertainty in R to uncertainty in q (the quaternion)
  Matrix<double,4,9> Jacobian4;
  Matrix4d CovarianceQ;
  Jacobian4.setZero();
  CovarianceQ.setZero();
  double qw = 1.0/2.0*sqrt(R(0,0) + R(1,1) + R(2,2) + 1.0);
  double dterm = R(0,0) + R(1,1) + R(2,2) + 1.0;

  Jacobian4(0,0) = (R(2,1) - R(1,2))/(4.0*pow(dterm,3.0/2.0));
  Jacobian4(0,4) = Jacobian4(0,0);
  Jacobian4(0,5) = -1.0/(4.*qw);
  Jacobian4(0,7) = -Jacobian4(0,5);
  Jacobian4(0,8) = Jacobian4(0,0);
  Jacobian4(1,0) = (R(0,2) - R(2,0))/(4.0*pow(dterm,3.0/2.0));
  Jacobian4(1,2) = 1.0/(4.0*qw);
  Jacobian4(1,4) = Jacobian4(1,0);
  Jacobian4(1,6) = -Jacobian4(1,2);
  Jacobian4(1,8) = Jacobian4(1,0);
  Jacobian4(2,0) = (R(1,0) - R(0,1))/(4.0*pow(dterm,1.0/2.0));
  Jacobian4(2,1) = -1.0/(4.0*qw);
  Jacobian4(2,3) = -Jacobian4(2,1);
  Jacobian4(2,4) = Jacobian4(2,0);
  Jacobian4(2,8) = Jacobian4(2,0);
  Jacobian4(3,0) = 1.0/(4.0*sqrt(dterm));
  Jacobian4(3,4) = Jacobian4(3,0);
  Jacobian4(3,8) = Jacobian4(3,0);

  CovarianceQ = Jacobian4*CovarianceR*Jacobian4.transpose()*1000000.0;

  //********************************************************
  //Step 5: Map uncertainty in 3D points and Rotation to Translation uncertainty
  Matrix3d Covariance_Ref_C, Covariance_Cur_C, CovarianceT; //reference and current centroid covariances, translation covar
  Matrix<double,3,15> Jacobian5; //Jacobian
  Matrix<double,15,15> Combined_Covariance;  //block diagonal covariance for Rotation and 2 centroids
  Jacobian5.setZero();
  Combined_Covariance.setZero();
  Covariance_Ref_C.setZero();
  Covariance_Cur_C.setZero();
  CovarianceT.setZero();

  for (int i = 0; i<number; i++)
  {
    Covariance_Ref_C += 1.0/(n*n)*(covariance_Ref_pts[i]);
    Covariance_Cur_C += 1.0/(n*n)*(covariance_Cur_pts[i]);
  }

  Jacobian5.block<3,3>(0,12) = Matrix3d::Identity();
  Jacobian5(0,0) = -reference_centroid_pt.x;
  Jacobian5(0,1) = -reference_centroid_pt.x;
  Jacobian5(0,2) = -reference_centroid_pt.x;
  Jacobian5(0,9) = -R(0,0);
  Jacobian5(0,10) = -R(0,1);
  Jacobian5(0,11) = -R(0,2);
  Jacobian5(1,3) = -reference_centroid_pt.y;
  Jacobian5(1,4) = -reference_centroid_pt.y;
  Jacobian5(1,5) = -reference_centroid_pt.y;
  Jacobian5(1,9) = -R(1,0);
  Jacobian5(1,10) = -R(1,1);
  Jacobian5(1,11) = -R(1,2);
  Jacobian5(2,6) = -reference_centroid_pt.z;
  Jacobian5(2,7) = -reference_centroid_pt.z;
  Jacobian5(2,8) = -reference_centroid_pt.z;
  Jacobian5(2,9) = -R(2,0);
  Jacobian5(2,10) = -R(2,1);
  Jacobian5(2,11) = -R(2,2);

  Combined_Covariance.block<9,9>(0,0) = CovarianceR;
  Combined_Covariance.block<3,3>(9,9) = Covariance_Ref_C;
  Combined_Covariance.block<3,3>(12,12) = Covariance_Cur_C;

  CovarianceT = Jacobian5*Combined_Covariance*Jacobian5.transpose()*20;

  if(!unique)
  {
    std::cout << CovarianceQ << std::endl;
    std::cout << std::endl;
    std::cout << CovarianceT << std::endl;
    std::cout << std::endl;
    std::cout << d1 << " " << d2 << " " << d3 << std::endl;
  }

  //Finally, set the covariances for q and T in the appropriate spot:
  Matrix<double,7,7> finalCovariance;
  finalCovariance.setZero();
  finalCovariance.block<3,3>(0,0) = CovarianceT;
  finalCovariance.block<4,4>(3,3) = CovarianceQ;

  return finalCovariance;
}



//
//This method calculates the 3D points from the features, depth, and calibration
//
void PoseEstimator::calc3DPoints(cv::Mat &depth_float,
                                 sensor_msgs::CameraInfo &kinect_calibration,
                                 std::vector<cv::Point2f> *features2D,
                                 std::vector<cv::Point2f> *features2D_undistored,
                                 std::vector<cv::Point3d> *features3D)
{
  //Check calibration info:
  ROS_ASSERT(kinect_calibration.K[0] != 0.0);

  double x,y;//temp point,
  //principal point and focal lengths:
  double cx = kinect_calibration.K[2];
  double cy = kinect_calibration.K[5];
  double fx = kinect_calibration.K[0];
  double fy = kinect_calibration.K[4];

  if(features3D->size())
  {
    ROS_INFO("There is already 3D info in the FrameInfo, clearing it");
    features3D->clear();
  }

  for(unsigned int i = 0; i < features2D_undistored->size(); i++)
  {
    //look up depth with raw (distorted) image points:

    /// \todo Apply the calibration transformation to the depth points to place them in the RGB image frame (i.e. perform
    /// depth registration, as they call it on the tutorials!)
    /// \todo Apply the transformation on the mask that I am using for feature locations as well!!!
    double z = depth_float.at<float>(features2D->at(i).y, features2D->at(i).x);

    // Check for invalid measurements (shouldn't happen because we used the depth image to mask finding features...)
    if (std::isnan (z))
    {
      ROS_WARN_STREAM("Bad depth on Keypoint, setting depth to 8m: " << features2D_undistored->at(i));
      //Set the z to something far out:
      z = 8.0;  //about 2x the valid limit for the kinect
      //deleting the feature wreaks havoc with my matching later on
    }


    x = ((features2D_undistored->at(i).x - cx) * z) / fx;
    y = ((features2D_undistored->at(i).y - cy) * z) / fy;

    features3D->push_back(cv::Point3d(x, y, z));
  }
}


//
//  Set the calibration parameters
//
void PoseEstimator::setKinectCalibration(const sensor_msgs::CameraInfoConstPtr &depth_info,
                                         const sensor_msgs::CameraInfoConstPtr &rbg_info)
{
    depth_info_ = *depth_info;//->CameraInfo_;
    rgb_info_ = *rbg_info;//->CameraInfo_;

    rgb_camera_matrix_ = (cv::Mat_<double>(3,3) <<  rgb_info_.K[0], 0, rgb_info_.K[2],
                                               0, rgb_info_.K[4], rgb_info_.K[5],
                                               0, 0, 1);

    rgb_camera_P_ = (cv::Mat_<double>(3,4) <<  rgb_info_.P[0], 0, rgb_info_.P[2], rgb_info_.P[3],
                                               0, rgb_info_.P[5], rgb_info_.P[6], rgb_info_.P[7],
                                               0, 0, 1, 0);

    rgb_camera_distortion_.clear();
    for (int i = 0; i< (int)rgb_info_.D.size(); i++)
    {
      rgb_camera_distortion_.push_back(rgb_info_.D[i]);
    }

    ROS_ASSERT((int)rgb_camera_distortion_.size() > 0);
}


//
//  This function takes the current image info and saves it to the class variables for the reference image.
//
void PoseEstimator::setCurrentAsReference(cv::Mat color_image,
                                          cv::Mat mono_image,
                                          cv::Mat depth_image,
                                          std::vector<cv::KeyPoint> features2D,
                                          std::vector<cv::Point2f> idealized_pts,
                                          std::vector<cv::Point3d> features3D,
                                          cv::Mat descriptors)
{
  //! \note To make this multi-threaded capable, some kind of Mutex should be in here, as we are adjusting class variables!

  reference_img_RGB_ = color_image.clone();
  reference_img_gray_ = mono_image.clone();
  reference_img_depth_ = depth_image.clone();
  reference2D_features_ = features2D;
  reference2D_idealized_ = idealized_pts;
  reference3D_features_ = features3D;
  reference_descriptors_ = descriptors.clone();
  reference_set_ = true;

  if(enable_optimizer_)
  {
//    //If the optimizer already has data, clear it, add new reference info:
//    if(optimizer_.vertices().size() > 0)
//    {
//      optimizer_.clear();
//      num_pose_verticies_ = 0;
//    }
//    edge_id_ = 0; //set to zero

//    Vector3d trans(0.d,0.d,0.d);
//    Quaterniond q;
//    q.setIdentity();

//    g2o::SBACam ref_sba(q,trans);
//    ref_sba.setKcam(rgb_info_.K[0],rgb_info_.K[4],rgb_info_.K[2],rgb_info_.K[5],0.d); //fx,fy,cx,cy,baseline

//    g2o::VertexCam *ref_vertex = new g2o::VertexCam();
//    ref_vertex->setId(0);
//    ref_vertex->setEstimate(ref_sba); //  estimate() = ;
//    ref_vertex->setFixed(true);

//    optimizer_.addVertex(ref_vertex);
//    pose_vertex_id_ = 0; //set to zero
//    pose_vertex_id_++; //increment
//    num_pose_verticies_ = 1;

//    //add edges between this vertex and the 3D points (and verticies for the 3D points)
//    for(size_t i = 0; i< reference3D_features_.size(); i++)
//    {
//      g2o::VertexPointXYZ *v_p = new g2o::VertexPointXYZ();
//      v_p->setId(i+point_vertex_offset_); //the ID is the position in the vector + the offset
//      v_p->setMarginalized(true);
//      v_p->estimate() = Vector3d(reference3D_features_[i].x,reference3D_features_[i].y,reference3D_features_[i].z);


//      Vector2d pt(reference2D_idealized_[i].x, reference2D_idealized_[i].y);
//      g2o::EdgeProjectP2MC *e = new g2o::EdgeProjectP2MC();
//      e->vertices()[0] = dynamic_cast<g2o::OptimizableGraph::Vertex*>(v_p);
//      e->vertices()[1] = dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer_.vertices().find(0)->second);
//      e->measurement() = pt;
//      e->inverseMeasurement() = -pt;
//      e->information() = Matrix2d::Identity();
//      e->setRobustKernel(true);
//      e->setHuberWidth(0.01);
//      e->setId(edge_id_);
//      optimizer_.addEdge(e);

//      optimizer_.addVertex(v_p);
//    }
  }
}



//
//  Draw the feature associations
//
void PoseEstimator::drawFeatureAssociations(std::vector<cv::Point2f> ref_features,
                                            std::vector<cv::Point2f> cur_features,
                                            std::vector<int> inliers,
                                            cv::Mat image)
{
  cv::Scalar lime(50,205,50),magenta(227,91,216),red(255,0,0),gray(190,190,190),salmon(255,160,122),maroon(176,48,96);
double average = 0;

  for(unsigned int i = 0; i < ref_features.size(); i++)
  {
    cv::Point2f start = ref_features[i];
    cv::Point2f end = cur_features[i];
    cv::Point2f total = end - start;
    double dist = sqrt(total.x*total.x + total.y*total.y);


    if(std::find(inliers.begin(),inliers.end(),i) != inliers.end() || i == inliers[inliers.size()-1])
    {
      //inlier
      cv::line(image, start, end, lime,1);
      cv::circle(image,start,2,magenta,1);
      cv::circle(image,end,2,red,1);
    }
    else
    {
      cv::line(image, start, end, gray,1);
      cv::circle(image,start,2,salmon,1);
      cv::circle(image,end,2,maroon,1);
    }
    average += dist;

  }
  average /= ref_features.size();

  std::cout << "Average dist: " << average << std::endl;

}


//
//  Extract angles
//
void PoseEstimator::extractAngles(cv::Mat &rotation_matrix, double *roll, double *pitch, double *yaw)
{
  if(rotation_matrix.at<double>(0,0) != 0.d)
  {
    double phi,theta,psi;
    phi = atan2(rotation_matrix.at<double>(1,2),rotation_matrix.at<double>(2,2)); // m23,m33
    theta = asin(-rotation_matrix.at<double>(0,2)); // -m13
    psi = atan2(rotation_matrix.at<double>(0,1), rotation_matrix.at<double>(0,0)); // m12,m11

    //Since the rotation is acually from the reference camera to the current camera, the global roll, pitch, and yaw are defined by:
    *roll = psi;
    *pitch = phi;
    *yaw = theta;
  }
  else
  {
    *roll = 0.d;
    *pitch = 0.d;
    *yaw = 0.d;
  }
}



