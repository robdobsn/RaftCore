# LEDPixels ColorOrder/Strip Plan

## Goal
Align color order and bytes-per-pixel with strip-level hardware configuration, while keeping segments as logical groupings. Reduce mismatches between segment color settings and on-wire data format.

## Current Behavior (Summary)
- Segments define `colorOrder` and write into the shared `_pixels` buffer using that order.
- Strips define `bpp` and serialize the raw bytes from `_pixels` based only on the strip `bpp`.
- This allows mismatches (for example, segment RGBWW but strip bpp=3), leading to unexpected LED output.

## Proposed Direction
- Treat `colorOrder` as a strip-level hardware property.
- Derive `bytesPerPixel` from `colorOrder` instead of specifying `bpp` separately.
- Keep segment-level color transforms optional and explicit (if needed for logical effects), but ensure they do not contradict strip hardware encoding.

## Plan
1. Inventory current configs and defaults
   - List all SysTypes LED strip configs and note where `bpp` is set or omitted.
   - List segment configs that specify `colorOrder`.

2. Define strip-level `colorOrder` and derived `bpp`
   - Add `colorOrder` to strip config schema.
   - Map `colorOrder` to `bytesPerPixel` using existing `LEDPixel::getBytesPerPixel()`.
   - Keep `bpp` as an optional override for compatibility, but warn if it conflicts with `colorOrder`.

3. Adjust segment config usage
   - Deprecate segment `colorOrder` for hardware encoding.
   - If segment `colorOrder` is present, treat it as a logical transform only (documented), or ignore it when strip `colorOrder` is set.

4. Update runtime validation and logging
   - On setup, log strip `colorOrder`, derived `bpp`, and any mismatch with explicit `bpp`.
   - If segment `colorOrder` differs from strip, log a warning indicating it does not affect on-wire encoding.

5. Update documentation and examples
   - Update docs to show strip-level `colorOrder` usage.
   - Add migration notes: how to move from segment `colorOrder` to strip `colorOrder`.

6. Test strategy (no code changes here)
   - With RGBWW strips, confirm `bpp=5` is automatically derived and matches on-wire data.
   - With RGB strips, confirm `bpp=3` and output is unchanged.
   - Validate that segment `colorOrder` no longer causes mismatched byte streams.

## Risks and Mitigations
- Risk: existing configs rely on segment `colorOrder`.
  - Mitigation: support a transition period with warnings and an opt-in flag.
- Risk: multiple strips with different color orders.
  - Mitigation: allow per-strip `colorOrder` in the strip list; keep segments mapped to strip ranges.

## Migration Notes
- Move `colorOrder` from segment config to strip config.
- Remove explicit `bpp` unless needed as an override.
- Verify physical LED output with a single known color and compare to expected ordering.

## Open Questions
- Do any current deployments require per-segment color order for intentional effects?
- Should `bpp` remain user-configurable once `colorOrder` is defined?
- Should a hard error be raised for `bpp`/`colorOrder` conflicts?
