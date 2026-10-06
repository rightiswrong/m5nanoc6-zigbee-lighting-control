"""Event entity that fires when a Grove LED joins or leaves the chain."""

from __future__ import annotations

from homeassistant.components.event import EventEntity
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import Layout, SpookyConfigEntry, SpookyRuntime
from .const import ROLE_COUNT, signal_chain
from .entity import SpookyEntity
from .logic import chain_change_kind


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    runtime = entry.runtime_data
    if runtime.entity(ROLE_COUNT):
        async_add_entities([ChainChangeEvent(runtime)])


class ChainChangeEvent(SpookyEntity, EventEntity):
    _attr_event_types = ["led_added", "led_removed"]
    _attr_icon = "mdi:transit-connection-variant"
    _primary_role = ROLE_COUNT

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "chain_change")

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.async_on_remove(async_dispatcher_connect(self.hass, signal_chain(self.runtime.entry_id), self._on_chain))

    @callback
    def _on_chain(self, old: Layout, new: Layout) -> None:
        if (kind := chain_change_kind(old.detected, new.detected)) is None:
            return
        self._trigger_event(
            kind,
            {"previous": old.detected, "current": new.detected, "pixels": new.pixels},
        )
        self.async_write_ha_state()
