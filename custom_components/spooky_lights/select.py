"""Effect and output-routing pickers."""

from __future__ import annotations

from homeassistant.components.select import SelectEntity
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from . import SpookyConfigEntry, SpookyRuntime
from .const import EFFECTS, OUTPUT_MODES, ROLE_EFFECT, ROLE_OUTPUT
from .entity import SpookyEntity
from .logic import to_index


async def async_setup_entry(
    hass: HomeAssistant, entry: SpookyConfigEntry, async_add_entities: AddConfigEntryEntitiesCallback
) -> None:
    runtime = entry.runtime_data
    entities: list[SelectEntity] = [EffectSelect(runtime)]
    if runtime.entity(ROLE_OUTPUT):
        entities.append(OutputSelect(runtime))
    async_add_entities(entities)


class _IndexSelect(SpookyEntity, SelectEntity):
    """A select whose option index is written to an Analog Output endpoint."""

    _role: str

    def __init__(self, runtime: SpookyRuntime, key: str, options: tuple[str, ...]) -> None:
        super().__init__(runtime, key)
        self._attr_options = list(options)
        self._source_roles = (self._role,)
        self._primary_role = self._role

    @property
    def current_option(self) -> str | None:
        src = self.runtime.state(self._role)
        if src is None:
            return None
        return self._attr_options[to_index(src.state, len(self._attr_options) - 1)]

    async def async_select_option(self, option: str) -> None:
        await self.runtime.async_set_number(self._role, self._attr_options.index(option))


class EffectSelect(_IndexSelect):
    _role = ROLE_EFFECT
    _attr_icon = "mdi:halloween"

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "effect", EFFECTS)


class OutputSelect(_IndexSelect):
    _role = ROLE_OUTPUT
    _attr_entity_category = EntityCategory.CONFIG
    _attr_icon = "mdi:led-strip-variant"

    def __init__(self, runtime: SpookyRuntime) -> None:
        super().__init__(runtime, "output", OUTPUT_MODES)
