// CNC pendant interface to Duet
// D Crocker, started 2020-05-04

//#define DEBUG

/* This Arduino sketch can be run on either Arduino Nano or Arduino Pro Micro. 

*** Pendant to Arduino Pro Micro connections ***

Pro Micro Pendant   Wire colours
VCC       +5V       red
GND       0V,       black
          COM,      orange/black
          CN,       blue/black
          LED-      white/black

D2        A         green
D3        B         white
D4        X         yellow
D5        Y         yellow/black
D6        Z         brown
D7        4         brown/black
D8        5         powder (if present)
D9        6         powder/black (if present)
D10       LED+      green/black
A0        STOP      blue
A1        X1        grey
A2        X10       grey/black
A3        X100      orange

NC        /A,       violet
          /B        violet/black

*** Arduino Pro Micro to Duet PanelDue connector connections ***

Pro Micro Duet
VCC       +5V
GND       GND
TX1/D0    Through 6K8 resistor to URXD, also connect 10K resistor between URXD and GND

To connect a PanelDue as well:

PanelDue +5V to +5V/VCC
PanelDue GND to GND
PanelDue DIN to Duet UTXD or IO_0_OUT
PanelDue DOUT to /Pro Micro RX1/D0.

*** Pendant to Arduino Nano connections ***

Nano    Pendant   Wire colours
+5V     +5V       red
GND     0V,       black
        COM,      orange/black
        CN,       blue/black
        LED-      white/black

D2      A         green
D3      B         white
D4      X         yellow
D5      Y         yellow/black
D6      Z         brown
D7      4         brown/black
D8      5         powder (if present)
D9      6         powder/black (if present)
D10     X1        grey
D11     X10       grey/black
D12     X100      orange
D13     LED+      green/black
A0      STOP      blue

NC      /A,       violet
        /B        violet/black

*** Arduino Nano to Duet PanelDue connector connections ***

Nano    Duet
+5V     +5V
GND     GND
TX1/D0  Through 6K8 resistor to URXD, also connect 10K resistor between URXD and GND

To connect a PanelDue as well:

PanelDue +5V to +5V
PanelDue GND to GND
PanelDue DIN to Duet UTXD or IO_0_OUT
PanelDue DOUT to Nano/Pro Micro RX1/D0.

On the Arduino Nano is necessary to replace the 1K resistor between the USB interface chip by a 10K resistor so that PanelDiue can override the USB chip.
On Arduino Nano clones with CH340G chip, it is also necessary to remove the RxD LED or its series resistor.

*/

// Configuration constants
const int PinA = 2;
const int PinB = 3;
const int PinX = 4;
const int PinY = 5;
const int PinZ = 6;
const int PinAxis4 = 7;
const int PinAxis5 = 8;
const int PinAxis6 = 9;
const int PinStop = A0;
int btnState = HIGH;

#if defined(__AVR_ATmega32U4__)     // Arduino Micro, Pro Micro or Leonardo
const int PinTimes1 = A1;
const int PinTimes10 = A2;
const int PinTimes100 = A3;
const int PinLed = 10;
#endif

#if defined(__AVR_ATmega328P__)     // Arduino Nano or Uno
const int PinTimes1 = 10;
const int PinTimes10 = 11;
const int PinTimes100 = 12;
const int PinLed = 13;
#endif

const unsigned long BaudRate = 57600;
const int PulsesPerClick = 4;
const unsigned long MinCommandInterval = 20;

// Table of commands we send, one entry for each axis
const char* const MoveCommands[] =
{
  "G91 G0 F6000 X",     // X axis
  "G91 G0 F6000 Y",     // Y axis
  "G91 G0 F600 Z",      // Z axis
  "G91 G0 F6000 U",     // axis 4
  "G91 G0 F6000 V",     // axis 5
  "G91 G0 F6000 W"      // axis 6
};

// WARNING: prevents attachInterrupt from working.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>

struct RotaryEncoder {
  Encoder enc;
  int ppc;
  int lastV;

  RotaryEncoder(int p0, int p1, int pulsesPerClick) :
    enc(p0, p1), ppc(pulsesPerClick), lastV(0) {}

  int getChange() {
    noInterrupts();

    int v = enc.read();
    int delta = v / ppc - lastV;

    if (v >= 100*ppc) {
      v -= 100 * ppc;
      enc.write(v);
    }
    if (v <= -100*ppc) {
      v += 100 * ppc;
      enc.write(v);
    }

    lastV = v / ppc;
    interrupts();

    return delta;
  }
};

#include "GCodeSerial.h"

RotaryEncoder encoder(PinA, PinB, PulsesPerClick);

int serialBufferSize;
int distanceMultiplier;
int axis;
uint32_t whenLastCommandSent = 0;

const int axisPins[] = { PinX, PinY, PinZ, PinAxis4 };
const int feedAmountPins[] = { PinTimes1, PinTimes10, PinTimes100 };

#if defined(__AVR_ATmega32U4__)     // Arduino Leonardo or Pro Micro
# define UartSerial   Serial1
#elif defined(__AVR_ATmega328P__)   // Arduino Uno or Nano
# define UartSerial   Serial
#endif

GCodeSerial output(UartSerial);

void setup()
{
#if defined(DEBUG)
  Serial.begin(9600);
  while (!Serial) {}
  delay(1000);
  Serial.println("setup()");
#endif  // DEBUG

  pinMode(PinA, INPUT_PULLUP);
  pinMode(PinB, INPUT_PULLUP);
  pinMode(PinX, INPUT_PULLUP);
  pinMode(PinY, INPUT_PULLUP);
  pinMode(PinZ, INPUT_PULLUP);
  pinMode(PinAxis4, INPUT_PULLUP);
  pinMode(PinAxis5, INPUT_PULLUP);
  pinMode(PinAxis6, INPUT_PULLUP);
  pinMode(PinTimes1, INPUT_PULLUP);
  pinMode(PinTimes10, INPUT_PULLUP);
  pinMode(PinTimes100, INPUT_PULLUP);
  pinMode(PinStop, INPUT_PULLUP);
  pinMode(PinLed, OUTPUT);
  output.begin(BaudRate);

  serialBufferSize = output.availableForWrite();

#if defined(__AVR_ATmega32U4__)     // Arduino Leonardo or Pro Micro
  TX_RX_LED_INIT;
#endif

#if defined(DEBUG)
  Serial.println("~setup()");
#endif  // DEBUG
}

void loop()
{
  // 1. Check for emergency stop
  if (digitalRead(PinStop) == HIGH) {
    // Send emergency stop command every 2 seconds
#if defined(DEBUG)
    Serial.println("e-stop begin");
#endif  // DEBUG
    do {
      output.write("M112 ;" "\xF0" "\x0F" "\n");
      digitalWrite(PinLed, LOW);
      encoder.getChange();      // ignore any movement
    } while (digitalRead(PinStop) == HIGH);

    output.write("M999\n");
#if defined(DEBUG)
    Serial.println("e-stop end");
#endif  // DEBUG
  }

  digitalWrite(PinLed, HIGH);

  // 2. Poll the feed amount switch
  distanceMultiplier = 0;
  int localDistanceMultiplier = 1;
  for (int pin : feedAmountPins)
  {
    if (digitalRead(pin) == LOW)
    {
      distanceMultiplier = localDistanceMultiplier;
      break;
    }
    localDistanceMultiplier *= 10;
  }

  // 3. Poll the axis selector switch
  axis = -1;
  int localAxis = 0;
  for (int pin : axisPins) {
    if (digitalRead(pin) == LOW) {
      axis = localAxis;
      break;
    } else if (digitalRead(PinAxis5) == LOW) {
      delay(1000);
      btnState = digitalRead(PinAxis5);
      if (btnState == LOW) {
#if defined(DEBUG)
        Serial.println("G28");
#endif  // DEBUG
        output.write("G28\n");
      }
    } else if (digitalRead(PinAxis6) == LOW) {
      delay(1000);
      btnState = digitalRead(PinAxis6);
      if (btnState == LOW) {
#if defined(DEBUG)
        Serial.println("G27");
#endif  // DEBUG
        output.write("G27\n");
      }
    }
    ++localAxis;
  }

  // 5. If the serial output buffer is empty, send a G0 command for the accumulated encoder motion.
  if (output.availableForWrite() == serialBufferSize)
  {
#if defined(__AVR_ATmega32U4__)       // Arduino Micro, Pro Micro or Leonardo
    // turn off transmit LED
    pinMode(LED_BUILTIN_TX, INPUT);
#endif
    const uint32_t now = millis();
    if (now - whenLastCommandSent >= MinCommandInterval) {
      int distance = encoder.getChange() * distanceMultiplier;
      if (axis >= 0 && distance != 0) {
#if defined(__AVR_ATmega32U4__)     // Arduino Micro, Pro Micro or Leonardo
        // turn on transmit LED
        pinMode(LED_BUILTIN_TX, OUTPUT);
        digitalWrite( LED_BUILTIN_TX, LOW);
#endif
        whenLastCommandSent = now;
        output.write(MoveCommands[axis]);
#if defined(DEBUG)
        Serial.print(MoveCommands[axis]);
        Serial.println(distance);
#endif  // DEBUG
        if (distance < 0) {
          output.write('-');
          distance = -distance;
        }
        output.print(distance/10);
        output.write('.');
        output.print(distance % 10);
        output.write('\n');
      }
    }
  }
}

// End
