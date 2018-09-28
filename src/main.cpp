
#include "ros/ros.h"
#include "sensor_msgs/Joy.h"
#include "geometry_msgs/Twist.h"
#include <time.h>
#include <math.h>

#include "std_msgs/Float32MultiArray.h"
#include "std_msgs/MultiArrayLayout.h"
#include "std_msgs/MultiArrayDimension.h"

//constant setup variabels change thise values here
#define NODE_NAME "engine_mgmt"
#define JOY_SUB_NODE "joy"
#define ADVERTISE_POWER "motor_power" //publichng chanel
#define POWER_BUFFER_SIZE 200
#define SUBSCRIBE_ENCODER "wheel_velocity"
#define ENCODER_BUFFER_SIZE 5
#define LOOP_FREQ 20

//#define PUSH_SPEED 250 // minimal ms betvin toggel buton register
#define TIME_OUT 500 // if no joy msg arives in thise time (ms) will it stop TODO test

// joy msg->axes array lay out; for a x-box 360 controller
// [left stik RL(0) , left stik upp/down(1), LT(2) ,right stik RL(3) , right stik upp/down(4) , RT(5) , pad RL(6), pad upp/down(7) ]
// joy msg->button array lay out
// [ A, B, X, Y, LB, RB, back, start, XBOX, L-stik, R-stik]


// message deglaraions
geometry_msgs::Twist vel_msg;
geometry_msgs::Twist pwr_msg;
ros::Publisher motor_power_pub;

std_msgs::Float32MultiArray wheel_velocities;

// TODO maybi change to a strukt. Global = bad!
float speed_referens = 0;
float stering_referens = 0;
float current_L_vel = 0;
float current_R_vel = 0;

bool stop_button = false;

bool init_joy = false;
bool l_triger = false;
bool r_triger = false;

int s_singel_press = 0;
double joy_timer;

void pubEnginePower();
void sterToSpeedBallanser();
void setVelMsg();
void encoderCallback(const std_msgs::Float32MultiArray::ConstPtr& array);

// reseves new data from joy
// TODO stor data and macke calebrations for mor fansy controling befor publiching
void joyCallback(const sensor_msgs::Joy::ConstPtr& msg)
{
  // read input form joj a start up securety
	// to start runing tirger bout trigers ones
	if (init_joy)
	{
		speed_referens = 50 * (-msg->axes[5] + msg->axes[2]);
	
		stering_referens = msg->axes[0]; 
		if (stering_referens >= 0) stering_referens = pow(stering_referens, 3);
		else stering_referens = pow(stering_referens, 3);
		stering_referens = stering_referens * 100;
	}
	else
	{
		speed_referens = 0;
		if (msg->axes[5] != 0.0) r_triger = true;
		if (msg->axes[2] != 0.0) l_triger = true;
		if (r_triger && l_triger) init_joy = true;
	}		

  //test if button hav ben presed
	if (msg->buttons[0] == 1 && s_singel_press != msg->buttons[0])
 	{
		stop_button = !stop_button;
 	}

	s_singel_press = msg->buttons[0];
 	joy_timer = clock();
	
	sterToSpeedBallanser();
	
}

// enshor that the speed dont exsit full power
void sterToSpeedBallanser(){
	while (abs(speed_referens) + abs(stering_referens) > 100){
		if (speed_referens > 0) speed_referens--;
		else speed_referens++;
		if (stering_referens > 0) stering_referens--;
		else stering_referens++;
	}
	return;
}

void encoderCallback(const std_msgs::Float32MultiArray::ConstPtr& array)
{
	/* idea: reference for both wheels come from joy, processed and globally declared variables
	*			The encoders provide feedvack in this function, where we then call a PID function for 
	*			each wheel independently,
	*			Then we publish the new motor powers.
	*/

	current_L_vel = array->data[0];
	current_R_vel = array->data[1];
	//ROS_INFO("L_speed %f\n",current_L_vel);

}

// set a the new walues to the messge
void setVelMsg(){
	
	if (((joy_timer - clock()) < TIME_OUT) && !stop_button)
	{
		// changes if in revers to not hav inverted stering in revers
		if (speed_referens < 0) stering_referens = -stering_referens;
		vel_msg.linear.x = speed_referens - stering_referens;
		vel_msg.linear.y = speed_referens + stering_referens;

	}
	else
	{
		vel_msg.linear.x = 0;
		vel_msg.linear.y = 0;
	}
	return;
}

// publiche when new stering diraktions is set
void pubEnginePower()
{
	
	motor_power_pub.publish(pwr_msg);
	return;
}

int main(int argc, char **argv)
{

  ros::init(argc, argv, NODE_NAME);

  ros::NodeHandle n;
  ros::Rate loop_rate(LOOP_FREQ);
  
  //set upp comunication chanels
  // TODO add the other chanels
  motor_power_pub = n.advertise<geometry_msgs::Twist>(ADVERTISE_POWER, POWER_BUFFER_SIZE);
  ros::Subscriber joysub = n.subscribe<sensor_msgs::Joy>(JOY_SUB_NODE, POWER_BUFFER_SIZE, joyCallback); 
  ros::Subscriber wheel_vel_sub = n.subscribe<std_msgs::Float32MultiArray>(SUBSCRIBE_ENCODER, ENCODER_BUFFER_SIZE, encoderCallback);
  //ros::spin();

  /* Data we have: vel_msg.linear.x - wanted speeds on wheels, ie r
  *  		   current_L_vel, ie y
  */


  float Le=0, Re=0, Lle=0, Rle=0, Lea=0, Rea=0, P=2, D=0, I=0.01, uL=0, uR=0;
  float updatefreq = LOOP_FREQ;

  while(ros::ok()){

  	setVelMsg();
	
	//PID

	Lle = Le;
	Rle = Re;
	Le =  vel_msg.linear.x - current_L_vel;
	Re =  vel_msg.linear.y - current_R_vel;
	Lea = Le+Lea;
	Rea = Re+Rea;
	uL = P*Le + D*updatefreq*(Le-Lle) + I*Lea;
	uR = P*Re + D*updatefreq*(Re-Rle) + I*Rea; 

	if(uL>100) uL=100;
	else if(uL<-100) uL=-100;
	if(uR>100) uR=100;
	else if(uR<-100) uR=-100;
	pwr_msg.linear.x = uL;
	pwr_msg.linear.y = uR;	

	//ROS_INFO("Le: %f, Lle: %f, Lea: %f",Le, Lle, Lea);

	pubEnginePower();
  	ros::spinOnce();
  	loop_rate.sleep();

  }

  return 0;
}

// %EndTag(FULLTEXT)%
