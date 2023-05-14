#include <LiquidCrystal.h> 

// general
unsigned long int loop_delay = 100; // time between refreshs in ms
int lvl(int val, int n_lvls, int n_vals=1023){ return val/(1 + n_vals/n_lvls); } // rescale integer values
const char* formatFloat(float x, int width=6, int precision=2, const char* unit="\0"); // fixed format (width, decimal places) floating point

// control
const int nob_pin = A0, button_pin = 13;

//! Possible cursor positions
enum class CursorPosition : int {
  setpoint = 0,
  step_down,
  step_up,
  set,
  reset,
  toggle_flow,
  flow_actual
};

int nob_value = 0;
bool button_ready = true; // after press, button is inactive until released
bool adjust_using_nob = false; // whether nob controls cursor position or setpoint value
CursorPosition cursor_position = CursorPosition(0);

// MFC
const int flow_signal_pin = A5, flow_setpoint_pin = 6;

int flow_signal = 0; // signal level from ADC
float flow_actual = 0.00; // converted to sccm

int setpoint = 0; // for flow setpoint selection
int setpoint_actual = 0; // current actual flow setpoint (set when new setpoint is confirmed)

float mfc_range = 100.0; // calibrated MFC full scale
float corr_factor = 1.39; // adjust MFC full scale for use with different gas
float offset = 0.; // zero flow offset

bool flow_on = false; // toggle flow

// LCD
const int rs = 12, enable = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
const int backlight_pin = 9, contrast_pin = 10;
const int lcd_width = 16, lcd_height = 2;
LiquidCrystal lcd = LiquidCrystal(rs, enable, d4, d5, d6, d7);

int backlight_timer = 0; // stores time since last button press in s
int timeout = 150; // time in s after which the lcd backlight will be turned off
bool backlight_on = true;
int lcd_brightness = 0, lcd_contrast = 255;

void switchBacklight(bool state); // switch backlight on/off
bool buttonPressed(); // detect whether button is pressed

//! custom characters for the LCD
enum class Symbol : int {
  checkmark = 1,
  cross,
  up,
  down,
  select
};
byte s_checkmark[8]{0b00000,0b00001,0b00001,0b10010,0b01010,0b00100,0b00000,0b00000}; // const
byte s_cross[8]{0b00000,0b10001,0b01010,0b00100,0b01010,0b10001,0b00000,0b00000}; // const
byte s_up[8]{0b00000,0b00100,0b01110,0b10101,0b00100,0b00100,0b00000,0b00000}; // const
byte s_down[8]{0b00000,0b00100,0b00100,0b10101,0b01110,0b00100,0b00000,0b00000}; // const
byte s_select[8]{0b00000,0b00100,0b00010,0b11111,0b00010,0b00100,0b00000,0b00000}; // const

/**
 * @brief Setup arduino.
 * Initialise serial communication and LCD screen, print splash screen and render static objects.
 * 
 * Besides the version number, the splash screen shows relevant hardcoded settings,
 * namely the calibrated range of the MFC and gas specific correction factor.
 */
void setup() {
  // initialise serial communication
  Serial.begin(9600);
  Serial.println();
  // print collumn headers
  Serial.print("# flow"); Serial.print(" "); // actual flow in sccm
  Serial.print("zero"); Serial.print(" "); // zero offset in volt
  Serial.print("sgnl"); Serial.print(" "); // actual signal in volt
  Serial.print(" spa"); Serial.print(" "); // actual setpoint
  Serial.print(" spt"); Serial.print(" "); // temporary setpoint
  Serial.print(" nob"); Serial.print("\n"); // nob value
  // set LCD pins
  pinMode(backlight_pin, OUTPUT);
  pinMode(contrast_pin, OUTPUT);
  // set control pins
  pinMode(nob_pin, INPUT);
  pinMode(button_pin, INPUT);
  // set MFC pins
  pinMode(flow_signal_pin, INPUT);
  pinMode(flow_setpoint_pin, OUTPUT);
  // prepare LCD screen
  lcd.begin(lcd_width, lcd_height);
  analogWrite(backlight_pin, lcd_brightness);
  analogWrite(contrast_pin, lcd_contrast);
  // add special characters
  lcd.createChar((int)Symbol::checkmark, s_checkmark);
  lcd.createChar((int)Symbol::cross, s_cross);
  lcd.createChar((int)Symbol::up, s_up);
  lcd.createChar((int)Symbol::down, s_down);
  lcd.createChar((int)Symbol::select, s_select);
  // splash screen
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MFC ArdCtrl S");
  lcd.setCursor(0, 1); lcd.print("v1.0");
  lcd.setCursor(5, 1); lcd.print("r");
  lcd.setCursor(6, 1); lcd.print(formatFloat(mfc_range, 3, 0));
  lcd.setCursor(10, 1); lcd.print("f");
  lcd.setCursor(11, 1); lcd.print(formatFloat(corr_factor, 3, 2));
  delay(2000);
  // render static objects
  //  0123456789abcdef
  // | 100.00sccm 100%|
  // | 255 xx xx  off | 
  lcd.clear();
  lcd.setCursor(7, 0); lcd.print("sccm");
  lcd.setCursor(15,0); lcd.print("\%");
  lcd.setCursor(5, 1); lcd.write((int)Symbol::down);
  lcd.setCursor(6, 1); lcd.write((int)Symbol::up);
  lcd.setCursor(8, 1); lcd.write((int)Symbol::checkmark);
  lcd.setCursor(9, 1); lcd.write((int)Symbol::cross);
}

/**
 * @brief Update input output and the LCD screen.
 * Update the flow setpoint (output) and actual (input) values, 
 * update and the LCD screen and handle user input.
 */
void update(){
  // read current flow
  flow_signal = analogRead(flow_signal_pin);
  // update flow setpoint
  if ( flow_on ) {
    analogWrite(flow_setpoint_pin, setpoint_actual);
  } else {
    analogWrite(flow_setpoint_pin, 0);
  }
  // convert ADC value to sccm
  flow_actual = corr_factor*(flow_signal/1023.0*5.0+offset)/5.0*mfc_range;
  // update values on screen
  lcd.setCursor(1, 0); lcd.print(formatFloat(flow_actual, 6, 2));
  lcd.setCursor(12,0); lcd.print(formatFloat(flow_on ? setpoint_actual/2.55 : 0, 3, 0)); // actual setpoint in percent
  lcd.setCursor(1, 1); lcd.print(formatFloat(setpoint,3,0)); // setpoint as level between 0 and 255
  lcd.setCursor(12,1); lcd.print(flow_on ? "on " : "off");
  // get nob user input
  nob_value = analogRead(nob_pin);
  if ( adjust_using_nob ){
    // use nob to adjust the setpoint value
    lcd.setCursor(0, 1);
    setpoint = lvl(nob_value, 256);
    if ( buttonPressed() ){
      lcd.noCursor();
      adjust_using_nob = false;
    }
  } else {
    // use nob to control the cursor position
    cursor_position = CursorPosition(lvl(nob_value, 7));
    switch ( cursor_position ){
      case CursorPosition::setpoint:
        // button press: enter/exit setpoint adjustment mode
        lcd.noCursor();
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.write((int)Symbol::select);
        if ( buttonPressed() ) {
          adjust_using_nob = true;
          lcd.setCursor(0, 1); lcd.cursor();
        }
        break;
      case CursorPosition::step_down:
        // button press: decrease the setpoint by one level
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(5,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = (setpoint - 1)%256;
        }
        break;
      case CursorPosition::step_up:
        // button press: increase the setpoint by one level
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(6,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = (setpoint + 1)%256;
        }
        break;
      case CursorPosition::set:
        // button press: update the actual setpoint with the current temporary value
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(8,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint_actual = setpoint;
        }
        break;
      case CursorPosition::reset:
        // button press: reset the temporary setpoint to the current actual value
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1);lcd.print(" ");
        lcd.setCursor(9, 1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = setpoint_actual;
        }
        break;
      case CursorPosition::toggle_flow:
        // button press: toggle flow on and off
        lcd.noCursor();
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.write((int)Symbol::select);
        if ( buttonPressed() ){
          flow_on = !flow_on;
        }
        break;
      case CursorPosition::flow_actual:
        // button press: set the zero flow offset to the current flow value
        lcd.noCursor();
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(0, 0); lcd.write((int)Symbol::select);
        if ( buttonPressed() ){
          if ( abs(offset) > 0. ){
            offset = 0.;
          } else {
            offset = -5.0*flow_signal/1023.0;
          }
        }
        break;
      default: break;
    }
  }
}

/**
 * @brief Log relevant variables on serial connection.
 * 
 * Logged values are:
 * 
 * - the measured flow in sccm
 * - the zero offset in volt
 * - the flow signal as a value between 0 and 1023
 * - the actual flow setpoint as a value between 0 and 255
 * - the temporary flow setpoint as a value between 0 and 255
 * - the nob position as a value between 0 and 1023
 */
void log(){
  Serial.print(formatFloat(flow_actual, 6, 2)); Serial.print(" ");
  Serial.print(formatFloat(offset, 4, 2)); Serial.print(" ");
  Serial.print(formatFloat(flow_signal, 4, 0)); Serial.print(" ");
  Serial.print(formatFloat(setpoint_actual, 3, 0)); Serial.print(" ");
  Serial.print(formatFloat(setpoint, 3, 0)); Serial.print(" ");
  Serial.print(formatFloat(nob_value, 4, 0)); Serial.print("\n");
}

/**
 * @brief Main loop.
 */
void loop() {
  static unsigned long time = millis();
  update(); // update the MFC, LCD and handle user input.
  log(); // log relevant values
  // check backlight
  if( backlight_timer < timeout || timeout == 0 ){ // never turn of if timeout is 0
    backlight_timer++;
  }else{
    switchBacklight(false);
  }
  // timing : aim for 100ms per refresh/loop iteration
  time = min(millis() - time, 1000); 
  delay(min(loop_delay - time, loop_delay));
}

/**
 * @brief Switch LCD backlight and reset backlight timer.
 * 
 * @param state The state to switch to (true->'on', false->'off')
 */
void switchBacklight(bool state){
  if( state ){
    analogWrite(backlight_pin, lcd_brightness);
    backlight_on = true;
    backlight_timer = 0;
    return;
  }
  analogWrite(backlight_pin, 0);
  backlight_on = false;
}

/**
 * @brief Check for button presses.
 * The button is "deactivated" when pressed until it is released again
 * in order to avoid falsly registering ultiple button presses.
 * Button presses are only registered when the backlight is on.
 * If the backlight is off, the button press is only used to toggle it on.
 * 
 * @returns true if button was pressed
 */
bool buttonPressed(){
  if( digitalRead(button_pin) == HIGH ){
    backlight_timer = 0;
    if( button_ready ){
      button_ready = false;
      if( !backlight_on ){
        switchBacklight(true);
        return false;
      }
      return true;
    }
    return false;
  }
  button_ready = true;
  return false;  
}

/**
 * @brief Format a floating point number using a fixed format.
 * A fixed format consists of a fixed total width, a fixed number of decimals and optionally a unit.
 * 
 * @param x The floating point number to be formatted
 * @param width The total width of the formatted number
 * @param precision The number of decimals
 * @param unit An optional unit for the number. Must be null-terminated (end on '\0').
 * @returns formatted floating point number as a c-string
 */
const char* formatFloat(float x, int width, int precision, const char* unit){
  static char number_buffer[11];
  dtostrf(x, min(width,6), precision, number_buffer);
  for (int i=0 ; i<4 ; i++) {
    number_buffer[6+i] = unit[i];
    if ( unit[i] == '\0' ) break;
  }
  number_buffer[10] = '\0';
  return number_buffer;
}
