#define Device_Name "ESP"                  // String
#define Manufacturer_Name "SouthernLights" // String

#define X_PIN GPIO_NUM_34
#define Y_PIN GPIO_NUM_35
#define SW_PIN GPIO_NUM_32
#define IR_PIN GPIO_NUM_33
#define SDA_PIN GPIO_NUM_25
#define SCL_PIN GPIO_NUM_26
#define INT_PIN GPIO_NUM_27

#include <BleComboKeyboard.h>
#include <BleComboMouse.h>
#include <Arduino_APDS9960.h>
#include <Bounce2.h>

struct config
{
  byte Battery_Value;        // 0-100
  byte LED_Boost;            // 0-3
  byte Gesture_Sens;         // 1-100
  unsigned long IR_Timer;    // in ms (0 to use hardware only)
  char Scroll_Speed;         // unit per send
  byte Prox_Thres;           // 0-255
  unsigned long Prox_Timer;  // in ms (can not be 0)
  unsigned long Right_Dur;   // in ms (can not be 0)
  unsigned long Disable_Dur; // in ms (can not be 0)
  float Dead_Zone;           // in %
  char Point_Speed;          // unit per send (at stick max) 1-127
} cfg = {
    .Battery_Value = 100, // Requiring reboot
    .LED_Boost = 0,       // Requiring reboot
    .Gesture_Sens = 80,   // Requiring reboot
    .IR_Timer = 60000,
    .Scroll_Speed = 10,
    .Prox_Thres = 100,
    .Prox_Timer = 3000,
    .Right_Dur = 500,
    .Disable_Dur = 10000,
    .Dead_Zone = 0.1, // Requiring reboot
    .Point_Speed = 64};

unsigned short midway_X = 0; // 0-4095
unsigned short midway_Y = 0; // 0-4095
unsigned short ddz_X_MIN;
unsigned short ddz_Y_MIN;
unsigned short ddz_X_MAX;
unsigned short ddz_Y_MAX;

bool proximity_triggered = false;
bool enable_stick = false;

volatile unsigned long ir_dur = 0;
unsigned long prox_dur = 0;
unsigned long sw_dur = 0;

BleComboKeyboard Keyboard(Device_Name, Manufacturer_Name, cfg.Battery_Value);
BleComboMouse Mouse(&Keyboard);
APDS9960 APDS(SDA_PIN, SCL_PIN, INT_PIN);
Bounce BUTTON;

void IRAM_ATTR handle_ir();
void lockScreen();
double readVoltage(byte pin);

void setup()
{
  pinMode(IR_PIN, INPUT_PULLDOWN);
  pinMode(INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(IR_PIN), handle_ir, CHANGE);
  BUTTON.attach(SW_PIN, INPUT_PULLDOWN);
  BUTTON.interval(10); // Debounce 10ms
  Keyboard.begin();
  Mouse.begin();
  APDS.begin();
  APDS.setLEDBoost(cfg.LED_Boost);
  APDS.setGestureSensitivity(cfg.Gesture_Sens);
  APDS.gestureAvailable();   // Enable gesture
  APDS.proximityAvailable(); // Enable proximity

  for (byte i = 0; i < 6; i++)
  {
    midway_X += readVoltage(X_PIN);
    midway_Y += readVoltage(Y_PIN);
  }
  midway_X /= 6;
  midway_Y /= 6;
  ddz_X_MIN = midway_X * (1 - cfg.Dead_Zone / 100);
  ddz_Y_MIN = midway_Y * (1 - cfg.Dead_Zone / 100);
  ddz_X_MAX = midway_X * (1 + cfg.Dead_Zone / 100);
  ddz_Y_MAX = midway_Y * (1 + cfg.Dead_Zone / 100);
}

void loop()
{
  if (!Keyboard.isConnected())
    delay(1000);

  if (ir_dur != 0 && millis() - ir_dur > cfg.IR_Timer)
  {
    if (Keyboard.isConnected())
    {
      lockScreen();
      ir_dur = 0;
      delay(10);
    }
  }

  if (APDS.gestureAvailable())
  {
    int gesture = APDS.readGesture();
    if (Keyboard.isConnected())
    {
      switch (gesture)
      {
      case GESTURE_UP:
        Mouse.move(0, 0, 0 - cfg.Scroll_Speed);
        break;

      case GESTURE_DOWN:
        Mouse.move(0, 0, cfg.Scroll_Speed);
        break;

      case GESTURE_LEFT:
        Keyboard.write(KEY_PAGE_DOWN);
        break;

      case GESTURE_RIGHT:
        Keyboard.write(KEY_PAGE_UP);
        break;

      default: // Ignore
        break;
      }
      delay(10);
    }
  }

  if (APDS.proximityAvailable())
  {
    int proximity = APDS.readProximity();
    if (proximity != -1) // If valid (0-255)
    {
      if (proximity < cfg.Prox_Thres)
      {
        if (prox_dur == 0)
          prox_dur = millis();
        else if (millis() - prox_dur > cfg.Prox_Timer)
        {
          proximity_triggered = !proximity_triggered;

          if (Keyboard.isConnected())
            lockScreen();
        }
      }
      else
        prox_dur = 0;
    }
  }

  BUTTON.update();
  if (BUTTON.rose())
  {
    sw_dur = millis();
  }
  else if (BUTTON.fell())
  {
    if (sw_dur != 0 && millis() - sw_dur > cfg.Right_Dur && millis() - sw_dur <= cfg.Disable_Dur)
    {
      if (enable_stick)
      {
        if (Keyboard.isConnected())
        {
          Mouse.click(MOUSE_RIGHT);
          delay(10);
        }
      }
    }
    else if (sw_dur != 0 && millis() - sw_dur > cfg.Disable_Dur)
      enable_stick = !enable_stick;
    else
    {
      if (enable_stick)
      {
        if (Keyboard.isConnected())
        {
          Mouse.click(MOUSE_LEFT);
          delay(10);
        }
      }
    }
    sw_dur = 0;
  }

  if (enable_stick)
  {
    double X_POS = readVoltage(X_PIN);
    double Y_POS = readVoltage(Y_PIN);
    if (X_POS < ddz_X_MIN || Y_POS < ddz_Y_MIN || X_POS > ddz_X_MAX || Y_POS > ddz_Y_MAX)
    {
      if (Keyboard.isConnected())
      {
        //Too simple actually
        Mouse.move(char((X_POS - midway_X) / 4095 * cfg.Point_Speed), char((Y_POS - midway_Y) / 4095 * cfg.Point_Speed));
        delay(10);
      }
    }
  }
}

void IRAM_ATTR handle_ir()
{
  if (digitalRead(IR_PIN) == HIGH) // Rising
    ir_dur = 0;
  else // Falling
    ir_dur = millis();
}

void lockScreen()
{
  Keyboard.press(KEY_LEFT_GUI); // key-left-command/win
  Keyboard.press('l');          // key-l
  delay(100);
  Keyboard.releaseAll();
}

double readVoltage(byte pin)
{
  double reading = analogRead(pin); // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095

  return reading; // No change, for now
}
