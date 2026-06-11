# 2026-06-11 Descent X50i FIT Location Observation

## Device

- Model: Garmin Descent X50i
- Product id: 4518
- Probe version: 0.6.7
- Capture path: Android BLE probe -> ECS sample upload -> sidecar parser

## FileSync Listing

The Gadgetbridge-style FileSync list request with flags `42405/42405` only exposed
small device/location files and did not include `FIT_TYPE_4`.

The FileSync request without flags exposed the full list:

- `FIT_TYPE_4`: Activities, includes dive log FIT files.
- `FIT_TYPE_8`: Locations, includes Garmin location records.
- `FIT_TYPE_1`: Device capability/summary style file.

## Dive Activity FIT

Sample:

- `S:\GMP\garmin-ble-probe\samples\x50i-067-fit-type-4.fit`

The activity FIT contains the dive profile and tank data:

- `session.start_position_lat` / `session.start_position_long`: empty.
- `lap.start_position_*` and `lap.end_position_*`: empty.
- `record.latitude` / `record.longitude`: no valid coordinates.
- `dive_summary`, `dive_settings`, `dive_gas`, `tank_summary`, and `tank_update` are present.

Conclusion: for this X50i sample, the activity file is sufficient for the dive
profile but not for GPS.

## Location FIT

Sample:

- `S:\GMP\garmin-ble-probe\samples\x50i-065-fit-type-8.fit`

The location file contains FIT native message `29`. Observed useful fields:

- field `0`: location name, example `8  6月 18:31`
- field `1`: latitude in Garmin semicircles
- field `2`: longitude in Garmin semicircles

Observed coordinate:

- raw latitude: `372672334` -> `31.237034`
- raw longitude: `1427213298` -> `119.627637`

Sidecar now maps message `29` to `location` and exposes parsed records under:

- `fields.location`
- `garmin.locations`

## Mk3i Follow-Up

Existing Mk3i activity FIT samples define standard GPS fields but contain empty
values:

- `session.start_position_lat` / `session.start_position_long`: empty.
- `lap.start/end_position_*`: empty.
- many `record.latitude` / `record.longitude` definitions exist, but values are empty.

Next test with Mk3i should capture the full no-flags FileSync list and download
`FIT_TYPE_8` if present. If Mk3i has a separate location file like X50i, use that
for dive-site GPS association. If not, inspect unknown native messages such as
`140` and compare against the watch UI location shown for the same dive.
