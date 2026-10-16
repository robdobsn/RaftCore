# RaftCore Linux unit tests

Host-side tests of RaftCore code that does not need ESP32 hardware. They are run by CI
(`.github/workflows/linux-unit-tests.yml`) on every push to `main` and every pull request.

## Running

```bash
make -C linux_unit_tests check      # build and run every test; exit code is non-zero if any fails
make -C linux_unit_tests <test>     # build one test, e.g. make test_raft_json, then run ./<test>
make -C linux_unit_tests clean
```

Requirements: `g++` with C++20, `make` and `python3` (the device type test runs the device type generator).

`test_device_type_records` can also check other device type catalogues, for example those in other
repositories:

```bash
make -C linux_unit_tests check DEV_TYPE_CATALOGUES="../../RaftI2C/linux_unit_tests/TestDevTypeRecs.json"
```

## Tests

| Test | What it covers |
|---|---|
| `test_raft_json` | RaftJson: path lookup, number/bool/string conversion, name-value pairs, arrays, objects, appending fields, escaped quotes |
| `test_raft_utils` | `parseIntList` (device address lists) and hex string conversion |
| `test_device_names` | `DeviceNames`: device names and roles as used by DeviceManager |
| `test_device_data_subscribers` | `DeviceDataSubscribers`: several subscribers per device, rate limits, unsubscribing while a callback runs |
| `test_device_type_records` | Detection value parsing in `DeviceTypeRecords`, against hand-checked cases and every built-in device type record (generated from `devtypes/DeviceTypeRecords.json`) |
| `test_protocol_exchange` | `ProtocolExchange`: a real-time stream session from start to final block, the file stream activity hook, and refusal of a stream to an unknown endpoint |
| `test_thread_safe_queue` | `ThreadSafeQueue` (with its `SpiramAwareAllocator` deque): order, length limits, peek/pop/clear, many elements, and a producer and consumer on two threads |
| `test_espnow_config` | ESP-NOW configuration parsing. The code is on the RaftSysMods branch `feature/ESPNow-as-first-class-protocol`, so `make check` skips this test unless `../../RaftSysMods/components/ESPNowManager` exists. It should move to RaftSysMods with that code |

## Adding a test

1. Create `test_<area>.cpp`. Include `testhdr.h`, check conditions with `TEST_ASSERT(condition, "name")`, and end
   `main()` with `return testSummary("test_<area>");`.
2. Test the real code. If the code is hard to construct on its own, move the logic into a small class that the
   production code uses and test that (as `DeviceNames` and `DeviceDataSubscribers` are), rather than testing a
   copy of it.
3. In the `Makefile`, add `SOURCES_test_<area> = test_<area>.cpp <sources it needs>` and add the test to
   `TESTS`. Each source is compiled once into `obj/` with its header dependencies tracked, so nothing else is
   needed.
4. Check that the test fails without the change it protects, then passes with it.
