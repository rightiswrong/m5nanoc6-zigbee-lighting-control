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
)
EFFECT_LIGHTNING = EFFECTS.index("Lightning Storm")

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


def chain_change_kind(previous: int, current: int) -> str | None:
    if current > previous:
        return "led_added"
    if current < previous:
        return "led_removed"
    return None
