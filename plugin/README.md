# Admin Give — LAC Online 1.9.7

Native .lacscripts plugin for the LAC 1.9.7 Android ARM64 runtime.

## Command

/admin give <playerID> <itemID> <quantity>

Example: /admin give 15 cash 5000

This is an inventory grant, not a transfer:
- Existing item: the requested amount is added to its current quantity.
- Missing item: the game's InventoryAdd creates a new inventory entry.
- Nothing is removed from the executor or another player.

Only a sender whose server-side ServerUser record has state host (1) or admin (2) is authorized. The handler also requires Mirror NetworkServer.active.

## Implementation

- Hooks server-side PlayerKillFeed.UserCode_CmdSendChat at RVA 0x1080A54.
- Checks the LAC 1.9.7 method prologue before installing.
- Resolves the target through PlayersManager.GetPlayer.
- Calls PlayerControl.InventoryAdd on the target.
- Reads the resulting authoritative inventory and invokes PlayerControl.RpcUpdateInventory to synchronize clients.
- Reports success/failure through PlayerKillFeed.RpcGetChat.
- Passes all non-matching chat messages to the original method unchanged.

## Build and install

GitHub Actions builds classes.dex, the ARM64 native library, and admin-give.lacscripts. Import the generated file in the game's SCRIPTS screen, enable it, and restart the game process.

## Verification boundary

CI validates package structure, SHA-256 manifest entries, DEX magic, and the ARM64 ELF header. It cannot prove live server authorization or client synchronization without a real LAC 1.9.7 session. A successful CI build is not a live-game test.
