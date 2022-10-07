#include <LiquidCrystal.h> 

// general
unsigned long int loop_delay = 100; // ms
int lvl(int val, int n_lvls, int n_vals=1023){ return val/(1 + n_vals/n_lvls); }
const char* formatFloat(float x, int width=6, int precision=2, const char* unit="\0");

// control
const int nob_pin = A0, button_pin = 13;

int nob_value = 0;
bool button_ready = true;
bool adjust_using_nob = false;
int cursor_position = 0;

// mfc
const int flow_signal_pin = A5, flow_setpoint_pin = 6;

int flow_signal = 0;
float flow_actual = 0.00;

int setpoint = 0;
int setpoint_actual = 0;

float mfc_range = 100.0;
float corr_factor = 1.39;
float offset = 0.;

bool flow_on = false;

// lcd
const int rs = 12, enable = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
const int backlight_pin = 9, contrast_pin = 10;
const int lcd_width = 16, lcd_height = 2;
LiquidCrystal lcd = LiquidCrystal(rs, enable, d4, d5, d6, d7);

enum class Symbol : int {
  checkmark = 1,
  cross,
  up,
  down,
  select
};

int backlight_timer = 0;
int timeout = 150;
bool backlight_on = true;
int lcd_brightness = 255, lcd_contrast = 255;

void switchBacklight(bool state);
bool buttonPressed();

// custom characters
const byte s_checkmark[8]{0b00000,0b00001,0b00001,0b10010,0b01010,0b00100,0b00000,0b00000};
const byte s_cross[8]{0b00000,0b10001,0b01010,0b00100,0b01010,0b10001,0b00000,0b00000};
const byte s_up[8]{0b00000,0b00100,0b01110,0b10101,0b00100,0b00100,0b00000,0b00000};
const byte s_down[8]{0b00000,0b00100,0b00100,0b10101,0b01110,0b00100,0b00000,0b00000};
const byte s_select[8]{0b00000,0b00100,0b00010,0b11111,0b00010,0b00100,0b00000,0b00000};

void setup() {
  // Serial communication
  Serial.begin(9600);
  Serial.println();
  Serial.print("# flow"); Serial.print(" ");
  Serial.print("zero"); Serial.print(" ");
  Serial.print("sgnl"); Serial.print(" ");
  Serial.print(" spa"); Serial.print(" ");
  Serial.print(" spt"); Serial.print(" ");
  Serial.print(" nob"); Serial.print("\n");
  // LCD pins
  pinMode(backlight_pin, OUTPUT);
  pinMode(contrast_pin, OUTPUT);
  // control pins
  pinMode(nob_pin, INPUT);
  pinMode(button_pin, INPUT);
  // mfc pins
  pinMode(flow_signal_pin, INPUT);
  pinMode(flow_setpoint_pin, OUTPUT);
  // prepare LCD screen
  lcd.begin(lcd_width, lcd_height);
  analogWrite(backlight_pin, lcd_brightness);
  analogWrite(contrast_pin, lcd_contrast);
  // special characters
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
  lcd.clear();
  lcd.setCursor(7, 0); lcd.print("sccm");
  lcd.setCursor(15,0); lcd.print("\%");
  lcd.setCursor(5, 1); lcd.write((int)Symbol::down);
  lcd.setCursor(6, 1); lcd.write((int)Symbol::up);
  lcd.setCursor(8, 1); lcd.write((int)Symbol::checkmark);
  lcd.setCursor(9, 1); lcd.write((int)Symbol::cross);
}
// 0123456789abcdef
//  100.00sccm 100%
void update(){
  flow_signal = analogRead(flow_signal_pin);
  if ( flow_on ) {
    analogWrite(flow_setpoint_pin, setpoint_actual);
  } else {
    analogWrite(flow_setpoint_pin, 0);
  }
  flow_actual = corr_factor*(flow_signal/1023.0*5.0+offset)/5.0*mfc_range;
  lcd.setCursor(1, 0); lcd.print(formatFloat(flow_actual, 6, 2));
  lcd.setCursor(12,0); lcd.print(formatFloat(flow_on ? setpoint_actual/2.55 : 0, 3, 0));
  lcd.setCursor(1, 1); lcd.print(formatFloat(setpoint,3,0));
  lcd.setCursor(12,1); lcd.print(flow_on ? "on " : "off");

  nob_value = analogRead(nob_pin);
  if ( adjust_using_nob ){
    lcd.setCursor(0, 1);
    setpoint = lvl(nob_value, 256);
    if ( buttonPressed() ){
      lcd.noCursor();
      adjust_using_nob = false;
    }
  } else {
    cursor_position = lvl(nob_value, 7);
    switch ( cursor_position ){
      case 0:
        lcd.noCursor();
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.write((int)Symbol::select);
        if ( buttonPressed() ) {
          adjust_using_nob = true;
          lcd.setCursor(0, 1); lcd.cursor();
        }
        break;
      case 1:
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(5,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = (setpoint - 1)%256;
        }
        break;
      case 2:
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(6,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = (setpoint + 1)%256;
        }
        break;
      case 3:
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.print(" ");
        lcd.setCursor(8,1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint_actual = setpoint;
        }
        break;
      case 4:
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1);lcd.print(" ");
        lcd.setCursor(9, 1); lcd.cursor();
        if ( buttonPressed() ) {
          setpoint = setpoint_actual;
        }
        break;
      case 5:
        lcd.noCursor();
        lcd.setCursor(0, 0); lcd.print(" ");
        lcd.setCursor(0, 1); lcd.print(" ");
        lcd.setCursor(11, 1); lcd.write((int)Symbol::select);
        if ( buttonPressed() ){
          flow_on = !flow_on;
        }
        break;
      case 6:
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

void log(){
  Serial.print(formatFloat(flow_actual, 6, 2)); Serial.print(" ");
  Serial.print(formatFloat(offset, 4, 2)); Serial.print(" ");
  Serial.print(formatFloat(flow_signal, 4, 0)); Serial.print(" ");
  Serial.print(formatFloat(setpoint_actual, 3, 0)); Serial.print(" ");
  Serial.print(formatFloat(setpoint, 3, 0)); Serial.print(" ");
  Serial.print(formatFloat(nob_value, 4, 0)); Serial.print("\n");
}

void loop() {
  static unsigned long time = millis();
  update();
  log();
  if( backlight_timer < timeout ){
    backlight_timer++;
  }else{
    switchBacklight(false);
  }
  time = min(millis() - time, 1000);
  delay(min(loop_delay - time, loop_delay));
}

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
