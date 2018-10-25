
#include "ros/ros.h"
#include "sensor_msgs/Joy.h"
#include "geometry_msgs/Twist.h"
#include <sys/timeb.h>
#include <math.h>

#include "std_msgs/Float32MultiArray.h"
#include "std_msgs/MultiArrayLayout.h"
#include "std_msgs/MultiArrayDimension.h"
#include "std_msgs/Bool.h"

//constant setup variables change those values here
#define NODE_NAME "engine_mgmt"
#define JOY_SUB_NODE "joy"
#define STOP_SUB_NODE "lidar_stop"
#define ADVERTISE_POWER "motor_power" //publishing channel
#define SUBSCRIBE_ENCODER "wheel_velocity"

// joy msg->axes array layout; for a x-box 360 controller
// [left stick RL(0) , left stick up/down(1), LT(2) ,right stick RL(3) , right stick up/down(4) , RT(5) , pad RL(6), pad up/down(7) ]
// joy msg->button array layout
// [ A, B, X, Y, LB, RB, back, start, XBOX, L-stick, R-stick]


// message declarations
geometry_msgs::Twist pwr_msg;
ros::Publisher motor_power_pub;

std_msgs::Float32MultiArray wheel_velocities;

// setings
int POWER_BUFFER_SIZE;
int ENCODER_BUFFER_SIZE;
int LOOP_FREQ;
int TIME_OUT;
// PID param
float P;
float I;
float D;


// TODO maybi change to a strukt. Global = bad!
float speed_reference = 0;
float steering_reference = 0;
float current_L_vel = 0;
float current_R_vel = 0;

int joy_timer;

bool coll_stop;
bool emergency_override;

struct toggleButton {
	bool on;
	bool previews;
};
struct toggleButton handbreak = {.on = false, .previews = false}; // A 
struct toggleButton startUp = {.on = true, .previews = false}; // start


void pubEnginePower();
void encoderCallback(const std_msgs::Float32MultiArray::ConstPtr& array);
void joyCallback(const sensor_msgs::Joy::ConstPtr& msg);
float inputSens(float ref);
void toggleButton(int val, struct toggleButton *b);
void emergencyStop(float *Le, float *Re, float *Lle, float *Rle, 
					float *Lea, float *Rea, float *uL, float *uR);
// temporary functions might change or be replaced when real controller implements
void sterToSpeedBalancer();
void controlerStandIn();
void setVelMsg();
void PID(float *Le, float *Re, float *Lle, float *Rle, float *Lea,
				float *Rea, float *uL, float *uR, float updatefreq);
int getMilliCount();
int getMilliSpan(int nTimeStart);

int getMilliCount(){
	timeb tb;
	ftime(&tb);
	int nCount = tb.millitm + (tb.time & 0xfffff) * 1000;
	return nCount;
}

int getMilliSpan(int nTimeStart){
	int nSpan = getMilliCount() - nTimeStart;
	if(nSpan < 0)
		nSpan += 0x100000 * 1000;
	return nSpan;
}

// receives new data from joy
void joyCallback(const sensor_msgs::Joy::ConstPtr& msg)
{
  // read input form joy
	speed_reference = 50 * (-msg->axes[5] + msg->axes[2]);
	steering_reference = inputSens(msg->axes[0]); 

  	//test if button have been pressed
	toggleButton(msg->buttons[0], &handbreak);
 	toggleButton(msg->buttons[7], &startUp);
	emergency_override = (msg->buttons[1] == 1); //B Button

	joy_timer = getMilliCount();
	if (speed_reference < 0) steering_reference = -steering_reference;
	ROS_INFO("joy callback");
}

void stopCallback(const std_msgs::Bool::ConstPtr& lidar_stop)
{
	coll_stop = lidar_stop->data;
}

// adjust input sensitivity
float inputSens(float ref){
	return pow(ref, 3) * 100;
}

// loadig to handle buttons that should toggle
void toggleButton(int val, struct toggleButton *b){
		if (val == 1 && !b->previews)
			b->on = !b->on;
		b->previews = (val == 1);
}

// ensure that the speed don't exist full power
void sterToSpeedBalancer(){
	while (abs(speed_reference) + abs(steering_reference) > 100){
		if (speed_reference > 0) speed_reference--;
		else speed_reference++;
		if (steering_reference > 0) steering_reference--;
		else steering_reference++;
	}
	return;
}

void encoderCallback(const std_msgs::Float32MultiArray::ConstPtr& array)
{
	/* idea: reference for both wheels come from joy, processed and globally declared variables
	*			The encoders provide feedback in this function, where we then call a PID function for 
	*			each wheel independently,
	*			Then we publish the new motor powers.
	*/

	current_L_vel = array->data[0];
	current_R_vel = array->data[1];
	//ROS_INFO("L_speed %f\n",current_L_vel);

}

// set the new values to the message
void setVelMsg(){
	
	if ((getMilliSpan(joy_timer) < TIME_OUT) && !startUp.on && (!coll_stop || emergency_override))
	{
		// changes if in reverse to not have inverted steering in reverse
		pwr_msg.linear.x = speed_reference - steering_reference;
		pwr_msg.linear.y = speed_reference + steering_reference;

	}
	else
	{
		pwr_msg.linear.x = 0;
		pwr_msg.linear.y = 0;
	}
	bool temp = getMilliSpan(joy_timer) < TIME_OUT;
	ROS_INFO("msg_ref_set pre control X: %f, Y: %f, strtUp: %d, coll: %d, overtide %d, clock_out: %d", pwr_msg.linear.x, pwr_msg.linear.y, startUp.on, coll_stop, emergency_override, temp);
	return;
}

// Emergency stop force to stop
void emergencyStop(float *Le, float *Re, float *Lle, float *Rle, 
					float *Lea, float *Rea, float *uL, float *uR){
	if (handbreak.on){
		pwr_msg.linear.x = 0;
		pwr_msg.linear.y = 0;
		*Le = 0; *Re = 0; *Lle = 0; *Rle = 0; 
		*Lea = 0; *Rea = 0; *uL = 0; *uR = 0;
	}
}

// publish when new steering directions are set
void pubEnginePower()
{
	motor_power_pub.publish(pwr_msg);
	return;
}

// PID
void PID(float *Le, float *Re, float *Lle, float *Rle, float *Lea,
		 float *Rea, float *uL, float *uR, float updatefreq){

	sterToSpeedBalancer();
  	setVelMsg();
	
	//PID

	*Lle = *Le;
	*Rle = *Re;
	*Le =  pwr_msg.linear.x/100 - current_L_vel;
	*Re =  pwr_msg.linear.y/100 - current_R_vel;
	
	//	if Lea+Le
	*Lea = *Le + *Lea;
	*Rea = *Re + *Rea;
	*uL = P * *Le + D * updatefreq * (*Le- *Lle) + I* *Lea;
	*uR = P * *Re + D * updatefreq * (*Re- *Rle) + I* *Rea; 

	if(*uL>100) *uL=100;
	else if(*uL<-100) *uL=-100;
	if(*uR>100) *uR=100;
	else if(*uR<-100) *uR=-100;

	pwr_msg.linear.x = *uL + pwr_msg.linear.x * 0.5;
	pwr_msg.linear.y = *uR + pwr_msg.linear.y * 0.5;

	ROS_INFO("rL: %f, Le: %f, Lle: %f, Lea: %f",pwr_msg.linear.x, *Le, *Lle, *Lea);

}

// TODO temporary for testing
void controlerStandIn()
{
	sterToSpeedBalancer();
	setVelMsg();
}

int main(int argc, char **argv)
{

  ros::init(argc, argv, NODE_NAME);

  ros::NodeHandle n;
  ros::NodeHandle nh("~");
  

	nh.param<float>("P",P,70.0);
	nh.param<float>("I",I,15.0);
	nh.param<float>("D",D,0.0);
	nh.param<int>("power_buffer_size",POWER_BUFFER_SIZE,200);
	nh.param<int>("encoder_buffer_size",ENCODER_BUFFER_SIZE,5);
	nh.param<int>("loop_freq",LOOP_FREQ,20);
	nh.param<int>("time_out",TIME_OUT,500);

	//ROS_INFO("time_out %d", TIME_OUT);
	
  ros::Rate loop_rate(LOOP_FREQ);
//set up communication channels
  // TODO add the other channels
  motor_power_pub = n.advertise<geometry_msgs::Twist>(ADVERTISE_POWER, POWER_BUFFER_SIZE);
  ros::Subscriber joysub = n.subscribe<sensor_msgs::Joy>(JOY_SUB_NODE, POWER_BUFFER_SIZE, joyCallback); 
  ros::Subscriber wheel_vel_sub = n.subscribe<std_msgs::Float32MultiArray>(SUBSCRIBE_ENCODER, ENCODER_BUFFER_SIZE, encoderCallback);
  ros::Subscriber stop_sub = n.subscribe<std_msgs::Bool>(STOP_SUB_NODE, 1, stopCallback);
  //ros::spin();

	// for diagnostik print
  /* Data we have: vel_msg.linear.x - wanted speeds on wheels, ie r
  *  		   current_L_vel, ie y
  */


  float Le=0, Re=0, Lle=0, Rle=0, Lea=0, Rea=0, uL=0, uR=0;
  
  while(ros::ok()){

	// choos one stand in or controller
	controlerStandIn();	
	
//	PID(&Le, &Re, &Lle, &Rle, &Lea, &Rea, &uL, &uR, LOOP_FREQ);
	

	emergencyStop(&Le, &Re, &Lle, &Rle, &Lea, &Rea, &uL, &uR);
	pubEnginePower();
  	ros::spinOnce();
  	loop_rate.sleep();

  }

  return 0;
}

		// %EndTag(FULLTEXT)%
