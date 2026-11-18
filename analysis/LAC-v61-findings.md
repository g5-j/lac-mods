# LAC v61 IL2CPP analysis findings

Source package: `com.MA.LAC`, requested XAPK versionCode 61.
Analysis run: https://github.com/g5-j/lac-mods/actions/runs/44346161627
Il2CppDumper: v6.7.46
Metadata version: 31
ABI: arm64-v8a

## Confirmed game symbols

### Chat path
- `PlayerKillFeed.CheckChat(string _chat)` — RVA `0x1079F60`
- `PlayerKillFeed.CmdSendChat(string _prefix, string _chat)` — RVA `0x1079890`
- `PlayerKillFeed.RpcGetChat(string _chat, int _range, uint _pmID)` — RVA `0x1079958`
- `PlayerKillFeed.chatInput` — field offset `0x118`
- `PlayerKillFeed.lastChat` — field offset `0x128`

### Player lookup and inventory
- `PlayersManager.players` — static Dictionary<string, PlayerControl>
- `PlayersManager.GetPlayer(string _playerID)` — RVA `0xF8DFC8`
- `PlayerControl._playerID` — field offset `0x78`
- `PlayerControl.inventory` — field offset `0x98`
- `PlayerControl.CmdUpdateInventory(string _data)` — RVA `0xF32168` (Mirror Command)
- `PlayerControl.RpcUpdateInventory(string _data)` — RVA `0xF3221C` (ClientRpc)
- `PlayerControl.RpcUpdateInventoryIndividual(string _data)` — RVA `0xF322D0` (ClientRpc)
- `PlayerControl.RpcLookInventory(uint _playerID)` — RVA `0xF32384`
- `PlayerControl.InventoryAdd(string _item, string _value)` — RVA `0xF341E8`
- `PlayerControl.InventoryDelete(string _item, string _value)` — RVA `0xF34608`
- `PlayerControl.InventoryGet(string _item)` — RVA `0xF34A60`

### User rank states
- `UserState.client = 0`
- `UserState.host = 1`
- `UserState.admin = 2`
- `UserState.kicked = 3`
- `UserState.banned = 4`
- `ServerUser.matter` — field offset `0x28`
- `ServerUserManager.Instance` exists and `serverUsers` is a List<ServerUser> at field offset `0x68`.
- `ServerGameManager.Instance` exists; `adminCode` is at field offset `0x250`.

## Important unresolved points

1. IL2CPP dump provides declarations, field offsets and RVAs, not the C# method bodies.
2. The exact serialized inventory format and whether `InventoryAdd` itself commits/synchronizes changes are not yet confirmed.
3. The exact association between a local player/device and its `ServerUser` rank is not yet confirmed.
4. The Mirror Command `CmdUpdateInventory` is not proof that arbitrary clients can update another player's inventory; server-side authority/validation must be verified before using it.
5. A safe implementation must validate rank/host on the authoritative side, not trust a client-supplied target ID or rank.

## Next technical step

Inspect the ARM64 native code at the listed RVAs and the generated registration metadata to determine:
- how `CheckChat` receives and routes chat input;
- the inventory serialization format and server synchronization path;
- how the current session's `ServerUser.matter` is resolved;
- whether an authoritative server-side extension point exists.

Do not ship a client-only implementation that simply invokes `InventoryAdd` or `CmdUpdateInventory` without verifying server authority.


## Additional chat-command evidence

The v61 IL2CPP string table contains these chat-command literals:
- `/give `
- `/inventory `
- `/pay `
- `/pm `
- `/do`, `/dolow`, `/me`, `/melow`
- `/CommandTutorial.txt`

It also contains item/currency literals `cash`, `:cash`, `*cash`, `Cash`, and `Money`, plus an admin label and admin-code related text. This confirms the game already has a chat command parser and cash-related vocabulary. It does **not** by itself prove what arguments `/give` accepts or what permission checks it performs.

## Disassembly pass

The ARM64 library disassembly confirms that the listed method RVAs resolve to executable code. `InventoryAdd` calls string parsing helpers (`Split`, `Contains`, equality checks, and integer parsing), so inventory is stored in a serialized string rather than a simple numeric field. The exact delimiter/record schema and sync behavior still need to be traced before constructing inventory data.


## Exact ARM64 method-range analysis (verified)

Analysis run: https://github.com/g5-j/lac-mods/actions/runs/44349704308
The disassembler now bounds each method by the next method RVA from dump.cs, instead of blindly reading a fixed number of bytes. All nine requested methods were found and disassembled; the workflow completed successfully.

### Confirmed inventory behavior

- PlayerControl.InventoryAdd reads the current inventory string at offset 0x98, splits/parses existing entries, compares item identifiers, and rebuilds the serialized string with string concatenation. It writes the updated string back to inventory at offset 0x98.
- PlayerControl.InventoryDelete similarly parses and rebuilds the inventory string.
- PlayerControl.InventoryGet reads/parses the serialized inventory string.
- Neither InventoryAdd nor InventoryDelete directly calls the game's Mirror command/RPC send methods in its own native method body. Therefore, calling either method on a client is only a local object mutation unless a separate authoritative synchronization operation is performed.
- PlayerControl.CmdUpdateInventory is a generated Mirror [Command] wrapper. Its body obtains a pooled NetworkWriter, writes the string argument, and calls SendCommandInternal with requiresAuthority = true. It has no target-player ID parameter. This is the normal authority-restricted command path, not a demonstrated way for an arbitrary client to edit another player's inventory.
- PlayerControl.RpcUpdateInventory is a generated [ClientRpc] wrapper and routes its string through Mirror's RPC send path. It is not a client-callable substitute for an authorized server operation.
- PlayersManager.GetPlayer(string) resolves a player through the static player dictionary; it does not itself perform an inventory update or permission check.

### Chat command path

- PlayerKillFeed.CheckChat is a large method (2,903 decoded instructions in its metadata-bounded range) and includes a call to CmdSendChat.
- The string table contains /give and the failure text "can't give the item.". This is evidence of an existing give-command path, but the string table alone does not identify its handler, argument order, or authorization logic.
- The exact command handler and the mapping from a player/session to ServerUser.matter remain unverified. No /admin give plugin command has been added yet.

### Implementation decision

Do not implement /admin give by directly calling InventoryAdd on the target PlayerControl: that would risk changing only the local copy and not the server's authoritative inventory. The next analysis task is to locate the existing /give handler and verify its server-side rank/host checks. If that built-in command already supports the desired operation, the plugin can potentially act as a validated command alias instead of reimplementing inventory networking.
