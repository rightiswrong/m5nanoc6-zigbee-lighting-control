"""Jump scare: full-brightness Lightning Storm for a few seconds, then back to normal."""

from __future__ import annotations

from homeassistant.components.button import ButtonEntity
from homeassistant.components.light import ATTR_BRIGHTNESS, ATTR_HS_COLOR
from homeassistant.const import ATTR_ENTITY_ID, STATE_ON
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.event import async_call_later

from . import SpookyConfigEntry, SpookyRuntime
from .const import EFFECT_LIGHTNING, EFFECTS, JUMP_SCARE_SECONDS, ROLE_EFFECT, ROLE_LIGHT
from .entity import SpookyEntity
from .logic import to_index


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    async_add_entities([JumpScareButton(entry.runtime_data)])


class JumpScareButton(SpookyEntity, ButtonEntity):
    _attr_icon = "mdi:ghost"
    _primary_role = ROLE_LIGHT

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "jump_scare")
        self._cancel_restore = None
        self._saved: dict | None = None

    async def async_press(self) -> None:
        light_id = self.runtime.entity(ROLE_LIGHT)
        light = self.runtime.state(ROLE_LIGHT)
        effect = self.runtime.state(ROLE_EFFECT)
        if self._saved is None:  # pressed again mid-scare: keep the original state
            self._saved = {
                "on": light is not None and light.state == STATE_ON,
                "brightness": light.attributes.get(ATTR_BRIGHTNESS) if light else None,
                "hs_color": light.attributes.get(ATTR_HS_COLOR) if light else None,
                "effect": to_index(effect.state if effect else None, len(EFFECTS) - 1),
            }
        await self.runtime.async_set_number(ROLE_EFFECT, EFFECT_LIGHTNING)
        await self.hass.services.async_call(
            "light", "turn_on", {ATTR_ENTITY_ID: light_id, ATTR_BRIGHTNESS: 255}, blocking=True
        )
        if self._cancel_restore:
            self._cancel_restore()
        self._cancel_restore = async_call_later(self.hass, JUMP_SCARE_SECONDS, self._restore)

    async def _restore(self, _now) -> None:
        self._cancel_restore = None
        saved, self._saved = self._saved, None
        if not saved:
            return
        light_id = self.runtime.entity(ROLE_LIGHT)
        await self.runtime.async_set_number(ROLE_EFFECT, saved["effect"])
        if saved["on"]:
            data = {ATTR_ENTITY_ID: light_id}
            if saved["brightness"] is not None:
                data[ATTR_BRIGHTNESS] = saved["brightness"]
            if saved["hs_color"] is not None:
                data[ATTR_HS_COLOR] = saved["hs_color"]
            await self.hass.services.async_call("light", "turn_on", data, blocking=True)
        else:
            await self.hass.services.async_call("light", "turn_off", {ATTR_ENTITY_ID: light_id}, blocking=True)

    async def async_will_remove_from_hass(self) -> None:
        if self._cancel_restore:
            self._cancel_restore()
