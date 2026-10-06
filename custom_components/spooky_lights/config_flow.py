"""Config flow: pick the NanoC6 device, confirm which of its entities does what."""

from __future__ import annotations

from typing import Any

import voluptuous as vol
from homeassistant.config_entries import ConfigEntry, ConfigFlow, ConfigFlowResult, OptionsFlow
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import selector

from .const import (
    CONF_DEVICE_ID,
    DOMAIN,
    ROLE_COUNT,
    ROLE_DOMAINS,
    ROLE_EFFECT,
    ROLE_LIGHT,
    ROLE_MANUAL,
    ROLE_OUTPUT,
    ROLE_SPEED,
    ROLE_SPOTLIGHT,
)
from .logic import guess_role

REQUIRED_ROLES = (ROLE_LIGHT, ROLE_EFFECT)
OPTIONAL_ROLES = (ROLE_COUNT, ROLE_MANUAL, ROLE_SPEED, ROLE_OUTPUT, ROLE_SPOTLIGHT)


def _entities_schema() -> vol.Schema:
    fields: dict[Any, Any] = {}
    for role in (*REQUIRED_ROLES, *OPTIONAL_ROLES):
        key = vol.Required(role) if role in REQUIRED_ROLES else vol.Optional(role)
        fields[key] = selector.EntitySelector(selector.EntitySelectorConfig(domain=ROLE_DOMAINS[role]))
    return vol.Schema(fields)


def guess_entities(hass: HomeAssistant, device_id: str) -> dict[str, str]:
    """Map the device's entities to Spooky roles (first match wins)."""
    found: dict[str, str] = {}
    for entry in er.async_entries_for_device(er.async_get(hass), device_id, include_disabled_entities=False):
        role = guess_role(entry.domain, entry.unique_id, entry.original_name, entry.name, entry.entity_id)
        if role and role not in found:
            found[role] = entry.entity_id
    return found


class SpookyLightsConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._device_id: str | None = None
        self._title = "Spooky Lights"

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            self._device_id = user_input.get(CONF_DEVICE_ID)
            if self._device_id:
                await self.async_set_unique_id(self._device_id)
                self._abort_if_unique_id_configured()
                if device := dr.async_get(self.hass).async_get(self._device_id):
                    self._title = device.name_by_user or device.name or self._title
            return await self.async_step_entities()

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Optional(CONF_DEVICE_ID): selector.DeviceSelector()}),
        )

    async def async_step_entities(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            if not self._device_id:
                await self.async_set_unique_id(user_input[ROLE_LIGHT])
                self._abort_if_unique_id_configured()
            return self.async_create_entry(title=self._title, data={CONF_DEVICE_ID: self._device_id, **user_input})

        suggested = guess_entities(self.hass, self._device_id) if self._device_id else {}
        return self.async_show_form(
            step_id="entities",
            data_schema=self.add_suggested_values_to_schema(_entities_schema(), suggested),
            errors=errors,
            description_placeholders={"found": str(len(suggested))},
        )

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        return SpookyLightsOptionsFlow()


class SpookyLightsOptionsFlow(OptionsFlow):
    """Re-map entities later (e.g. after re-pairing the NanoC6)."""

    async def async_step_init(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        if user_input is not None:
            # Optional fields left blank must clear a previous mapping.
            cleared = {role: "" for role in OPTIONAL_ROLES if role not in user_input}
            return self.async_create_entry(data={**cleared, **user_input})
        current = {**self.config_entry.data, **self.config_entry.options}
        current = {k: v for k, v in current.items() if k in ROLE_DOMAINS and v}
        return self.async_show_form(
            step_id="init",
            data_schema=self.add_suggested_values_to_schema(_entities_schema(), current),
        )
