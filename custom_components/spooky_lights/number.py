"""Number controls.

* Spotlight: pick one pixel to hold the light's colour while the rest keep haunting.
  Its range is not fixed — it follows the number of pixels actually present, so the
  slider grows when a Grove LED is plugged in and shrinks when one is removed.
* Effect speed: 1–100 (50 = normal); also stretches or squeezes the Demo Reel.
"""

from __future__ import annotations

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry, SpookyRuntime
from .const import ROLE_COUNT, ROLE_MANUAL, ROLE_OUTPUT, ROLE_SPEED, ROLE_SPOTLIGHT
from .entity import SpookyEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    runtime = entry.runtime_data
    entities: list[NumberEntity] = []
    if runtime.has(ROLE_SPOTLIGHT):
        entities.append(SpotlightNumber(runtime))
    if runtime.has(ROLE_SPEED):
        entities.append(SpeedNumber(runtime))
    async_add_entities(entities)


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
        value = self.runtime.read(ROLE_SPOTLIGHT)
        return None if value is None else min(value, self.native_max_value)

    async def async_set_native_value(self, value: float) -> None:
        await self.runtime.async_write(ROLE_SPOTLIGHT, round(value))


class SpeedNumber(SpookyEntity, NumberEntity):
    _source_roles = (ROLE_SPEED,)
    _primary_role = ROLE_SPEED
    _attr_icon = "mdi:speedometer"
    _attr_mode = NumberMode.SLIDER
    _attr_native_min_value = 1
    _attr_native_max_value = 100
    _attr_native_step = 1

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "speed")

    @property
    def native_value(self) -> float | None:
        return self.runtime.read(ROLE_SPEED)

    async def async_set_native_value(self, value: float) -> None:
        await self.runtime.async_write(ROLE_SPEED, round(value))
