// This file deals with the rover's ability to drop off cubes to the center collection disk
// There are only two forms of driving: precision driving and waypoints
// Precision Driving == any controller (drive, pickup, dropoff, obstacle)
// continously feeding data into the feedback loop needed for drive controls
// has more precise control over rover's movements, more accurate of less than 1cm

// Waypoint Driving == drive controller feeding one data point (waypoint coordinates)
// with an accuracy of at least 15cm

#include "DropOffController.h"

using namespace std;
DropOffController::DropOffController() {

  reachedCollectionPoint = false;
  // The result object is from the Result struct (see Result.h for more information)
  result.type = behavior;
  // The b is of the BehaviorTrigger enum
  result.b = wait;
  //result.wristAngle = 0.7;
  result.wristAngle = 1.0;
  result.reset = false;
  interrupt = false;

  circularCenterSearching = false;
  spinner = 0;
  centerApproach = false;
  seenEnoughCenterTags = false;
  prevCount = 0;

  // Number of tags viewed
  countLeft = 0;
  countRight = 0;
  pitches = 0.0;

  isPrecisionDriving = false;
  startWaypoint = false;
  timerTimeElapsed = -1;
 
  currentLocation.x = 0;
  currentLocation.y = 0;
  currentLocation.theta = 0; 
  returnTimer = 0;
}

DropOffController::~DropOffController() {

}

double DropOffController::getPoissonCDF(const double lambda)
{
  double sumAccumulator       = 1.0;
  double factorialAccumulator = 1.0;
  for (size_t i = 1; i <= local_resource_density; i++) {
    factorialAccumulator *= i;
    sumAccumulator += pow(lambda, i) / factorialAccumulator;
  }

  return (exp(-lambda) * sumAccumulator);
}

Result DropOffController::DoWork() {

  //cout << "DropOffController::DoWork() " << endl;
  // Getting the total tag count from the left and the right side of the rover
  int count = countLeft + countRight;

  // If the timer has started
  if(timerTimeElapsed > -1) {
    // Calcuate the elapsed time from the current time and the time since
    long int elapsed = current_time - returnTimer;
    timerTimeElapsed = elapsed/1e3; // Convert from milliseconds to seconds
  }
  else
  {
	  returnTimer = current_time;
	  timerTimeElapsed = 0;
	  }
  //if we are in the routine for exiting the circle once we have dropped a block off and reseting all our flags
  //to resart our search.
  if(reachedCollectionPoint)
  {
    if (timerTimeElapsed >= 12)
    {
      if (finalInterrupt)
      {
        result.type = behavior;
        result.b = nextProcess;
        result.reset = true;
        targetHeld = false; //qilu 02/2018
        return result;       
      }
      else
      {
        finalInterrupt = true;
        //cout << "TestTimeout: finalInterrupt, true" << endl;
      }
    }
    else if (timerTimeElapsed >= 3)
    {
      result.fingerAngle = M_PI_2; //open fingers and drop cubes
      result.pd.cmdVel = -0.15;
    }
    else
    {
      isPrecisionDriving = true;
      result.type = precisionDriving;
      result.wristAngle = 0; //raise wrist
      result.pd.cmdVel = 0.05;
      result.pd.cmdAngularError = 0.0;
    }

    return result;
  }

  // Calculates the shortest distance to the center location from the current location
  double distanceToCenter = hypot(this->centerLocation.x - this->currentLocation.x, this->centerLocation.y - this->currentLocation.y);

  //check to see if we are driving to the center location or if we need to drive in a circle and look.
  if (distanceToCenter > collectionPointVisualDistance && !circularCenterSearching && (count == 0)) {
    // Sets driving mode to waypoint
    result.type = waypoint;
    // Clears all the waypoints in the vector
    result.wpts.waypoints.clear();
    // Adds the current location's point into the waypoint vector
    result.wpts.waypoints.push_back(this->centerLocation);
    // Do not start following waypoints
    startWaypoint = false;
    // Disable precision driving
    isPrecisionDriving = false;
    // Reset elapsed time
    timerTimeElapsed = 0;
    SetCPFAState(return_to_nest);
    return result;

  }
  else if (timerTimeElapsed >= 2)//spin search for center
  {
    Point nextSpinPoint;

    //sets a goal that is 60cm from the centerLocation and spinner
    //radians counterclockwise from being purly along the x-axis.
    nextSpinPoint.x = centerLocation.x + (initialSpinSize + spinSizeIncrease) * cos(spinner);
    nextSpinPoint.y = centerLocation.y + (initialSpinSize + spinSizeIncrease) * sin(spinner);
    nextSpinPoint.theta = atan2(nextSpinPoint.y - currentLocation.y, nextSpinPoint.x - currentLocation.x);

    result.type = waypoint;
    result.wpts.waypoints.clear();
    result.wpts.waypoints.push_back(nextSpinPoint);

    spinner += 45*(M_PI/180); //add 45 degrees in radians to spinner.
    if (spinner > 2*M_PI) {
      spinner -= 2*M_PI;
    }
    spinSizeIncrease += spinSizeIncrement/8;
    circularCenterSearching = true;
    //safety flag to prevent us trying to drive back to the
    //center since we have a block with us and the above point is
    //greater than collectionPointVisualDistance from the center.

    //returnTimer = current_time; //qilu 03/2018 comment out for timeout of the dropoff
    //timerTimeElapsed = 0; //qilu 03/2018

  }

  bool left = (countLeft > 0);
  bool right = (countRight > 0);
  bool centerSeen = (right || left);

  //reset lastCenterTagThresholdTime timout timer to current time
  if ((!centerApproach && !seenEnoughCenterTags) || (count > 0 && !seenEnoughCenterTags)) {
    lastCenterTagThresholdTime = current_time;

  }

  if (count > 0 || seenEnoughCenterTags || prevCount > 0) //if we have a target and the center is located drive towards it.
  {

    //cout << "CPFAStatus: drive to center" << endl;
    centerSeen = true;

    if (first_center && isPrecisionDriving)
    {
      first_center = false;
      result.type = behavior;
      result.reset = false;
      result.b = nextProcess;
      return result;
    }
    isPrecisionDriving = true;

    if (seenEnoughCenterTags) //if we have seen enough tags
    {
	  if (pitches < -0.5) //turn to the left
      {
		left = true;  
        right = false; 
        }
      else if (pitches > 0.5)//turn to the right
      {
        left = false;
        right = true;
        }
    }
    else //not seen enough tags, then drive forward
    {
		left = false;
		right = false;
		}

    float turnDirection = 1;
    //reverse tag rejection when we have seen enough tags that we are on a
    //trajectory in to the square we dont want to follow an edge.
    if (seenEnoughCenterTags) turnDirection = -3;

    result.type = precisionDriving;

    //otherwise turn till tags on both sides of image then drive straight
    if (left && right) 
    {
	  result.pd.cmdVel = searchVelocity;
      result.pd.cmdAngularError = 0.0;
    }
    else if (right) 
    {
	  result.pd.cmdVel = -0.1 * turnDirection;
      result.pd.cmdAngularError = centeringTurnRate*turnDirection;
    }
    else if (left)
    {
      result.pd.cmdVel = -0.1 * turnDirection;
      result.pd.cmdAngularError = -centeringTurnRate*turnDirection;
    }
    else
    {
      result.pd.cmdVel = searchVelocity;
      result.pd.cmdAngularError = 0.0;
      }

    //must see greater than this many tags before assuming we are driving into the center and not along an edge.
    if (count > centerTagThreshold)
    {
      seenEnoughCenterTags = true; //we have driven far enough forward to be in and aligned with the circle.
      lastCenterTagThresholdTime = current_time;
    }
    /*if (count > 0) // Reset guard to prevent drop offs due to loosing tracking on tags for a frame or 2.
    {
      lastCenterTagThresholdTime = current_time;
    }*/
    //time since we dropped below countGuard tags
    long int elapsed = current_time - lastCenterTagThresholdTime;
    float timeSinceSeeingEnoughCenterTags = elapsed/1e3; // Convert from milliseconds to seconds

    //we have driven far enough forward to have passed over the circle.
    //if (count < 5 && seenEnoughCenterTags && timeSinceSeeingEnoughCenterTags > dropDelay) {
    if (seenEnoughCenterTags && timeSinceSeeingEnoughCenterTags > dropDelay) {
      centerSeen = false;
      //cout<<"not seen center"<<endl;
    }
    centerApproach = true;
    prevCount = count;
    count = 0;
    countLeft = 0;
    countRight = 0;
  }

  //was on approach to center and did not seenEnoughCenterTags
  //for lostCenterCutoff seconds so reset.
  else if (centerApproach) {

    long int elapsed = current_time - lastCenterTagThresholdTime;
    float timeSinceSeeingEnoughCenterTags = elapsed/1e3; // Convert from milliseconds to seconds
    if (timeSinceSeeingEnoughCenterTags > lostCenterCutoff)
    {
      //cout << "back to drive to center base location..." << endl;
      //go back to drive to center base location instead of drop off attempt
      reachedCollectionPoint = false;
      seenEnoughCenterTags = false;
      centerApproach = false;

      result.type = waypoint;
      result.wpts.waypoints.push_back(this->centerLocation);
      if (isPrecisionDriving) {
        result.type = behavior;
        result.b = prevProcess;
        result.reset = false;
      }
      isPrecisionDriving = false;
      interrupt = false;
      precisionInterrupt = false;
    }
    else
    {
      result.pd.cmdVel = searchVelocity;
      result.pd.cmdAngularError = 0.0;
    }

    return result;

  }
  if (!centerSeen && seenEnoughCenterTags)
  {
	  cout<<"TestStatusA: reach nest..."<<endl;
    reachedCollectionPoint = true;
    centerApproach = false;
    returnTimer = current_time;
  }

  return result;
}

void DropOffController::SetRoverInitLocation(Point location) 
{
  roverInitLocation = location;
  //cout<<"TestStatus: rover init location=["<<roverInitLocation.x<<","<<roverInitLocation.y<<"]"<<endl;
}


// Reset to default values
void DropOffController::Reset() {
	//cout<<"DropOffController::Reset()"<<endl;
  result.type = behavior;
  result.b = wait;
  result.pd.cmdVel = 0;
  result.pd.cmdAngularError = 0;
  result.fingerAngle = -1;
  //result.wristAngle = 0.7;
  result.wristAngle = 1.0;
  result.reset = false;
  //result.lay_pheromone = false;
  result.wpts.waypoints.clear();
  spinner = 0;
  spinSizeIncrease = 0;
  prevCount = 0;
  timerTimeElapsed = -1;

  countLeft = 0;
  countRight = 0;
  pitches = 0.0;

  returnTimer = 0;//qilu 03/2018
  //reset flags
  reachedCollectionPoint = false;
  seenEnoughCenterTags = false;
  circularCenterSearching = false;
  isPrecisionDriving = false;
  finalInterrupt = false;
  precisionInterrupt = false;
  targetHeld = false;
  startWaypoint = false;
  first_center = true;
  cpfa_state = start_state;
  
  

}


void DropOffController::SetTagData(vector<Tag> tags) {
  countRight = 0;
  countLeft = 0;
  pitches = 0.0;

  if(targetHeld) {
    // if a target is detected and we are looking for center tags
    if (tags.size() > 0 && !reachedCollectionPoint) {

      // this loop is to get the number of center tags
      for (int i = 0; i < tags.size(); i++) {
        if (tags[i].getID() == 256) {

          // checks if tag is on the right or left side of the image
          if (tags[i].getPositionX() + cameraOffsetCorrection > 0) 
          {
            countRight++;
          } 
          else 
          {
            countLeft++;
          }
          pitches += tags[i].calcPitch();
        }
      }
      pitches /= (countLeft + countRight);
    }
  }

}

// Sets the driving mode (precision or waypoint) depending on the
// number of tags seen on the left and the right side of the rover
void DropOffController::ProcessData() {
  // If there are tags seen
  if((countLeft + countRight) > 0) {
    isPrecisionDriving = true;
  } else {
    startWaypoint = true;
  }
}


bool DropOffController::ShouldInterrupt() {
  ProcessData();
  if (startWaypoint && !interrupt) {
    interrupt = true;
    precisionInterrupt = false;
    //cout<<"D: true d1"<<endl;
    return true;
  }
  else if (isPrecisionDriving && !precisionInterrupt) {
    precisionInterrupt = true;
    //cout<<"D: true d2"<<endl;
    return true;
  }
  if (finalInterrupt) {
	  //cout<<"D: true d3"<<endl;
    return true;
  }
}


bool DropOffController::HasWork() {
  // If the timer has started
  if(timerTimeElapsed > -1) {
    // Calcuate the elapsed time from the current time and the time since
    // it dropped a target (cube) to the center collection disk
    long int elapsed = current_time - returnTimer;
    timerTimeElapsed = elapsed/1e3; // Convert from milliseconds to seconds
  }

  if (circularCenterSearching && timerTimeElapsed < 2 && !isPrecisionDriving) {
    return false;
  }
   //cout <<"Dropoff has work..."<<(startWaypoint || isPrecisionDriving)<<endl;
  return ((startWaypoint || isPrecisionDriving));
}

// Checking function to see if the driving mode (precision or waypoint) has been changed
bool DropOffController::IsChangingMode() {
  return isPrecisionDriving;
}

// Setter function to set the center location (the collection disk)
// Of the Point class (x, y, theta)
void DropOffController::SetCenterLocation(Point center) {
  centerLocation = center;
}

// Setter function to set the current location of the Point class (x, y, theta)
void DropOffController::SetCurrentLocation(Point current) {
  currentLocation = current;
}

// Setter function to set the variable to true if a target (cube) has been picked up
// And that it is currently holding the target (cube)
void DropOffController::SetTargetPickedUp() {
  targetHeld = true;
}

// Setter function to stop the ultrasound from being blocked
// In other words, to block the ultrasound or not
void DropOffController::SetBlockBlockingUltrasound(bool blockBlock) {
  targetHeld = targetHeld || blockBlock;
}

// Setter function to set the current time (in milliseconds)
void DropOffController::SetCurrentTimeInMilliSecs( long int time )
{
  current_time = time;
}
CPFAState DropOffController::GetCPFAState() 
{
  return cpfa_state;
}

void DropOffController::SetCPFAState(CPFAState state) {
  cpfa_state = state;
  result.cpfa_state = state;
}


