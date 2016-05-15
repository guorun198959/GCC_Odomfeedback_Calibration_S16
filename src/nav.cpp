#ifndef NAV_CPP
#define NAV_CPP
#include <turtlebot/mymsg.h>
#include "nav.h"
#include "ros/ros.h"
#include <string>
#include <cstring>
#include <math.h>
#include <cmath>
#include <nav_msgs/Odometry.h>
#include <tf/transform_broadcaster.h>

const double YAW_ERR = 10;
// how much we need in velocity commands to move INCREMENT_AMT forward
const double MOVEMENT_MULTIPLE = 1.8; 
// how much we move forward/backward each increment
const double INCREMENT_AMT = .1;
// how much angular velocity we need to move right 90 degrees
const double LEFT_90 = 2.54629;
// how much angular velocity we need to move right 90 degrees
const double RIGHT_90 = -2.56;
// this is how long the TurtleBot takes to move INCREMENT_AMT distance forward
const double MOVEMENT_INTERVAL = 500000;
// how much we need to multiply radians by in order to get degrees
const double ANGLE_CONVERT = 57.2958;
// approximately Pi, used to indicate around 180 degrees (must be under pi, incase of overshooting
const double YAW_180 = 3.1415;

// initializer for RoboState (with default values)
RoboState::RoboState(ros::NodeHandle rosNode): xCoord(0), yCoord(0), messageStatus(false), xOdomOld(0), yOdomOld(0), negXturn(false), initXneg(false), yaw(0), rotated(false), yawGoal(0), count(0)
{
  this->node = rosNode;
  // publishes velocity messages
  this->velocityPublisher = this->node.advertise<geometry_msgs::Twist>("/mobile_base/commands/velocity", 1);
  // subscribes to messages from ui_node
  this->messageSubscriber= this->node.subscribe("my_msg", 1000, &RoboState::messageCallback, this);
  // subscribes to data from bumpers
  this->bumperSubscriber = this->node.subscribe("/mobile_base/sensors/core", 100, &RoboState::bumperCallback, this);
  // subscribes to encoders
  this->odomSubscriber = this->node.subscribe("/odom", 100, &RoboState::odomCallback, this);
}

void RoboState::odomCallback(const nav_msgs::Odometry::ConstPtr& msg)
{
  
  // set odometry
  xOdom = msg->pose.pose.position.x;
  yOdom = msg->pose.pose.position.y;
  
  // convert from quaternions to angles
  tf::Quaternion q(msg->pose.pose.position.x, msg->pose.pose.position.y, msg->pose.pose.orientation.z,msg->pose.pose.orientation.w);
  tf::Matrix3x3 m(q);
  double roll, pitch, tempYaw;
  m.getRPY(roll, pitch, tempYaw);

  // determine whether to send positive or negative angles
  bool isTempYawNeg = (tempYaw < 0);
  
  // we always want a positive yaw value to be consistent
  if(getY() >= 0){
    if(isTempYawNeg)
      setYaw(2*YAW_180+tempYaw);
    else
      setYaw(tempYaw);
  }
  // we always want a negative yaw value to be consistent
  else{
    if(isTempYawNeg)
      setYaw(tempYaw);
    else
      setYaw(-2*YAW_180+tempYaw);
  }
  count++;
  if(count % 100==1){
  ROS_INFO("The yaw was %f", getYaw()*ANGLE_CONVERT);

  if(getY() >= 0)
    ROS_INFO("The yaw is supposed to be positive.");
  else
    ROS_INFO("The yaw is supposed to be negative.");
  }
}

// face the final destination, which is determined by yawGoal
void RoboState::faceDestination()
{
  ROS_INFO("Calling face destination.");
  // how much we are off the goal
  double yawOffset = getYawGoal() - getYaw();

  // means that we are off by more than 90 degrees
  if(yawOffset >= YAW_180/2) {
    ROS_INFO("We are turning left because we were off by %f)", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be more than 90 degrees)");
    rotateLeft();
  }
  // means that we are off by less than -90 degrees
  else if(yawOffset <= -YAW_180/2){
    ROS_INFO("We are turning right because yawOffset was %f", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be less than -90 degrees)");
    rotateRight();
  }
  // means that we are off by between 90 and -90 degrees
  else if(yawOffset>= yawErr || yawOffset <= -yawErr){
    ROS_INFO("We are turning less because yawOffset was %f", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be between 90 and -90 degrees)");
    usleep(MOVEMENT_INTERVAL);
    this->velocityCommand.linear.x = 0;
    this->velocityCommand.angular.z = yawOffset*ANGLE_CONVERT*LEFT_90*LEFT_90;
    velocityPublisher.publish(this->velocityCommand);
	  
  }
  else{ // means are we are close enough to facing 180 degrees 
    ROS_INFO("We should be facing %f degrees.", getYawGoal()*ANGLE_CONVERT);
    setRotated(true);
  }
}

// means that we need to face backwards, since initial x was negative
// and that initial y was positive
void RoboState::turn_180()
{

  ROS_INFO("Calling turn_180()");
  // yaw is always positive, so we have to subtrace from positive 180 degrees
  double yawOffset = 0;
  
  // compensate for whether yaw is positive or negative
  if(getY() >= 0)
    yawOffset = YAW_180 - getYaw();
  else
    yawOffset = -YAW_180 - getYaw();


  // means that we are off by more than 90 degrees
  if(yawOffset >= YAW_180/2){
    ROS_INFO("We are turning because we were off by %f)", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be more than 90 degrees)");
    rotateLeft();
  }
  // means that we are off by less than -90 degrees
  else if(yawOffset <= -YAW_180/2){
        ROS_INFO("We are turning right because yawOffset was %f", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be less than -90 degrees)");
    rotateRight();
  }
  // means that we are off by between 90 and -90 degrees
  if(yawOffset>= yawErr || yawOffset* <= -yawErr){
    ROS_INFO("We are turning less because yawOffset was %f", yawOffset*ANGLE_CONVERT);
    ROS_INFO("(Should be between 90 and -90 degrees)");
    usleep(MOVEMENT_INTERVAL);
    this->velocityCommand.linear.x = 0.0;
    this->velocityCommand.angular.z = yawOffset*ANGLE_CONVERT*LEFT_90/90;
    velocityPublisher.publish(this->velocityCommand);

  }
  else{ // means that we are basically facing backwards, with a margin of yawErr
    ROS_INFO("We should have turned 180 degrees backwards.");
    setTurnNegX(true);
  }
}

/*
void RoboState::goRobotGo()
{
  if(isMessageSet()){

    if(getInitialXnegative()){
      if(!getTurnNegX()){
	ROS_INFO("Turning 180 degrees.");
	turn_180();
      }
      else if(!getRotated()){
	ROS_INFO("Moving forward in -X.");
	goForwardX();
      }
      else if(getRotated()){
	ROS_INFO("Moving forward in Y.");
	goForwardY();
      }
    } // end if
    else{
      if(!getRotated()){
	ROS_INFO("Moving forward in X.");
	goForwardX();
      }
      else if(getRotated()){
	ROS_INFO("Moving forward in Y.");
	goForwardY();
	}
    } // end else
  } // end if

} // end goRobotGo
*/

  // assumes that x is zero and y is nonzero. May want to add cases for when method is called in error
void RoboState::determineYawGoal()
{
  ROS_INFO("Switching x and y coordinates.");
  usleep(MOVEMENT_INTERVAL);

  if( getY() > 0){ // means that destination is on right
    setYawGoal(YAW_180/2);
    ROS_INFO("Our yaw goal is %f", getYaw());
  }
  else if ( getY() < 0){ // means that destination is on right
    setYawGoal(-YAW_180/2);
  }
  else 
    setYawGoal(0); // means that y is zero

  if(getY() > 0){
    ROS_INFO("Your y-coordinate is positive.");
  }
  else if(getY() < 0){
    ROS_INFO("Your y-coordinate is negative.");
  }
  else{
    ROS_INFO("Your y-coordinate is zero.");
  }
}

void RoboState::goForwardX()
{

  if(getX() <= -INCREMENT_AMT || getX() >= INCREMENT_AMT )
    {
      double xMoveCommand; 
      usleep(MOVEMENT_INTERVAL);
      xMoveCommand = INCREMENT_AMT*MOVEMENT_MULTIPLE;
      this->velocityCommand.linear.x = xMoveCommand;
      this->velocityCommand.angular.z = 0.0;
      velocityPublisher.publish(this->velocityCommand);
      double currentXodom = getXodom();
      double amountMoved = currentXodom-getXodomOld();
      setX(getX()-amountMoved);
      setXodomOld(currentXodom);
      //      usleep(MOVEMENT_INTERVAL/10);
      ROS_INFO("We moved %f", amountMoved);
      ROS_INFO("The remaining amount to move is %f", getX());
    }
  else if(getX() <= getErr() && getX() >= -getErr()){
    faceDestination();
  }
  else{
    usleep(MOVEMENT_INTERVAL);
    double xMoveCommand = abs(getX())*MOVEMENT_MULTIPLE;
    this->velocityCommand.linear.x = xMoveCommand;
    this->velocityCommand.angular.z = 0.0;
    velocityPublisher.publish(this->velocityCommand);
    ROS_INFO("We moved forward %f", getX());
    double currentXodom = getXodom();
    double amountMoved = currentXodom-getXodomOld();
    setX(getX()-amountMoved);
    setXodomOld(currentXodom);
    //    usleep(MOVEMENT_INTERVAL/10);
    ROS_INFO("The remaining amount to move is %f (should be around zero)", getX());
  }
}
	      
void RoboState::goForwardY()
{
 
  if(getY() <= -INCREMENT_AMT || getY() >= INCREMENT_AMT )
    {
      ROS_INFO("Moving forward by INCREMENT_AMT.");
      double xMoveCommand; 
      usleep(MOVEMENT_INTERVAL);
      xMoveCommand = INCREMENT_AMT*MOVEMENT_MULTIPLE;
      this->velocityCommand.linear.x = xMoveCommand;
      this->velocityCommand.angular.z = 0.0;
      velocityPublisher.publish(this->velocityCommand);
      double currentYodom = getYodom();
      double amountMoved = currentYodom-getYodomOld();
      setY(getY()-amountMoved);
      setYodomOld(currentYodom);
      usleep(50000);
      ROS_INFO("We moved %f", amountMoved);
      ROS_INFO("The remaining amount to move is %f", getY());
    }
  else if(getX() <= getErr() && getX() >= -getErr()){
    ROS_INFO("We are done with y-movement.");
    setMessageStatus(false);
  }
  else{
    ROS_INFO("Moving forward by less than INCREMENT_AMT.");
    double xMoveCommand = abs(getY())*MOVEMENT_MULTIPLE;
    usleep(MOVEMENT_INTERVAL);
    this->velocityCommand.linear.x = xMoveCommand;
    this->velocityCommand.angular.z = 0.0;
    velocityPublisher.publish(this->velocityCommand);
    double currentYodom = getXodom();
    double amountMoved = currentYodom-getYodomOld();
    setY(getY()-amountMoved);
    setYodomOld(currentYodom);
    ROS_INFO("The remaining amount to move is %f (should be around zero)", getY());
  }
}

// Change the value of Movement multiple until turtlebot moves forward by INCREMENT_AMT.
// And stops for MOVEMENT_INTERVAL.
void RoboState::testForward()
{
  // may need to adjust value for whatever reason
  usleep(MOVEMENT_INTERVAL);
  double xMoveCommand; 
  // only move forward INCREMENT_AMT if the amount left to move is greater than INCREMENT_AMT

  // ideally, this should result in forward (or backward movement)
  // in the x direction by INCREMENT_AMT

  this->velocityCommand.linear.x = .1;
  this->velocityCommand.angular.z = 0;
	  
  velocityPublisher.publish(this->velocityCommand);
  // ideally, this is the amount that x has changed
  // we should wait until forward movement has finished before we go on
  usleep(MOVEMENT_INTERVAL);

}

void RoboState::messageCallback(const turtlebot::mymsg::ConstPtr& msg)
{

  if(!isMessageSet())
    {

      if(msg->x==0 && msg->y==0)
	ROS_INFO("No reason to move a distance of 0. Message not sent.");
      else{	  
	ROS_INFO("X and Y coordinates sent were: x:%f y:%f", msg->x, msg->y);
	setX(msg->x);
	setY(msg->y);
	ROS_INFO("xCoord is: %f. yCoord is: %f", getX(), getY());
	setMessageStatus(true);
	setRotated(false);
	if(getX() >= 0){
	  setInitialXnegative(false);
	  setTurnNegX(false);
	}
	else{
	  setInitialXnegative(true);
	  setTurnNegX(false);
	}
	//setErr(sqrt(pow(getX(),2)+pow(getY(),2))*.1);
	setErr(.1);
	setRotated(false);
      }
      // need to determine what direction we will ultimately face
      determineYawGoal();

    }
  else
    ROS_INFO("Cannot accept message. Movement still in progress.");

}

void RoboState::bumperCallback(const create_node::TurtlebotSensorState::ConstPtr& msg)
{
  // if bumpers don't complain, don't run the loop
  if(msg->bumps_wheeldrops != 0){
    ROS_INFO("You hit an object! Motion terminating.");
    ROS_INFO("The remaining x was:%f and the remaining y was: %f.", getX(), getY());
    setX(0);
    setY(0);
    // allow RoboState to receive messages again
    setMessageStatus(false);
  }
}
  
// we use this because it has been calibrated to turn 90 degrees left
void RoboState::rotateLeft()
{
  ROS_INFO("Rotating left.");
  usleep(MOVEMENT_INTERVAL);
  this->velocityCommand.linear.x = 0.0;
  this->velocityCommand.angular.z = LEFT_90;
  velocityPublisher.publish(this->velocityCommand);
  usleep(MOVEMENT_INTERVAL/5);
}

// we use this because it has been calibrated to turn 90 degrees right
void RoboState::rotateRight()
{
  ROS_INFO("Rotating right.");
  usleep(MOVEMENT_INTERVAL);
  this->velocityCommand.linear.x = 0.0;
  this->velocityCommand.angular.z = RIGHT_90;
  velocityPublisher.publish(this->velocityCommand);
  usleep(MOVEMENT_INTERVAL/5);
}

void RoboState::setMessageStatus(bool status)
{
  messageStatus=status;
}

bool RoboState::isMessageSet()
{
  return messageStatus;
}
  
void RoboState::incrementX(double x)
{
  xCoord += x;
}

void RoboState::incrementY(double y)
{
  yCoord += y;
}
  
double RoboState::getX()
{
  return xCoord;
}

void RoboState::setX(double x)
{
  xCoord=x;
}

void RoboState::setY(double y)
{
  yCoord=y;
}

double RoboState::getY()
{
  return yCoord;
}

double RoboState::getXodom()
{
  return xOdom;
}

double RoboState::getYodom()
{
  return yOdom;
}

void RoboState::setXodom(double xOdomCurrent)
{
  xOdom = xOdomCurrent;
}

void RoboState::setYodom(double yOdomCurrent)
{
  yOdom = yOdomCurrent;
}

double RoboState::getYodomOld()
{
  return yOdomOld;
}

double RoboState::getXodomOld()
{
  return xOdomOld;
}

void RoboState::setYodomOld(double yOdomCurrent)
{
  yOdomOld = yOdomCurrent;
}

void RoboState::setXodomOld(double xOdomCurrent)
{
  xOdomOld = xOdomCurrent;
}

double RoboState::getErr()
{
  return acceptErr;
}

void RoboState::setErr(double err)
{
  acceptErr = err;
}

bool RoboState::getRotated()
{
  return rotated;
}

void RoboState::setRotated(bool rotatedState)
{
  rotated = rotatedState;
}

void RoboState::setInitialXnegative(bool initialXstatus)
{
  initXneg = initialXstatus;
}

bool RoboState::getInitialXnegative()
{
  return initXneg;
}

bool RoboState::getTurnNegX()
{
  return negXturn;
}

void RoboState::setTurnNegX(bool turnStatus)
{
  negXturn = turnStatus;
}


void RoboState::setYaw(double newYaw)
{
  yaw = newYaw;
}

double RoboState::getYaw()
{
  return yaw;
}

void RoboState::setYawGoal(double newYawGoal)
{
  yawGoal = newYawGoal;
}

double RoboState::getYawGoal()
{
  return yawGoal;
}

#endif
