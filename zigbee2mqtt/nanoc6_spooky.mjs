// Zigbee2MQTT external converter for NanoC6 Spooky Lights (OPTIONAL).
//
// ZHA users don't need this — every endpoint uses standard ZCL clusters and ZHA
// creates the entities on its own. Zigbee2MQTT doesn't expose unknown devices'
// analog endpoints, so drop this file into  <z2m data>/external_converters/
// and restart Zigbee2MQTT (2.x loads that folder automatically).
//
// Status: written against zigbee-herdsman-converters' modernExtend API but not
// yet tested on real Zigbee2MQTT hardware — please open an issue if an expose
// misbehaves.

import * as m from "zigbee-herdsman-converters/lib/modernExtend";

const analogOut = (name, endpoint, min, max, description) =>
  m.numeric({
    name,
    cluster: "genAnalogOutput",
    attribute: "presentValue",
    endpointNames: [endpoint],
    valueMin: min,
    valueMax: max,
    valueStep: 1,
    access: "ALL",
    description,
  });

export default {
  zigbeeModel: ["NanoC6-Spooky"],
  model: "NanoC6-Spooky",
  vendor: "SpookyLab",
  description: "NanoC6 Halloween light: onboard WS2812 + hot-pluggable Grove chainable LEDs",
  extend: [
    m.deviceEndpoints({
      endpoints: { light: 10, count: 11, effect: 12, speed: 13, output: 14, spotlight: 15 },
    }),
    m.light({ color: true, endpointNames: ["light"] }),
    m.numeric({
      name: "led_count",
      cluster: "genAnalogInput",
      attribute: "presentValue",
      endpointNames: ["count"],
      access: "STATE_GET",
      reporting: { min: 0, max: 300, change: 1 },
      description: "Grove LEDs detected on the chain (live)",
    }),
    analogOut("manual_led_count", "count", 0, 24, "Force a Grove LED count (0 = auto-detect)"),
    analogOut("effect_index", "effect", 0, 9, "Effect index (see README for names)"),
    analogOut("effect_speed", "speed", 1, 100, "Effect speed, 50 = normal"),
    analogOut("output_mode", "output", 0, 3, "0 auto, 1 onboard, 2 grove, 3 both"),
    analogOut("spotlight_pixel", "spotlight", 0, 25, "Pixel that holds the light colour (0 = off)"),
  ],
};
