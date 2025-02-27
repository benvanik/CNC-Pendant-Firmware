#ifndef __RotaryEncoderIncluded
#define __RotaryEncoderIncluded

#include <Arduino.h>
#include <Encoder.h>

class RotaryEncoder
{
  Encoder enc;

  int ppc;

  int lastV;

public:
  RotaryEncoder(int p0, int p1, int pulsesPerClick) : 
    enc(p0, p1), ppc(pulsesPerClick), lastV(0) {}

  int getChange();
};

#endif
