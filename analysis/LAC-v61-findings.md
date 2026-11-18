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
