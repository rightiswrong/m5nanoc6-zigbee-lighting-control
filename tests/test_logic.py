"""Unit tests for the HA-free logic module, plus firmware ⇄ integration sync checks.

Runs with the standard library only:  python3 -m unittest discover -s tests -v
"""

from __future__ import annotations

import importlib.util
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "firmware" / "NanoC6_SpookyLights"
_spec = importlib.util.spec_from_file_location(
    "spooky_logic", ROOT / "custom_components" / "spooky_lights" / "logic.py"
)
logic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(logic)

IEEE = "00:12:4b:00:2a:3b:4c:5d"


class FirmwareSyncTests(unittest.TestCase):
    """If someone edits the firmware, these fail until the integration follows."""

    def test_effect_names_match_firmware(self):
        src = (FW / "Effects.cpp").read_text()
        block = re.search(r"EFFECT_NAMES\[FX_COUNT\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
        names = re.findall(r'"((?:[^"\\]|\\.)*)"', block)
        self.assertEqual(tuple(names), logic.EFFECTS)

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


if __name__ == "__main__":
    unittest.main()
