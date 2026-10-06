"""A light with a named Halloween effect list, layered on the NanoC6's Zigbee light."""

from __future__ import annotations

from typing import Any

from homeassistant.components.light import (
    ATTR_BRIGHTNESS,
    ATTR_EFFECT,
    ATTR_HS_COLOR,
    ATTR_RGB_COLOR,
    ATTR_TRANSITION,
    ATTR_XY_COLOR,
    ColorMode,
    LightEntity,
    LightEntityFeature,
)
from homeassistant.const import ATTR_ENTITY_ID, ATTR_SUPPORTED_FEATURES, STATE_ON
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry
from .const import EFFECTS, ROLE_EFFECT, ROLE_LIGHT
from .entity import SpookyEntity
from .logic import effect_index, effect_name, to_index

_PASSTHROUGH = (ATTR_BRIGHTNESS, ATTR_HS_COLOR, ATTR_RGB_COLOR, ATTR_XY_COLOR, ATTR_TRANSITION)


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([SpookyLight(entry.runtime_data)])


class SpookyLight(SpookyEntity, LightEntity):
    """Proxy light: on/off/colour go to the Zigbee light, effects to endpoint 12."""

    _source_roles = (ROLE_LIGHT, ROLE_EFFECT)
    _primary_role = ROLE_LIGHT
    _attr_color_mode = ColorMode.HS
    _attr_supported_color_modes = {ColorMode.HS}
    _attr_effect_list = list(EFFECTS)

    def __init__(self, runtime) -> None:
        super().__init__(runtime, "spooky_light")

    @property
    def supported_features(self) -> LightEntityFeature:
        features = LightEntityFeature.EFFECT
        src = self.runtime.state(ROLE_LIGHT)
        if src and int(src.attributes.get(ATTR_SUPPORTED_FEATURES, 0)) & LightEntityFeature.TRANSITION:
            features |= LightEntityFeature.TRANSITION
        return features

    @property
    def is_on(self) -> bool | None:
        src = self.runtime.state(ROLE_LIGHT)
        return None if src is None else src.state == STATE_ON

    @property
    def brightness(self) -> int | None:
        src = self.runtime.state(ROLE_LIGHT)
        return src.attributes.get(ATTR_BRIGHTNESS) if src else None

    @property
    def hs_color(self) -> tuple[float, float] | None:
        src = self.runtime.state(ROLE_LIGHT)
        return src.attributes.get(ATTR_HS_COLOR) if src else None

    @property
    def effect(self) -> str | None:
        src = self.runtime.state(ROLE_EFFECT)
        if src is None:
            return None
        return effect_name(to_index(src.state, len(EFFECTS) - 1))

    async def async_turn_on(self, **kwargs: Any) -> None:
        if (name := kwargs.get(ATTR_EFFECT)) is not None and (idx := effect_index(name)) is not None:
            await self.runtime.async_set_number(ROLE_EFFECT, idx)
        data: dict[str, Any] = {ATTR_ENTITY_ID: self.runtime.entity(ROLE_LIGHT)}
        data.update({k: v for k, v in kwargs.items() if k in _PASSTHROUGH})
        await self.hass.services.async_call("light", "turn_on", data, blocking=True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        data: dict[str, Any] = {ATTR_ENTITY_ID: self.runtime.entity(ROLE_LIGHT)}
        if ATTR_TRANSITION in kwargs:
            data[ATTR_TRANSITION] = kwargs[ATTR_TRANSITION]
        await self.hass.services.async_call("light", "turn_off", data, blocking=True)
