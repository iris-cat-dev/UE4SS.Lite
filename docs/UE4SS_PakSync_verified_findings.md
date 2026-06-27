# UE4SS PakSync Verified Findings

Last updated: 2026-06-21

This document records facts already verified during the DRG PakSync debugging sessions. Treat these as current baselines unless a later log or IDA result explicitly disproves them.

## Stable Baseline

- `PendingNetGame::Tick` travel gating is the correct waiting point for DRG join.
- `LoadMapPre` is too late to block travel safely. It should remain diagnostic only.
- `PendingNetGame + 0xA8` is the ready flag currently used by the gate.
- Verified `UPendingNetGame::Tick` target:
  - vtable slot: `78`
  - RVA: `0x3942CA0`
- The gate must still call the original `PendingNetGame::Tick`, then suppress ready only while receiver-side PakSync work is incomplete.
- Resolver mismatch should disable the travel gate and log a warning, not call `PreventOriginalFunctionCall`.

## Current Code Structure

- `crates/ue4ssl-paksync/native/cpp/PakSync.cpp` remains the main PakSync runtime file.
- Legacy immediate hot-refresh code has been split into:
  - `crates/ue4ssl-paksync/native/cpp/PakSync/LegacyHotRefresh.hpp`
  - `crates/ue4ssl-paksync/native/cpp/PakSync/LegacyHotRefresh.cpp`
- `PakSync.cpp` now calls the legacy branch through `m_legacy_refresh`.
- The following implementation areas live in `LegacyHotRefresh.cpp`, not in the main runtime file:
  - AssetRegistry scan/retry after mount
  - UGC package-backed refresh
  - `ApplyPendingMods`
  - generic game cache refresh
  - pak asset package extraction/cache
  - preloaded package conflict and partial hot-reload reporting
- This keeps the stable restart-based PakSync path separate from the experimental legacy hot-refresh path.

## Current Restart-Based PakSync Flow

- Downloaded pak files are cached under `Mods\UE4SSL.PakSync\incoming` using sha256-based names.
- Room-specific pak selection is recorded under `Mods\UE4SSL.PakSync\rooms\<room_id>\manifest.txt`.
- Restart uses `-PakSyncRoom=<room_id>` to opt into loading the synced pak set for that room.
- Normal game launch without `-PakSyncRoom` should not load previously received room paks.
- Received paks are not copied into `FSD\Content\Paks`, to avoid mixing different rooms' pak sets with the base game/global mod set.

## Network And Transfer

- Control-channel transport is working.
- Manifest, manifest-end/no-work, resume, chunk, ack, and done frames are implemented.
- Resume retry fixed the earlier missing-chunk stalls:
  - `ResumeRetryIntervalMs = 2000`
  - `MaxResumeRetriesPerSession = 10`
- Latest successful client logs show:
  - `manifest received`
  - `queued Resume reason=initial`
  - chunk progress reaching `4/4`
  - `received pak verified`
  - final pak written under `Mods\UE4SSL.PakSync\incoming`
- A lingering `.TMP` file means an incoming session did not finish cleanly in that run; a run with `received pak verified` and a final `.pak` proves the transfer path itself is good.

## Mount And AssetRegistry

- `FPakPlatformFile::Mount` works in DRG with current resolver:
  - RVA: `0x33C4A10`
  - observed log: `pre-travel mount ... ok=true`
- `FPackageName::RegisterMountPoint` remains unsafe to call by resolver because current match confidence is low:
  - observed confidence: `55`
  - observed matches: `756`
- AssetRegistry refresh works after mount:
  - `UAssetRegistryHelpers` fallback finds `AssetRegistryImpl`
  - `ScanPathsSynchronous` completes
  - current first version scans pak-derived paths plus `/Game` when needed
- Successful mount and AssetRegistry scan only make future loads see the pak. They do not replace UObjects already loaded before mount.

## DRG SimpleUGC Refresh

- Reflected DRG/SimpleUGC functions verified in logs/dump:
  - `UUGCRegistry::MountUGCPackage(Package, FromJoining) -> bool`
  - `UUGCRegistry::RegisterAssetFromPackage(Package)`
  - `UUGCRegistry::ResetUGCPackagesManipulatedDuringJoin()`
  - `UUGCSubsystem::ApplyPendingMods(FromJoining, FromStartScreen)`
  - `UUGCSubsystem::SetPackagesAsRecentlyInstalled(RecentMods)`
  - `UUGCSubsystem::MarkRecentlyInstalledModsSuccesful()`
- `ApplyPendingMods` has exactly two bool params:
  - `FromJoining`
  - `FromStartScreen`
- Current code synthesizes a reflected `UGCPackage` object rather than writing raw layout memory.
- The refresh path can make the client show the mod as enabled, but that does not prove already-loaded game assets were replaced.
- `UE4SS (5).log` verified that calling `UUGCRegistry::MountUGCPackage` with the synthesized PakSync `UGCPackage` is unsafe:
  - the function was reflected successfully
  - params matched `Package`, `FromJoining`, `ReturnValue`
  - `SafeProcessEvent` caught SEH `0xC0000005`
  - code path fell back to manual `RegisterAssetFromPackage`
- Therefore `auto_ugc_mount_package` must default to `false`. Only enable it for experiments with a real game-created `UUGCPackage`, not a synthesized one.

## Proven Unsafe Or Ineffective Paths

- Direct `UFSDGameInstance::ResetAlwaysLoadedWorldsAndGameData()` during `PendingNetGame` travel is unsafe.
  - It hung inside `ProcessEvent`.
  - Logs stopped after `invoking ResetAlwaysLoadedWorldsAndGameData ... start`.
  - Keep `auto_game_cache_refresh=false` by default.
- Ad hoc `StaticFindObject` diagnostics in the hot update path caused SEH exceptions before. Use `RC::Seh::SafeStaticFindObject` or avoid the diagnostic.
- Do not special-case one mod, one mission, or one path such as `PZ_Zone03`. The remaining problem is generic loaded package replacement.

## Current Remaining Problem

Successful client logs prove transfer, verification, mount, AssetRegistry scan, and UGC refresh all complete. One unresolved issue is this class of log:

```text
pak contains 1 package(s) already known before mount
preexisting package /Game/GameElements/Missions/PlanetZones/PZ_Zone03
post-mount refresh partial ... package(s) were loaded before mount and may keep old UObject state
sync phase MountPending -> MountedPartial
```

Meaning: the pak overrides a package that was already known or loaded before the received pak was mounted. UE will keep existing in-memory `UObject` state unless a safe reload/fixup path exists.

`UE4SS (5).log` is different: it reached `ReadyToTravel` and logged `no preloaded package conflicts detected`. If the mod still has no visible effect in that case, the blocker is not the preloaded-package detector for that run. It is more likely that the synthesized UGC package/manual registry path is insufficient to reproduce DRG's original mod activation side effects.

`UE4SS (6).log` verified the new default:

- client loaded config with `ugc_mount_package=false`
- `MountUGCPackage` reflection still logged for diagnostics, but invocation was skipped by config
- no SEH occurred
- transfer, mount, AssetRegistry scan, manual UGC registration, `ApplyPendingMods`, and `ReadyToTravel` all completed
- one `failed to append frame bytes=0` appeared during control-frame flushing, but the run still completed; this pointed to flush reentrancy/queue mutation rather than a transfer failure

## Package Reload Status

- UE4.27 source has native `ReloadPackage(UPackage*, uint32)` in `CoreUObject\Private\UObject\PackageReload.cpp`.
- That implementation performs heavy operations:
  - `FlushAsyncLoading`
  - `ResetLoaders`
  - old package rename
  - replacement load
  - object mapping and reference fixup
  - GC
- Current DRG dump search did not find the typical `ReloadPackage` log strings such as:
  - `Reloading Packages`
  - `ReloadPackage cannot reload`
  - `Preparing Packages for Reload`
  - `Fixing-Up References`
- Therefore do not hardcode a `ReloadPackage` call unless IDA confirms the function exists in this shipping binary and the calling convention/address are stable.

## 2026-06-20 Generic UGC Refresh Follow-Up

- The synthesized `UUGCPackage` path has been expanded to populate the remaining generic fields from `UGCPackage.h`:
  - `Status=Fully`
  - `DownloadVersion=Required`
  - `MountingToBeApplied=false`
  - `DeprecatedLocation=false`
  - `ShowStatusForAudioCosmetic=false`
  - empty `Dependencies`
  - `DependencyRemoved=false`
  - `PackagedForLatestVersion=true`
  - `OverridePackedForLatestVersion=true`
- This remains generic and does not special-case a mission, asset path, or particular mod.
- `MountUGCPackage` remains disabled by default because the synthesized package previously caused SEH `0xC0000005` inside `ProcessEvent`.
- `invoke_no_param_function` now uses `SafeProcessEvent`, so reflected no-param calls report SEH failure instead of crashing immediately.
- `auto_game_cache_refresh` was changed from a pre-travel blocker to a `LoadMapPost` experiment:
  - after mount and AssetRegistry scan, it arms a refresh for `LoadMapPost`
  - `receiver_side_ready_for_travel()` no longer waits on game-cache refresh
  - `LoadMapPost` schedules `ResetAlwaysLoadedWorldsAndGameData` / `RefreshIsGameModded` after a short delay
  - default remains `auto_game_cache_refresh=false`
- Expected diagnostic log lines when enabled:
  - `armed generic game cache refresh for LoadMapPost`
  - `LoadMapPost diagnostic ... cache_waiting=true`
  - `scheduled post-load generic game cache refresh`
  - `starting deferred generic game cache refresh after pak mount`

## UE4SS (7).log Follow-Up

- Client config had `auto_game_cache_refresh=true`.
- The pak was an existing incoming pak and mounted during Unreal init before the network join handshake.
- AssetRegistry scanning finished after several `LoadMapPost` diagnostics had already fired.
- The cache refresh was armed only after scan/UGC refresh completed, so it missed the earlier `LoadMapPost` event and never ran.
- Code now records whether `LoadMapPost` has already been observed during the join. If refresh is armed afterward, it schedules the delayed post-load refresh immediately.
- The log also showed the client flushing its local `manifest:no-work` frame during join and then catching an SEH near `UControlChannel::SendBunch`.
- Code now treats seeing a real `PendingNetGame` in `LoadMapPre` as enough evidence that this instance is a joining client, so outbound local catalog frames are dropped even if the `PendingNetGame::Tick` vtable detour did not install yet.

## UE4SS (8).log Follow-Up

- The core PakSync path completed successfully:
  - host manifest and chunks were received
  - `received pak verified` was logged
  - `pre-travel mount ... ok=true` was logged
  - pak-derived AssetRegistry paths plus `/Game` were scanned
  - `AssetRegistry ScanPathsSynchronous completed` was logged
  - synthesized `UUGCPackage`, manual `RegisterAssetFromPackage`, `SetPackagesAsRecentlyInstalled`, `MarkRecentlyInstalledModsSuccesful`, and `ApplyPendingMods` all ran
  - travel reached `ReadyToTravel`
- This proves the current failure is not transfer, verification, mount, or AssetRegistry visibility.
- The new post-load cache-refresh scheduling also ran:
  - `LoadMapPost already observed; scheduled post-load generic game cache refresh`
  - `starting deferred generic game cache refresh after pak mount`
- `UFSDGameInstance::ResetAlwaysLoadedWorldsAndGameData()` is unsafe even after load:
  - `SafeProcessEvent` caught SEH `0xC00000FD` stack overflow
  - the call returned failure and should not be used during join/post-load refresh
- `RefreshIsGameModded()` succeeds but is insufficient to make the mounted pak's gameplay effect active.
- Therefore the next generic diagnostic target is DRG/SimpleUGC identity and enablement state:
  - synthesized package `GetIdAsString()` / `GetIdAsInt()`
  - `UUGCSettings.SelectedSlot`
  - selected slot contents
  - whether the selected slot contains the synthesized package id
- If the synthesized package id is empty/unavailable or absent from the selected slot, DRG can show a package-like entry as installed/enabled while not applying it as an actually enabled mod for gameplay cache construction.
- Follow-up implementation:
  - PakSync now derives a stable numeric mod id from the received pak's SHA-256.
  - The synthesized `UUGCPackage.ModURL` is populated with that stable id instead of an empty string.
  - The UGC refresh path now calls both `SetPackagesAsRecentlyInstalled(TArray<UUGCPackage*>)` and `SetModsAsRecentlyInstalled(TArray<FString>)`.
  - `SetModsAsRecentlyInstalled` writes DRG's `/Script/FSD.UserGeneratedContent RecentlyInstalledMods` string list through the game's reflected API, based on local dump/decompile evidence.
  - `MarkRecentlyInstalledModsSuccesful()` is invoked after `ApplyPendingMods()` so it does not clear `RecentlyInstalledMods` before `ApplyPendingMods()` consumes the id.
- Expected new diagnostic lines:
  - `synthesized UGC package ... stable_mod_id=...`
  - `synthesized UGC identity ... id_string=... id_int=... mod_url=...`
  - `invoked SetModsAsRecentlyInstalled ... mod_id=...`
  - `UGCSettings slotN ... contains_synthesized_id=...`

## UE4SS (9).log Follow-Up

- The client joined successfully and the mod appeared in the mod list, but gameplay effect still did not apply.
- The log proved why the previous `SetModsAsRecentlyInstalled` path did not run:
  - PakSync generated `stable_mod_id=991242320643089658`
  - the synthesized package had `mod_url=991242320643089658`
  - `GetIdAsString()` still returned empty
  - `GetIdAsInt()` still returned `0`
  - `SetModsAsRecentlyInstalled` was skipped because code was still taking the id from `GetIdAsString()`
- SDK template source confirms base `UUGCPackage::GetIdAsString()` returns empty and `GetIdAsInt()` returns `0`; therefore a synthesized base package cannot be trusted to provide a mod id through those functions.
- Follow-up implementation:
  - added `effective_ugc_mod_id()`: use `GetIdAsString()` when present, otherwise fall back to reflected `ModURL`
  - `SetModsAsRecentlyInstalled` now uses this effective id, so synthesized PakSync packages use the stable id
  - UGCSettings diagnostics now check `effective_mod_id` instead of only `GetIdAsString()`
  - PakSync now inserts the effective id into the current in-memory `UUGCSettings.SelectedSlot` array before `ApplyPendingMods`
  - this slot insertion is runtime-only; code does not call `SaveToSelectedSlot`, so host-provided PakSync ids are not intentionally persisted to player settings
- Expected next-run log lines:
  - `UGCSettings candidates=... effective_mod_id=991242320643089658`
  - `added mod_id to UGCSettings selected slot ...`
  - `invoked SetModsAsRecentlyInstalled ... mod_id=991242320643089658`
  - after refresh, selected `UGCSettings slotN ... contains_effective_mod_id=true`

## UE4SS (10).log Follow-Up

- The new effective-id path executed:
  - `effective_mod_id=991242320643089658`
  - `added mod_id to UGCSettings selected slot`
  - `invoked SetModsAsRecentlyInstalled`
  - selected slot later showed `contains_effective_mod_id=true`
- User observed the mod disappeared from the mod list.
- Interpretation:
  - writing a stable id into the selected slot is not sufficient when the synthesized package's own `GetIdAsString()` remains empty and `GetIdAsInt()` remains `0`
  - DRG likely tries to resolve enabled slot ids back to registry packages; an id that no package reflects as its own id can hide or invalidate the displayed entry
- Follow-up implementation:
  - synthesized package creation now prefers `/Script/SimpleUGC.UGCPackage_Windows` over base `UGCPackage`
  - selected-slot insertion and `SetModsAsRecentlyInstalled` now require a non-empty reflected `GetIdAsString()`
  - `ModURL` / stable id is still logged as `effective_mod_id`, but it is no longer forced into selected slot when the package cannot reflect that id
- Expected next-run behavior:
  - if `UGCPackage_Windows` returns a non-empty `id_string`, slot and `SetModsAsRecentlyInstalled` run with that id
  - if it still returns empty, PakSync should not force the id into slot, avoiding the `UE4SS (10).log` disappearance regression
- The same log also hit `MountedPartial`:
  - `/Game/GameElements/Missions/PlanetZones/PZ_Zone01` was already known before mount
  - this remains a separate loaded/cached UObject replacement problem even if UGC enablement is fixed

## UE4SS (11).log Follow-Up

- Synthesizing `UGCPackage_Windows` directly is unsafe.
- The object was created, but reflected id calls failed:
  - `GetIdAsString` hit SEH `0xC0000005`
  - `GetIdAsInt` hit SEH `0xC0000005`
  - a later repeated `GetIdAsString` caused `SEH exception in on_update`
- Because the update path was interrupted before `ApplyPendingMods` completed, the mod list remained missing.

## UGCExample / Generic Resource Enumeration Baseline

- `D:\Project\UGCExample` confirms SimpleUGC's generic resource enumeration is AssetRegistry-backed:
  - `UUGCRegistry::GetAllClassesInPackage` builds an AssetRegistry filter for `UBlueprint` under `Package.PackagePath`, reads the `GeneratedClass` tag, converts it to an object path, then loads the `UClass`.
  - `UUGCRegistry::GetMapsInPackage` builds an AssetRegistry filter for `UWorld` under `Package.PackagePath` and returns each asset name as a map name.
- This means SimpleUGC does not require a separate project-private resource cache in the sample implementation. If DRG's reflected `GetAllClassesInPackage` / `GetMapsInPackage` returns `false` with zero entries for a PakSync package while AssetRegistry scan succeeded, the likely problem is that the synthesized package does not give DRG's query code the same package path / registry state as a real game-created package.
- Current DRG logs repeatedly show:
  - `GetAllClassesInPackage result=false classes=0`
  - `GetMapsInPackage result=false maps=0`
- The safe next direction is PakSync-owned AssetRegistry/resource enumeration and diagnostics, followed by a compatibility hook only if needed:
  - keep `MountUGCPackage` disabled for synthesized packages because it has proven SEH-unsafe
  - do not hardcode one mission, one map, or one mod id
  - enumerate pak resources generically from pak-derived package names and AssetRegistry-visible packages
  - later intercept/fill DRG `GetAllClassesInPackage` / `GetMapsInPackage` only for PakSync-created packages if a stable hook point is confirmed
- Follow-up implementation:
  - reverted synthesized package class selection to base `/Script/SimpleUGC.UGCPackage`
  - stopped calling synthesized package `GetIdAsString()` / `GetIdAsInt()` for diagnostics or flow control
  - `effective_mod_id` now only uses reflected `ModURL`
  - selected-slot insertion and `SetModsAsRecentlyInstalled` remain skipped unless a future safe path can provide a true reflected id
- Interpretation:
  - `UGCPackage_Windows` likely requires platform/modio-owned internal state that `NewObject` does not initialize
  - the next valid direction is not hand-constructing the Windows subclass, but finding the game's real package creation/loading path, or bypassing UGC enablement and focusing on loaded asset/cache refresh

## IDA MCP Status

- IDA process was observed running locally.
- This Codex session did not expose an IDA MCP tool namespace through tool discovery.
- Local dump at `D:\Project\UE4SS.Lite\FSD-Win64-Shipping` remains the available analysis source in this session.

## Working Test Commands

```powershell
cargo check -p ue4ssl-paksync
cargo build -p ue4ssl-paksync --release
.\crates\ue4ssl-paksync\deploy.ps1 -SkipBuild -Destination 'D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss'
```

Client deployment path previously used:

```powershell
.\crates\ue4ssl-paksync\deploy.ps1 -SkipBuild -Destination 'J:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss'
```

## 2026-06-21 Refactor Verification

- Refactor completed:
  - mainline PakSync logic remains in `PakSync.cpp`
  - legacy asset/UGC/game-cache hot-refresh logic moved to `PakSync/LegacyHotRefresh.*`
- Search verification confirmed legacy refresh markers such as `ApplyPendingMods`, `AssetRegistry candidates`, `UGCRegistry candidates`, `generic game cache`, and `pak file asset extraction` are in `LegacyHotRefresh.cpp`, not `PakSync.cpp`.
- Build verification passed:

```powershell
cargo check -p ue4ssl-paksync
cargo build -p ue4ssl-paksync --release
```

- Host deployment completed successfully:

```powershell
.\crates\ue4ssl-paksync\deploy.ps1 -SkipBuild -Destination 'D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss'
```

## 2026-06-21 Resolver Lifetime Fix

- Symptom after refactor:
  - client/host logs showed `SEH exception in on_unreal_init: code=0xC0000005`
  - resolver names printed as corrupted text
  - runtime stayed at `resolver_ready=false detours=false`
  - travel gate and network detours were not installed, so the client could join directly without PakSync waiting
- Root cause:
  - `resolve_functions()` returned `ResolveResult` values containing `spec` pointers into a temporary `std::vector<PatternSpec>`
  - after `resolve_functions()` returned, `result.spec` became dangling
  - `log_resolvers()` later read the dangling pointer while formatting resolver names
- Fix:
  - `resolver_specs()` now returns a static `const std::vector<PatternSpec>&`
  - `resolve_functions()` must bind it as `const auto& specs`; copying it back into a local vector recreates the dangling-pointer bug
  - `ResolveResult.spec` points to stable process-lifetime storage
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment to `D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss` completed
- Note:
  - deployment to local `J:\...` client path failed in this environment because drive `J:` was not mounted; deploy the updated build on the client device separately

## 2026-06-21 Native Sync Status Window

- Restart/missing-pak prompts were changed from one-shot `MessageBoxW` dialogs to a small Win32 native status window.
- New files:
  - `crates/ue4ssl-paksync/native/cpp/PakSync/SyncStatusWindow.hpp`
  - `crates/ue4ssl-paksync/native/cpp/PakSync/SyncStatusWindow.cpp`
- The window runs on a separate Win32 UI thread so game/network hooks do not block on a modal dialog.
- Current Chinese UI shows:
  - sync status text
  - pak name
  - room id
  - sha256
  - byte progress
  - chunk progress
  - final restart reminder
- The `立即重启游戏` button calls the same restart path as PakSync and launches DRG with `-PakSyncRoom=<room_id>`.
- Build links now include `comctl32` for the progress bar and `gdi32` for default GUI font access.
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment to `D:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss` completed

## 2026-06-21 Download Approval Gate

- Client no longer starts pak transfer immediately after receiving a host manifest.
- New flow:
  - host manifest arrives
  - PakSync records a pending download approval
  - native status window shows pak name, size, sha256, room id, and `批准下载` / `拒绝`
  - only after `批准下载` does PakSync create the `.tmp` file, queue `Resume`, and request chunk transfer
  - choosing `拒绝` logs `user declined pak download` and does not download the pak
- Expected logs:
  - before approval: `manifest accepted; waiting for user approval before requesting pak chunks`
  - after approval: `user approved pak download; requesting chunks`
  - after rejection: `user declined pak download`
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment completed

## UE4SS (2).log Approval Timeout Follow-Up

- Observed client behavior:
  - manifest arrived and approval window appeared
  - PakSync immediately moved `AwaitingHostManifest -> ReadyToTravel`
  - travel gate restored and the client loaded into the room
  - connection later disappeared
  - clicking `批准下载` after that logged `download approval ignored: no pending manifest`
- Root cause:
  - pending download approval was not included in receiver-side readiness
  - `update_sync_phase(connection_count=0)` cleared `m_pending_download_approval`
- Fix:
  - pending download approval now keeps sync phase in `Receiving`
  - `receiver_side_ready_for_travel()` requires `!m_pending_download_approval`
  - host manifest timeout does not allow travel while an approval is pending
  - connection-loss reset no longer clears the pending approval
  - if approval happens after disconnect, the UI reports that the user should rejoin the same room to continue transfer
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment completed

## Host Two-Pak Follow-Up

- Host directory contained two paks:
  - `Anime-Dwarf1.pak`
  - `CustomMission.pak`
- Host log verified both were discovered and announced:
  - `pak manifest Anime-Dwarf1.pak ...`
  - `pak manifest CustomMission.pak ...`
  - `prepared ManifestEnd frame manifest_count=2`
  - both manifest frames were flushed
- Host only received `Done` for `CustomMission.pak`; no `Resume` for `Anime-Dwarf1.pak` appeared in the log.
- Root cause:
  - client approval state used a single `m_pending_download_approval`
  - when multiple manifest frames arrived before approval, the later manifest overwrote the earlier one
- Fix:
  - pending approval is now `m_pending_download_approvals`
  - each missing pak manifest is appended to the approval list
  - the approval window summarizes all pending paks and total size/chunks
  - approving starts downloads for every pending pak and queues `Resume` for each session
  - rejecting rejects every pending pak in the current approval batch
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment completed

## UE4SS (3).log Mixed Cached/Missing Pak Follow-Up

- Client log showed a mixed case:
  - `Anime-Dwarf1.pak` was missing and entered pending approval
  - `CustomMission.pak` was already present in incoming cache and immediately staged
- The cached pak path called `maybe_prompt_restart_after_stage()` immediately, before host manifest completion and before the missing pak was approved/downloaded.
- Result:
  - the final restart window could replace the approval window
  - the room manifest/staged set only contained the already cached pak
  - the missing large pak remained pending and never received `Resume`
- Fix:
  - cached-pak status window updates are skipped while pending approvals exist
  - final restart prompt is gated on:
    - host manifest complete
    - no pending download approvals
    - no incoming sessions
    - restart required
  - `ManifestEnd` now retries the restart prompt, but the guard prevents it from appearing until pending/missing paks are resolved
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment completed

## Next Valid Directions

- Prefer DRG/SimpleUGC reflected generic entry points when parameter names and sizes match.
- Keep package reload experimental and config-gated if implemented.
- Keep travel gate waiting until all receiver-side work is complete.
- Continue treating `MountedPartial` as not fully solved: it means the pak is present and enabled, but some already-loaded package state may still be stale.

## 2026-06-20 Real UGC Package / Generic Refresh Follow-Up

- IDA MCP still was not exposed as a callable tool in this Codex session; analysis used the checked-in local dump under `FSD-Win64-Shipping` and SDK headers under `D:\Project\FSD-Template`.
- Dump findings:
  - `UUGCPackage_Windows` is 288 bytes while base `UUGCPackage` is 280 bytes.
  - `UUGCPackage_Windows::GetIdAsString()` / `GetIdAsInt()` read through the native pointer at `package + 280`.
  - Real DRG code paths such as `sub_14132B0F0` and `sub_14132BC10` create `UUGCPackage_Windows` and initialize that native state before adding it to the registry.
  - The SDK does not expose a public `UUGCPackage_Windows` header, so this is a private platform implementation detail, not a safe reflected object to synthesize.
- Safety conclusion:
  - Do not synthesize `UUGCPackage_Windows`.
  - Do not write a PakSync stable id into `UGCSettings.SelectedSlot` unless the package itself safely reflects the same non-empty id through `GetIdAsString()`.
  - Keep `MountUGCPackage` disabled by default for synthesized packages.
- Follow-up implementation:
  - PakSync now first scans the selected `UUGCRegistry.UGCPackages` array for an existing real package matching the received pak by `PakFileLocation`, pak filename, asset overlap, or weak name match.
  - If a matching real package exists, PakSync reuses it instead of synthesizing a base package.
  - If no real package exists, PakSync still falls back to the safer base `UUGCPackage` synthesis for diagnostics/manual registration.
  - UGC identity logging safely reports `GetIdAsString()` / `GetIdAsInt()` results via `SafeProcessEvent`.
  - After `RegisterAssetFromPackage`, PakSync invokes reflected generic `GetAllClassesInPackage(Package, Classes)` and `GetMapsInPackage(Package, Maps)` when signatures match. These calls are generic SimpleUGC resource enumeration points, not mission-specific hooks.
- Expected next-run log lines:
  - `existing UGC package candidate ...`
  - `reusing existing UGC package ...` if DRG already made a true package
  - `UGC package identity ... reflected_id=... reflected_id_int=...`
  - `invoked GetAllClassesInPackage ... result=... classes=...`
  - `invoked GetMapsInPackage ... result=... maps=...`
- How to read the next log:
  - If no `reusing existing UGC package` appears and reflected id stays empty/0, DRG has not created a true UGC package for the received pak. The remaining problem is finding/calling the native package creation path or bypassing UGC entirely.
  - If a real package is reused and class/map counts are non-zero but gameplay still does not change, the remaining problem is loaded/cached UObject replacement after mount.

## UE4SS (12).log Follow-Up

- The log did not reach the PakSync receive/mount/UGC path:
  - no `NetConnection` was discovered
  - no host manifest was received
  - no chunk transfer started
  - no pak mount or AssetRegistry scan occurred
  - no new UGC package/resource enumeration code ran
- The client was loading from a J: drive UE4SS install in that log:
  - `J:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64\ue4ss`
- The only PakSync state visible during the failed attempt was a locally queued `NoWork` frame created at startup before any connection existed.
- Follow-up implementation:
  - startup `scan_local_paks()` now only scans local pak manifests
  - local `Manifest/ManifestEnd/NoWork` frames are queued only after a `NetConnection` is discovered
  - this removes the persistent pre-connection `queued=1` state and reduces client-join interference/diagnostic noise
- Expected next-run difference:
  - initial `update ready` should show `queued_frames=0` when there is no connection and no local pak work yet
  - once a connection appears, PakSync should log `queued local manifest announcement for new connection ...`

## UE4SS (4).log Large Pak Transfer Stall

- Client case:
  - missing pak: `Anime-Dwarf1.pak`
  - size: `26119564`
  - old chunking: `32768` bytes, `798` chunks
  - client created `incoming\46b55..._Anime-Dwarf1.pak.tmp`
- Log facts:
  - approval worked and queued/flushed an initial `Resume`
  - no chunk `0` was received
  - a later `Resume` flush hit `SEH exception in on_update: code=0xC0000005`
  - after that, one `Resume` stayed queued and the old retry budget reached `10/10`
  - the client then spammed `incoming session retry limit reached` and stopped making progress
- Practical cause:
  - large control-channel transfers were too aggressive for this path
  - `SendBunch` failure could leave the flush state/queue stuck
  - retry logic treated a temporary stall as permanent failure
  - old `.tmp` files did not have trustworthy chunk progress metadata, so retrying could not safely resume
- Fix implemented:
  - default transfer settings reduced to `chunk_size=16384` and `send_window=2`
  - per-update flush batch reduced to two frames
  - queued-frame flush now has a stuck-guard reset and send-failure backoff
  - incoming retry budget increased and no longer permanently gives up; after the budget it continues slow `Resume` retries with throttled logging
  - receiving any chunk resets retry state
  - `.tmp.map` progress files now record exactly which chunks were received; only sessions with a valid map resume, older untracked `.tmp` files are discarded and rebuilt
- Verification:
  - `cargo check -p ue4ssl-paksync` passed
  - `cargo build -p ue4ssl-paksync --release` passed
  - host deployment completed
- Next test expectations:
  - client should show `queued Resume reason=approved`
  - host should show `Resume received for Anime-Dwarf1.pak`
  - host should queue chunks in small windows
  - client should show `chunk received ... progress=N/...`
  - if connection drops, the next approval/join should show `resuming existing temp pak ...` only when a valid `.tmp.map` exists
