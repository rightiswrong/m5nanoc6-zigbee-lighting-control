"""Base entity: mirrors the firmware entities it is built on."""

from __future__ import annotations

from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.core import Event, EventStateChangedData, callback
from homeassistant.helpers.entity import Entity
from homeassistant.helpers.event import async_track_state_change_event

from . import SpookyRuntime


class SpookyEntity(Entity):
    """An entity derived from one or more source entities on the NanoC6."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    # Roles whose source entities this entity reads; changes trigger a refresh.
    _source_roles: tuple[str, ...] = ()
    # The role that must be available for this entity to be available.
    _primary_role: str | None = None

    def __init__(self, runtime: SpookyRuntime, key: str) -> None:
        self.runtime = runtime
        self._attr_unique_id = f"{runtime.entry_id}_{key}"
        self._attr_translation_key = key
        self._attr_device_info = runtime.device_info

    @property
    def available(self) -> bool:
        if self._primary_role is None:
            return True
        state = self.runtime.state(self._primary_role)
        return state is not None and state.state != STATE_UNAVAILABLE

    async def async_added_to_hass(self) -> None:
        sources = [e for r in self._source_roles if (e := self.runtime.entity(r))]
        if sources:
            self.async_on_remove(async_track_state_change_event(self.hass, sources, self._source_changed))

    @callback
    def _source_changed(self, event: Event[EventStateChangedData]) -> None:
        self.async_write_ha_state()
