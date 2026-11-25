# Object scripts

Object scripts (`scripts/object/<name>.obs`) drive guests and staff inside the simulation tick. Because they change the world state, a mod that ships object scripts or vocabulary patches is a **world mod** (`"scope": "world"` or omitted in `mod.json`); presentation mods are refused at start-up if they contain files under `scripts/object/` or `scripts/object_vocabulary.patch.json`.

## File format and header

Every object script starts with the header line `#obl-script 1 object` (LF or CRLF) followed by `State` blocks. Comments begin with `//` (or `/`) and run to the end of the line. Statements end with `;`.

```text
#obl-script 1 object
State STATE_NULL
	OnEntry
		Set WalkAnimation "D_WALK_NORMAL";
		Set StandAnimation "D_STAND_NORMAL";
		Set WalkSpeed 60;
		ChangeState STATE_STARTUP;
	End
End
```

All `scripts/object/*.obs` files shipped by active mods are parsed in **strict mode** at start-up: any unknown command, function, value, state, or message (including in `ChangeState`, `ExitState`, and `EndMessage`), or any parse error, stops the start naming the mod id, file path, and 1-based line number.

## Mod states and messages (`scripts/object_vocabulary.patch.json`)

Mods never pick numeric ids for their own states or messages. Instead, declare new names in `scripts/object_vocabulary.patch.json` (`obl.object_script_vocabulary_patch` v1):

```json
{
  "format": "obl.object_script_vocabulary_patch",
  "version": 1,
  "states": ["STATE_BUSKER_WALK", "STATE_BUSKER_PERFORM", "STATE_BUSKER_LEAVE"],
  "messages": ["MT_BUSKER_DONE"]
}
```

- State names must match `^STATE_[A-Za-z0-9_]+$`; message names must match `^MT_[A-Za-z0-9_]+$`.
- The engine assigns sequential ids after the pack's maximum id, walking active mods in load order and sorting names alphabetically within each mod. Because saves and replays record the active world mods in load order, ids match on every load.
- Any name that collides with the pack or with an earlier mod is refused at start-up, naming both sides.
- `obl.save` writes `base.state` by name using the merged vocabulary, so saving mid-script in a mod state reloads into the same state.

## Attaching a script to guests and staff

- **Guest types (`guests/<set>.json`, `obl.guest_set` v2):** add an optional `"script": "<stem>"` field on a guest type entry (for example via `guests/l0.patch.json`). The engine loads `scripts/object/<stem>.obs` on first use. Omitting `"script"` keeps the scenario's default `GuestLogic`.
- **Staff records (`catalog/staff/<key>.json`, `obl.staff` v2):** `"script"` selects the staff profession class (`BUILDER`, `CLEANER`, `MECHANIC`, `LIFEGUARD`, `REP`, `SECURITYGUARD`, `SERVE`). Add an optional `"object_script": "<stem>"` field to override that profession's default object script with `scripts/object/<stem>.obs`.

## Blocks and commands

- **State block:** `State <STATE_...> ... End` containing `OnEntry ... End`, `OnExit ... End`, `OnUpdate ... End`, and `Message <MT_...> ... End` (with `OnReceipt ... End` and/or `OnSend ... End`).
- **Commands:**
  - `ChangeState <STATE_...> [Time <seconds>];` — switch state immediately or after a delay.
  - `Message <MT_...> <receiver> <data> <flags> [Time <sec>] [MaxTime <sec>] [FloatData <v>] [State <v>] [SendFrom <v>];` — send a message.
  - `PlayAnimation <name> [Finish <anim>] [NotLooped] [Reverse] [ExitState <STATE_...>] [MaxTime <sec>] [EndMessage <MT_...>];` — play an animation.
  - `PlaySound <name>;` — play a 3D sound at the object's position.
  - `Function <name>;` — call a built-in engine function (case-insensitive).
  - `Set <target> <value>;`, `Add <target> <value>;`, `Minus <target> <value>;`, `Multiply <target> <value>;`, `Divide <target> <value>;` — update a variable.
  - `If (<left> [<op> <right>]) ... [Else ...] End` — branch; operators are `==`, `!=`, `<`, `>`, `<=`, `=<`, `>=`, `=>`, `&&`, `||` (separated by spaces).
  - `Switch (<value>) Case <v>: ... End` — multi-way branch.
- **Operands:** built-in value names (case-insensitive), numbers, `"quoted strings"`, `STATE_*` names, `Random<n>`, and `RandDWORD<n>`.

## Functions

The engine's fixed list of `Function <name>;` targets (matched ignoring ASCII case; kept in step with `src/game/objects/script_program_api.cpp` by a unit test):

- **Guest desires & opinions:** `UpdateDesires`, `UpdateOpinions`, `SearchDesire`, `AlterDesires`, `MissedDesire`, `ImproveOpinions`, `AlterPriceOpinion`, `AlterQueueOpinion`, `AlterSafetyOpinion`, `BadSafetyOpinion`, `IncrementOpinions`, `DecrementOpinions`, `DoSpotOpinion`, `UpdatePurchaseOpinion`, `UpdatePriceScore`, `ShowThumbsOverlay`, `ShowThumbsDown`, `SetupStartValues`, `AddRide`, `RandomisePallete`, `RandomiseAttachedPallete`, `SetVisible`, `SetInvisible`, `Lit`, `UnLit`, `FaceSender`, `FaceEachOther`, `FaceCamera`, `ClearMyDelayedMsgs`, `KillMe`, `Debug`
- **Guest movement & areas:** `GetConsumePoint`, `GotoDancePoint`, `WalkToBeach`, `FindWalkPoint`, `WalkAlongPath`, `WanderBeach`, `GetonPath`, `HeadForBin`, `LookForBench`, `HeadForShore`, `LookForIllPoint`, `HuddleComplaintsOffice`, `HuddleWharf`, `HeadForEntryPoint`, `GotoWorldUnloadPoint`, `SetSurfacePoint`, `GenerateBlockedMessage`, `AlertGuests`
- **Guest hotel, social & misc:** `FindSomeWhereToStay`, `WakeUpTime`, `LeaveHotel`, `ReleaseHotel`, `DemolishedAsleep`, `NoPathToAccomadation`, `LookForTotty`, `DoIFancySender`, `MoveActionPerson`, `SetupSnogOffset`, `FindFightPoint`, `FindFightPerson`, `SendOutFightingMsgs`, `SetupFightTime`, `CreateLitter`, `LookForVandalItem`, `VandaliseBin`, `ReleaseBin`, `PlayToiletSound`, `CheckToiletStench`, `SendOutToplessMessages`, `RandomCrowdAnim`, `Sunburn`, `SendOutDeathMessage`
- **Guest thoughts:** `AddPulledThought`, `AddSexThought`, `AddDumpedThought`, `AddOffThought`, `AddDrowningThought`, `AddBadLeaveThought`, `AddBuildingFullThought`, `AddThoughtIDidnt`, `AddThoughtIDidIt`, `AddCommodBeachClosed`, `AddIllPurchaseThought`
- **Swimming & water sports:** `UpdateDrowning`, `DecideSwimAction`, `SurfPaddlePoint`, `SurfToBeach`, `GiveBackBoard`, `GiveBackLilo`, `GetNextPoolPoint`, `ChooseFunSlideAction`, `SetupFunPoolSwim`, `GetSeaEntry`, `WindSurfStraight`, `WalkToWindSurf`
- **Builder:** `ResetBuilderIdle`, `FindNearestValidTile`, `MakeStartEndCellValid`, `MakeStartEndCellBuilding`, `CheckForFemaleGuests`, `ResetNumBuildersWhistling`, `FaceGuestWhistling`, `RemoveBuilding`, `RemoveCostPoint`, `FreeBuilderOfJob`, `FreeBuilderOfJobNoBuildCallCheck`, `ReallocateBuilders`, `DeterminWhenToGoOnBreak`, `BuilderWasASleep`, `CheckForJobs`, `SetDrawPosBuild`, `SetDrawPosServe`
- **Cleaner:** `FindLitter`, `FindBinToClean`, `FindToiletToClean`, `FindUrgentClean`, `RemoveLitter`, `GoToBinFront`, `FaceBin`, `CleanBin`, `CleanToilet`, `ResetIdle`, `FaceLitter`, `FindWanderPos`, `DidNotClean`, `StartedCleaning`, `IdleThought`, `DirectedThought`, `StrollThought`, `CleanToiletTime`, `FindBestScoredJob`
- **Security:** `FindMisdemeanour`, `IncNumSeriousFines`, `IncNumMinorFines`, `IncNumJailed`, `IncNumCautioned`, `GoToFineOffsetAtHut`, `SetFacingDir`, `ChooseAction`, `SendDetainTickerMsg`, `Move2Hotel`
- **Lifeguard:** `Move2Tower`, `FindBeachPoint`, `FnMove2SeaEntry`, `Move2SeaEntryPoint`, `SearchForGuests`, `PullGuest`, `FaceLadder`, `FindBeach`, `GuestDrowned`, `GuestSaved`, `IsInMyArea`, `OffsetAfterRescue`, `SetGuestInvisible`, `SetGuestVisible`, `CheckGuestStillAlive`
- **Mechanic & rep & shared staff:** `FindJob`, `LookForBetterJob`, `LookWatering`, `DidNotRepair`, `FinishedRepairs`, `Repairing`, `GetWateringPos`, `FacePlant`, `MoveToNextBuilding`, `LookForGuests`, `ArrivedAtGuest`, `AddToBoostTotal`, `PatrollingThought`, `AddOpinionData`, `DecIdleTime`, `ResetDirectingStaff`, `SendToJob`, `StartShift`, `EndShift`, `DrawAttention`, `GotAttention`, `ForgetAttention`

## Values

The engine's fixed list of script variables and constants (matched ignoring ASCII case; kept in step with `src/game/objects/script_program_api.cpp` by a unit test):

- **Core & message operands:** `Me`, `Sender`, `Owner`, `NO_OBJECT`, `true`, `false`, `Default`, `State`, `GameTime`, `WorldStarted`, `WorldX`, `WorldY`, `Moving`, `DestObject`, `DestObjectEntranceX`, `DestObjectEntranceY`, `DrawOffsetX`, `DrawOffsetY`, `MessData`, `MessFloatData`, `MessFlags`, `MessState`, `MsgChar`, `DebugInfo`, `NeedsAttention`, `CurWeather`, `WthrLightRain`, `CurEvent`, `EventID`
- **Guest desires:** `DesireCommodFam`, `DesiredCommod`, `DesiredCommodText`, `DesiredCommodSeaClosed`, `DesireStroll`, `DesireSunbathing`, `DesireSwimming`, `DesireAccomodation`, `DesireAlcohol`, `DesireAmusement`, `DesireFirstAid`, `DesireHeadAche`, `DesireMeals`, `DesirePartyFun`, `DesireShower`, `DesireSnacks`, `DesireSoftDrinks`, `DesireSouvenirs`, `DesireSportsFun`, `DesireToilet`, `DesireWaterSportsFun`, `DesireLeaveResort`, `DesireVandalise`, `DesireFight`, `DesireFlirt`, `DesireUmbrellas`, `DesireSunTanLotion`, `DesireHardSex`, `DesireSleep`, `DesireScared`
- **Guest state & attributes:** `Cash`, `CommodPrice`, `MaxPrice`, `Happiness`, `Drunk`, `Oxygen`, `ImMale`, `Topless`, `FoodIll`, `ImIll`, `FoodIllDecay`, `SunburnLevel`, `ApplyLotion`, `AppliedLotion`, `AmountLotion`, `LeavingEarly`, `MissedPurchase`, `JustLittered`, `LitterInBin`, `WaitingForDeath`, `Vehicle`, `WharfID`, `HotelID`, `StayingAt`, `ComplaintsHut`, `IceDropScore`, `MySeaArea`, `StageDirection`, `IsEntryPointClear`, `OpinionBeauty`, `OpinionCost`, `WorstOpinion`, `WorstOpinionVal`, `BestOpinion`, `BestOpinionVal`, `MyOffence`, `NoOffence`, `FightPerson`
- **Movement & animation:** `Walkpoint`, `WalkpointX`, `WalkpointY`, `Walkanimation`, `StandAnimation`, `CurrentAnimation`, `WalkSpeed`, `MyDir`, `DestDir`, `NotOnPath`, `NumTimesBlocked`, `Fidget`, `Activity`, `OnBack`, `OnFront`, `OnSide`, `StopAction`, `RepeatAction`, `ActionState`, `ActionPerson`, `ActionFat`, `AttachedSprite`, `AttachedAnimation`, `AttachedDirection`, `AttachedOffsetX`, `AttachedOffsetY`
- **Staff shared:** `Home`, `HomeEntranceX`, `HomeEntranceY`, `HomeClosed`, `AtHome`, `AtWork`, `StartTime`, `EndTime`, `StaffWage`, `StaffWalkpoint`, `IdleTime`, `IdleTimePerHour`, `LastAction`, `DirectingStaff`, `DirectToObject`, `FoundDirecting`, `DirectingPosX`, `DirectingPosY`, `TimesPathBlocked`, `NearestValidTileX`, `NearestValidTileY`, `Night`, `InHut`
- **Cleaner & mechanic:** `FoundLitter`, `FoundBin`, `BinFull`, `ToiletDirty`, `FoundToilet`, `LitterDistance`, `EmptyBinAnimation`, `StaticBinAnimation`, `WanderPosX`, `WanderPosY`, `BinPosX`, `BinPosY`, `UrgentClean`, `UrgentType`, `FoundRepair`, `FoundBetterJob`, `IsBuilding`, `RepairType`, `WateringPosX`, `WateringPosY`, `LookForNearestJob`, `JobType`
- **Builder:** `BuildEntPointX`, `BuildEntPointY`, `BuildFacing`, `RestFacing`, `BuilderNumber`, `NumTimesPlayedAnimInRow`, `WaitTillMorning`, `NumBuildersWhistling`, `AmIDemolishing`, `IsJobStillValid`, `BuilderLocation`
- **Security:** `FoundMisdemeanour`, `Tolerance`, `ActionToTake`, `Offender`, `FightWithID`, `NumMinorFines`, `NumSeriousFines`, `MinorFineCost`, `SeriousFineCost`, `PatrolVelocity`, `DoubleFine`, `FineOffsetX`, `FineOffsetY`, `FineLevel`, `ExitPointX`, `ExitPointY`, `SecurityOffice`, `TellOff`
- **Lifeguard & rep & shark:** `Saving`, `SavingID`, `InSea`, `EnteringSea`, `SavingX`, `SavingY`, `DistanceToSender`, `Rescue`, `RescueSwim`, `InMyArea`, `AtHotel`, `DrawingGuest`, `SavingDistance`, `RescueThenDeath`, `GuestAlreadyBeingSaved`, `TalkingID`, `TimetalkingToGuest`, `TimetalkingToGuestFloat`, `PerCentToBoostGuest`, `SentToGuest`, `GuestOffSetPosX`, `GuestOffSetPosY`, `MyVictim`, `MyVictimOffsetX`, `MyVictimOffsetY`, `VictimDir`
