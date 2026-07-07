## Changes

### REMOTE — OrdKeyCount() fix + AdsGetDate() crash fix

- **OrdKeyCount() returns 0 on remote aliases (#128)**: xBrowse grids showed no rows because OrdKeyCount() called AdsGetKeyCount(hOrdCurrent) which had no remote code path. Added new wire opcode GetKeyCount (0xB0/0xB1), client/server round-trip, and remote index + table handle routing. Server handler computes the true filtered key count from the active index order.

- **AdsGetDate() crashes on remote Date-type fields (#128)**: rddads passes hOrdCurrent (a RemoteIndex handle) to AdsGetDate for Date columns. Fixed by resolving RemoteIndex -> parent RemoteTable before delegating to AdsGetField.

Full details in CHANGELOG.md.

---
