"""A light with a named Halloween effect list, layered on the NanoC6's Zigbee light."""

from __future__ import annotations

import asyncio
import logging
import time
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
from homeassistant.const import ATTR_ENTITY_ID, ATTR_SUPPORTED_FEATURES, STATE_OFF, STATE_ON
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry
from .const import EFFECTS, ROLE_COUNT, ROLE_EFFECT, ROLE_LIGHT
from .entity import SpookyEntity
from .logic import DELIVERY_WINDOW_S, Courier, effect_index, effect_name

_LOGGER = logging.getLogger(__name__)

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
        self._courier_task: asyncio.Task | None = None

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(self._cancel_courier)

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
        index = self.runtime.read(ROLE_EFFECT)
        return None if index is None else effect_name(index)

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self._deliver(True, kwargs)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self._deliver(False, kwargs)

    # ------------------------------------------------------------ delivery
    async def _send(self, on: bool, kwargs: dict[str, Any]) -> None:
        light = self.runtime.entity(ROLE_LIGHT)
        if not on:
            data: dict[str, Any] = {ATTR_ENTITY_ID: light}
            if ATTR_TRANSITION in kwargs:
                data[ATTR_TRANSITION] = kwargs[ATTR_TRANSITION]
            await self.hass.services.async_call("light", "turn_off", data, blocking=True)
            return
        if (name := kwargs.get(ATTR_EFFECT)) is not None and (idx := effect_index(name)) is not None:
            await self.runtime.async_write(ROLE_EFFECT, idx)
        data = {ATTR_ENTITY_ID: light}
        data.update({k: v for k, v in kwargs.items() if k in _PASSTHROUGH})
        await self.hass.services.async_call("light", "turn_on", data, blocking=True)

    def _delivered(self, on: bool) -> bool:
        src = self.runtime.state(ROLE_LIGHT)
        return src is not None and src.state == (STATE_ON if on else STATE_OFF)

    def _heard(self) -> tuple:
        """Changes whenever the device reports anything (a battery-build check-in)."""
        states = (self.runtime.state(role) for role in (ROLE_LIGHT, ROLE_COUNT))
        return tuple(s.last_reported for s in states if s is not None)

    @callback
    def _cancel_courier(self) -> None:
        if self._courier_task and not self._courier_task.done():
            self._courier_task.cancel()
        self._courier_task = None

    async def _deliver(self, on: bool, kwargs: dict[str, Any]) -> None:
        """Send now; if the light is asleep (battery build), keep trying until it wakes."""
        self._cancel_courier()  # the newest command wins
        try:
            await self._send(on, kwargs)
        except HomeAssistantError as err:
            _LOGGER.debug("%s didn't answer (asleep?): %s — will retry", self.entity_id, err)
        if self._delivered(on):
            return
        self._courier_task = self.hass.async_create_background_task(
            self._courier(on, dict(kwargs)), name=f"{self.entity_id} courier"
        )

    async def _courier(self, on: bool, kwargs: dict[str, Any]) -> None:
        courier = Courier(time.monotonic(), self._heard())
        while True:
            await asyncio.sleep(1)
            action = courier.step(time.monotonic(), self._delivered(on), self._heard())
            if action == "done":
                _LOGGER.debug("%s woke up and is now %s", self.entity_id, "on" if on else "off")
                return
            if action == "give_up":
                _LOGGER.warning(
                    "%s didn't answer the turn_%s command for %d minutes; is it powered and in range?",
                    self.entity_id,
                    "on" if on else "off",
                    DELIVERY_WINDOW_S // 60,
                )
                return
            if action == "send":
                try:
                    await self._send(on, kwargs)
                except HomeAssistantError:
                    pass  # still asleep; try again at the next check-in
