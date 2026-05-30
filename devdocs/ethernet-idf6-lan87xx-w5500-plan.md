# Ethernet on ESP-IDF 6.0: LAN87xx migration (done) and W5500 implementation plan

Status: LAN87xx (RMII / internal EMAC) migrated and working. W5500 (SPI)
**implemented** per [§3](#3-w5500-spi--implementation-plan) — wired as a build
option, first consumer is `ScaderLedsWaveshare`. Pending on-hardware confirmation
([§5](#5-verification-checklist)).

Hard constraint driving the whole design: **firmware binary size is near the
partition maximum.** Any systype that does not use Ethernet — and any
Ethernet systype that does not use the W5500 — must see **zero** byte increase
versus today. The gating strategy in
[§4 Gating strategy](#4-gating-strategy-zero-size-when-disabled) is therefore the
load-bearing part of this document, not an afterthought.

Verified against the local toolchain at `/home/rob/esp/esp-idf-v6.0.1`.

---

## 1. Background — what IDF 6.0 broke

ESP-IDF 6.0 reorganised the `esp_eth` component. Three independent breaking
changes affect `NetworkSystem`
([components/core/NetworkSystem/NetworkSystem.cpp](../components/core/NetworkSystem/NetworkSystem.cpp)):

1. **RMII clock Kconfig removed.** `CONFIG_ETH_RMII_CLK_INPUT` / `_OUTPUT`,
   `CONFIG_ETH_RMII_CLK_IN_GPIO`, `CONFIG_ETH_RMII_CLK_OUTPUT_GPIO0`,
   `CONFIG_ETH_RMII_CLK_OUT_GPIO` and `CONFIG_ETH_PHY_INTERFACE_RMII` no longer
   exist. Clock mode / GPIO must be set at runtime in the EMAC config struct.

2. **Per-chip PHY constructors removed from `esp_eth`.**
   `esp_eth_phy_new_lan87xx`, `_ip101`, `_rtl8201`, `_dp83848`, `_ksz80xx` are
   gone. The generic IEEE-802.3 constructor `esp_eth_phy_new_generic()` **remains**
   in `esp_eth` (`components/esp_eth/include/esp_eth_phy.h`,
   `src/phy/esp_eth_phy_generic.c`) and covers any standards-compliant RMII PHY,
   including the LAN87xx family.

3. **SPI Ethernet module drivers removed from `esp_eth` entirely.**
   `esp_eth_mac_new_w5500`, `esp_eth_phy_new_w5500`, `eth_w5500_config_t` and
   `ETH_W5500_DEFAULT_CONFIG` were moved to the external
   [`esp-eth-drivers`](https://github.com/espressif/esp-eth-drivers) repo,
   published on the ESP Component Registry as the standalone component
   **`espressif/w5500`**. There is **no in-`esp_eth` replacement** — the W5500
   needs its own PHY driver (the generic 802.3 PHY does *not* work for it).
   The Kconfig sub-option `CONFIG_ETH_SPI_ETHERNET_W5500` was also removed
   (`CONFIG_ETH_USE_SPI_ETHERNET` survives but now only means "SPI Ethernet
   framework present; bring your own chip driver as a component").

Migration guide:
<https://docs.espressif.com/projects/esp-idf/en/stable/esp32/migration-guides/release-6.x/6.0/networking.html>

---

## 2. LAN87xx (RMII / internal EMAC) — changes already made

These are the changes already present in the working tree and confirmed correct
against IDF 6.0.1. Documented here for the record.

### 2.1 Runtime RMII clock configuration

The compile-time `CONFIG_ETH_RMII_CLK_*` `#if` ladder was deleted. Two new
fields drive the clock from SysType JSON:

[NetworkSettings.h](../components/core/NetworkSystem/NetworkSettings.h)
- New enum `EthRmiiClockMode { ETH_RMII_CLK_EXT_IN, ETH_RMII_CLK_OUT }`.
- New members `EthRmiiClockMode ethRmiiClockMode = ETH_RMII_CLK_OUT` and
  `int ethRmiiClockGpio = 17`.
- Parsed from JSON keys `ethRmiiClockMode` (`"OUTPUT"`/`"INPUT"`, default OUTPUT)
  and `ethRmiiClockGpio` (default 17) via `getRmiiClockModeEnum()`.

[NetworkSystem.cpp](../components/core/NetworkSystem/NetworkSystem.cpp)
(`startEthernet()`, the `ESP_IDF_VERSION >= 5.3.0` branch):
```c
eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
esp32_emac_config.smi_gpio.mdc_num  = (gpio_num_t) _networkSettings.smiMDCPin;
esp32_emac_config.smi_gpio.mdio_num = (gpio_num_t) _networkSettings.smiMDIOPin;
esp32_emac_config.interface = EMAC_DATA_INTERFACE_RMII;
esp32_emac_config.clock_config.rmii.clock_mode =
    (_networkSettings.ethRmiiClockMode == NetworkSettings::ETH_RMII_CLK_EXT_IN)
        ? EMAC_CLK_EXT_IN : EMAC_CLK_OUT;
esp32_emac_config.clock_config.rmii.clock_gpio = _networkSettings.ethRmiiClockGpio;
```
This matches the migration guide verbatim. Valid clock-out GPIOs on ESP32 are
0/16/17; `EMAC_CLK_EXT_IN` requires GPIO0. The 5.0–5.3 and pre-5.0 branches are
retained for older toolchains.

### 2.2 Generic PHY constructor

The per-chip constructor calls are kept under `#elif` for IDF < 6.0; for IDF ≥ 6.0
the code selects the generic driver:
```c
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    phy = esp_eth_phy_new_generic(&phy_config);
#elif defined(HW_ETH_PHY_IP101)
    phy = esp_eth_phy_new_ip101(&phy_config);
    ... (other chips) ...
#endif
```
The whole PHY block is still gated on the `HW_ETH_PHY_*` macro set, so it only
compiles when a PHY is actually selected by the SysType.

### 2.3 SysType prerequisites for LAN87xx (no code change needed)

For an RMII board (e.g. Olimex ESP32-PoE-ISO used by `ScaderRelays`,
`ScaderShades`, `ScaderLeds`):
- `features.cmake`: `add_compile_definitions(HW_ETH_PHY_LAN87XX)` — already present.
- `sdkconfig.defaults`: `CONFIG_ETH_ENABLED=y`, `CONFIG_ETH_USE_ESP32_EMAC=y`.
  The removed `CONFIG_ETH_PHY_INTERFACE_RMII`, `CONFIG_ETH_RMII_CLK_OUTPUT`,
  `CONFIG_ETH_RMII_CLK_OUT_GPIO` lines are now **dead keys** — harmless but should
  be removed during cleanup, replaced by the JSON `ethRmiiClockMode` /
  `ethRmiiClockGpio` fields where the default (OUTPUT/17) is not correct.
- No new managed component: `esp_eth_phy_new_generic` lives in `esp_eth`, which
  RaftCore already `REQUIRES`.

### 2.4 Known caveat

`esp_eth_phy_new_generic()` implements only the standard 802.3 register set — no
chip-specific quirks. For LAN8710/LAN8720/LAN8742 on the Olimex boards this is
expected to work. If link detection misbehaves, the fallback is the dedicated
`espressif/lan87xx` component (same pattern as the W5500 plan below). Flagged as a
test item, not a planned change.

---

## 3. W5500 (SPI) — implementation plan

The W5500 block in `startEthernet()` (the
`#if defined(CONFIG_ETH_USE_SPI_ETHERNET) && defined(CONFIG_ETH_SPI_ETHERNET_W5500)`
section) is currently **dead and non-building** on IDF 6.0:
- `CONFIG_ETH_SPI_ETHERNET_W5500` no longer exists → the block compiles out
  silently, so W5500 can never be selected.
- Even if re-gated, it calls `esp_eth_mac_new_w5500` / `esp_eth_phy_new_w5500` /
  `ETH_W5500_DEFAULT_CONFIG`, none of which exist in `esp_eth` anymore → link error.

Note the asymmetry: the RMII PHY path was migrated (switched to the generic
constructor that survives in `esp_eth`), but the SPI path was not, because its
APIs have no in-`esp_eth` replacement — they require the external component.

### 3.1 Add the W5500 driver component

Standalone component (smaller than the `espressif/ethernet_init` umbrella, which
pulls in every driver — avoid it for size reasons):

```
idf.py add-dependency "espressif/w5500^1.0.1"
```

This must be added so that it is **only linked for W5500 systypes** — see
[§4](#4-gating-strategy-zero-size-when-disabled). It provides headers
`esp_eth_mac_w5500.h` and `esp_eth_phy_w5500.h`.

### 3.2 New gating macro (replaces removed Kconfig)

Since `CONFIG_ETH_SPI_ETHERNET_W5500` is gone, introduce a RaftCore-owned compile
macro, consistent with the existing `HW_ETH_PHY_*` pattern:

- Macro name: **`HW_ETH_USE_W5500`** (set via `add_compile_definitions` in the
  SysType `features.cmake`).
- Drives the source `#if` and is paired with a CMake variable
  **`RAFT_ENABLE_ETH_W5500`** that gates the component `REQUIRES`
  (see [§4](#4-gating-strategy-zero-size-when-disabled)).

### 3.3 Includes

In [NetworkSystem.cpp](../components/core/NetworkSystem/NetworkSystem.cpp), the
current SPI include block pulls only `esp_eth_phy.h` / `esp_eth_mac.h` from
`esp_eth`. Add, guarded:
```c
#if defined(HW_ETH_USE_W5500) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#endif
```
(Keep the legacy `esp_eth_mac.h`/`esp_eth_phy.h` W5500 declarations for IDF < 6.0
via `#elif`, if pre-6.0 builds must still be supported. If not, simplify.)

### 3.4 Re-gate and update the W5500 block

Replace the dead guard:
```c
#if defined(CONFIG_ETH_USE_SPI_ETHERNET) && defined(CONFIG_ETH_SPI_ETHERNET_W5500)
```
with:
```c
#if defined(HW_ETH_USE_W5500) && defined(CONFIG_ETH_USE_SPI_ETHERNET)
```
The body (SPI bus init, `spi_device_interface_config_t`, `ETH_W5500_DEFAULT_CONFIG`,
MAC/PHY creation, driver install, MAC-from-eFuse, attach, start) is essentially
unchanged — the `espressif/w5500` API is API-compatible with the old in-tree
driver. Re-verify against the component README:
- `ETH_W5500_DEFAULT_CONFIG(spi_host, &devcfg)` signature.
- `eth_w5500_config_t` fields actually used: `int_gpio_num` (and consider
  `poll_period_ms` for interrupt-less polling mode if INT pin is -1).
- The W5500 has an internal MAC+PHY; `esp_eth_phy_new_w5500()` is still required
  (the generic 802.3 PHY does **not** work for it).

### 3.5 SysType configuration for a W5500 board

Example (`ScaderLedsWaveshare` already has the SPI-only sdkconfig skeleton):
- `features.cmake`:
  ```cmake
  set(RAFT_ENABLE_ETH_W5500 ON)          # gates the component REQUIRES
  add_compile_definitions(HW_ETH_USE_W5500)
  ```
  Do **not** also define `HW_ETH_PHY_LAN87XX` for a pure-W5500 board.
- `sdkconfig.defaults`:
  ```
  CONFIG_ETH_ENABLED=y
  CONFIG_ETH_USE_SPI_ETHERNET=y
  CONFIG_ETH_USE_ESP32_EMAC=n
  ```
- JSON network settings: `ethLanChip:"W5500"`, `spiHostDevice`, `spiMOSIPin`,
  `spiMISOPin`, `spiSCLKPin`, `spiCSPin`, `spiIntPin`, `spiClockSpeedMHz`,
  `ethPhyRstPin`, `ethPhyAddr` (already parsed by `NetworkSettings`).

---

## 4. Gating strategy: zero size when disabled

This is the central requirement. There are three independent gates; understanding
which one provides the guarantee matters.

### Gate A — Source compilation (`ETHERNET_IS_ENABLED`)
[NetworkSystem.h](../components/core/NetworkSystem/NetworkSystem.h) defines
`ETHERNET_IS_ENABLED` **iff** `CONFIG_ETH_ENABLED`. All Ethernet code
(`startEthernet()`, handles, JSON) is inside `#ifdef ETHERNET_IS_ENABLED`.
[systypes/Common/sdkconfig.defaults](../../../systypes/Common/sdkconfig.defaults)
already sets `CONFIG_ETH_ENABLED=n` **and** `CONFIG_ETH_USE_SPI_ETHERNET=n` (the
latter is mandatory because it otherwise defaults to `y` and `select`s
`ETH_ENABLED`). So for non-Ethernet systypes, **no Ethernet *driver* code compiles
at all.** This gate is the primary zero-impact mechanism for non-Ethernet builds —
but see [§4.1](#41-known-non-zero-residues-accepted) for two residues it does *not*
cover.

### Gate B — W5500 source sub-gate (`HW_ETH_USE_W5500`)
Within Ethernet-enabled builds, the W5500 block must compile only when the SysType
asks for it. RMII (LAN87xx) systypes must not pull in any W5500 SPI code. Achieved
by `#if defined(HW_ETH_USE_W5500) ...` ([§3.4](#34-re-gate-and-update-the-w5500-block)).
This guarantees a LAN87xx build is byte-identical before and after the W5500
feature is added to the codebase.

### Gate C — W5500 component linkage (`RAFT_ENABLE_ETH_W5500`)
This is the new and subtle one. A managed dependency in `idf_component.yml` is
*downloaded* unconditionally (disk only), but only contributes to the **firmware**
if it is in the component dependency closure (i.e. something `REQUIRES` it) and its
symbols are referenced.

Approach, mirroring the existing `RAFT_ENABLE_SD` / `FS_TYPE` pattern in
[CMakeLists.txt](../CMakeLists.txt):
```cmake
# In RaftCore/CMakeLists.txt, near the esp_eth block (line ~48-52).
# esp_eth stays unconditional (the existing comment explains why: component
# requirements are resolved before sdkconfig, so CONFIG_* can't gate REQUIRES).
# The W5500 driver is gated on a plain CMake var set by features.cmake, which IS
# available at config time.
if(DEFINED RAFT_ENABLE_ETH_W5500 AND RAFT_ENABLE_ETH_W5500)
  set(RAFT_CORE_REQUIRES ${RAFT_CORE_REQUIRES} espressif__w5500)
endif()
```
The registry component `espressif/w5500` registers under the CMake component name
`espressif__w5500`. Because `RAFT_ENABLE_ETH_W5500` is unset for every other
systype, the W5500 component is **not in their REQUIRES closure** and is therefore
not linked.

Two independent backstops make this robust:
1. **`-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`** are default in
   IDF 6.0 (`tools/cmake/build.cmake:178-179`, confirmed). Any unreferenced
   function/data is dropped from the final image regardless of what got compiled.
2. **Minimal build** (`MINIMAL_BUILD` property, `tools/cmake/project.cmake:519+`):
   when enabled, only components in `main`'s closure are compiled at all.

So the firmware-size guarantee comes from **Gate C (conditional REQUIRES) +
gc-sections**, not from the manifest. The manifest dependency costs disk and
download time only.

### 4.1 Known non-zero residues (accepted)

`ETHERNET_IS_ENABLED` gates the *driver* code, but two things sit outside it.
Both are pre-existing and judged negligible — **decision: do not gate
`NetworkSettings`; accept the residue.** Documented here so the size budget is
understood, not hidden.

1. **`NetworkSettings` parsing is ungated.**
   [NetworkSettings.h](../components/core/NetworkSystem/NetworkSettings.h) has no
   `ETHERNET_IS_ENABLED` guard anywhere. `setFromConfig()` parses every
   Ethernet/SPI JSON key (`ethLanChip`, `smiMDCPin`, the new `ethRmiiClockMode` /
   `ethRmiiClockGpio`, all `spi*` fields) and stores the members for **every**
   systype, Ethernet or not. The LAN87xx migration added a sliver to this
   always-compiled path: 2 members (~8 bytes in the settings object) plus two
   parse calls and the inlined `getRmiiClockModeEnum()` helper (order tens of
   bytes of `.text`). The W5500 SPI fields were already parsed unconditionally, so
   the W5500 work adds nothing further here. Cost is tens of bytes, constant
   across all builds. If a literal zero is ever required, wrap the Ethernet/SPI
   block of `NetworkSettings` in `#ifdef ETHERNET_IS_ENABLED` — not done, by
   choice.

2. **`esp_eth` is an unconditional `REQUIRES`.**
   [CMakeLists.txt:52](../CMakeLists.txt) requires `esp_eth` unconditionally (the
   comment there explains why: component requirements resolve before sdkconfig, so
   `CONFIG_*` can't gate it). For a non-Ethernet build nothing references any
   `esp_eth` symbol (all references are inside `ETHERNET_IS_ENABLED`), so
   `-Wl,--gc-sections` drops it to ~zero in the final image. This is sound in
   principle but rests on the linker; confirm once with an `idf.py size` diff
   ([§5](#5-verification-checklist)). The W5500 component is *not* treated this way
   — it is gated via Gate C precisely so it never enters a non-W5500 link graph.

### `idf_component.yml` consideration
The component manager resolves `idf_component.yml` dependencies regardless of
SysType. Options, in order of preference:
1. **Preferred:** list `espressif/w5500` in the manifest but keep it out of
   `REQUIRES` unless `RAFT_ENABLE_ETH_W5500` (Gate C). Verify with a size diff that
   a non-W5500 build is unchanged. This is the least fragile and keeps a single
   manifest.
2. If even the download is undesirable for most builds, the dependency can instead
   be declared in the W5500 SysType's own component manifest rather than RaftCore's,
   keeping it out of the dependency tree entirely for other systypes. Heavier to
   maintain; only do this if option 1 shows a measurable size regression.

---

## 5. Verification checklist

Size (the binding constraint):
- [ ] `idf.py size` for a **non-Ethernet** systype (e.g. `ScaderLocks`) — must be
      byte-identical before/after the W5500 changes land.
- [ ] `idf.py size` for an **RMII/LAN87xx** systype (`ScaderRelays`) — must be
      unchanged by the W5500 work (Gate B + C).
- [ ] `idf.py size-components` on a W5500 systype to confirm `espressif__w5500`
      appears only there.

Functional:
- [ ] LAN87xx (Olimex ESP32-PoE-ISO): builds on IDF 6.0.1, links, gets DHCP IP via
      `IP_EVENT_ETH_GOT_IP`. Confirm `ethRmiiClockMode`/`ethRmiiClockGpio` defaults
      (OUTPUT/17) match the board, else set in JSON.
- [ ] W5500 board: builds, links `espressif/w5500`, SPI bus init OK, MAC read from
      eFuse, link up, DHCP IP.
- [ ] Cleanup: remove dead `CONFIG_ETH_PHY_INTERFACE_RMII` / `CONFIG_ETH_RMII_CLK_*`
      / `CONFIG_ETH_SPI_ETHERNET_W5500` keys from all `sdkconfig.defaults`.

---

## 6. Open questions

1. ~~Is pre-6.0 IDF support still required?~~ **Decided: keep pre-6.0 branches for
   now.** The `#elif` ladders for IDF 5.0–5.3 and the per-chip PHY constructors are
   retained; the W5500 changes are layered on top with `ESP_IDF_VERSION >= 6.0.0`
   guards rather than replacing them.
2. Confirm the exact `espressif/w5500` CMake component target name
   (`espressif__w5500` expected) once the component is fetched.
3. Decide manifest strategy ([§4](#4-gating-strategy-zero-size-when-disabled),
   option 1 vs 2) after the first size-diff measurement.
