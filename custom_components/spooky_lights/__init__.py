"""Spooky Lights — Halloween effects for the NanoC6 Zigbee light.

The NanoC6 firmware exposes plain ZCL clusters (a colour light plus a handful of
Analog Input/Output endpoints), which ZHA or Zigbee2MQTT turn into raw entities
like "number: 3". This integration sits on top of those entities and gives you:

* a light with a named effect list (usable from any light card / scene),
* an effect picker and output-routing picker,
* a spotlight control whose range follows the number of LEDs actually plugged in,
* an event entity + bus event when a Grove LED is added or removed,
* a "jump scare" button.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, State, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.event import async_track_state_change_event

from .const import (
    CONF_DEVICE_ID,
    DOMAIN,
    EVENT_CHAIN_CHANGED,
    MANUFACTURER,
    MAX_CHAIN,
    MODEL,
    OUTPUT_MODES,
    PLATFORMS,
    ROLE_COUNT,
    ROLE_DOMAINS,
    ROLE_MANUAL,
    ROLE_OUTPUT,
    ROLE_SPOTLIGHT,
    signal_chain,
)
from .logic import effective_chain_count, to_index, virtual_pixel_count

_LOGGER = logging.getLogger(__name__)

type SpookyConfigEntry = ConfigEntry[SpookyRuntime]


@dataclass
class Layout:
    """Snapshot of how many pixels the firmware is drawing."""

    detected: int = 0
    manual: int = 0
    chain: int = 0
    output_mode: int = 0
    pixels: int = 1


@dataclass
class SpookyRuntime:
    """Per-entry runtime data shared by all platforms."""

    hass: HomeAssistant
    entry_id: str
    roles: dict[str, str]
    device_info: DeviceInfo
    device_id: str | None
    layout: Layout = field(default_factory=Layout)

    def entity(self, role: str) -> str | None:
        return self.roles.get(role) or None

    def state(self, role: str) -> State | None:
        entity_id = self.entity(role)
        return self.hass.states.get(entity_id) if entity_id else None

    def compute_layout(self) -> Layout:
        detected_state = self.state(ROLE_COUNT)
        manual_state = self.state(ROLE_MANUAL)
        output_state = self.state(ROLE_OUTPUT)
        detected = to_index(detected_state.state if detected_state else None, MAX_CHAIN)
        manual = to_index(manual_state.state if manual_state else None, MAX_CHAIN)
        output = to_index(output_state.state if output_state else None, len(OUTPUT_MODES) - 1)
        chain = effective_chain_count(detected, manual)
        return Layout(detected, manual, chain, output, virtual_pixel_count(chain, output))

    async def async_set_number(self, role: str, value: float) -> None:
        """Write a value to one of the firmware's Analog Output endpoints."""
        entity_id = self.entity(role)
        if not entity_id:
            return
        await self.hass.services.async_call(
            "number", "set_value", {"entity_id": entity_id, "value": value}, blocking=True
        )


def _build_device_info(hass: HomeAssistant, entry: ConfigEntry) -> tuple[DeviceInfo, str | None]:
    """Attach our entities to the existing ZHA/Z2M device when we know it."""
    device_id = entry.data.get(CONF_DEVICE_ID)
    if device_id and (device := dr.async_get(hass).async_get(device_id)):
        if device.identifiers:
            return DeviceInfo(identifiers=set(device.identifiers)), device_id
        if device.connections:
            return DeviceInfo(connections=set(device.connections)), device_id
    return (
        DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)},
            manufacturer=MANUFACTURER,
            model=MODEL,
            name=entry.title,
        ),
        None,
    )


def roles_from_entry(entry: ConfigEntry) -> dict[str, str]:
    """Options override data, so re-mapping entities doesn't need a re-add."""
    merged = {**entry.data, **entry.options}
    return {role: merged[role] for role in ROLE_DOMAINS if merged.get(role)}


async def async_setup_entry(hass: HomeAssistant, entry: SpookyConfigEntry) -> bool:
    device_info, device_id = _build_device_info(hass, entry)
    runtime = SpookyRuntime(hass, entry.entry_id, roles_from_entry(entry), device_info, device_id)
    runtime.layout = runtime.compute_layout()
    entry.runtime_data = runtime

    watched = [e for e in (runtime.entity(ROLE_COUNT), runtime.entity(ROLE_MANUAL), runtime.entity(ROLE_OUTPUT)) if e]

    @callback
    def _layout_changed(event: Event[EventStateChangedData]) -> None:
        old = runtime.layout
        new = runtime.compute_layout()
        runtime.layout = new
        if (old.detected, old.pixels) == (new.detected, new.pixels):
            return
        _LOGGER.debug("Spooky layout %s -> %s", old, new)
        data = {
            "device_id": runtime.device_id,
            "previous_detected": old.detected,
            "detected": new.detected,
            "previous_pixels": old.pixels,
            "pixels": new.pixels,
        }
        hass.bus.async_fire(EVENT_CHAIN_CHANGED, data)
        async_dispatcher_send(hass, signal_chain(entry.entry_id), old, new)

        # Keep parameters valid for the new string: a spotlight on a pixel that
        # no longer exists is switched off rather than silently ignored.
        spot = runtime.state(ROLE_SPOTLIGHT)
        if spot and to_index(spot.state, MAX_CHAIN + 1) > new.pixels:
            hass.async_create_task(runtime.async_set_number(ROLE_SPOTLIGHT, 0))

    if watched:
        entry.async_on_unload(async_track_state_change_event(hass, watched, _layout_changed))

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_async_reload))
    return True


async def _async_reload(hass: HomeAssistant, entry: SpookyConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)


async def async_unload_entry(hass: HomeAssistant, entry: SpookyConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
