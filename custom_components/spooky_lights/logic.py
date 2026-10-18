"""Pure logic for Spooky Lights — no Home Assistant imports, so it is unit-testable anywhere.

Everything here mirrors the firmware (firmware/NanoC6_SpookyLights): if you change
the effect list or the output routing there, change it here too.
"""

from __future__ import annotations

import re

MAX_CHAIN = 24  # firmware config.h MAX_CHAIN

# Index == the value written to the firmware's "Effect" analog output (endpoint 12).
EFFECTS: tuple[str, ...] = (
    "Solid",
    "Candle Flicker",
    "Ghost Breath",
    "Lightning Storm",
    "Witch's Cauldron",
    "Tell-Tale Heart",
    "Haunted Chase",
    "Poltergeist",
    "Possessed Pumpkin",
    "Blood Moon",
    "Demo Reel",
)
EFFECT_LIGHTNING = EFFECTS.index("Lightning Storm")
EFFECT_DEMO = EFFECTS.index("Demo Reel")

# Index == the value written to the firmware's "Output" analog output (endpoint 14).
OUTPUT_MODES: tuple[str, ...] = ("auto", "onboard", "grove", "both")

# Roles the integration maps to entities exposed by ZHA / Zigbee2MQTT.
ROLE_LIGHT = "light"
ROLE_COUNT = "count_sensor"
ROLE_MANUAL = "manual_number"
ROLE_EFFECT = "effect_number"
ROLE_SPEED = "speed_number"
ROLE_OUTPUT = "output_number"
ROLE_SPOTLIGHT = "spotlight_number"

ROLE_DOMAINS: dict[str, str] = {
    ROLE_LIGHT: "light",
    ROLE_COUNT: "sensor",
    ROLE_MANUAL: "number",
    ROLE_EFFECT: "number",
    ROLE_SPEED: "number",
    ROLE_OUTPUT: "number",
    ROLE_SPOTLIGHT: "number",
}

# Firmware endpoint for each role (config.h EP_*)
ROLE_ENDPOINTS: dict[str, int] = {
    ROLE_LIGHT: 10,
    ROLE_COUNT: 11,
    ROLE_MANUAL: 11,
    ROLE_EFFECT: 12,
    ROLE_SPEED: 13,
    ROLE_OUTPUT: 14,
    ROLE_SPOTLIGHT: 15,
}

# Keyword fallbacks (ZHA names entities from the ZCL "description" attribute set by
# the firmware; the Zigbee2MQTT converter in /zigbee2mqtt uses these property names).
ROLE_KEYWORDS: dict[str, tuple[str, ...]] = {
    ROLE_COUNT: ("detected", "led_count"),
    ROLE_MANUAL: ("manual",),
    ROLE_EFFECT: ("effect_index", "effect"),
    ROLE_SPEED: ("speed",),
    ROLE_OUTPUT: ("output",),
    ROLE_SPOTLIGHT: ("spotlight",),
}

_KEYWORD_PRIORITY = (ROLE_COUNT, ROLE_MANUAL, ROLE_SPEED, ROLE_OUTPUT, ROLE_SPOTLIGHT, ROLE_EFFECT)

_EP_PATTERNS = (
    re.compile(r"[-_](1[0-5])(?:[-_]|$)"),  # ZHA: "<ieee>-12-13" / "<ieee>-10"
    re.compile(r"(?:^|[-_])ep_?(1[0-5])(?:[-_]|$)"),  # "..._ep12" style
)


def to_number(state: str | None) -> float | None:
    """Parse a HA state string into a float (None for unknown/unavailable/garbage)."""
    if state is None:
        return None
    try:
        return float(state)
    except (TypeError, ValueError):
        return None


def to_index(state: str | None, upper: int, default: int = 0) -> int:
    """Parse a numeric state and clamp it into 0..upper (inclusive)."""
    value = to_number(state)
    if value is None:
        return default
    return max(0, min(upper, round(value)))


def effective_chain_count(detected: int, manual: int) -> int:
    """Mirror of the firmware's effectiveChainCount(): a manual count wins."""
    count = manual if manual > 0 else detected
    return max(0, min(MAX_CHAIN, count))


def virtual_pixel_count(chain: int, output_mode: int) -> int:
    """Mirror of Renderer::virtualCount() — how many pixels the effect draws."""
    chain = max(0, min(MAX_CHAIN, chain))
    mode = OUTPUT_MODES[output_mode] if 0 <= output_mode < len(OUTPUT_MODES) else "auto"
    if mode == "onboard":
        return 1
    if mode == "grove":
        return chain
    if mode == "both":
        return 1 + chain
    return chain if chain else 1


def effect_name(index: int) -> str | None:
    return EFFECTS[index] if 0 <= index < len(EFFECTS) else None


def effect_index(name: str) -> int | None:
    try:
        return EFFECTS.index(name)
    except ValueError:
        return None


def endpoint_from_unique_id(unique_id: str) -> int | None:
    for pattern in _EP_PATTERNS:
        if match := pattern.search(unique_id.lower()):
            return int(match.group(1))
    return None


def guess_role(domain: str, unique_id: str, *names: str | None) -> str | None:
    """Guess which Spooky role an entity plays from its domain, unique_id and names."""
    candidates = [role for role, dom in ROLE_DOMAINS.items() if dom == domain]
    if not candidates:
        return None
    if domain == "light":
        return ROLE_LIGHT

    text = " ".join(n for n in (unique_id, *names) if n).lower().replace(" ", "_")
    # Keywords are the most specific signal (ZHA names come from our descriptions).
    # Check specific roles first: "Effect speed" must not be mistaken for "Effect".
    for role in _KEYWORD_PRIORITY:
        if role not in candidates:
            continue
        if any(k in text for k in ROLE_KEYWORDS.get(role, ())):
            return role
    endpoint = endpoint_from_unique_id(unique_id)
    if endpoint is not None:
        matches = [r for r in candidates if ROLE_ENDPOINTS[r] == endpoint]
        if len(matches) == 1:
            return matches[0]
    return None


# --------------------------------------------------------------------- Matter
TRANSPORT_ZIGBEE = "zigbee"
TRANSPORT_MATTER = "matter"
CONF_TRANSPORT = "transport"

# Endpoint plan of the Matter firmware (firmware/SpookyLights/TransportMatter.cpp).
# Six endpoints, because the ESP32-H2 Matter library allows 7 including the root:
# light · effect · speed · spotlight · setup (output + manual count) · LED count.
MATTER_EP_LIGHT = 1
MATTER_EP_EFFECT = 2
MATTER_EP_SPEED = 3
MATTER_EP_SPOTLIGHT = 4
MATTER_EP_SETUP = 5
MATTER_EP_COUNT = 6

MATTER_ROLE_BY_EP: dict[int, tuple[tuple[str, ...], str]] = {  # endpoint → (roles, HA domain)
    MATTER_EP_LIGHT: ((ROLE_LIGHT,), "light"),
    MATTER_EP_EFFECT: ((ROLE_EFFECT,), "light"),
    MATTER_EP_SPEED: ((ROLE_SPEED,), "light"),
    MATTER_EP_SPOTLIGHT: ((ROLE_SPOTLIGHT,), "light"),
    MATTER_EP_SETUP: ((ROLE_OUTPUT, ROLE_MANUAL), "light"),  # one endpoint, two settings
    MATTER_EP_COUNT: ((ROLE_COUNT,), "sensor"),
}

# HA Matter unique_id: "<fabric>-<node>-MatterNodeDevice-<endpoint>-<key>-<cluster>-<attribute>"
_MATTER_EP = re.compile(r"-MatterNodeDevice-(\d+)-")

# Value bands carried on dimmer levels — must equal firmware MatterCodes.h.
BAND_OFFSET = 10
BAND_STEP = 9
BAND_MAX_VALUE = 26
FINE_OFFSET = 10
FINE_STEP = 2
FINE_MAX_VALUE = 99
SETUP_STRIDE = 25


def matter_endpoint(unique_id: str) -> int | None:
    m = _MATTER_EP.search(unique_id)
    return int(m.group(1)) if m else None


def guess_matter_roles(domain: str, unique_id: str) -> tuple[str, ...]:
    """Spooky roles one HA Matter entity plays (empty if it isn't one of ours)."""
    endpoint = matter_endpoint(unique_id)
    hit = MATTER_ROLE_BY_EP.get(endpoint) if endpoint is not None else None
    if hit is None or hit[1] != domain:
        return ()
    if hit[0] == (ROLE_COUNT,) and "-LightSensor-" not in unique_id:
        return ()
    return hit[0]


def band_to_level(value: int) -> int:
    return BAND_OFFSET + BAND_STEP * max(0, min(BAND_MAX_VALUE, value))


def level_to_band(level: int) -> int:
    if level <= BAND_OFFSET:
        return 0
    return min(BAND_MAX_VALUE, (level - BAND_OFFSET + BAND_STEP // 2) // BAND_STEP)


def fine_to_level(value: int) -> int:
    return FINE_OFFSET + FINE_STEP * max(0, min(FINE_MAX_VALUE, value))


def level_to_fine(level: int) -> int:
    if level <= FINE_OFFSET:
        return 0
    return min(FINE_MAX_VALUE, (level - FINE_OFFSET + FINE_STEP // 2) // FINE_STEP)


def pack_setup(output: int, manual: int) -> int:
    return output * SETUP_STRIDE + manual


def unpack_setup(packed: int) -> tuple[int, int]:
    """→ (output mode, manual LED count)"""
    output, manual = divmod(packed, SETUP_STRIDE)
    return min(output, len(OUTPUT_MODES) - 1), min(manual, MAX_CHAIN)


def setup_from_brightness(brightness: int | None) -> tuple[int, int]:
    if brightness is None:
        return 0, 0
    return unpack_setup(level_to_fine(ha_level_for_brightness(brightness)))


def speed_to_level(speed: int) -> int:
    speed = max(1, min(100, speed))
    return max(1, (speed * 254 + 50) // 100)


def level_to_speed(level: int) -> int:
    return max(1, min(100, (level * 100 + 127) // 254))


# Home Assistant's Matter light maps LevelControl 1..254 ↔ brightness 0..255 linearly.
def ha_brightness_for_level(level: int) -> int:
    return max(0, min(255, round((level - 1) * 255 / 253)))


def ha_level_for_brightness(brightness: int) -> int:
    return round(brightness * 253 / 255 + 1)


def band_from_brightness(on: bool, brightness: int | None) -> int:
    if not on or brightness is None:
        return 0
    return level_to_band(ha_level_for_brightness(brightness))


def speed_from_brightness(brightness: int | None) -> int | None:
    return None if brightness is None else level_to_speed(ha_level_for_brightness(brightness))


def count_from_lux(state: str | None) -> int:
    """The firmware reports N detected LEDs as N + 1 lux."""
    value = to_number(state)
    return 0 if value is None else max(0, min(MAX_CHAIN, round(value) - 1))


def chain_change_kind(previous: int, current: int) -> str | None:
    if current > previous:
        return "led_added"
    if current < previous:
        return "led_removed"
    return None
