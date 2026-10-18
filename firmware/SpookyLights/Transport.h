// =============================================================================
//  Transport — the radio side. Exactly one implementation is compiled in:
//    TransportZigbee.cpp  (Tools > Zigbee mode = ZCZR)
//    TransportMatter.cpp  (Tools > Zigbee mode = Disabled → Matter over Thread)
//
//  Contract: callbacks from the radio task only write g_show (and the
//  transport's own "what the controller already knows" copy). loop() compares
//  g_show with that copy and publishes the differences — so a button press, a
//  restored setting or a controller command all reach the controller the same
//  way, and nothing calls back into the radio stack from its own task.
// =============================================================================
#pragma once
#include <stdint.h>

class Transport {
public:
  virtual ~Transport() {}
  virtual const char *name() const = 0;                     // for the log
  virtual void begin(uint8_t detectedLeds) = 0;              // start the stack
  virtual void loop(uint32_t now, uint8_t detectedLeds) = 0; // publish changes
  virtual bool connected() = 0;                              // paired & online
  virtual void factoryReset() = 0;                           // leave the network
};

Transport &radio();
