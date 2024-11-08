# Models

The `models` directory contains models for the parts. These are OpenSCAD code to draw the part. This does not have to be pretty, and will often be a simple cuboid. It is used to create a cavity in the case, and any necessary holes, etc.

## Filename

For each part a file name is found for the necessary OpenSCAD code. This is done by checking a number of possible filenames until one if found.

- If there is an `LCSC Part #`, then this is checked. e.g. `C265103`
- The footprint is checked exactly, without prefix, e.g. `RevK:JST_EH_S5B-EH_1x05_P2.50mm_Horizontal` will check for `JST_EH_S5B-EH_1x05_P2.50mm_Horizontal`
- If neither of those matched, then the name of each 3D model is processed (all of them, not just the first) isong the filename without directory or suffix, e.g. `${KICAD6_3DMODEL_DIR}/Connector_JST.3dshapes/JST_EH_S5B-EH_1x05_P2.50mm_Horizontal.wrl` would check `JST_EH_S5B-EH_1x05_P2.50mm_Horizontal`.

In any case, when checking a filename (apart from the LCSC Part), any numbers in the filename are replaced with `ℕ`, e.g. for `JST_EH_S5B-EH_1x05_P2.50mm_Horizontal` the filenames checked are as follows, matching the first one found.

- `JST_EH_SℕB-EH_1x05_P2.50mm_Horizontal`
- `JST_EH_S5B-EH_ℕx05_P2.50mm_Horizontal`
- `JST_EH_S5B-EH_1xℕ_P2.50mm_Horizontal`
- `JST_EH_S5B-EH_1x05_Pℕmm_Horizontal`
- `JST_EH_S5B-EH_1x05_P2.50mm_Horizontal`

## How OpenSCAD is called

- If `N` was used in the filename, then `N` is set to the numeric value, e.g. ``JST_EH_S5B-EH_1xN_P2.50mm_Horizontal.wrl`` will have `N` et to `5`.
- For the part shape itself, `part` is set `true`.
- For the associated holes, `hole` is set `true`.
- For the associated block, `block` is set `true`.

This basically means the OpenSCAD can be called for three things...

In all cases the origin is the base of the centre of the part. The orientation is as per the 3D model.

### Orientation

When using the part number or footprint the orientation of the part matters, but when using the 3D model the exact 3D model orientation matters, so offset and rotations for the 3D model matter.

### Part

The `part` simply needs to cover the physical size/shape of the part. A function `module b(cx,cy,z,w,l,h)` is provided allowing a simple cuboid. It is possible for the part to go below the z axis, e.g. for the leads for through hole parts.

### Hole

The `hole` covers any holes in the case. This can be holes through the top of the case, e.g. for an LED to be visible. It can also cover holds in the side of the case for plugs that go in to a connector, etc.

When a hole goes through the side, the case itself is adjusted to have the cut line through the hole.

In either case, holes should be big enough for any sensible size of case.

Obviously only some parts need a hole.

### Block

The `block` is rarely used. It provides for a part of the case that must be present. It is added after the cavity is create, but before any holes. One use cases are a channel/tube around a hole for an LED in the top of the case, but this has to allow clear space on the PCB for the channel as adjacent parts won't cut in to the channel. Another use case is something like a tamper switch where a part of the case has to be present to press on the switch.

The block has to be big enough for any sensible size of case, it is truncated so it is within the case.
