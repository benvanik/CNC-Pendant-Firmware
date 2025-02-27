#include "RotaryEncoder.h"
#include "arduino.h"


int RotaryEncoder::getChange()
{
  noInterrupts();

  int v = enc.read();
  int delta = v/ppc-lastV;

  if(v >= 100*ppc)
  {
    v-=100*ppc;
    enc.write(v);
  }
  if(v <= -100*ppc)
  {
    v+=100*ppc;
    enc.write(v);
  }

    lastV = v/ppc;
  interrupts();


  return delta;
}

// End
