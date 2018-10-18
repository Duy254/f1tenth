#include <vector>
#include <math.h> /* floor, abs */
#include <queue>
#include <algorithm> /* min, min_element, max_element */

#include "ros/ros.h"
#include "ros/console.h"
#include <std_msgs/UInt16.h>
#include <std_msgs/UInt32.h>
#include <std_msgs/Float32.h>

#include "ultimate_racer_in_cpp3.h"


#define PI_BY_180 3.14159265 / 180


// TODO: add `clip` template function


UltimateRacer::UltimateRacer(
  ros::NodeHandle* nodehandle,
  int slow_esc,
  int medium_esc,
  int fast_esc,
  float drive_medium_thr=3.5,
  float drive_fast_thr=5.0
) {

  nh = *nodehandle;
  throttle = 1500;
  yaw = YAW_MID;
  this->slow_esc = slow_esc;
  this->medium_esc = medium_esc;
  this->fast_esc = fast_esc;
  this->drive_medium_thr = drive_medium_thr;
  this->drive_fast_thr = drive_fast_thr;

  estop = false;
  estart = false;
  ego = false;

  // TODO(MD): throw away once not needed
  // this->kp = kp;
  // this->kd = kd;
  // prev_error = 0.0;

  curr_speed = 0.0;
  speed_record = 0.0;

  float angle_step = 1. / STEPS_PER_DEGREE;
  for (int i=0; i<NUM_ANGLES_TO_STORE; i++)
    sin_alpha[i] = std::sin((i+1)*angle_step * PI_BY_180);

  old_time_ = ros::Time::now();
  last_stop_msg_ts = ros::Time::now().toSec();

  pub_esc = nh.advertise<std_msgs::UInt16>(
    "/esc",
    1
  );

  pub_servo = nh.advertise<std_msgs::UInt16>(
    "/servo",
    1
  );

  sub_spd = nh.subscribe(
    "/spd",
    1,
    &UltimateRacer::spd_cb,
    this
  );

  sub_scan = nh.subscribe(
    "/scan",
    1,
    &UltimateRacer::scan_cb,
    this,
    ros::TransportHints().tcpNoDelay()
  );

  sub_estop = nh.subscribe(
    "/eStop",
    10,
    &UltimateRacer::estop_cb,
    this
  );
}

void UltimateRacer::spd_cb(const std_msgs::Float32 & data) {
  curr_speed = data.data;
}

void UltimateRacer::calc_stats(int best_idx, float best_scan) {
  mean_best_idx = add_and_calc_mean(prev_best_idx, 1.0*best_idx);
  mean_best_scan = add_and_calc_mean(prev_best_scan, best_scan);
}

void UltimateRacer::add_to_deque(std::deque<float> & q, float el) {
  if (q.size() == deque_len)
    q.pop_back();
  q.push_front(el);
}

float UltimateRacer::add_and_calc_mean(std::deque<float> & q, float el) {
  add_to_deque(q, el);
  float mean = 0;
  for (auto & a : q)
    mean += a;
  return mean / deque_len;
}

void UltimateRacer::scan_cb(const sensor_msgs::LaserScan & data) {
  time_ = ros::Time::now();
  if (time_.toSec() - last_stop_msg_ts > 0.5 && (ego || estart))
    exec_estop();

  // Finding the smallest range near the center
  int mid_point = floor(data.ranges.size() / 2.);
  int margin = 20; // TODO: make this a parameter
  float min_central_value = *std::min_element(
    data.ranges.begin() + mid_point - margin,
    data.ranges.begin() + mid_point + margin
  );
  if (min_central_value < SAFE_OBSTACLE_DIST3) {
    throttle = ESC_BRAKE;
    tmp_uint16.data = throttle;
    pub_esc.publish(tmp_uint16);
  }

  // Clip the values
  std::vector<float> scan;
  float one_scan;
  for (int i=0; i<data.ranges.size(); i++) {
    one_scan = data.ranges[i];
    one_scan = one_scan < 0.06 ? 0.06 : one_scan > 10 ? 10 : one_scan;
    scan.push_back(one_scan);
  }
  for (int i=0; i<90; i++)
    scan[i] = 10.0;
  for (int i=scan.size()-90; i<scan.size(); i++)
    scan[i] = 10.0;
  // TODO: the above three loops can be squashed into one

  int idx = steerMAX(scan, MARGIN_DRIVE_FAST);

  // Gather stats
  calc_stats(idx, scan[idx]);

  if (idx == -1 || scan[idx] < drive_fast_thr || idx > 600 || idx < 480) {
    // TODO: parametrize those: 600 and 480 as: MID +/- 60
    // MID should be controlled through a topic of some sorts (or parameter)
    idx = steerMAX(scan, MARGIN_DRIVE_SLOW);
    if (idx == -1)
      throttle = ESC_BRAKE;
    else if (idx >= 0 && scan[idx]  < SAFE_OBSTACLE_DIST4)
      idx = -1;
    else {
      if (scan[idx] > drive_medium_thr)
        throttle = medium_esc;
      else
        throttle = slow_esc;
    }
  } else {
    // The road ahead is clear
    throttle = fast_esc;
  }

  float idx2yaw;
  if (idx >= 0) {
    idx2yaw = 1.0 * idx / scan.size() - 0.5;
    // TODO(MD): parametrize 1.2
    idx2yaw = 1.4*idx2yaw;
    idx2yaw = idx2yaw < -0.5 ? -0.5 : idx2yaw > 0.5 ? 0.5 : idx2yaw;
    yaw = floor(idx2yaw * YAW_RANGE + YAW_MID);
  }

  if (estart && !estop and idx >= 0) {
    tmp_uint16.data = throttle;
    pub_esc.publish(tmp_uint16);
    tmp_uint16.data = yaw;
    pub_servo.publish(tmp_uint16);
  } else {
    throttle = ESC_BRAKE;
    tmp_uint16.data = throttle;
    pub_esc.publish(tmp_uint16);
  }

  if (curr_speed > speed_record)
    speed_record = curr_speed;

  // Log everything
  double delta_between_callbacks = time_.toSec() - old_time_.toSec();
  double delta_within_callback = ros::Time::now().toSec() - time_.toSec();
  ROS_WARN(
    "y: %d t: %d <scan>: %.2f <idx>: %.2f dt_bet_cb: %.4f dt_in_cb: %.4f",
    yaw, throttle, mean_best_scan, mean_best_idx, delta_between_callbacks, delta_within_callback
  );

  old_time_ = time_;
}


void UltimateRacer::estop_cb(const std_msgs::UInt16 & data) {
  last_stop_msg_ts = ros::Time::now().toSec();
  if (data.data == 0) {
    ROS_WARN("Emergency stop!");
    exec_estop();
  } else if (data.data == 2309) {
    ROS_WARN("GO!");
    estart = true;
  }
}


void UltimateRacer::exec_estop() {
  estop = true;
  yaw = YAW_MID;
  throttle = ESC_BRAKE;
  tmp_uint16.data = throttle;
  pub_esc.publish(tmp_uint16);
  tmp_uint16.data = yaw;
  pub_servo.publish(tmp_uint16);
}


float UltimateRacer::steerMAX(std::vector<float> & scan, float width_margin) {
  float min_scan = *std::min_element(scan.begin(), scan.end());
  if (min_scan <= SAFE_OBSTACLE_DIST2)
    return -1;

  // The following actually copies the vector `scan`
  std::vector<float> scan2(scan);
  int idx = 0;
  bool is_reachable = false;
  int scan_size = scan.size();

  // First, prepare `segs`
  std::vector<int> segs = {180, scan_size-180};
  for (int i=1; i<scan_size; i++)
    if (std::abs(scan[i]-scan[i-1]) > NON_CONT_DIST) {
      segs.push_back(i);
      segs.push_back(i-1);
    }

  for (int i=0; i<100; i++)
    scan2[i] = -1;
  for (int i=scan2.size()-100; i<scan2.size(); i++)
    scan2[i] = -1;

  while (!is_reachable) {
    float max_value = *std::max_element(scan2.begin(), scan2.end());
    if (max_value <= 0)
      break;

    // Search for argmax (https://en.cppreference.com/w/cpp/algorithm/max_element)
    idx = std::distance(
      scan2.begin(),
      std::max_element(scan2.begin(), scan2.end())
    );

    for (auto s : segs) {
      if (s != idx) {
        bool could_be_reached = check_if_reachable(
          scan[idx],
          scan[s],
          std::abs(s-idx),
          width_margin
        );

        if (!could_be_reached) {
          int left_limit = std::max(0, idx-5);
          int right_limit = std::min(idx+5, int(scan2.size()-1));
          for (int i=left_limit; i<right_limit; i++)
            scan2[i] = -1;
          break;
        }
      }
    }
    // Instead of comparing to -1 (remember, we're dealing with floats),
    //  we check whether it's non-negative
    if (scan2[idx] >= 0)
      is_reachable = true;
  }

  if (is_reachable == false)
    idx = -1;

  return idx;
}


bool UltimateRacer::check_if_reachable(float r1, float r2, int alpha, float width_margin) {
  // if (SAFE_OBSTACLE_DIST1 < r1 < r2 + SAFE_OBSTACLE_DIST1)
  if (r1 - SAFE_OBSTACLE_DIST2 < r2 && r1 > SAFE_OBSTACLE_DIST2 )
    return true;
  else
    return (r2*sin_alpha[alpha] > width_margin);
}


int main(int argc, char **argv) {

  ros::init(argc, argv, "ultimate_racer_in_cpp2");

  ros::NodeHandle nh;
  int slow_esc;
  int medium_esc;
  int fast_esc;
  float drive_medium_thr;
  float drive_fast_thr;

  if (argc == 6) {
    slow_esc = atoi(argv[1]);
    medium_esc = atoi(argv[2]);
    fast_esc = atoi(argv[3]);
    drive_medium_thr = atof(argv[4]);
    drive_fast_thr = atof(argv[5]);
  } else {
    slow_esc = 1555;
    medium_esc = 1560;
    fast_esc = 1565;
    drive_medium_thr = 3.5;
    drive_fast_thr = 5.0;
  }

  std::cout << "slow_esc: " << slow_esc
            << " medium_esc: " << medium_esc
            << " fast_esc: " << fast_esc
            << " drive_medium_thr: " << drive_medium_thr
            << " drive_fast_thr: " << drive_fast_thr
            << std::endl;

  UltimateRacer racer(
    &nh,
    slow_esc, medium_esc, fast_esc,
    drive_medium_thr=drive_medium_thr, drive_fast_thr=drive_fast_thr
  );

  ros::Rate loop_rate(40);

  ros::spin();

  return 0;
}
