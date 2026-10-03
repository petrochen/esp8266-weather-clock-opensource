# Home Assistant and local display cards

The 1.11 beta adds display controls and one temporary external card. All readings
are supplied by your automation: this board has no indoor temperature, CO₂ or air
quality sensor. The clock needs no MQTT broker, account or extra firmware library.
The [REST API](API.md) remains available without enabling external cards.

## Start with the example

1. Reserve an IP address for the clock in your router.
2. Open [the package example](../examples/home-assistant/weather-clock.yaml).
   Replace **every** `CLOCK_IP` with that address and `sensor.living_room_co2` with
   an existing numeric sensor. This is a Home Assistant package, not an entire
   `configuration.yaml`; merge it with your existing package configuration.
3. In the clock's Settings → More display options, enable external cards and Save
   if you want the room-reading example. Ordinary weather sensors need no opt-in.
4. Check Home Assistant's configuration before reloading/restarting it. Inspect
   the REST command result and the clock's `card` status when troubleshooting.

The example polls one shared `/api/status` response every 60 seconds. Weather
sensors become unavailable when disabled, invalid or stale. Display units do not
change API units: temperatures remain °C and wind remains km/h.

The optional automation sends a short card every minute with a 180-second expiry.
If its source is unknown/unavailable it clears the card. A stopped automation also
expires naturally; nothing is saved to the clock's flash. Cards enter the normal
rotation, respect hold/night mode, and never replace the maintenance PIN overlay.

## Send your own reading

```sh
curl -H 'Content-Type: application/json'   -d '{"title":"Living room CO2","value":"920","unit":"ppm","ttl":180}'   http://CLOCK_IP/api/card
```

`value` must be text, even for a number. Limits are 31 UTF-8 bytes for title,
15 for value, 11 for unit and 5–3600 seconds for TTL. One card replaces the previous
one; at most one accepted update per second. `{"ttl":0}` clears it. Disabled cards
return 403, malformed input 400 and excessive updates 429. Display control accepts
`next`, `hold`, `resume` or `show` with an available screen ID; see the API reference.

Good candidates are room temperature, CO₂, current power or a short timer value.
Keep labels brief. Unsupported glyphs have a fallback; a short ASCII label is most
predictable. A long value is fitted to the available pixels, not scrolled.

These endpoints share the project's trusted-LAN model: any local client that can
reach the clock can use ordinary settings/display controls. Maintenance still
needs its PIN. There is no cloud push service, MQTT discovery or incoming Internet
access requirement.

The YAML/Jinja example is locally syntax-checked; a live Home Assistant instance
and the beta firmware on hardware have not yet been tested together.
References: [REST integration](https://www.home-assistant.io/integrations/rest/),
[RESTful sensor](https://www.home-assistant.io/integrations/sensor.rest/),
[RESTful command](https://www.home-assistant.io/integrations/rest_command/).
