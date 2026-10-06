"""Constants for Spooky Lights."""

from __future__ import annotations

from typing import Final

from homeassistant.const import Platform

from .logic import (  # noqa: F401  (re-exported)
    EFFECT_LIGHTNING,
    EFFECTS,
    MAX_CHAIN,
    OUTPUT_MODES,
    ROLE_COUNT,
    ROLE_DOMAINS,
    ROLE_EFFECT,
    ROLE_LIGHT,
    ROLE_MANUAL,
    ROLE_OUTPUT,
    ROLE_SPEED,
    ROLE_SPOTLIGHT,
)

DOMAIN: Final = "spooky_lights"
MANUFACTURER: Final = "SpookyLab"
MODEL: Final = "NanoC6-Spooky"

CONF_DEVICE_ID: Final = "device_id"

PLATFORMS: Final = [
    Platform.BUTTON,
    Platform.EVENT,
    Platform.LIGHT,
    Platform.NUMBER,
    Platform.SELECT,
    Platform.SENSOR,
]

# Fired on the event bus whenever the number of lit pixels changes.
EVENT_CHAIN_CHANGED: Final = f"{DOMAIN}_chain_changed"

JUMP_SCARE_SECONDS: Final = 8


def signal_chain(entry_id: str) -> str:
    """Dispatcher signal sent when the pixel layout changes."""
    return f"{DOMAIN}_{entry_id}_chain"
