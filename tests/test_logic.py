"""Unit tests for the HA-free logic module, plus firmware ⇄ integration sync checks.

Runs with the standard library only:  python3 -m unittest discover -s tests -v
"""

from __future__ import annotations

import importlib.util
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "SpookyLights"
_spec = importlib.util.spec_from_file_location(
    "spooky_logic", ROOT / "custom_components" / "spooky_lights" / "logic.py"
)
logic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(logic)

IEEE = "00:12:4b:00:2a:3b:4c:5d"
sys.path.insert(0, str(ROOT / "tools"))
import matter_codes  # noqa: E402
import matter_label  # noqa: E402


class FirmwareSyncTests(unittest.TestCase):
    """If someone edits the firmware, these fail until the integration follows."""

    def test_effect_names_match_firmware(self):
        src = (FW / "Effects.cpp").read_text()
        block = re.search(r"EFFECT_NAMES\[FX_COUNT\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
        names = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
        self.assertEqual(tuple(names), logic.EFFECTS)

    def test_h2_matter_is_refused_at_build_time(self):
        cfg = (FW / "config.h").read_text()
        self.assertRegex(cfg, r"#if SPOOKY_MATTER && defined\(CONFIG_IDF_TARGET_ESP32H2\)\s*\n#error")

    def test_max_chain_matches_firmware(self):
        cfg = (FW / "config.h").read_text()
        self.assertEqual(int(re.search(r"#define MAX_CHAIN\s+(\d+)", cfg).group(1)), logic.MAX_CHAIN)

    def test_endpoints_match_firmware(self):
        cfg = (FW / "config.h").read_text()
        eps = {k: int(v) for k, v in re.findall(r"#define EP_(\w+)\s+(\d+)", cfg)}
        expected = {
            logic.ROLE_LIGHT: eps["LIGHT"],
            logic.ROLE_COUNT: eps["COUNT"],
            logic.ROLE_MANUAL: eps["COUNT"],
            logic.ROLE_EFFECT: eps["EFFECT"],
            logic.ROLE_SPEED: eps["SPEED"],
            logic.ROLE_OUTPUT: eps["OUTPUT"],
            logic.ROLE_SPOTLIGHT: eps["SPOTLIGHT"],
        }
        self.assertEqual(expected, logic.ROLE_ENDPOINTS)

    def test_output_mode_order_matches_firmware(self):
        src = (FW / "Renderer.h").read_text()
        order = re.findall(r"OUT_(AUTO|ONBOARD|GROVE|BOTH)\s*=\s*(\d)", src)
        self.assertEqual([m.lower() for m, _ in sorted(order, key=lambda t: int(t[1]))], list(logic.OUTPUT_MODES))


class LayoutTests(unittest.TestCase):
    def test_virtual_pixel_count_table(self):
        cases = {
            (0, 0): 1,
            (5, 0): 5,  # auto: onboard when no chain, else chain
            (0, 1): 1,
            (5, 1): 1,  # onboard only
            (0, 2): 0,
            (5, 2): 5,  # grove only
            (0, 3): 1,
            (5, 3): 6,  # both
            (99, 3): 25,  # clamped to MAX_CHAIN + onboard
        }
        for (chain, mode), want in cases.items():
            with self.subTest(chain=chain, mode=mode):
                self.assertEqual(logic.virtual_pixel_count(chain, mode), want)

    def test_manual_count_wins(self):
        self.assertEqual(logic.effective_chain_count(detected=3, manual=0), 3)
        self.assertEqual(logic.effective_chain_count(detected=3, manual=7), 7)
        self.assertEqual(logic.effective_chain_count(detected=3, manual=500), logic.MAX_CHAIN)

    def test_state_parsing(self):
        self.assertEqual(logic.to_index("6.0", 9), 6)
        self.assertEqual(logic.to_index("99", 9), 9)
        self.assertEqual(logic.to_index("-4", 9), 0)
        self.assertEqual(logic.to_index("unavailable", 9, default=2), 2)
        self.assertEqual(logic.to_index(None, 9), 0)

    def test_chain_change_kind(self):
        self.assertEqual(logic.chain_change_kind(3, 5), "led_added")
        self.assertEqual(logic.chain_change_kind(5, 2), "led_removed")
        self.assertIsNone(logic.chain_change_kind(4, 4))

    def test_effect_lookup(self):
        self.assertEqual(logic.effect_index("Lightning Storm"), logic.EFFECT_LIGHTNING)
        self.assertEqual(logic.effect_name(logic.EFFECT_LIGHTNING), "Lightning Storm")
        self.assertIsNone(logic.effect_index("Disco"))
        self.assertIsNone(logic.effect_name(42))


class GuessRoleTests(unittest.TestCase):
    """Entity auto-mapping for ZHA (descriptions → names) and Zigbee2MQTT (property names)."""

    def test_zha_with_names(self):
        cases = [
            ("light", f"{IEEE}-10", "Light", logic.ROLE_LIGHT),
            ("sensor", f"{IEEE}-11-12", "Grove LEDs detected", logic.ROLE_COUNT),
            ("number", f"{IEEE}-11-13", "Manual LED count (0 = auto)", logic.ROLE_MANUAL),
            ("number", f"{IEEE}-12-13", "Effect", logic.ROLE_EFFECT),
            ("number", f"{IEEE}-13-13", "Effect speed", logic.ROLE_SPEED),
            ("number", f"{IEEE}-14-13", "Output (0 auto,1 onboard,2 grove,3 both)", logic.ROLE_OUTPUT),
            ("number", f"{IEEE}-15-13", "Spotlight pixel (0 = off)", logic.ROLE_SPOTLIGHT),
        ]
        for domain, uid, name, want in cases:
            with self.subTest(name=name):
                self.assertEqual(logic.guess_role(domain, uid, name), want)

    def test_zha_endpoint_only(self):
        # No descriptive names (e.g. renamed by the user) → endpoint number decides
        self.assertEqual(logic.guess_role("number", f"{IEEE}-12-13", None), logic.ROLE_EFFECT)
        self.assertEqual(logic.guess_role("number", f"{IEEE}-11-13", None), logic.ROLE_MANUAL)
        self.assertEqual(logic.guess_role("number", f"{IEEE}-15-13", None), logic.ROLE_SPOTLIGHT)
        self.assertEqual(logic.guess_role("sensor", f"{IEEE}-11-12", None), logic.ROLE_COUNT)

    def test_zigbee2mqtt_property_names(self):
        base = "0x00124b002a3b4c5d"
        cases = [
            ("sensor", f"{base}_led_count_count_zigbee2mqtt", logic.ROLE_COUNT),
            ("number", f"{base}_manual_led_count_count_zigbee2mqtt", logic.ROLE_MANUAL),
            ("number", f"{base}_effect_index_effect_zigbee2mqtt", logic.ROLE_EFFECT),
            ("number", f"{base}_effect_speed_speed_zigbee2mqtt", logic.ROLE_SPEED),
            ("number", f"{base}_output_mode_output_zigbee2mqtt", logic.ROLE_OUTPUT),
            ("number", f"{base}_spotlight_pixel_spotlight_zigbee2mqtt", logic.ROLE_SPOTLIGHT),
        ]
        for domain, uid, want in cases:
            with self.subTest(uid=uid):
                self.assertEqual(logic.guess_role(domain, uid), want)

    def test_unrelated_entities_ignored(self):
        self.assertIsNone(logic.guess_role("sensor", f"{IEEE}-1-1794", "LQI"))
        self.assertIsNone(logic.guess_role("update", f"{IEEE}-1-25", "Firmware"))


MATTER_NODE = "6F1A2B3C4D5E6F70-00000000000000A1-MatterNodeDevice"


class MatterMappingTests(unittest.TestCase):
    """HA Matter unique_ids → Spooky roles, by the firmware's endpoint plan."""

    def test_endpoint_plan_matches_firmware(self):
        src = (FW / "TransportMatter.cpp").read_text()
        endpoints = {"light": 1, "effectEp": 2, "speedEp": 3, "spotEp": 4, "setupEp": 5, "countEp": 6}
        order = [m for m in re.findall(r"^\s+(?:ok \+= |if \(!)?(\w+)\.begin\(", src, re.M) if m in endpoints]
        self.assertEqual(order, list(endpoints))  # creation order = endpoint numbers
        self.assertEqual(
            [logic.MATTER_EP_LIGHT, logic.MATTER_EP_EFFECT, logic.MATTER_EP_SPEED, logic.MATTER_EP_SPOTLIGHT,
             logic.MATTER_EP_SETUP, logic.MATTER_EP_COUNT],
            list(endpoints.values()),
        )  # fmt: skip
        # The most restrictive Arduino Matter targets allow 7 endpoints including the root.
        self.assertLessEqual(len(endpoints) + 1, 7)
        self.assertIn("MATTER_APP_ENDPOINTS = 6", src)

    def test_guess_matter_roles(self):
        cases = [
            ("light", f"{MATTER_NODE}-1-MatterLight-6-0", (logic.ROLE_LIGHT,)),
            ("light", f"{MATTER_NODE}-2-MatterLight-6-0", (logic.ROLE_EFFECT,)),
            ("light", f"{MATTER_NODE}-3-MatterLight-6-0", (logic.ROLE_SPEED,)),
            ("light", f"{MATTER_NODE}-4-MatterLight-6-0", (logic.ROLE_SPOTLIGHT,)),
            ("light", f"{MATTER_NODE}-5-MatterLight-6-0", (logic.ROLE_OUTPUT, logic.ROLE_MANUAL)),
            ("sensor", f"{MATTER_NODE}-6-LightSensor-1024-0", (logic.ROLE_COUNT,)),
        ]
        for domain, uid, want in cases:
            with self.subTest(uid=uid):
                self.assertEqual(logic.guess_matter_roles(domain, uid), want)
        # things that aren't ours
        self.assertEqual(logic.guess_matter_roles("button", f"{MATTER_NODE}-1-IdentifyButton-3-1"), ())
        self.assertEqual(logic.guess_matter_roles("sensor", f"{MATTER_NODE}-0-MatterUpdate-40-10"), ())
        self.assertEqual(logic.guess_matter_roles("switch", f"{MATTER_NODE}-2-MatterPlug-6-0"), ())
        self.assertEqual(logic.guess_matter_roles("light", f"{IEEE}-10"), ())

    def test_band_constants_match_firmware(self):
        hdr = (FW / "MatterCodes.h").read_text()
        consts = {k: int(v) for k, v in re.findall(r"constexpr uint8_t ((?:BAND|FINE|SETUP)_\w+) = (\d+);", hdr)}
        self.assertEqual(consts, {"BAND_OFFSET": logic.BAND_OFFSET, "BAND_STEP": logic.BAND_STEP,
                                  "BAND_MAX_VALUE": logic.BAND_MAX_VALUE, "FINE_OFFSET": logic.FINE_OFFSET,
                                  "FINE_STEP": logic.FINE_STEP, "FINE_MAX_VALUE": logic.FINE_MAX_VALUE,
                                  "SETUP_STRIDE": logic.SETUP_STRIDE})  # fmt: skip

    def test_values_survive_home_assistant_brightness(self):
        for level in range(1, 255):  # HA's level ↔ brightness conversion is exact
            self.assertEqual(logic.ha_level_for_brightness(logic.ha_brightness_for_level(level)), level)
        for v in range(logic.BAND_MAX_VALUE + 1):
            b = logic.ha_brightness_for_level(logic.band_to_level(v))
            self.assertEqual(logic.band_from_brightness(v != 0, b), v)
        for speed in range(1, 101):
            b = logic.ha_brightness_for_level(logic.speed_to_level(speed))
            self.assertEqual(logic.speed_from_brightness(b), speed)
        for output in range(len(logic.OUTPUT_MODES)):
            for manual in range(logic.MAX_CHAIN + 1):
                b = logic.ha_brightness_for_level(logic.fine_to_level(logic.pack_setup(output, manual)))
                self.assertEqual(logic.setup_from_brightness(b), (output, manual))
        self.assertEqual(logic.band_from_brightness(False, 200), 0)
        self.assertLessEqual(logic.pack_setup(len(logic.OUTPUT_MODES) - 1, logic.MAX_CHAIN), logic.FINE_MAX_VALUE)

    def test_count_from_lux(self):
        self.assertEqual(logic.count_from_lux("1.0"), 0)
        self.assertEqual(logic.count_from_lux("4.0"), 3)
        self.assertEqual(logic.count_from_lux("25.0"), 24)
        self.assertEqual(logic.count_from_lux("unavailable"), 0)


class OnboardingCodeTests(unittest.TestCase):
    def test_chip_reference_vector(self):
        p = matter_codes.SetupPayload(0xFFF1, 0x8000, 3840, 20202021)
        self.assertEqual(matter_codes.encode_qr(p), "MT:Y.K9042C00KA0648G00")
        self.assertEqual(matter_codes.encode_manual(p), "34970112332")
        self.assertEqual(matter_codes.decode_qr("MT:Y.K9042C00KA0648G00"), p)

    def test_round_trip_and_passcode_rules(self):
        for disc, pin in [(0, 1), (0xFFF, 99999998), (2311, 78546665)]:
            p = matter_codes.SetupPayload(0xFFF1, 0x8005, disc, pin)
            self.assertEqual(matter_codes.decode_qr(matter_codes.encode_qr(p)), p)
        for bad in (0, 11111111, 12345678, 87654321, 100000000):
            self.assertFalse(matter_codes.is_valid_passcode(bad))

    def test_label_from_serial_log(self):
        log = (
            "  ==== NanoC6 Spooky Lights — ready to pair (Matter) ====\n"
            "  QR payload          : MT:6FCJ1MBW17KGJG03P10\n"
        )
        lb = matter_label.parse_log(log)
        self.assertEqual((lb.payload, lb.board, lb.manual), ("MT:6FCJ1MBW17KGJG03P10", "NanoC6", "2181-534-7940"))
        svg = matter_label.sheet_svg([lb])
        self.assertIn("2181-534-7940", svg)
        self.assertTrue(svg.startswith("<svg"))


if __name__ == "__main__":
    unittest.main()


class CourierTests(unittest.TestCase):
    """Delivering on/off to a battery-build light that sleeps between check-ins."""

    def test_delivered_straight_away(self):
        c = logic.Courier(0, heard=("t0",))
        self.assertEqual(c.step(1, True, ("t0",)), "done")

    def test_resends_when_the_device_checks_in(self):
        c = logic.Courier(0, heard=("t0",), resend=30)
        self.assertEqual(c.step(1, False, ("t0",)), "wait")
        self.assertEqual(c.step(5, False, ("t1",)), "send")  # it reported in: send now
        self.assertEqual(c.step(6, False, ("t1",)), "wait")  # no repeat for the same report
        self.assertEqual(c.step(8, True, ("t1",)), "done")

    def test_fallback_resend_and_give_up(self):
        c = logic.Courier(0, heard=(), window=100, resend=30)
        self.assertEqual(c.step(29, False, ()), "wait")
        self.assertEqual(c.step(30, False, ()), "send")
        self.assertEqual(c.step(59, False, ()), "wait")
        self.assertEqual(c.step(60, False, ()), "send")
        self.assertEqual(c.step(100, False, ()), "give_up")

    def test_window_outlasts_several_checkins(self):
        text = (FW / "config.h").read_text()
        interval = int(re.search(r"#define CHECKIN_INTERVAL_S\s+(\d+)", text).group(1))
        self.assertGreaterEqual(logic.DELIVERY_WINDOW_S, 3 * interval)
        self.assertLess(logic.DELIVERY_RESEND_S, interval)
