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
- **Sets:** `sets/<name>.json` (`{"records": [keys]}`) are named selections - `common` (about 10 popular sensors) and
  `extended` (about 25) - chosen in `DeviceTypeSources.json` as `"raftcore:@common"`. `raft new` offers them. A set
  naming a key that isn't a record stops the build, and the tests check RaftCore's own sets.
- **Third-party sources:** a record whose init, decode or settings come from a third-party driver, library or
  document says so in its `_notes` (the source, with a link, and its licence). If the licence asks for its notice to
  be kept (MIT, BSD), add the record and the notice to `THIRD_PARTY_NOTICES.md` at the top of RaftCore. Use only
  sources whose licence allows it - not code without a licence, or under one such as the GPL.
- **Board choices** (poll bus speed and slots, poll interval, samples kept, sample rate, scan priority) are not part
  of a record: a SysType sets them in DevMan `typeSettings` (wiki page *DeviceManager Settings*).
- **One file:** `python3 scripts/AssembleDeviceTypes.py --raftcore . --all --out DeviceTypeRecords.json` writes every
  record as one catalogue file (it is also attached to each release).

Design: `devdocs/device-type-record-management-plan.md`.
