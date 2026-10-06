"""Active pixels: how many LEDs the current effect is actually painting."""

from __future__ import annotations

from typing import Any

from homeassistant.components.sensor import SensorEntity, SensorStateClass
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry, SpookyRuntime
from .const import OUTPUT_MODES, ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT
from .entity import SpookyEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([ActivePixelsSensor(entry.runtime_data)])


class ActivePixelsSensor(SpookyEntity, SensorEntity):
    _source_roles = (ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT)
    _attr_icon = "mdi:led-on"
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "active_pixels")

    @property
    def native_value(self) -> int:
        return self.runtime.compute_layout().pixels

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        layout = self.runtime.compute_layout()
        return {
            "grove_detected": layout.detected,
            "manual_count": layout.manual,
            "grove_in_use": layout.chain,
            "output_mode": OUTPUT_MODES[layout.output_mode],
        }
