"""Spotlight: pick one pixel to hold the light's colour while the rest keep haunting.

Its range is not fixed — it follows the number of pixels actually present, so the
slider grows when a Grove LED is plugged in and shrinks when one is removed.
"""

from __future__ import annotations

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry, SpookyRuntime
from .const import MAX_CHAIN, ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT, ROLE_SPOTLIGHT
from .entity import SpookyEntity
from .logic import to_index


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    runtime = entry.runtime_data
    if runtime.entity(ROLE_SPOTLIGHT):
        async_add_entities([SpotlightNumber(runtime)])


class SpotlightNumber(SpookyEntity, NumberEntity):
    _source_roles = (ROLE_SPOTLIGHT, ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT)
    _primary_role = ROLE_SPOTLIGHT
    _attr_icon = "mdi:spotlight-beam"
    _attr_mode = NumberMode.SLIDER
    _attr_native_min_value = 0
    _attr_native_step = 1

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "spotlight")

    @property
    def native_max_value(self) -> float:
        # Never 0 so the slider stays usable; 0 always means "off".
        return max(1, self.runtime.compute_layout().pixels)

    @property
    def native_value(self) -> float | None:
        src = self.runtime.state(ROLE_SPOTLIGHT)
        if src is None:
            return None
        return min(to_index(src.state, MAX_CHAIN + 1), self.native_max_value)

    async def async_set_native_value(self, value: float) -> None:
        await self.runtime.async_set_number(ROLE_SPOTLIGHT, round(value))
