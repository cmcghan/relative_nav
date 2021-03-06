 /* \copyright This work was completed by Robert Leishman while performing official duties as 
  * a federal government employee with the Air Force Research Laboratory and is therefore in the 
  * public domain (see 17 USC § 105). Public domain software can be used by anyone for any purpose,
  * and cannot be released under a copyright license
  */

/*!
 *  \title main.cpp for the hex_planner project
 *  \author Robert Leishman
 *  \date October 2012
 *  \brief This code implements the costmap_2d and navfn classes available through ROS (and the ros wrapper versions).
 *  It aims to plan paths given a cost map generated using sensor data.
 *
 */

#include <ros/ros.h>
#include "hex_planner/hex_planner.h"


/*!
 *  \brief The main function starts ROS and hands it off to the hex_planner.
 *  Main will also check to see how long it has been since a valid plan has been created. If so much time has passed
 *  (currently hardcoded as a second), it will request a new plan.
 *
*/
int main(int argc, char **argv)
{

  ros::init(argc,argv,"hex_planner");

  ros::NodeHandle nh;

  std::string costmap_name;

  //retrieve name from server
  ros::param::param<std::string>("~costmap_name", costmap_name, "hex_costmap");

  /*!
    \note Below are the private parameters that are available to change through the param server:
     \code{.cpp}
  ros::param::param<std::string>("~costmap_name", costmap_name, "hex_costmap"); //!< name for the costmap (need this name to access its parameters)
     \endcode
  */

  tf::TransformListener tf_listen(ros::Duration(10));
  costmap_2d::Costmap2DROS costmap(costmap_name, tf_listen);

  /*!
   *  \attention This is not documented well, but the Costmap2DROS and NavfnROS classes use a private node handle to
   *  get their private parameters off of the parameter server, like so (where "name" is as set in the constructor):
     \code{.cpp}
      ros::NodeHandle private_nh("~/" + name);

      std::string map_type;
      private_nh.param("map_type", map_type, std::string("voxel"));

      private_nh.param("publish_voxel_map", publish_voxel_, false);
     \endcode

   *  So, one would access the "map_type" private parameter using: <node_name>/name/map_type
   *  using either the launch file or command line arguments. See bottom of the page
   *  http://www.ros.org/wiki/roscpp/Overview/NodeHandles
  */


  HexPlanner planner(nh, &costmap);

  ros::Rate r(10);

  bool worked;

  while(ros::ok())
  {
    ros::spinOnce();

    //If no new goal has been recieved after so long, plan again (to avoid slow moving obstacles, like cows):
    if(planner.initGoalLocationRecieved() && (ros::Time::now() - planner.getLastPlanTimestamp()).toSec() >= 1.0)
    {
      worked = planner.updatePlan();
    }
    r.sleep();
  }


}
