# PINE Ignition extensions (`0x10`-`0x12`, `0x18`-`0x19`)

RPCS3 implements the same wire format as PCSX2's Ignition extensions (length-prefixed UTF-8 bytes for section and key names).

## Section and key paths

- **section**: Path from the config root to the **parent** YAML node, using `/` between segments. Examples: `Video`, `Video/Vulkan`, `Core`.
- **key**: The **leaf** entry name under that node (must be a scalar config entry, not a nested `cfg::node`). Examples: `Resolution Scale`, `Adapter`.

Names match the human-readable names in `g_cfg` / `config.yml` (see `rpcs3/Emu/system_config.h`).

## Opcodes

| Opcode | Role |
|--------|------|
| `0x10` | Get current value as string (`to_string()` on the leaf). Unknown path returns empty string. |
| `0x11` | Set value via `from_string`. Fails if the key is unknown, parse fails, or the entry is non-dynamic while emulation is running. |
| `0x12` | Persist and reload active config using the same `save_emu_settings` path as the home menu. |
| `0x18` | List active-title patch entries. Response shape matches Ignition patch catalog wire format. |
| `0x19` | Enable/disable a patch entry by opaque patch ID. Persists to `patch_config.yml`. |

## Patch opcodes (`0x18`-`0x19`)

- `0x18` response fields keep Ignition compatibility:
  - `name`: opaque stable patch ID (`rpcs3:` + encoded key fields)
  - `description`: human-readable patch label
  - `place`: domain hint derived from hash prefix (`PPU`, `SPU`, `Other`)
  - `enabled`: current enabled flag for the resolved title/serial/app-version node
  - `global_toggleable`: always `true` in this milestone
- `0x19` accepts the opaque ID in `patch_name` and writes enabled state to `patch_config.yml`.
- Runtime effect is persist-first: while emulation is running, full effect may require reboot/reload.

## Version string

`MsgVersion` (`0x08`) includes the suffix `| IGNITION_PINE:1` when Ignition opcodes are supported.

## Manual test (TCP)

1. Enable **IPC Server** in RPCS3 and note the port (default **28012** on Windows).
2. Send a packet: 4-byte LE total length, then payload `[0x10][u32 section_len][section][u32 key_len][key]`.
3. Expect a reply with total length, `0x00` status at byte offset 4, then `[u32 value_len][value bytes]`.

Example dynamic key while a game is running: `Video` / `Debug overlay` (see `system_config.h`).
