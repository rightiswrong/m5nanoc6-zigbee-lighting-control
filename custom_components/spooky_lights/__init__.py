"""Spooky Lights — Halloween effects for the M5NanoC6 / M5NanoH2 light.

The firmware speaks either Zigbee (paired with ZHA or Zigbee2MQTT) or Matter over
Thread (paired with Home Assistant's Matter integration). Either way it shows up
as a handful of raw entities — "number: 3", "Light (2)", "4.0 lx". This
integration sits on top of those and gives you:

* a light with a named effect list (usable from any light card / scene),
* effect, output and speed controls,
* a spotlight control whose range follows the number of LEDs actually plugged in,
* an event entity + bus event when a Grove LED is added or removed,
* a "jump scare" button.

All transport differences live in SpookyRuntime.read() / async_write().
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import ATTR_ENTITY_ID, STATE_ON
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, State, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.event import async_track_state_change_event

from .const import (
    CONF_DEVICE_ID,
    DOMAIN,
    EFFECTS,
    EVENT_CHAIN_CHANGED,
    MANUFACTURER,
    MAX_CHAIN,
    MODEL,
    OUTPUT_MODES,
    PLATFORMS,
    ROLE_COUNT,
    ROLE_DOMAINS,
    ROLE_EFFECT,
    ROLE_LIGHT,
    ROLE_MANUAL,
    ROLE_OUTPUT,
    ROLE_SPEED,
    ROLE_SPOTLIGHT,
    signal_chain,
)
from .logic import (
    CONF_TRANSPORT,
    TRANSPORT_MATTER,
    TRANSPORT_ZIGBEE,
    band_from_brightness,
    band_to_level,
    count_from_lux,
    effective_chain_count,
    fine_to_level,
    ha_brightness_for_level,
    pack_setup,
    setup_from_brightness,
    speed_from_brightness,
    speed_to_level,
    to_index,
    virtual_pixel_count,
)

_LOGGER = logging.getLogger(__name__)

type SpookyConfigEntry = ConfigEntry[SpookyRuntime]

# Upper bound of each integer role (inclusive).
_ROLE_MAX = {
    ROLE_EFFECT: len(EFFECTS) - 1,
    ROLE_SPEED: 100,
    ROLE_OUTPUT: len(OUTPUT_MODES) - 1,
    ROLE_SPOTLIGHT: MAX_CHAIN + 1,
    ROLE_MANUAL: MAX_CHAIN,
    ROLE_COUNT: MAX_CHAIN,
}


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
    transport: str = TRANSPORT_ZIGBEE
    layout: Layout = field(default_factory=Layout)

    # ------------------------------------------------------------ entities
    def entity(self, role: str) -> str | None:
        return self.roles.get(role) or None

    def entities_for(self, role: str) -> list[str]:
        """Every source entity whose state feeds `role` (for change tracking)."""
        entity_id = self.roles.get(role)
        return [entity_id] if entity_id else []

    def has(self, role: str) -> bool:
        return bool(self.entities_for(role))

    def state(self, role: str) -> State | None:
        entity_id = self.entity(role)
        return self.hass.states.get(entity_id) if entity_id else None

    # ------------------------------------------------------- read / write
    def read(self, role: str) -> int | None:
        """Current integer value of a firmware setting, whatever the transport."""
        upper = _ROLE_MAX[role]
        if self.transport != TRANSPORT_MATTER:
            st = self.state(role)
            if st is None:
                return None
            return to_index(st.state, upper, default=50 if role == ROLE_SPEED else 0)
        st = self.state(role)
        if st is None:
            return None
        if role == ROLE_COUNT:
            return count_from_lux(st.state)
        brightness = st.attributes.get("brightness") if st.state == STATE_ON else None
        if role == ROLE_SPEED:
            return speed_from_brightness(brightness)
        if role in (ROLE_OUTPUT, ROLE_MANUAL):
            output, manual = setup_from_brightness(brightness)
            return output if role == ROLE_OUTPUT else manual
        return min(upper, band_from_brightness(st.state == STATE_ON, brightness))

    async def async_write(self, role: str, value: int) -> None:
        """Set a firmware setting, whatever the transport."""
        value = max(0, min(_ROLE_MAX[role], int(value)))
        if self.transport != TRANSPORT_MATTER:
            if entity_id := self.entity(role):
                await self.hass.services.async_call(
                    "number", "set_value", {ATTR_ENTITY_ID: entity_id, "value": value}, blocking=True
                )
            return
        entity_id = self.entity(role)
        if not entity_id:
            return
        if role == ROLE_SPEED:
            level = speed_to_level(max(1, value))
        elif role in (ROLE_OUTPUT, ROLE_MANUAL):
            # One endpoint carries both: keep the other half as it is.
            output = value if role == ROLE_OUTPUT else self.read(ROLE_OUTPUT) or 0
            manual = value if role == ROLE_MANUAL else self.read(ROLE_MANUAL) or 0
            level = fine_to_level(pack_setup(output, manual))
        elif value == 0:
            await self.hass.services.async_call("light", "turn_off", {ATTR_ENTITY_ID: entity_id}, blocking=True)
            return
        else:
            level = band_to_level(value)
        await self.hass.services.async_call(
            "light", "turn_on", {ATTR_ENTITY_ID: entity_id, "brightness": ha_brightness_for_level(level)}, blocking=True
        )

    def compute_layout(self) -> Layout:
        detected = self.read(ROLE_COUNT) or 0
        manual = self.read(ROLE_MANUAL) or 0
        output = self.read(ROLE_OUTPUT) or 0
        chain = effective_chain_count(detected, manual)
        return Layout(detected, manual, chain, output, virtual_pixel_count(chain, output))

    def matter_helper_entities(self) -> list[str]:
        """Raw Matter entities this integration presents better (hidden from the device page)."""
        if self.transport != TRANSPORT_MATTER:
            return []
        roles = (ROLE_EFFECT, ROLE_SPEED, ROLE_SPOTLIGHT, ROLE_OUTPUT, ROLE_COUNT)  # setup = output = manual
        return list(dict.fromkeys(e for r in roles if (e := self.roles.get(r))))


def _build_device_info(hass: HomeAssistant, entry: ConfigEntry) -> tuple[DeviceInfo, str | None]:
    """Attach our entities to the existing ZHA/Z2M/Matter device when we know it."""
    device_id = entry.data.get(CONF_DEVICE_ID)
    if device_id and (device := dr.async_get(hass).async_get(device_id)):
        if device.identifiers:
            return DeviceInfo(identifiers=set(device.identifiers)), device_id
        if device.connections:
            return DeviceInfo(connections=set(device.connections)), device_id
    return (
        DeviceInfo(identifiers={(DOMAIN, entry.entry_id)}, manufacturer=MANUFACTURER, model=MODEL, name=entry.title),
        None,
    )


def roles_from_entry(entry: ConfigEntry) -> dict[str, str]:
    """Options override data, so re-mapping entities doesn't need a re-add."""
    merged = {**entry.data, **entry.options}
    return {role: merged[role] for role in ROLE_DOMAINS if merged.get(role)}


def runtime_from_entry(hass: HomeAssistant, entry: ConfigEntry) -> SpookyRuntime:
    merged = {**entry.data, **entry.options}
    device_info, device_id = _build_device_info(hass, entry)
    return SpookyRuntime(
        hass,
        entry.entry_id,
        roles_from_entry(entry),
        device_info,
        device_id,
        transport=merged.get(CONF_TRANSPORT, TRANSPORT_ZIGBEE),
    )


@callback
def _set_hidden(hass: HomeAssistant, entity_ids: list[str], hide: bool) -> None:
    registry = er.async_get(hass)
    for entity_id in entity_ids:
        entry = registry.async_get(entity_id)
        if entry is None:
            continue
        if hide and entry.hidden_by is None:
            registry.async_update_entity(entity_id, hidden_by=er.RegistryEntryHider.INTEGRATION)
        elif not hide and entry.hidden_by == er.RegistryEntryHider.INTEGRATION:
            registry.async_update_entity(entity_id, hidden_by=None)


async def async_setup_entry(hass: HomeAssistant, entry: SpookyConfigEntry) -> bool:
    runtime = runtime_from_entry(hass, entry)
    runtime.layout = runtime.compute_layout()
    entry.runtime_data = runtime
    _set_hidden(hass, runtime.matter_helper_entities(), True)

    watched = [e for r in (ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT) for e in runtime.entities_for(r)]

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
        spot = runtime.read(ROLE_SPOTLIGHT)
        if spot is not None and spot > new.pixels:
            hass.async_create_task(runtime.async_write(ROLE_SPOTLIGHT, 0))

    if watched:
        entry.async_on_unload(async_track_state_change_event(hass, watched, _layout_changed))

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_async_reload))
    return True


async def _async_reload(hass: HomeAssistant, entry: SpookyConfigEntry) -> None:
    await hass.config_entries.async_reload(entry.entry_id)


async def async_unload_entry(hass: HomeAssistant, entry: SpookyConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_remove_entry(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Removing Spooky Lights shows the raw Matter entities again."""
    _set_hidden(hass, runtime_from_entry(hass, entry).matter_helper_entities(), False)


__all__ = ["ROLE_LIGHT", "Layout", "SpookyConfigEntry", "SpookyRuntime"]
