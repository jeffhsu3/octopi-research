#include <PacketSerial.h>
#include <AccelStepper.h>
#include <Adafruit_DotStar.h>

#include "def_octopi.h"
//#include "def_gravitymachine.h"
//#include "def_squid.h"
//#include "def_platereader.h"
//#include "def_squid_vertical.h"

/***************************************************************************************************/
/***************************************** Communications ******************************************/
/***************************************************************************************************/
// byte[0]: which motor to move: 0 x, 1 y, 2 z, 3 LED, 4 Laser
// byte[1]: what direction: 1 forward, 0 backward
// byte[2]: how many micro steps - upper 8 bits
// byte[3]: how many micro steps - lower 8 bits

static const int CMD_LENGTH = 8;
static const int MSG_LENGTH = 24;
byte buffer_rx[512];
byte buffer_tx[MSG_LENGTH];
volatile int buffer_rx_ptr;
static const int N_BYTES_POS = 4;
byte cmd_id = 0;
bool mcu_cmd_execution_in_progress = false;

// command sets
static const int MOVE_X = 0;
static const int MOVE_Y = 1;
static const int MOVE_Z = 2;
static const int MOVE_THETA = 3;
static const int HOME_OR_ZERO = 5;
static const int MOVETO_X = 6;
static const int MOVETO_Y = 7;
static const int MOVETO_Z = 8;
static const int SET_LIM = 9;
static const int TURN_ON_ILLUMINATION = 10;
static const int TURN_OFF_ILLUMINATION = 11;
static const int SET_ILLUMINATION = 12;
static const int SET_ILLUMINATION_LED_MATRIX = 13;
static const int ACK_JOYSTICK_BUTTON_PRESSED = 14;
static const int ANALOG_WRITE_ONBOARD_DAC = 15;
static const int SET_LIM_SWITCH_POLARITY = 20;
static const int CONFIGURE_STEPPER_DRIVER = 21;
static const int SET_MAX_VELOCITY_ACCELERATION = 22;
static const int SET_LEAD_SCREW_PITCH = 23;
static const int SET_OFFSET_VELOCITY = 24;
static const int SEND_HARDWARE_TRIGGER = 30;
static const int SET_STROBE_DELAY = 31;

static const int COMPLETED_WITHOUT_ERRORS = 0;
static const int IN_PROGRESS = 1;
static const int CMD_CHECKSUM_ERROR = 2;
static const int CMD_INVALID = 3;
static const int CMD_EXECUTION_ERROR = 4;

static const int HOME_NEGATIVE = 1;
static const int HOME_POSITIVE = 0;
static const int HOME_OR_ZERO_ZERO = 2;

static const int AXIS_X = 0;
static const int AXIS_Y = 1;
static const int AXIS_Z = 2;
static const int AXIS_THETA = 3;
static const int AXES_XY = 4;

static const int BIT_POS_JOYSTICK_BUTTON = 0;

static const int LIM_CODE_X_POSITIVE = 0;
static const int LIM_CODE_X_NEGATIVE = 1;
static const int LIM_CODE_Y_POSITIVE = 2;
static const int LIM_CODE_Y_NEGATIVE = 3;
static const int LIM_CODE_Z_POSITIVE = 4;
static const int LIM_CODE_Z_NEGATIVE = 5;

static const int ACTIVE_LOW = 0;
static const int ACTIVE_HIGH = 1;
static const int DISABLED = 2;

/***************************************************************************************************/
/**************************************** Pin definations ******************************************/
/***************************************************************************************************/
// Teensy4.1 board v1 def

// illumination
static const int LASER_405nm = 31;
static const int LASER_488nm = 32;
static const int LASER_638nm = 33;
static const int LASER_561nm = 34;
static const int LASER_730nm = 30;

// camera trigger 
static const int camera_trigger_pins[] = {35,36,37,38,39}; // to replace

/***************************************************************************************************/
/************************************ camera trigger and strobe ************************************/
/***************************************************************************************************/
static const int TRIGGER_PULSE_LENGTH_us = 50;
bool trigger_output_level[5] = {LOW,LOW,LOW,LOW,LOW};
bool control_strobe[5] = {false,false,false,false,false};
bool strobe_output_level[5] = {LOW,LOW,LOW,LOW,LOW};
bool strobe_on[5] = {false,false,false,false,false};
int strobe_delay[5] = {0,0,0,0,0};
long illumination_on_time[5] = {0,0,0,0,0};
long timestamp_trigger_rising_edge[5] = {0,0,0,0,0};
// to do: change the number of channels (5) to a named constant

/***************************************************************************************************/
/******************************************* steppers **********************************************/
/***************************************************************************************************/
static const float R_SENSE = 0.11f;

AccelStepper stepper_X = AccelStepper(AccelStepper::DRIVER, 0, 0);
AccelStepper stepper_Y = AccelStepper(AccelStepper::DRIVER, 0, 0);
AccelStepper stepper_Z = AccelStepper(AccelStepper::DRIVER, 0, 0);

volatile bool runSpeed_flag_X = false;
volatile bool runSpeed_flag_Y = false;
volatile bool runSpeed_flag_Z = false;
volatile long X_commanded_target_position = 0;
volatile long Y_commanded_target_position = 0;
volatile long Z_commanded_target_position = 0;
volatile bool X_commanded_movement_in_progress = false;
volatile bool Y_commanded_movement_in_progress = false;
volatile bool Z_commanded_movement_in_progress = false;

int32_t focusPosition = 0;

long target_position;

volatile int32_t X_pos = 0;
volatile int32_t Y_pos = 0;
volatile int32_t Z_pos = 0;

float offset_velocity_x = 0;
float offset_velocity_y = 0;

bool closed_loop_position_control = false;

// limit swittch
bool is_homing_X = false;
bool is_homing_Y = false;
bool is_homing_Z = false;
bool is_homing_XY = false;
volatile bool home_X_found = false;
volatile bool home_Y_found = false;
volatile bool home_Z_found = false;
bool is_preparing_for_homing_X = false;
bool is_preparing_for_homing_Y = false;
bool is_preparing_for_homing_Z = false;
bool homing_direction_X;
bool homing_direction_Y;
bool homing_direction_Z;
/* to do: move the movement direction sign from configuration.txt (python) to the firmware (with 
 * setPinsInverted() so that homing_direction_X, homing_direction_Y, homing_direction_Z will no 
 * longer be needed. This way the home switches can act as limit switches - right now because 
 * homing_direction_ needs be set by the computer, before they're set, the home switches cannot be
 * used as limit switches. Alternatively, add homing_direction_set variables.
 */

long X_POS_LIMIT = X_POS_LIMIT_MM*steps_per_mm_X;
long X_NEG_LIMIT = X_NEG_LIMIT_MM*steps_per_mm_X;
long Y_POS_LIMIT = Y_POS_LIMIT_MM*steps_per_mm_Y;
long Y_NEG_LIMIT = Y_NEG_LIMIT_MM*steps_per_mm_Y;
long Z_POS_LIMIT = Z_POS_LIMIT_MM*steps_per_mm_Z;
long Z_NEG_LIMIT = Z_NEG_LIMIT_MM*steps_per_mm_Z;


/***************************************************************************************************/
/******************************************** timing ***********************************************/
/***************************************************************************************************/
static const int TIMER_PERIOD = 500; // in us
static const int interval_send_pos_update = 10000; // in us
volatile int counter_send_pos_update = 0;
volatile bool flag_send_pos_update = false;

/***************************************************************************************************/
/******************************************* joystick **********************************************/
/***************************************************************************************************/
PacketSerial joystick_packetSerial;
static const int JOYSTICK_MSG_LENGTH = 10;
bool flag_read_joystick = false;

// joystick xy
int16_t joystick_delta_x = 0;
int16_t joystick_delta_y = 0; 

// joystick button
bool joystick_button_pressed = false;
long joystick_button_pressed_timestamp = 0;

// focus
int32_t focuswheel_pos = 0;

// btns
uint8_t btns;

void onJoystickPacketReceived(const uint8_t* buffer, size_t size)
{
  
  if(size != JOYSTICK_MSG_LENGTH)
  {
    Serial.println("! wrong number of bytes received !");
    return;
  }

  focuswheel_pos = uint32_t(buffer[0])*16777216 + uint32_t(buffer[1])*65536 + uint32_t(buffer[2])*256 + uint32_t(buffer[3]);
  focusPosition = focuswheel_pos;
  joystick_delta_x = JOYSTICK_SIGN_X*int16_t( uint16_t(buffer[4])*256 + uint16_t(buffer[5]) );
  joystick_delta_y = JOYSTICK_SIGN_Y*int16_t( uint16_t(buffer[6])*256 + uint16_t(buffer[7]) );
  btns = buffer[8];

  if(btns & 0x01)
  {
    joystick_button_pressed = true;
    joystick_button_pressed_timestamp = millis();
    // to add: ACK for the joystick panel
  }

  flag_read_joystick = true;

}

/***************************************************************************************************/
/***************************************** illumination ********************************************/
/***************************************************************************************************/
int illumination_source = 0;
uint16_t illumination_intensity = 65535;
uint8_t led_matrix_r = 0;
uint8_t led_matrix_g = 0;
uint8_t led_matrix_b = 0;
static const int LED_MATRIX_MAX_INTENSITY = 100;
static const float GREEN_ADJUSTMENT_FACTOR = 2.5;
static const float RED_ADJUSTMENT_FACTOR = 1;
static const float BLUE_ADJUSTMENT_FACTOR = 0.7;
bool illumination_is_on = false;
void turn_on_illumination();
void turn_off_illumination();

static const int ILLUMINATION_SOURCE_LED_ARRAY_FULL = 0;
static const int ILLUMINATION_SOURCE_LED_ARRAY_LEFT_HALF = 1;
static const int ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_HALF = 2;
static const int ILLUMINATION_SOURCE_LED_ARRAY_LEFTB_RIGHTR = 3;
static const int ILLUMINATION_SOURCE_LED_ARRAY_LOW_NA = 4;
static const int ILLUMINATION_SOURCE_LED_EXTERNAL_FET = 20;
static const int ILLUMINATION_SOURCE_LED_ARRAY_LEFT_DOT = 5;
static const int ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_DOT = 6;
static const int ILLUMINATION_SOURCE_405NM = 11;
static const int ILLUMINATION_SOURCE_488NM = 12;
static const int ILLUMINATION_SOURCE_638NM = 13;
static const int ILLUMINATION_SOURCE_561NM = 14;
static const int ILLUMINATION_SOURCE_730NM = 15;

Adafruit_DotStar matrix(DOTSTAR_NUM_LEDS, DOTSTAR_BRG);
void set_all(Adafruit_DotStar & matrix, int r, int g, int b);
void set_left(Adafruit_DotStar & matrix, int r, int g, int b);
void set_right(Adafruit_DotStar & matrix, int r, int g, int b);
void set_low_na(Adafruit_DotStar & matrix, int r, int g, int b);
void set_left_dot(Adafruit_DotStar & matrix, int r, int g, int b);
void set_right_dot(Adafruit_DotStar & matrix, int r, int g, int b);
void clear_matrix(Adafruit_DotStar & matrix);
void turn_on_LED_matrix_pattern(Adafruit_DotStar & matrix, int pattern, uint8_t led_matrix_r, uint8_t led_matrix_g, uint8_t led_matrix_b);

void turn_on_illumination()
{
  illumination_is_on = true;
  switch(illumination_source)
  {
    case ILLUMINATION_SOURCE_LED_ARRAY_FULL:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_FULL,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_HALF:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_LEFT_HALF,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_HALF:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_HALF,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFTB_RIGHTR:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_LEFTB_RIGHTR,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LOW_NA:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_LOW_NA,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_DOT:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_LEFT_DOT,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_DOT:
      turn_on_LED_matrix_pattern(matrix,ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_DOT,led_matrix_r,led_matrix_g,led_matrix_b);
      break;
    case ILLUMINATION_SOURCE_LED_EXTERNAL_FET:
      break;
    case ILLUMINATION_SOURCE_405NM:
      digitalWrite(LASER_405nm,HIGH);
      break;
    case ILLUMINATION_SOURCE_488NM:
      digitalWrite(LASER_488nm,HIGH);
      break;
    case ILLUMINATION_SOURCE_638NM:
      digitalWrite(LASER_638nm,HIGH);
      break;
    case ILLUMINATION_SOURCE_561NM:
      digitalWrite(LASER_561nm,HIGH);
      break;
    case ILLUMINATION_SOURCE_730NM:
      digitalWrite(LASER_730nm,HIGH);
      break;
  }
}

void turn_off_illumination()
{
  switch(illumination_source)
  {
    case ILLUMINATION_SOURCE_LED_ARRAY_FULL:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_HALF:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_HALF:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFTB_RIGHTR:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LOW_NA:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_DOT:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_DOT:
      clear_matrix(matrix);
      break;
    case ILLUMINATION_SOURCE_LED_EXTERNAL_FET:
      break;
    case ILLUMINATION_SOURCE_405NM:
      digitalWrite(LASER_405nm,LOW);
      break;
    case ILLUMINATION_SOURCE_488NM:
      digitalWrite(LASER_488nm,LOW);
      break;
    case ILLUMINATION_SOURCE_638NM:
      digitalWrite(LASER_638nm,LOW);
      break;
    case ILLUMINATION_SOURCE_561NM:
      digitalWrite(LASER_561nm,LOW);
      break;
    case ILLUMINATION_SOURCE_730NM:
      digitalWrite(LASER_730nm,LOW);
      break;
  }
  illumination_is_on = false;
}

void set_illumination(int source, uint16_t intensity)
{
  illumination_source = source;
  illumination_intensity = intensity;
  if(illumination_is_on)
    turn_on_illumination(); //update the illumination
}

void set_illumination_led_matrix(int source, uint8_t r, uint8_t g, uint8_t b)
{
  illumination_source = source;
  led_matrix_r = r;
  led_matrix_g = g;
  led_matrix_b = b;
  if(illumination_is_on)
    turn_on_illumination(); //update the illumination
}

/***************************************************************************************************/
/********************************************* setup ***********************************************/
/***************************************************************************************************/

void setup() {

  // Initialize Native USB port
  SerialUSB.begin(2000000);     
  //while(!SerialUSB);            // Wait until connection is established
  buffer_rx_ptr = 0;

  // Joystick packet serial
  Serial5.begin(115200);
  joystick_packetSerial.setStream(&Serial5);
  joystick_packetSerial.setPacketHandler(&onJoystickPacketReceived);

  // camera trigger pins
  for(int i=0;i<5;i++)
  {
    pinMode(camera_trigger_pins[i], OUTPUT);
    digitalWrite(camera_trigger_pins[i], LOW);
  }
  
  // enable pins
  pinMode(LASER_405nm, OUTPUT);
  digitalWrite(LASER_405nm, LOW);

  pinMode(LASER_488nm, OUTPUT);
  digitalWrite(LASER_488nm, LOW);

  pinMode(LASER_638nm, OUTPUT);
  digitalWrite(LASER_638nm, LOW);

  pinMode(LASER_561nm, OUTPUT);
  digitalWrite(LASER_561nm, LOW);

  pinMode(LASER_730nm, OUTPUT);
  digitalWrite(LASER_730nm, LOW);
  
  X_pos = 0;
  Y_pos = 0;
  Z_pos = 0;

  offset_velocity_x = 0;
  offset_velocity_y = 0;

  IntervalTimer systemTimer;
  systemTimer.begin(timer_interruptHandler, TIMER_PERIOD);

  // led matrix
  matrix.begin();

  // DAC
  analogWriteResolution(12);
  
}

/***************************************************************************************************/
/********************************************** loop ***********************************************/
/***************************************************************************************************/

void loop() {

  // process incoming packets
  joystick_packetSerial.update();

  // read one meesage from the buffer
  while (SerialUSB.available()) 
  { 
    buffer_rx[buffer_rx_ptr] = SerialUSB.read();
    buffer_rx_ptr = buffer_rx_ptr + 1;
    if (buffer_rx_ptr == CMD_LENGTH) 
    {
      buffer_rx_ptr = 0;
      cmd_id = buffer_rx[0];
      switch(buffer_rx[1])
      {
        case MOVE_X:
        {
          long relative_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          X_commanded_target_position = ( relative_position>0?min(stepper_X.currentPosition()+relative_position,X_POS_LIMIT):max(stepper_X.currentPosition()+relative_position,X_NEG_LIMIT) );
          stepper_X.moveTo(X_commanded_target_position);
          X_commanded_movement_in_progress = true;
          runSpeed_flag_X = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case MOVE_Y:
        {
          long relative_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          Y_commanded_target_position = ( relative_position>0?min(stepper_Y.currentPosition()+relative_position,Y_POS_LIMIT):max(stepper_Y.currentPosition()+relative_position,Y_NEG_LIMIT) );
          stepper_Y.moveTo(Y_commanded_target_position);
          Y_commanded_movement_in_progress = true;
          runSpeed_flag_Y = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case MOVE_Z:
        {
          long relative_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          Z_commanded_target_position = ( relative_position>0?min(stepper_Z.currentPosition()+relative_position,Z_POS_LIMIT):max(stepper_Z.currentPosition()+relative_position,Z_NEG_LIMIT) );
          focusPosition = Z_commanded_target_position;
          stepper_Z.moveTo(Z_commanded_target_position);
          Z_commanded_movement_in_progress = true;
          runSpeed_flag_Z = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case MOVETO_X:
        {
          long absolute_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          X_commanded_target_position = absolute_position;
          stepper_X.moveTo(absolute_position);
          X_commanded_movement_in_progress = true;
          runSpeed_flag_X = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case MOVETO_Y:
        {
          long absolute_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          Y_commanded_target_position = absolute_position;
          stepper_Y.moveTo(absolute_position);
          Y_commanded_movement_in_progress = true;
          runSpeed_flag_Y = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case MOVETO_Z:
        {
          long absolute_position = int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5]));
          // mcu_cmd_execution_in_progress = true; // because runToNewPosition is blocking, changing this flag is not needed
          Z_commanded_target_position = absolute_position;
          stepper_Z.moveTo(absolute_position);
          focusPosition = absolute_position;
          Z_commanded_movement_in_progress = true;
          runSpeed_flag_Z = false;
          mcu_cmd_execution_in_progress = true;
          break;
        }
        case SET_LIM:
        {
          switch(buffer_rx[2])
          {
            case LIM_CODE_X_POSITIVE:
            {
              X_POS_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
            case LIM_CODE_X_NEGATIVE:
            {
              X_NEG_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
            case LIM_CODE_Y_POSITIVE:
            {
              Y_POS_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
            case LIM_CODE_Y_NEGATIVE:
            {
              Y_NEG_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
            case LIM_CODE_Z_POSITIVE:
            {
              Z_POS_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
            case LIM_CODE_Z_NEGATIVE:
            {
              Z_NEG_LIMIT = int32_t(uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]));
              break;
            }
          }
          break;
        }
        case SET_LIM_SWITCH_POLARITY:
        {
          switch(buffer_rx[2])
          {
            case AXIS_X:
            {
              if(buffer_rx[3]!=DISABLED)
              {
                LIM_SWITCH_X_ACTIVE_LOW = (buffer_rx[3]==ACTIVE_LOW);
              }
              break;
            }
            case AXIS_Y:
            {
              if(buffer_rx[3]!=DISABLED)
              {
                LIM_SWITCH_Y_ACTIVE_LOW = (buffer_rx[3]==ACTIVE_LOW);
              }
              break;
            }
            case AXIS_Z:
            {
              if(buffer_rx[3]!=DISABLED)
              {
                LIM_SWITCH_Z_ACTIVE_LOW = (buffer_rx[3]==ACTIVE_LOW);
              }
              break;
            }
          }
          break;
        }
        case CONFIGURE_STEPPER_DRIVER:
        {
          switch(buffer_rx[2])
          {
            case AXIS_X:
            {
              int microstepping_setting = buffer_rx[3];
              if(microstepping_setting>128)
                microstepping_setting = 256;
              // X_driver.microsteps(microstepping_setting);
              MICROSTEPPING_X = microstepping_setting==0?1:microstepping_setting;
              steps_per_mm_X = FULLSTEPS_PER_REV_X*MICROSTEPPING_X/SCREW_PITCH_X_MM;
              X_MOTOR_RMS_CURRENT_mA = uint16_t(buffer_rx[4])*256+uint16_t(buffer_rx[5]);
              X_MOTOR_I_HOLD = float(buffer_rx[6])/255;
              // X_driver.rms_current(X_MOTOR_RMS_CURRENT_mA,X_MOTOR_I_HOLD); //I_run and holdMultiplier
              break;
            }
            case AXIS_Y:
            {
              int microstepping_setting = buffer_rx[3];
              if(microstepping_setting>128)
                microstepping_setting = 256;
              // Y_driver.microsteps(microstepping_setting);
              MICROSTEPPING_Y = microstepping_setting==0?1:microstepping_setting;
              steps_per_mm_Y = FULLSTEPS_PER_REV_Y*MICROSTEPPING_Y/SCREW_PITCH_Y_MM;
              Y_MOTOR_RMS_CURRENT_mA = uint16_t(buffer_rx[4])*256+uint16_t(buffer_rx[5]);
              Y_MOTOR_I_HOLD = float(buffer_rx[6])/255;
              // Y_driver.rms_current(Y_MOTOR_RMS_CURRENT_mA,Y_MOTOR_I_HOLD); //I_run and holdMultiplier
              break;
            }
            case AXIS_Z:
            {
              int microstepping_setting = buffer_rx[3];
              if(microstepping_setting>128)
                microstepping_setting = 256;
              //Z_driver.microsteps(microstepping_setting);
              MICROSTEPPING_Z = microstepping_setting==0?1:microstepping_setting;
              steps_per_mm_Z = FULLSTEPS_PER_REV_Z*MICROSTEPPING_Z/SCREW_PITCH_Z_MM;
              Z_MOTOR_RMS_CURRENT_mA = uint16_t(buffer_rx[4])*256+uint16_t(buffer_rx[5]);
              Z_MOTOR_I_HOLD = float(buffer_rx[6])/255;
              //Z_driver.rms_current(Z_MOTOR_RMS_CURRENT_mA,Z_MOTOR_I_HOLD); //I_run and holdMultiplier
              break;
            }
          }
          break;
        }
        case SET_MAX_VELOCITY_ACCELERATION:
        {
          switch(buffer_rx[2])
          {
            case AXIS_X:
            {
              MAX_VELOCITY_X_mm = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/100;
              MAX_ACCELERATION_X_mm = float(uint16_t(buffer_rx[5])*256+uint16_t(buffer_rx[6]))/10;
              stepper_X.setMaxSpeed(MAX_VELOCITY_X_mm*steps_per_mm_X);
              stepper_X.setAcceleration(MAX_ACCELERATION_X_mm*steps_per_mm_X);
              break;
            }
            case AXIS_Y:
            {
              MAX_VELOCITY_Y_mm = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/100;
              MAX_ACCELERATION_Y_mm = float(uint16_t(buffer_rx[5])*256+uint16_t(buffer_rx[6]))/10;
              stepper_Y.setMaxSpeed(MAX_VELOCITY_Y_mm*steps_per_mm_Y);
              stepper_Y.setAcceleration(MAX_ACCELERATION_Y_mm*steps_per_mm_Y);
              break;
            }
            case AXIS_Z:
            {
              MAX_VELOCITY_Z_mm = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/100;
              MAX_ACCELERATION_Z_mm = float(uint16_t(buffer_rx[5])*256+uint16_t(buffer_rx[6]))/10;
              stepper_Z.setMaxSpeed(MAX_VELOCITY_Z_mm*steps_per_mm_Z);
              stepper_Z.setAcceleration(MAX_ACCELERATION_Z_mm*steps_per_mm_Z);
              break;
            }
          }
          break;
        }
        case SET_LEAD_SCREW_PITCH:
        {
          switch(buffer_rx[2])
          {
            case AXIS_X:
            {
              SCREW_PITCH_X_MM = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/1000;
              steps_per_mm_X = FULLSTEPS_PER_REV_X*MICROSTEPPING_X/SCREW_PITCH_X_MM;
              break;
            }
            case AXIS_Y:
            {
              SCREW_PITCH_Y_MM = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/1000;
              steps_per_mm_Y = FULLSTEPS_PER_REV_Y*MICROSTEPPING_Y/SCREW_PITCH_Y_MM;
              break;
            }
            case AXIS_Z:
            {
              SCREW_PITCH_Z_MM = float(uint16_t(buffer_rx[3])*256+uint16_t(buffer_rx[4]))/1000;
              steps_per_mm_Z = FULLSTEPS_PER_REV_Z*MICROSTEPPING_Z/SCREW_PITCH_Z_MM;
              break;
            }
          }
          break;
        }
        case HOME_OR_ZERO:
        {
          // zeroing
          if(buffer_rx[3]==HOME_OR_ZERO_ZERO)
          {
            switch(buffer_rx[2])
            {
              case AXIS_X:
                stepper_X.setCurrentPosition(0);
                X_pos = 0;
                break;
              case AXIS_Y:
                stepper_Y.setCurrentPosition(0);
                Y_pos = 0;
                break;
              case AXIS_Z:
                stepper_Z.setCurrentPosition(0);
                Z_pos = 0;
                focusPosition = 0;
                break;
            }
            // atomic operation, no need to change mcu_cmd_execution_in_progress flag
          }
          // homing
          else if(buffer_rx[3]==HOME_NEGATIVE || buffer_rx[3]==HOME_POSITIVE)
          {
            switch(buffer_rx[2])
            {
              case AXIS_X:
                homing_direction_X = buffer_rx[3];
                home_X_found = false;
                /*
                if(digitalRead(X_LIM)==(LIM_SWITCH_X_ACTIVE_LOW?HIGH:LOW))
                {
                  is_homing_X = true;
                  runSpeed_flag_X = true;
                  if(homing_direction_X==HOME_NEGATIVE)
                    stepper_X.setSpeed(-HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                  else
                    stepper_X.setSpeed(HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                }
                else
                {
                  // get out of the hysteresis zone
                  is_preparing_for_homing_X = true;
                  runSpeed_flag_X = true;
                  if(homing_direction_X==HOME_NEGATIVE)
                    stepper_X.setSpeed(HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                  else
                    stepper_X.setSpeed(-HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                }
                */
                break;
              case AXIS_Y:
                homing_direction_Y = buffer_rx[3];
                home_Y_found = false;
                /*
                if(digitalRead(Y_LIM)==(LIM_SWITCH_Y_ACTIVE_LOW?HIGH:LOW))
                {
                  is_homing_Y = true;
                  runSpeed_flag_Y = true;
                  if(homing_direction_Y==HOME_NEGATIVE)
                    stepper_Y.setSpeed(-HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                  else
                    stepper_Y.setSpeed(HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                }
                else
                {
                  // get out of the hysteresis zone
                  is_preparing_for_homing_Y = true;
                  runSpeed_flag_Y = true;
                  if(homing_direction_Y==HOME_NEGATIVE)
                    stepper_Y.setSpeed(HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                  else
                    stepper_Y.setSpeed(-HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                }
                */
                break;
              case AXIS_Z:
                homing_direction_Z = buffer_rx[3];
                home_Z_found = false;
                /*
                if(digitalRead(Z_LIM)==(LIM_SWITCH_Z_ACTIVE_LOW?HIGH:LOW))
                {
                  is_homing_Z = true;
                  runSpeed_flag_Z = true;
                  if(homing_direction_Z==HOME_NEGATIVE)
                    stepper_Z.setSpeed(-HOMING_VELOCITY_Z*MAX_VELOCITY_Z_mm*steps_per_mm_Z);
                  else
                    stepper_Z.setSpeed(HOMING_VELOCITY_Z*MAX_VELOCITY_Z_mm*steps_per_mm_Z);
                }
                else
                {
                  // get out of the hysteresis zone
                  is_preparing_for_homing_Z = true;
                  runSpeed_flag_Z = true;
                  if(homing_direction_Z==HOME_NEGATIVE)
                    stepper_Z.setSpeed(HOMING_VELOCITY_Z*MAX_VELOCITY_Z_mm*steps_per_mm_Z);
                  else
                    stepper_Z.setSpeed(-HOMING_VELOCITY_Z*MAX_VELOCITY_Z_mm*steps_per_mm_Z);
                }
                */
                break;
              case AXES_XY:
                is_homing_XY = true;
                home_X_found = false;
                home_Y_found = false;
                /*
                // homing x 
                homing_direction_X = buffer_rx[3];
                if(digitalRead(X_LIM)==(LIM_SWITCH_X_ACTIVE_LOW?HIGH:LOW))
                {
                  is_homing_X = true;
                  runSpeed_flag_X = true;
                  if(homing_direction_X==HOME_NEGATIVE)
                    stepper_X.setSpeed(-HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                  else
                    stepper_X.setSpeed(HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                }
                else
                {
                  // get out of the hysteresis zone
                  is_preparing_for_homing_X = true;
                  runSpeed_flag_X = true;
                  if(homing_direction_X==HOME_NEGATIVE)
                    stepper_X.setSpeed(HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                  else
                    stepper_X.setSpeed(-HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
                }
                // homing y
                homing_direction_Y = buffer_rx[4];
                if(digitalRead(Y_LIM)==(LIM_SWITCH_Y_ACTIVE_LOW?HIGH:LOW))
                {
                  is_homing_Y = true;
                  runSpeed_flag_Y = true;
                  if(homing_direction_Y==HOME_NEGATIVE)
                    stepper_Y.setSpeed(-HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                  else
                    stepper_Y.setSpeed(HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                }
                else
                {
                  // get out of the hysteresis zone
                  is_preparing_for_homing_Y = true;
                  runSpeed_flag_Y = true;
                  if(homing_direction_Y==HOME_NEGATIVE)
                    stepper_Y.setSpeed(HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                  else
                    stepper_Y.setSpeed(-HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
                }
                break;
                */
            }
            mcu_cmd_execution_in_progress = true;
          }
          break;
        }
        case SET_OFFSET_VELOCITY:
        {
          if(enable_offset_velocity)
          {
            switch(buffer_rx[2])
            {
              case AXIS_X:
                offset_velocity_x = float( int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5])) )/1000000;
                if( abs(offset_velocity_x)>0.000005 )
                  runSpeed_flag_X = true;
                else
                  runSpeed_flag_X = false;
                break;
              case AXIS_Y:
                offset_velocity_y = float( int32_t(uint32_t(buffer_rx[2])*16777216 + uint32_t(buffer_rx[3])*65536 + uint32_t(buffer_rx[4])*256 + uint32_t(buffer_rx[5])) )/1000000;
                if( abs(offset_velocity_y)>0.000005 )
                  runSpeed_flag_Y = true;
                else
                  runSpeed_flag_Y = false;
                break;
            }
            break;
          }
        }
        case TURN_ON_ILLUMINATION:
        {
          // mcu_cmd_execution_in_progress = true;
          turn_on_illumination();
          // mcu_cmd_execution_in_progress = false;
          // these are atomic operations - do not change the mcu_cmd_execution_in_progress flag
          break;
        }
        case TURN_OFF_ILLUMINATION:
        {
          turn_off_illumination();
          break;
        }
        case SET_ILLUMINATION:
        {
          set_illumination(buffer_rx[2],(uint16_t(buffer_rx[2])<<8) + uint16_t(buffer_rx[3])); //important to have "<<8" with in "()"
          break;
        }
        case SET_ILLUMINATION_LED_MATRIX:
        {
          set_illumination_led_matrix(buffer_rx[2],buffer_rx[3],buffer_rx[4],buffer_rx[5]);
          break;
        }
        case ACK_JOYSTICK_BUTTON_PRESSED:
        {
          joystick_button_pressed = false;
          break;
        }
        case ANALOG_WRITE_ONBOARD_DAC:
        {
          uint16_t value = ( uint16_t(buffer_rx[3])*256 + uint16_t(buffer_rx[4]) )/16;
        }
        case SET_STROBE_DELAY:
        {
          strobe_delay[buffer_rx[2]] = uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]);
          break;
        }
        case SEND_HARDWARE_TRIGGER:
        {
          int camera_channel = buffer_rx[2] & 0x0f;
          control_strobe[camera_channel] = buffer_rx[2] >> 7;
          illumination_on_time[camera_channel] = uint32_t(buffer_rx[3])*16777216 + uint32_t(buffer_rx[4])*65536 + uint32_t(buffer_rx[5])*256 + uint32_t(buffer_rx[6]);
          digitalWrite(camera_trigger_pins[camera_channel],HIGH);
          timestamp_trigger_rising_edge[camera_channel] = micros();
          trigger_output_level[camera_channel] = HIGH;
          break;
        }
        default:
          break;
      }
      //break; // exit the while loop after reading one message
    }
  }

  // camera trigger
  for(int camera_channel=0;camera_channel<5;camera_channel++)
  {
    // end the trigger pulse
    if(trigger_output_level[camera_channel] == HIGH && (micros()-timestamp_trigger_rising_edge[camera_channel])>= TRIGGER_PULSE_LENGTH_us )
    {
      digitalWrite(camera_trigger_pins[camera_channel],LOW);
      trigger_output_level[camera_channel] = LOW;
    }

    // strobe pulse
    if(control_strobe[camera_channel])
    {
      if(illumination_on_time[camera_channel] <= 30000)
      {
        // if the illumination on time is smaller than 30 ms, use delayMicroseconds to control the pulse length to avoid pulse length jitter (can be up to 20 us if using the code in the else branch)
        if( ((micros()-timestamp_trigger_rising_edge[camera_channel])>=strobe_delay[camera_channel]) && strobe_output_level[camera_channel]==LOW )
        {
          turn_on_illumination();
          delayMicroseconds(illumination_on_time[camera_channel]);
          turn_off_illumination();
          control_strobe[camera_channel] = false;
        }
      }
      else
      {
        // start the strobe
        if( ((micros()-timestamp_trigger_rising_edge[camera_channel])>=strobe_delay[camera_channel]) && strobe_output_level[camera_channel]==LOW )
        {
          turn_on_illumination();
          strobe_output_level[camera_channel] = HIGH;
        }
        // end the strobe
        if(((micros()-timestamp_trigger_rising_edge[camera_channel])>=strobe_delay[camera_channel]+illumination_on_time[camera_channel]) && strobe_output_level[camera_channel]==HIGH)
        {
          turn_off_illumination();
          strobe_output_level[camera_channel] = LOW;
          control_strobe[camera_channel] = false;
        }
      }      
    }
  }

  /*
  // homing - preparing for homing
  if(is_preparing_for_homing_X)
  {
    if(digitalRead(X_LIM)==(LIM_SWITCH_X_ACTIVE_LOW?HIGH:LOW))
    {
      is_preparing_for_homing_X = false;
      is_homing_X = true;
      runSpeed_flag_X = true;
      if(homing_direction_X==HOME_NEGATIVE)
        stepper_X.setSpeed(-HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
      else
        stepper_X.setSpeed(HOMING_VELOCITY_X*MAX_VELOCITY_X_mm*steps_per_mm_X);
    }
  }
  if(is_preparing_for_homing_Y)
  {
    if(digitalRead(Y_LIM)==(LIM_SWITCH_Y_ACTIVE_LOW?HIGH:LOW))
    {
      is_preparing_for_homing_Y = false;
      is_homing_Y = true;
      runSpeed_flag_Y = true;
      if(homing_direction_Y==HOME_NEGATIVE)
        stepper_Y.setSpeed(-HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
      else
        stepper_Y.setSpeed(HOMING_VELOCITY_Y*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
    }
  }
  if(is_preparing_for_homing_Z)
  {
    if(digitalRead(Z_LIM)==(LIM_SWITCH_Z_ACTIVE_LOW?HIGH:LOW))
    {
      is_preparing_for_homing_Z = false;
      is_homing_Z = true;
      runSpeed_flag_Z = true;
      if(homing_direction_Z==HOME_NEGATIVE)
        stepper_Z.setSpeed(-HOMING_VELOCITY_Z*MAX_VELOCITY_X_mm*steps_per_mm_Z);
      else
        stepper_Z.setSpeed(HOMING_VELOCITY_Z*MAX_VELOCITY_X_mm*steps_per_mm_Z);
    }
  }
  */

  // finish homing
  if(is_homing_X && home_X_found && stepper_X.distanceToGo() == 0)
  {
    stepper_X.setCurrentPosition(0);
    X_pos = 0;
    is_homing_X = false;
    X_commanded_movement_in_progress = false;
    if(is_homing_XY==false)
      mcu_cmd_execution_in_progress = false;
  }
  if(is_homing_Y && home_Y_found && stepper_Y.distanceToGo() == 0)
  {
    stepper_Y.setCurrentPosition(0);
    Y_pos = 0;
    is_homing_Y = false;
    Y_commanded_movement_in_progress = false;
    if(is_homing_XY==false)
      mcu_cmd_execution_in_progress = false;
  }
  if(is_homing_Z && home_Z_found && stepper_Z.distanceToGo() == 0)
  {
    stepper_Z.setCurrentPosition(0);
    Z_pos = 0;
    focusPosition = 0;
    is_homing_Z = false;
    Z_commanded_movement_in_progress = false;
    mcu_cmd_execution_in_progress = false;
  }

  // homing complete
  if(is_homing_XY && home_X_found && !is_homing_X && home_Y_found && !is_homing_Y)
  {
    is_homing_XY = false;
    mcu_cmd_execution_in_progress = false;
  }

  if(flag_read_joystick)
  {
    // read x joystick
    if(!X_commanded_movement_in_progress && !is_homing_X && !is_preparing_for_homing_X) //if(stepper_X.distanceToGo()==0) // only read joystick when computer commanded travel has finished - doens't work
    {
      // joystick at motion position
      if(abs(joystick_delta_x)>0)
      {
        stepper_X.setSpeed( offset_velocity_x*steps_per_mm_X + (joystick_delta_x/32768.0)*MAX_VELOCITY_X_mm*steps_per_mm_X );
      }
      // joystick at rest position
      else
      {
        if(enable_offset_velocity)
        {
          stepper_X.setSpeed( offset_velocity_x*steps_per_mm_X );
        }
        else
        {
          runSpeed_flag_X = false;
          stepper_X.setSpeed( 0 );
        }
      }
    }
  
    // read y joystick
    if(!Y_commanded_movement_in_progress && !is_homing_Y && !is_preparing_for_homing_Y)
    {
      // joystick at motion position
      if(abs(joystick_delta_y)>0)
      {
        stepper_Y.setSpeed( offset_velocity_y*steps_per_mm_Y + (joystick_delta_x/32768.0)*MAX_VELOCITY_Y_mm*steps_per_mm_Y);
      }
      // joystick at rest position
      else
      {
        if(enable_offset_velocity)
        {
          stepper_Y.setSpeed( offset_velocity_y*steps_per_mm_Y );
        }
        else
        {
          stepper_Y.setSpeed( 0 );
        }
      }
    }

    // set the read joystick flag to false
    flag_read_joystick = false;
  }

  // handle limits
  if( stepper_X.currentPosition()>=X_POS_LIMIT && offset_velocity_x>0 )
  {
    runSpeed_flag_X = false;
    stepper_X.setSpeed( 0 );
  }
  if( stepper_X.currentPosition()<=X_NEG_LIMIT && offset_velocity_x<0 )
  {
    runSpeed_flag_X = false;
    stepper_X.setSpeed( 0 );
  }
  if( stepper_Y.currentPosition()>=Y_POS_LIMIT && offset_velocity_y>0 )
  {
    runSpeed_flag_Y = false;
    stepper_Y.setSpeed( 0 );
  }
  if( stepper_Y.currentPosition()<=Y_NEG_LIMIT && offset_velocity_y<0 )
  {
    runSpeed_flag_Y = false;
    stepper_Y.setSpeed( 0 );
  }
  
  // focus control
  if(focusPosition > Z_POS_LIMIT)
    focusPosition = Z_POS_LIMIT;
  if(focusPosition < Z_NEG_LIMIT)
    focusPosition = Z_NEG_LIMIT;
  stepper_Z.moveTo(focusPosition);

  // send position update to computer
  if(flag_send_pos_update)
  {

    buffer_tx[0] = cmd_id;
    buffer_tx[1] = mcu_cmd_execution_in_progress; // cmd_execution_status
    
    uint32_t X_pos_int32t = uint32_t( X_use_encoder?X_pos:int32_t(stepper_X.currentPosition()) );
    buffer_tx[2] = byte(X_pos_int32t>>24);
    buffer_tx[3] = byte((X_pos_int32t>>16)%256);
    buffer_tx[4] = byte((X_pos_int32t>>8)%256);
    buffer_tx[5] = byte((X_pos_int32t)%256);
    
    uint32_t Y_pos_int32t = uint32_t( Y_use_encoder?Y_pos:int32_t(stepper_Y.currentPosition()) );
    buffer_tx[6] = byte(Y_pos_int32t>>24);
    buffer_tx[7] = byte((Y_pos_int32t>>16)%256);
    buffer_tx[8] = byte((Y_pos_int32t>>8)%256);
    buffer_tx[9] = byte((Y_pos_int32t)%256);

    uint32_t Z_pos_int32t = uint32_t( Z_use_encoder?Z_pos:int32_t(stepper_Z.currentPosition()) );
    buffer_tx[10] = byte(Z_pos_int32t>>24);
    buffer_tx[11] = byte((Z_pos_int32t>>16)%256);
    buffer_tx[12] = byte((Z_pos_int32t>>8)%256);
    buffer_tx[13] = byte((Z_pos_int32t)%256);

    // fail-safe clearing of the joystick_button_pressed bit (in case the ack is not received)
    if(joystick_button_pressed && millis() - joystick_button_pressed_timestamp > 1000)
      joystick_button_pressed = false;

    buffer_tx[18] &= ~ (1 << BIT_POS_JOYSTICK_BUTTON); // clear the joystick button bit
    buffer_tx[18] = buffer_tx[18] | joystick_button_pressed << BIT_POS_JOYSTICK_BUTTON;
    
    SerialUSB.write(buffer_tx,MSG_LENGTH);
    flag_send_pos_update = false;
    
  }

  // check if commanded position has been reached
  if(X_commanded_movement_in_progress && stepper_X.currentPosition()==X_commanded_target_position && !is_homing_X) // homing is handled separately
  {
    X_commanded_movement_in_progress = false;
    mcu_cmd_execution_in_progress = false || Y_commanded_movement_in_progress || Z_commanded_movement_in_progress;
  }
  if(Y_commanded_movement_in_progress && stepper_Y.currentPosition()==Y_commanded_target_position && !is_homing_Y)
  {
    Y_commanded_movement_in_progress = false;
    mcu_cmd_execution_in_progress = false || X_commanded_movement_in_progress || Z_commanded_movement_in_progress;
  }
  if(Z_commanded_movement_in_progress && stepper_Z.currentPosition()==Z_commanded_target_position && !is_homing_Z)
  {
    Z_commanded_movement_in_progress = false;
    mcu_cmd_execution_in_progress = false || X_commanded_movement_in_progress || Y_commanded_movement_in_progress;
  }
    
}

/***************************************************
 *  
 *                  timer interrupt 
 *  
 ***************************************************/
 
// timer interrupt
void timer_interruptHandler()
{
  counter_send_pos_update = counter_send_pos_update + 1;
  if(counter_send_pos_update==interval_send_pos_update/TIMER_PERIOD)
  {
    flag_send_pos_update = true;
    counter_send_pos_update = 0;
  }
}

/***************************************************************************************************/
/*********************************************  utils  *********************************************/
/***************************************************************************************************/
long signed2NBytesUnsigned(long signedLong,int N)
{
  long NBytesUnsigned = signedLong + pow(256L,N)/2;
  //long NBytesUnsigned = signedLong + 8388608L;
  return NBytesUnsigned;
}

static inline int sgn(int val) {
 if (val < 0) return -1;
 if (val==0) return 0;
 return 1;
}

/***************************************************************************************************/
/*******************************************  LED Array  *******************************************/
/***************************************************************************************************/
void set_all(Adafruit_DotStar & matrix, int r, int g, int b)
{
  for (int i = 0; i < DOTSTAR_NUM_LEDS; i++)
    matrix.setPixelColor(i,r,g,b);
}

void set_left(Adafruit_DotStar & matrix, int r, int g, int b)
{
  for (int i = 0; i < DOTSTAR_NUM_LEDS/2; i++)
    matrix.setPixelColor(i,r,g,b);
}

void set_right(Adafruit_DotStar & matrix, int r, int g, int b)
{
  for (int i = DOTSTAR_NUM_LEDS/2; i < DOTSTAR_NUM_LEDS; i++)
    matrix.setPixelColor(i,r,g,b);
}

void set_low_na(Adafruit_DotStar & matrix, int r, int g, int b)
{
  // matrix.setPixelColor(44,r,g,b);
  matrix.setPixelColor(45,r,g,b);
  matrix.setPixelColor(46,r,g,b);
  // matrix.setPixelColor(47,r,g,b);
  matrix.setPixelColor(56,r,g,b);
  matrix.setPixelColor(57,r,g,b);
  matrix.setPixelColor(58,r,g,b);
  matrix.setPixelColor(59,r,g,b);
  matrix.setPixelColor(68,r,g,b);
  matrix.setPixelColor(69,r,g,b);
  matrix.setPixelColor(70,r,g,b);
  matrix.setPixelColor(71,r,g,b);
  // matrix.setPixelColor(80,r,g,b);
  matrix.setPixelColor(81,r,g,b);
  matrix.setPixelColor(82,r,g,b);
  // matrix.setPixelColor(83,r,g,b);
}

void set_left_dot(Adafruit_DotStar & matrix, int r, int g, int b)
{
  matrix.setPixelColor(3,r,g,b);
  matrix.setPixelColor(4,r,g,b);
  matrix.setPixelColor(11,r,g,b);
  matrix.setPixelColor(12,r,g,b);
}

void set_right_dot(Adafruit_DotStar & matrix, int r, int g, int b)
{
  matrix.setPixelColor(115,r,g,b);
  matrix.setPixelColor(116,r,g,b);
  matrix.setPixelColor(123,r,g,b);
  matrix.setPixelColor(124,r,g,b);
}

void clear_matrix(Adafruit_DotStar & matrix)
{
  for (int i = 0; i < DOTSTAR_NUM_LEDS; i++)
    matrix.setPixelColor(i,0,0,0);
  matrix.show();
}

void turn_on_LED_matrix_pattern(Adafruit_DotStar & matrix, int pattern, uint8_t led_matrix_r, uint8_t led_matrix_g, uint8_t led_matrix_b)
{

  led_matrix_r = (float(led_matrix_r)/255)*LED_MATRIX_MAX_INTENSITY;
  led_matrix_g = (float(led_matrix_g)/255)*LED_MATRIX_MAX_INTENSITY;
  led_matrix_b = (float(led_matrix_b)/255)*LED_MATRIX_MAX_INTENSITY;

  // clear matrix
  set_all(matrix, 0, 0, 0);
    
  switch(pattern)
  {
    case ILLUMINATION_SOURCE_LED_ARRAY_FULL:
      set_all(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_HALF:
      set_left(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_HALF:
      set_right(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFTB_RIGHTR:
      set_left(matrix,0,0,led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      set_right(matrix,0,led_matrix_r*RED_ADJUSTMENT_FACTOR,0);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LOW_NA:
      set_low_na(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_LEFT_DOT:
      set_left_dot(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
    case ILLUMINATION_SOURCE_LED_ARRAY_RIGHT_DOT:
      set_right_dot(matrix, led_matrix_g*GREEN_ADJUSTMENT_FACTOR, led_matrix_r*RED_ADJUSTMENT_FACTOR, led_matrix_b*BLUE_ADJUSTMENT_FACTOR);
      break;
  }
  matrix.show();
}