# Device type records

One file per device type record: `records/<Key>.json` holds the record as it appears under `"devTypes"` in a
catalogue file. The file name is the device type key; a `deviceType` field, if present, must equal it. The record
format is described on the wiki page *Device Type Record Format*.

- **Adding or changing a record** is a change to its own file, reviewed and committed on its own.
- **Order:** records are compiled in key order, and at an address they are tried in descending `identPriority`,
  then descending detection strength, then key order. Where the order between two records matters, give one of them an
  `identPriority`; position never counts.
- **Projects** select the records they compile in with `DeviceTypeSources.json` in their SysType folders (wiki page
  *Selecting Device Types*). `/devtypes/DeviceTypeRecords.json` in `DEV_TYPE_JSON_FILES` still means every record here.
- **Board choices** (poll bus speed and slots, poll interval, samples kept, sample rate, scan priority) are not part
  of a record: a SysType sets them in DevMan `typeSettings` (wiki page *DeviceManager Settings*).
- **One file:** `python3 scripts/AssembleDeviceTypes.py --raftcore . --all --out DeviceTypeRecords.json` writes every
  record as one catalogue file (it is also attached to each release).

Design: `devdocs/device-type-record-management-plan.md`.
