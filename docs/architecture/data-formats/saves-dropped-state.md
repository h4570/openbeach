# Data formats: saves, state the old save drops

Part of [saves.md](saves.md). Each entry names state that the old save writer drops, the chaos round trip that showed
it, and the optional `obl.save` field that keeps it.

- **State the old save drops.** `GuestObject_Save` does not write `m_bTopless` (set by the guest
  script, read by other guests' reactions), `m_NumTimesBlocked` or `m_fMoodIconTime`, so a reloaded guest is not
  topless and the world goes on differently (chaos bot, scenario 1 seed 1 saved at 1500: one guest's action at 1548).
  It writes `m_Best` but not `m_Worst` (the worst spot of the day; `m_WorstType` and the score are saved). The staff
  save drops the happiness walk (`m_fTargetHappiness`, `m_fHappinessRate`, `m_bHappinessChanging`); the rate depends on
  the happiness at the last recalculation, so a load cannot rebuild it (chaos bot, scenario 12 seed 4: a builder).
  No section saves `CommodityProviders::m_complaints` (missed desires per commodity family since the last half-hourly
  feedback review), so a reloaded world skips the next complaint notice (chaos bot, scenario 2 seed 7).
  `ConstructionManager_Save` writes each job's work points but not which builder holds each one, so a reloaded builder
  claims the first free point instead of its own (chaos bot, `s1_work_points_save`: two sites, saved at tick 40).
  No save stores `g_ServedBeerCounter` (beers served by every bar since the last random beer effect, building_lifecycle.h
  `BeerRound`), and a load does not reset it, so a reloaded world draws its next random beer at another time (chaos bot,
  `s1_beer_counter_save`: the counter left by the session's own later ticks).
  The old queue record keeps no stand points (`BuildingQueue::m_pPoints`) or tail, so a load lays the points out straight
  from the serve point and a walker sent to the queue tail goes elsewhere (chaos bot, `s12_queue_points_save`).
  obl.save stores them as optional fields: guest `topless`, `times_blocked`, `mood_icon_time`, `worst`; staff
  `target_happiness`, `happiness_rate`, `happiness_changing`; `managers.commodities.complaints`;
  `managers.construction.jobs[].work_point_owners` (builder tokens, -1 free); `managers.beer.served`; `building.queue.tail` and `building.queue.points`. `game::WriteOblSave`
  adds them from the live objects by id; the load writes each present one into the objects right after
  World_Load (`QueueMemorySave`'s after-load step). A converted old save has none, so its objects keep what the load
  leaves. The chaos round trip saves and loads obl.save, so a field missing from this list shows as a reload mismatch.
