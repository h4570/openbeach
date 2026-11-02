#include "game/ui/sprite_bank.h"
#include "game/weather/weather_state.h"
// Test-only observers for the guest desire model (docs/specs/index.md, docs/architecture/data-formats/converter/scripts.md):
// Guest::UpdateDesires is checked against the formula written down in the desire tests on every call for
// every guest, GuestApplyWeatherModifiers against "Desire[i] += Weather::m_desireModifiers[i]".

#include "guest_desires.h"
#include "game/objects/script_text.h"
#include "game/objects/building_queue_ops.h"
#include "game/objects/building_queue_state.h"
#include "game/objects/building_methods.h"
#include "game/objects/building_api.h"
#include "game/objects/building_queue.h"
#include "game/objects/object_registry_api.h"
#include "game/weather/weather.h"

#include "game/map/grid_state.h"
#include "game/objects/guest.h"
#include "game/ui/map_view_math.h"
#include "game/map/grid_math.h"
#include "game/objects/guest_lookups.h"
#include "game/objects/guest_walk_routes.h"
#include "game/objects/building_events_state.h"
#include "game/map/grid_cell.h"
#include "game/messages/message_state.h"
#include "game/objects/object_moving.h"
#include "game/objects/object_guest.h"
#include "game/tables/world_params_leaves.h"
#include "game/ui/sprite.h"
#include "game/objects/object_building.h"
#include "game/objects/object_scenery.h"
#include "core/log.h"
#include "core/call_counter.h"
#include "determinism.h"
#include "game/world/world_state.h"
#include "game/tables/world_params_tables.h"
#include "test_util.h"
#include "game/messages/message_handler.h"
#include "game/objects/commodity_providers.h"
#include "game/world/world_params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

namespace obl::test {
namespace {
constexpr int kDesireCount = 26;
constexpr int kDesireVandalise = 18, kDesireFight = 19;

using object::Building;
using object::Guest;
using object::Scenery;
using tables::BuildingDef;
using tables::GuestDef;
using tables::SceneryDef;
using enum tables::GuestOpinion;
using enum messages::MessageType;

using GuestMethod = void (*)(object::Guest*);
GuestMethod g_updateDesires, g_updateOpinions;
GuestMethod g_alterQueue, g_alterSafety, g_updatePriceScore, g_alterPrice, g_updatePurchase, g_doSpot;
void (*g_applyWeather)(object::Guest*);

// The WorldParams definition record behind an object's m_definition. Tests lend values in it, hence the mutable view.
GuestDef* Def(const object::Person* object)
{
    return const_cast<GuestDef*>(object->m_definition);
}
SceneryDef* Def(const Scenery* object)
{
    return const_cast<SceneryDef*>(object->m_definition);
}
BuildingDef* Def(const Building* object)
{
    return const_cast<BuildingDef*>(object->m_definition);
}

// Desire index -> index of the guest growth-rate field in m_person (filled in the guest constructor from the *PREF pairs).
constexpr std::array<std::pair<int, int>, 12> kGrowth{{
        {1, 0},
        {2, 1},
        {5, 2},
        {6, 3},
        {8, 4},
        {9, 5},
        {10, 6},
        {11, 7},
        {12, 8},
        {13, 9},
        {15, 10},
        {16, 11},
}};
// Desire index -> guest definition decay field (DRUNKDECAY is separate: it lowers Drunk, not a desire).
constexpr std::array<std::pair<int, float GuestDef::*>, 3> kDecay{
        {{20, &GuestDef::m_FlirtDecay}, {19, &GuestDef::m_FightDecay}, {18, &GuestDef::m_VandaliseDecay}}};

struct Stats {
    unsigned calls = 0, mismatches = 0, atCap = 0;
    float maxError = 0;
    std::string firstBad;
} g_update;

// Guest::UpdateOpinions: Happiness is the mean of the seven opinions, and the beauty
// and cleanliness opinions move by the definition steps when the resort value (the world's beauty / litter total)
// leaves the guest's tolerance band.
struct OpinionStats {
    unsigned calls = 0, skipped = 0, happinessBad = 0, beautyChecked = 0, beautyBad = 0, cleanChecked = 0, cleanBad = 0;
    float maxHappinessError = 0;
} g_opinions;

struct WeatherStats {
    unsigned guests = 0, mismatches = 0;
    float maxDelta[kDesireCount]{};
} g_weather;

bool Near(float a, float b, float& error)
{
    error = std::fabs(a - b);
    return error <= 1e-5f;
}

void UpdateDesires(object::Guest* guest)
{
    world_data::World* world = World();
    float dt = world->m_gameDeltaTime;
    auto def = Def(guest);
    std::array<float, kDesireCount> expected;
    std::memcpy(expected.data(), guest->m_desires, sizeof(expected));
    float drunk = guest->m_drunk;
    float foodIll = guest->m_foodIll;
    bool illFlag = guest->m_imIll != 0;

    drunk -= dt * def->m_DrunkDecay;
    for (auto [index, decay] : kDecay) expected[index] -= dt * def->*decay;
    drunk = std::max(drunk, 0.0f);
    for (auto [index, decay] : kDecay) expected[index] = std::max(expected[index], 0.0f);
    for (auto [index, person] : kGrowth) expected[index] += dt * guest->m_person[person];
    for (auto& value : expected) value = std::min(value, 1.0f);
    bool expectedFlag = illFlag;
    if (!illFlag) {
        if (foodIll > 0.99f) {
            expectedFlag = true;
            foodIll = 1.0f;
        } else {
            foodIll = std::max(foodIll - dt * def->m_IllDecay, 0.0f);
        }
    }

    // Check the update against the formula computed before this observer calls it.
    g_updateDesires(guest);

    ++g_update.calls;
    float worst = 0, error = 0;
    auto note = [&](float want, float got) {
        if (!Near(want, got, error)) worst = std::max(worst, error);
    };
    for (int i = 0; i < kDesireCount; ++i) note(expected[i], guest->m_desires[i]);
    note(drunk, guest->m_drunk);
    note(foodIll, guest->m_foodIll);
    bool flagOk = (guest->m_imIll != 0) == expectedFlag;
    for (float value : expected) g_update.atCap += value >= 1.0f;
    if (worst > 0 || !flagOk) {
        ++g_update.mismatches;
        g_update.maxError = std::max(g_update.maxError, worst);
        if (g_update.firstBad.empty()) {
            std::ostringstream out;
            out << "first bad: guest=" << guest->m_id << " dt=" << dt << " error=" << worst << " flagOk=" << flagOk;
            g_update.firstBad = out.str();
        }
    }
}

// Expected opinion after one normal-branch update (the guest's variety opinion is non-zero).
float BandStep(float opinion, float value, float lowLimit, float highLimit, float up, float down)
{
    if (value <= highLimit) {
        if (value < lowLimit) opinion = std::max(opinion - down, 0.0f);
        return opinion;
    }
    return std::min(opinion + up, 1.0f);
}

// Cleanliness is driven by the world's litter total (a litter count, so low is good): the directions are the other way round.
float LitterStep(float opinion, float litter, float lowLimit, float highLimit, float up, float down)
{
    if (litter <= highLimit) {
        if (litter < lowLimit) opinion = std::min(opinion + up, 1.0f);
        return opinion;
    }
    return std::max(opinion - down, 0.0f);
}

void UpdateOpinions(object::Guest* guest)
{
    world_data::World* world = World();
    auto def = Def(guest);
    int state = guest->m_state;
    bool active = state != 0x28 && state != 0x29 && state != 0x26 && state != 0x25;
    bool normal = active && guest->m_opinion[4] >= 1e-6f;
    float beautyBefore = guest->m_opinion[0], cleanBefore = guest->m_opinion[5];
    float beauty = world->m_scenicVal, clean = static_cast<float>(world->m_numLitter);
    float expectedBeauty = BandStep(beautyBefore, beauty, guest->m_Tolerance[0], guest->m_Tolerance[1], def->m_OpinionEffect[Beauty][0],
            def->m_OpinionEffect[Beauty][1]);
    float expectedClean = LitterStep(cleanBefore, clean, guest->m_Tolerance[10], guest->m_Tolerance[11],
            def->m_OpinionEffect[Cleanliness][0], def->m_OpinionEffect[Cleanliness][1]);
    g_updateOpinions(guest);
    ++g_opinions.calls;
    if (!active) {
        ++g_opinions.skipped;
        return;
    }
    float sum = 0;
    for (int i = 0; i < 7; ++i) sum += guest->m_opinion[i];
    float error = std::fabs(guest->m_happiness - sum * 0.14285715f);
    g_opinions.maxHappinessError = std::max(g_opinions.maxHappinessError, error);
    g_opinions.happinessBad += error > 1e-5f;
    if (normal) {
        ++g_opinions.beautyChecked;
        g_opinions.beautyBad += std::fabs(guest->m_opinion[0] - expectedBeauty) > 1e-5f;
        ++g_opinions.cleanChecked;
        g_opinions.cleanBad += std::fabs(guest->m_opinion[5] - expectedClean) > 1e-5f;
    }
}

// The per-visit opinion helpers (docs/specs/staff-duties.md): each one works on the guest's current building
// (the guest's current building, object type 2) and moves the opinion *and* the running visit score. The checks recompute that
// from the documented rules and compare it with what the game leaves behind.
struct HelperStats {
    unsigned calls = 0, skipped = 0, bad = 0;
    unsigned branch[3]{};
    std::string firstBad;
};
HelperStats g_queue, g_safety, g_price, g_expensive;
// Off unless a script asks (`opinion-helpers on`, or for the duration of `opinion-helpers-force`): every script run would
// otherwise pay for these checks and flood the log with `helper mismatch` lines of tests that never read them.
bool g_helperChecks = false;

Building* BuildingOf(const object::Guest* guest)
{
    int id = guest->m_DestObjectID;
    if (id == -1) return nullptr;
    object::Manager* manager = game::GetObjectManager();
    object::Base* object = object::object_manager::ObjectById(manager, id);
    return object && object->m_type == object::ObjectType::Building ? static_cast<Building*>(object) : nullptr;
}

float Clamp01(float value)
{
    return std::min(std::max(value, 0.0f), 1.0f);
}

bool Close(float a, float b)
{
    return std::fabs(a - b) <= 1e-5f;
}

void Expect(HelperStats& stats, const char* what, bool ok, const object::Guest* guest)
{
    if (ok) return;
    ++stats.bad;
    if (!guest) { // helpers that take no guest (GuestFindSwimPoint)
        OBL_LOG("autotest: helper mismatch (%s)", what);
        if (stats.firstBad.empty()) stats.firstBad = std::string("first bad (") + what + ")";
        return;
    }
    OBL_LOG("autotest: helper mismatch (%s): guest=%d building=%d actionState=%d missed=%d crowding=%g queueScore=%g "
            "cur=%g safety=%g",
            what, guest->m_id, guest->m_DestObjectID, guest->m_ActionState, guest->m_missedPurchase, guest->m_opinion[1],
            guest->m_queueScore, guest->m_curScore, guest->m_safetyScore);
    if (stats.firstBad.empty()) {
        std::ostringstream out;
        out << "first bad (" << what << "): guest=" << guest->m_id << " building=" << guest->m_DestObjectID;
        stats.firstBad = out.str();
    }
}

// The long-queue and bad-safety branches end with a STATE/0xc message the guest posts to itself, and the message is delivered at once, so the
// guest's script (UpdatePurchaseOpinion etc.) rewrites the visit score, missed flag and opinions before the helper returns. The
// penalty fields are therefore sampled inside the post; `swallow` keeps the forced runs from delivering the message at all.
struct PostCapture {
    object::Guest* guest = nullptr;
    bool seen = false, swallow = false;
    int type = 0;
    int actionState = 0;
    uint8_t missed = 0;
    float crowding = 0, safetyOpinion = 0, cur = 0;
    // every message posted while `guest` is set as {type, flags, data}
    int count = 0;
    int sent[4][3]{};
    // the first 64 messages in full: {sender, receiver, type, flags, data}
    int logged = 0;
    int log[64][5]{};
} g_post;

// The probe is the message_handler seam for Post (called before the delivery; true swallows the message).
bool PostMessage(messages::Handler*, messages::Message* message)
{
    if (g_post.guest) {
        if (g_post.count < 4) {
            int* sent = g_post.sent[g_post.count];
            sent[0] = message->type;
            sent[1] = message->flags;
            sent[2] = message->data;
        }
        if (g_post.logged < 64) {
            int* logged = g_post.log[g_post.logged];
            logged[0] = message->sender;
            logged[1] = message->receiver;
            logged[2] = message->type;
            logged[3] = message->flags;
            logged[4] = message->data;
        }
        ++g_post.logged;
        ++g_post.count;
        // Keep the helper's penalty before its state change is delivered; cascaded scripts can reset the visit and post again.
        if (!g_post.seen && message->type == messages::MT_STATECHANGE && message->receiver == static_cast<int32_t>(g_post.guest->m_id)) {
            g_post.seen = true;
            g_post.type = message->data;
            g_post.actionState = g_post.guest->m_ActionState;
            g_post.missed = g_post.guest->m_missedPurchase;
            g_post.crowding = g_post.guest->m_opinion[1];
            g_post.safetyOpinion = g_post.guest->m_opinion[6];
            g_post.cur = g_post.guest->m_curScore;
        }
        if (g_post.swallow) return true;
    }
    return false;
}

// Guest::AlterQueueOpinion: queue length = size of the building's guest queue, or the truncated queue estimate for a building whose
// definition has no queue (always "short"). Short: QueueScore += CROWDING_EFFECT[0]; between the limits: only
// ActionState = 1; long: crowding opinion -= CROWDING_EFFECT[1], visit score -= CROWDING_EFFECT[1] + VARIETY_EFFECT[1],
// missed-purchase flag.
void AlterQueueOpinion(object::Guest* guest)
{
    if (!g_helperChecks) return g_alterQueue(guest);
    Building* building = BuildingOf(guest);
    auto def = Def(guest);
    if (!building) {
        ++g_queue.skipped;
        g_alterQueue(guest);
        return;
    }
    float low = guest->m_Tolerance[2], high = guest->m_Tolerance[3];
    float length = Def(building)->m_MaxQueue == 0
            ? static_cast<float>(static_cast<int>(low))
            : static_cast<float>(static_cast<const object::BuildingQueue*>(building->m_queue)->m_qlQueue.size());
    float crowding = guest->m_opinion[1], queueScore = guest->m_queueScore, cur = guest->m_curScore;
    int branch = length < low ? 0 : (length > high ? 2 : 1);
    g_post.guest = guest;
    g_post.seen = false;
    g_alterQueue(guest);
    g_post.guest = nullptr;
    ++g_queue.calls;
    ++g_queue.branch[branch];
    float effectUp = def->m_OpinionEffect[Crowding][0], effectDown = def->m_OpinionEffect[Crowding][1],
          variety = def->m_OpinionEffect[Variety][1];
    if (branch == 2) {
        Expect(g_queue, "long: message", g_post.seen && g_post.type == 0xc, guest);
        Expect(g_queue, "long: action state", g_post.actionState == 0, guest);
        Expect(g_queue, "long: missed flag", g_post.missed == 1, guest);
        Expect(g_queue, "long: crowding", Close(g_post.crowding, crowding - effectDown), guest);
        Expect(g_queue, "long: visit score", Close(g_post.cur, cur - effectDown - variety), guest);
    } else {
        Expect(g_queue, "state", guest->m_ActionState == 1, guest);
        Expect(g_queue, "queue score", Close(guest->m_queueScore, branch == 0 ? queueScore + effectUp : queueScore), guest);
        Expect(g_queue, "crowding", Close(guest->m_opinion[1], Clamp01(crowding)), guest);
    }
}

// Guest::AlterSafetyOpinion: quality = (3 - RepairState) * 0.33 against the guest's safety band. At or above
// the lower limit: ActionState = 1 and, above the upper one, SafetyScore = SAFETY_EFFECT[0]; below it: SAFETY_EFFECT[1] is
// taken from the opinion and the visit score, missed-purchase flag, and a guest staying in this building leaves early.
void AlterSafetyOpinion(object::Guest* guest)
{
    if (!g_helperChecks) return g_alterSafety(guest);
    Building* building = BuildingOf(guest);
    auto def = Def(guest);
    if (!building) {
        ++g_safety.skipped;
        g_alterSafety(guest);
        return;
    }
    double quality = (3 - building->m_RepairState) * 0.33;
    float low = guest->m_Tolerance[12], high = guest->m_Tolerance[13];
    float opinion = guest->m_opinion[6], cur = guest->m_curScore, score = guest->m_safetyScore;
    bool staying = guest->m_StayingAt == guest->m_DestObjectID;
    int branch = quality < low ? 2 : (quality > high ? 0 : 1);
    g_post.guest = guest;
    g_post.seen = false;
    g_alterSafety(guest);
    g_post.guest = nullptr;
    ++g_safety.calls;
    ++g_safety.branch[branch];
    float effectUp = def->m_OpinionEffect[Safety][0], effectDown = def->m_OpinionEffect[Safety][1];
    if (branch == 2) {
        Expect(g_safety, "bad: message", g_post.seen && g_post.type == (staying ? 0x29 : 0xc), guest);
        Expect(g_safety, "bad: action state", g_post.actionState == 0, guest);
        Expect(g_safety, "bad: missed flag", g_post.missed == 1, guest);
        Expect(g_safety, "bad: opinion", Close(g_post.safetyOpinion, opinion - effectDown), guest);
        Expect(g_safety, "bad: visit score", Close(g_post.cur, cur - effectDown), guest);
        Expect(g_safety, "bad: leaving early", !staying || guest->m_leavingEarly == 1, guest);
    } else {
        Expect(g_safety, "state", guest->m_ActionState == 1, guest);
        Expect(g_safety, "safety score", Close(guest->m_safetyScore, branch == 0 ? effectUp : score), guest);
        Expect(g_safety, "opinion", Close(guest->m_opinion[6], Clamp01(opinion)), guest);
    }
}

// Price ratio used by both price helpers: numerator over the building's reference price for the slot (numerator + 1 when the
// reference is not positive). Guest::UpdatePriceScore uses the building's own price, Guest::AlterPriceOpinion the guest's
// purchase record. The reference is stored as a float and the ratio stays a double, as in the game: a float ratio rounds
// a price that sits exactly on a tolerance boundary (e.g. 0.8 * reference) to the tolerance and flips the comparison.
double PriceRatio(Building* building, float numerator, int slot)
{
    float reference = static_cast<float>(object::commodity_providers::SlotReferencePrice(building, slot));
    return reference > 0.0f ? static_cast<double>(numerator) / reference : static_cast<double>(numerator) + 1.0;
}

// Guest::UpdatePriceScore ("cheap"): ratio below the guest's lower price tolerance -> PriceScore = PRICE_EFFECT[0]
// and the price opinion rises by it.
void UpdatePriceScore(object::Guest* guest)
{
    if (!g_helperChecks) return g_updatePriceScore(guest);
    Building* building = BuildingOf(guest);
    auto def = Def(guest);
    if (!building) {
        ++g_price.skipped;
        g_updatePriceScore(guest);
        return;
    }
    int slot = guest->m_desired_commod;
    float price = static_cast<float>(object::commodity_providers::SlotPrice(building, slot));
    bool cheap = PriceRatio(building, price, slot) < guest->m_Tolerance[6];
    float opinion = guest->m_opinion[3], score = guest->m_priceScore;
    g_updatePriceScore(guest);
    ++g_price.calls;
    ++g_price.branch[cheap];
    float effect = def->m_OpinionEffect[Price][0];
    Expect(g_price, "price score", Close(guest->m_priceScore, cheap ? effect : score), guest);
    Expect(g_price, "price opinion", Close(guest->m_opinion[3], cheap ? std::min(opinion + effect, 1.0f) : opinion), guest);
}

// Guest::AlterPriceOpinion ("expensive"): ratio above the upper tolerance -> opinion and visit score -=
// PRICE_EFFECT[1], missed-purchase flag.
void AlterPriceOpinion(object::Guest* guest)
{
    if (!g_helperChecks) return g_alterPrice(guest);
    Building* building = BuildingOf(guest);
    auto def = Def(guest);
    auto* record = static_cast<messages::Message*>(guest->m_currentMessage);
    if (!building || !record) {
        ++g_expensive.skipped;
        g_alterPrice(guest);
        return;
    }
    bool dear = PriceRatio(building, record->floatData, guest->m_desired_commod) > guest->m_Tolerance[7];
    float opinion = guest->m_opinion[3], cur = guest->m_curScore;
    g_alterPrice(guest);
    ++g_expensive.calls;
    ++g_expensive.branch[dear];
    float effect = def->m_OpinionEffect[Price][1];
    Expect(g_expensive, "opinion", Close(guest->m_opinion[3], dear ? Clamp01(opinion - effect) : opinion), guest);
    Expect(g_expensive, "visit score", Close(guest->m_curScore, dear ? cur - effect : cur), guest);
    Expect(g_expensive, "missed flag", !dear || guest->m_missedPurchase == 1, guest);
}

// Per-visit bookkeeping shared by Guest::UpdatePurchaseOpinion and Guest::ReactToSpottedEvent: the seven opinions (beauty,
// crowding, "totty", price, variety, cleanliness, safety), the "greatest" visit record (flag, building or event kind,
// commodity, score) and the "smallest" one.
struct Visit {
    float op[7];
    uint8_t flagHigh, flagLow;
    int idHigh, idLow, commodityHigh, commodityLow;
    float scoreHigh, scoreLow;
    float cur, clean, queue, price, safety;
    uint8_t missed;
};

Visit ReadVisit(const object::Guest* guest)
{
    Visit v{};
    std::memcpy(v.op, guest->m_opinion, sizeof(v.op));
    v.flagHigh = guest->m_BestSpotOpinion;
    v.idHigh = guest->m_Best;
    v.commodityHigh = guest->m_BestType;
    v.scoreHigh = guest->m_bestScore;
    v.flagLow = guest->m_WorstSpotOpinion;
    v.idLow = guest->m_Worst;
    v.commodityLow = guest->m_WorstType;
    v.scoreLow = guest->m_worstScore;
    v.cur = guest->m_curScore;
    v.price = guest->m_priceScore;
    v.queue = guest->m_queueScore;
    v.safety = guest->m_safetyScore;
    v.clean = guest->m_cleanScore;
    v.missed = guest->m_missedPurchase;
    return v;
}

bool SameVisit(const Visit& a, const Visit& b)
{
    for (int i = 0; i < 7; ++i)
        if (!Close(a.op[i], b.op[i])) return false;
    return a.flagHigh == b.flagHigh && a.flagLow == b.flagLow && a.idHigh == b.idHigh && a.idLow == b.idLow &&
            a.commodityHigh == b.commodityHigh && a.commodityLow == b.commodityLow && Close(a.scoreHigh, b.scoreHigh) &&
            Close(a.scoreLow, b.scoreLow);
}

HelperStats g_purchase, g_spot;
unsigned g_spotKind[12]; // index 11 = kind outside 0..10, or an ignored event

// Guest::UpdatePurchaseOpinion (end of a visit): a missed purchase (the flag set by the penalty helpers) only clears the flag and
// the four score components. Otherwise the visit score becomes Clean + Queue + Price + Safety, the queue and safety components are
// added to the crowding / safety opinions (upper clamp only) and the components are cleared. Either way the score is then filed
// into the "greatest" record when it beats it, else into the "smallest" one when it is below it (both skipped when the no-record flag is set),
// and the score is reset to 0.
void UpdatePurchaseOpinion(object::Guest* guest)
{
    if (!g_helperChecks) return g_updatePurchase(guest);
    Visit before = ReadVisit(guest);
    bool skipRecords = guest->m_activity != 0;
    int id = guest->m_CurID, commodity = guest->m_CurTypeID;
    g_updatePurchase(guest);
    Visit after = ReadVisit(guest);
    Visit want = before;
    float cur = before.cur;
    int branch = 0;
    if (!before.missed) {
        cur = before.clean + before.queue + before.price + before.safety;
        want.op[1] = std::min(before.op[1] + before.queue, 1.0f);
        want.op[6] = std::min(before.op[6] + before.safety, 1.0f);
        branch = before.clean < 0 ? 2 : 1;
    }
    if (!skipRecords) {
        if (before.scoreHigh < cur) {
            want.flagHigh = 0, want.idHigh = id, want.commodityHigh = commodity, want.scoreHigh = cur;
        } else if (before.scoreLow > cur) {
            want.flagLow = 0, want.idLow = id, want.commodityLow = commodity, want.scoreLow = cur;
        }
    }
    ++g_purchase.calls;
    ++g_purchase.branch[branch];
    Expect(g_purchase, "records/opinions", SameVisit(after, want), guest);
    Expect(g_purchase, "flag and components cleared",
            !after.missed && after.price == 0 && after.queue == 0 && after.safety == 0 && after.clean == 0, guest);
    Expect(g_purchase, "score reset", after.cur == 0, guest);
}

// Guest::ReactToSpottedEvent: reaction to an event the guest saw; the event record holds the event kind. Deltas come from the guest
// definition: CLEANLINESS_EFFECT[1], BEAUTY_EFFECT[1], SAFETY_EFFECT[1], CROWDING_EFFECT[1] and the
// "totty" gain. Kinds: 0 stench, 1 vomit, 2 vandalism, 3 fight, 4 topless, 5 wet T-shirt, 6 queue closed, 7 queue broken,
// 8 eaten, 9 run from shark, 10 drowned. Bad events (all but 4/5) compete for the "smallest" record and set the sad icon
// (the opinion slot set to 1); 4/5 use the "greatest" one with the happy icon. All opinions are clamped to 0..1 at the end. Quirks
// the check keeps: kind 1 stores +CLEANLINESS_EFFECT[1] (not negated) as the record score, and kinds 8..10 compare
// 2 * SAFETY_EFFECT[1] but store 2 * CROWDING_EFFECT[1].
void ReactToSpottedEvent(object::Guest* guest)
{
    if (!g_helperChecks) return g_doSpot(guest);
    auto* record = static_cast<messages::Message*>(guest->m_currentMessage);
    auto def = Def(guest);
    if (!record) {
        ++g_spot.skipped;
        g_doSpot(guest);
        return;
    }
    Visit before = ReadVisit(guest);
    unsigned kind = static_cast<unsigned>(record->data);
    int state = guest->m_state;
    bool ignored = (kind == 5 && def->m_Gender != 0) || ((state == 0x16 || state == 0x36 || state == 0x37 || state == 0x17) && kind < 8);
    g_post.guest = guest;
    g_post.seen = false;
    g_doSpot(guest);
    g_post.guest = nullptr;
    Visit after = ReadVisit(guest);
    ++g_spot.calls;
    ++g_spotKind[ignored || kind > 10 ? 11 : kind];
    if (ignored) {
        Expect(g_spot, "ignored event", SameVisit(after, before), guest);
        return;
    }
    float clean = def->m_OpinionEffect[Cleanliness][1], beauty = def->m_OpinionEffect[Beauty][1], safety = def->m_OpinionEffect[Safety][1];
    float crowd = def->m_OpinionEffect[Crowding][1], totty = def->m_OpinionEffect[Totty][0];
    Visit want = before;
    auto low = [&](float candidate, float stored) {
        if (before.scoreLow > candidate) want.flagLow = 1, want.idLow = kind, want.scoreLow = stored;
    };
    auto high = [&](float candidate) {
        if (before.scoreHigh < candidate) want.flagHigh = 1, want.idHigh = kind, want.scoreHigh = candidate;
    };
    bool sad = true;
    switch (kind) {
    case 0:
        want.op[5] -= clean;
        low(-clean, -clean);
        break;
    case 1:
        want.op[5] -= clean;
        low(-clean, clean);
        break;
    case 2:
        want.op[5] -= clean, want.op[0] -= beauty, want.op[6] -= safety;
        low((-clean - beauty) - safety, (-clean - beauty) - safety);
        break;
    case 3:
        want.op[6] -= safety;
        low(-safety, -safety);
        break;
    case 4:
    case 5:
        want.op[2] += totty;
        high(totty);
        sad = false;
        break;
    case 6:
    case 7:
        want.op[1] -= crowd;
        low(-crowd, -crowd);
        break;
    case 8:
    case 9:
    case 10:
        want.op[6] -= safety + safety;
        low(-(safety + safety), -(crowd + crowd));
        break;
    default: break;
    }
    for (float& value : want.op) value = Clamp01(value);
    if (!SameVisit(after, want))
        OBL_LOG("autotest: spot kind %u: high %d/%u/%g want %d/%u/%g, low %d/%u/%g want %d/%u/%g, opinions %g %g %g %g %g %g %g "
                "want %g %g %g %g %g %g %g",
                kind, after.flagHigh, after.idHigh, after.scoreHigh, want.flagHigh, want.idHigh, want.scoreHigh, after.flagLow, after.idLow,
                after.scoreLow, want.flagLow, want.idLow, want.scoreLow, after.op[0], after.op[1], after.op[2], after.op[3], after.op[4],
                after.op[5], after.op[6], want.op[0], want.op[1], want.op[2], want.op[3], want.op[4], want.op[5], want.op[6]);
    Expect(g_spot, "opinions/records", SameVisit(after, want), guest);
    if (kind <= 10) {
        const auto* icon = guest->m_moodIcon;
        Expect(g_spot, "icon and timer", icon && icon->m_frameIndex == (sad ? 1 : 0) && guest->m_moodIconTime == 2.0f, guest);
    }
    if (kind == 6 || kind == 7) Expect(g_spot, "message", g_post.seen && g_post.type == 0xc, guest);
}

// Guest::CheckBedTime (script-driven, hourly): WakeTime and BedTime against the world hour
// and day number, taken literally from the documented rules. When the hour is judged "night" and nights remain
//, one night is used up; the last one draws TimeToLeave = WakeTime + 1 + 2 * rand() / 32767. A guest staying
// in a building is sent to it (state message 0xd, then a class-9 "go to" message with the building id) and, otherwise,
// leaves (state message 0x29, marked as leaving early).
using Query = bool (*)(object::Guest*);
Query g_checkBedTime, g_checkLeave;
HelperStats g_bed, g_leave;

bool BedTimeProceeds(float wake, float bed, float hour, bool firstDay)
{
    if (!(wake <= bed || hour <= bed || wake <= hour || firstDay)) return true;
    if (bed <= wake) return false;
    if (hour <= bed) {
        if (hour >= wake) return false;
        if (firstDay) return false;
    }
    return true;
}

bool Posted(int cls, int arg1, int param)
{
    for (int i = 0; i < std::min(g_post.count, 4); ++i)
        if (g_post.sent[i][0] == cls && g_post.sent[i][1] == arg1 && g_post.sent[i][2] == param) return true;
    return false;
}

bool CheckBedTime(object::Guest* guest)
{
    if (!g_helperChecks) return g_checkBedTime(guest);
    world_data::World* world = World();
    float wake = guest->m_WakeTime, bed = guest->m_BedTime, hour = world->m_gameDayTime;
    bool firstDay = world->m_gameDay == 0;
    int nights = guest->m_NightsToStay, hotel = guest->m_StayingAt;
    bool proceeds = BedTimeProceeds(wake, bed, hour, firstDay) && nights > 0;
    g_post.guest = guest;
    g_post.count = 0;
    bool result = g_checkBedTime(guest);
    g_post.guest = nullptr;
    ++g_bed.calls;
    ++g_bed.branch[!proceeds ? 0 : (nights > 1 ? 1 : 2)];
    if (!proceeds) {
        Expect(g_bed, "idle: nights", guest->m_NightsToStay == nights, guest);
        Expect(g_bed, "idle: no message", g_post.count == 0, guest);
        return result;
    }
    Expect(g_bed, "night used", guest->m_NightsToStay == nights - 1, guest);
    if (nights == 1) {
        float leave = guest->m_TimeToLeave;
        Expect(g_bed, "time to leave", leave >= wake + 1.0f - 1e-4f && leave <= wake + 3.0f + 1e-4f, guest);
    }
    if (hotel != -1) {
        Expect(g_bed, "hotel: go to sleep", Posted(MT_STATECHANGE, 0, 0xd) && Posted(MT_PAVEMENT2OBJECT, 1, hotel), guest);
        Expect(g_bed, "hotel: destination", guest->m_CurID == hotel && guest->m_desired_commod == 0, guest);
    } else {
        Expect(g_bed, "no hotel: leaves", Posted(MT_STATECHANGE, 0, 0x29), guest);
        Expect(g_bed, "no hotel: leaving early", guest->m_leavingEarly == 1, guest);
    }
    return result;
}

// Guest::CheckLeaveResort (script-driven decision to go home): Happiness <= 0.25 -> leave at once.
// Otherwise the souvenir flag decides: set -> leave; clear -> when the nights are used up and TimeToLeave
// has passed, set the flag and raise Desire[Souvenirs] to 1. Messages are delivered at once, so the script may
// post more of its own while the decision is still running: the checks only require the expected message to be among them.
bool CheckLeaveResort(object::Guest* guest)
{
    if (!g_helperChecks) return g_checkLeave(guest);
    world_data::World* world = World();
    float hour = world->m_gameDayTime, happiness = guest->m_happiness;
    bool unhappy = happiness <= 0.25f, souvenirs = guest->m_souvenirsDecided != 0;
    bool due = guest->m_NightsToStay == 0 && guest->m_TimeToLeave < hour;
    float desire = guest->m_desires[10];
    g_post.guest = guest;
    g_post.count = 0;
    bool result = g_checkLeave(guest);
    g_post.guest = nullptr;
    ++g_leave.calls;
    ++g_leave.branch[unhappy ? 0 : (souvenirs ? 1 : 2)];
    bool leaves = unhappy || souvenirs;
    Expect(g_leave, "message", leaves ? Posted(MT_STATECHANGE, 0, 0x29) : g_post.count == 0, guest);
    Expect(g_leave, "result", (result & 0xff) == (leaves ? 1u : 0u), guest);
    if (unhappy) Expect(g_leave, "leaving early", guest->m_leavingEarly == 1, guest);
    if (!leaves) {
        Expect(g_leave, "souvenir flag", guest->m_souvenirsDecided == (due ? 1 : 0), guest);
        Expect(g_leave, "souvenir desire", Close(guest->m_desires[10], due ? 1.0f : desire), guest);
    }
    return result;
}

void ReportHelper(const char* name, HelperStats& stats, const char* branches, bool required = true)
{
    bool ok = (stats.calls || !required) && !stats.bad;
    OBL_LOG("autotest: %s calls=%u skipped=%u bad=%u branches(%s)=%u/%u/%u %s", name, stats.calls, stats.skipped, stats.bad, branches,
            stats.branch[0], stats.branch[1], stats.branch[2], stats.firstBad.c_str());
    OBL_LOG("autotest: %s %s", ok ? "PASS" : "FAILED", name);
    stats = {};
}

void ApplyWeather(object::Guest* guest)
{
    const weather::Weather* weather = game::GetWeather();
    std::array<float, kDesireCount> before;
    std::memcpy(before.data(), guest->m_desires, sizeof(before));
    g_applyWeather(guest);
    ++g_weather.guests;
    bool ok = true;
    for (int i = 0; i < kDesireCount; ++i) {
        float modifier = weather->m_desireModifiers[i], error = 0;
        float actual = guest->m_desires[i];
        if (!Near(actual, before[i] + modifier, error)) ok = false;
        g_weather.maxDelta[i] = std::max(g_weather.maxDelta[i], actual - before[i]);
    }
    if (!ok) ++g_weather.mismatches;
}

// Non-zero entries of a desire modifier table (the active weather or a schedule entry).
std::string Modifiers(const float (&modifiers)[kDesireCount])
{
    std::ostringstream out;
    for (int i = 0; i < kDesireCount; ++i)
        if (modifiers[i] != 0) out << " [" << i << "]=" << modifiers[i];
    return out.str();
}

// One log line per schedule activation (weather::runtime::UpdateSchedule applies every due entry to every guest).
void UpdateSchedule(weather::Weather* weather)
{
    int nextBefore = weather->m_nextEvent;
    uint32_t kindBefore = weather->m_currentState;
    g_weather = {};
    weather::runtime::RunSchedule(weather);
    if (weather->m_nextEvent == nextBefore) return;
    OBL_LOG("autotest: weather activated kind %u->%u entry=%d/%d guests=%u mismatches=%u modifiers:%s "
            "max-softdrinks-delta=%.4f",
            kindBefore, weather->m_currentState, weather->m_nextEvent, weather->m_eventCount, g_weather.guests, g_weather.mismatches,
            Modifiers(weather->m_desireModifiers).c_str(), g_weather.maxDelta[9]);
    // A schedule entry that fires during the scenario's startup finds no guests: nothing to compare, not a failure.
    OBL_LOG("autotest: %s weather-modifiers",
            !g_weather.guests && !g_weather.mismatches ? "SKIP"
                    : !g_weather.mismatches            ? "PASS"
                                                       : "FAILED");
}

object::Guest* GuestAt(int index)
{
    object::Manager* manager = game::GetObjectManager();
    if (!manager || index < 0 || index >= static_cast<int>(manager->m_guests.size())) return nullptr;
    return static_cast<object::Guest*>(manager->m_guests[index]);
}

// Natural play rarely reaches the penalty branches (no long queues, no broken buildings, prices near the reference), so each
// helper is also called on a scratch guest whose building and tolerance bands are overridden to force every branch.
// The guest is restored from a full snapshot afterwards; the wrappers above do the checking.
constexpr size_t kGuestSize = sizeof(Guest);

Building* FindBuilding(bool withQueue)
{
    object::Manager* manager = game::GetObjectManager();
    for (int i = 0; manager && i < static_cast<int>(manager->m_buildings.size()); ++i) {
        auto building = static_cast<Building*>(manager->m_buildings[i]);
        if ((Def(building)->m_MaxQueue != 0) == withQueue) return building;
    }
    return nullptr;
}

template<class Callback> void Forced(object::Guest* guest, const std::array<std::pair<float*, float>, 2>& tolerance, Callback&& call)
{
    std::array<uint8_t, kGuestSize> snapshot;
    std::copy_n(reinterpret_cast<const uint8_t*>(guest), snapshot.size(), snapshot.begin());
    for (auto [field, value] : tolerance)
        if (field) *field = value;
    g_post.swallow = true;
    call();
    g_post.swallow = false;
    std::copy(snapshot.begin(), snapshot.end(), reinterpret_cast<uint8_t*>(guest));
}

template<class Callback> void Forced(object::Guest* guest, Callback&& call)
{
    Forced(guest, std::array<std::pair<float*, float>, 2>{}, std::forward<Callback>(call));
}

// UpdatePurchaseOpinion / ReactToSpottedEvent get state from the script that natural play rarely covers (missed purchases, every event
// kind, both record branches), so their fields are driven directly on the scratch guest.
void ForcePurchaseAndSpot(object::Guest* guest, Building* queued)
{
    if (!guest->m_moodIcon) {
        OBL_LOG("autotest: FAILED opinion-helpers-force: guest %d has no mood sprite", guest->m_id);
        return;
    }
    int type = world::world_params::GetBuildingId(game::GetWorldParams(), Def(queued)->m_key.c_str());
    const float big = 1e9f;
    int icon = guest->m_moodIcon->m_frameIndex;
    // {greatest, smallest} record scores: update both / update neither
    const float records[2][2] = {{-big, big}, {big, -big}};
    static messages::Message event{};
    struct Case {
        uint8_t missed;
        float cur, clean, queue, price, safety;
    };
    const Case cases[] = {{1, 0.7f, 0, 0, 0, 0}, {1, -0.7f, 0, 0, 0, 0}, {0, 0, 0.3f, 0.2f, 0.1f, 0.2f}, {0, 0, -0.4f, 0.2f, 0, 0},
            {0, 0, 0, -0.1f, -0.2f, -0.3f}, {0, 0, 0.1f, 0.9f, 0, 0.9f}};
    for (const auto& record : records) {
        auto scores = [&] {
            // Fresh guests may not have visited a building yet. Lend a complete visit,
            // including the definition used to construct the purchase thought text.
            guest->m_CurID = queued->m_id;
            guest->m_CurTypeID = type;
            guest->m_desired_commod = 0;
            guest->m_bestScore = record[0];
            guest->m_worstScore = record[1];
        };
        for (const Case& c : cases)
            Forced(guest, [&] {
                scores();
                guest->m_activity = 0;
                guest->m_missedPurchase = c.missed;
                guest->m_curScore = c.cur;
                guest->m_cleanScore = c.clean;
                guest->m_queueScore = c.queue;
                guest->m_priceScore = c.price;
                guest->m_safetyScore = c.safety;
                guest->m_opinion[1] = 0.5f;
                guest->m_opinion[6] = 0.5f;
                UpdatePurchaseOpinion(guest);
            });
        // no-record flag set: the records are left alone
        Forced(guest, [&] {
            scores();
            guest->m_activity = 1;
            guest->m_curScore = 0.4f;
            UpdatePurchaseOpinion(guest);
        });
        // Lend a private definition so both sound tables and wet T-shirt reactions are covered even on an all-female fixture.
        GuestDef definition = *Def(guest);
        for (int gender : {0, 1}) {
            definition.m_Gender = gender;
            for (uint32_t kind = 0; kind <= 11; ++kind)
                for (float start : {0.5f, 0.05f, 0.99f})
                    Forced(guest, [&] {
                        scores();
                        guest->m_definition = &definition;
                        event.data = static_cast<int>(kind);
                        guest->m_currentMessage = &event;
                        guest->m_state = 0;
                        guest->m_ActivityPerson = queued->m_id;
                        for (float& opinion : guest->m_opinion) opinion = start;
                        ReactToSpottedEvent(guest);
                    });
            for (int state : {0x16, 0x17, 0x36, 0x37})
                for (uint32_t kind : {0u, 4u, 5u, 8u})
                    Forced(guest, [&] {
                        scores();
                        guest->m_definition = &definition;
                        event.data = static_cast<int>(kind);
                        guest->m_currentMessage = &event;
                        guest->m_state = state;
                        guest->m_ActivityPerson = queued->m_id;
                        ReactToSpottedEvent(guest);
                    });
        }
    }
    guest->m_moodIcon->m_frameIndex = icon;
}

// The scratch guest is driven through the night/leave decisions with the world hour and day overridden (restored afterwards).
void ForceLifecycle(object::Guest* guest, Building* hotel)
{
    world_data::World* world = World();
    float savedHour = world->m_gameDayTime;
    int savedDay = world->m_gameDay;
    struct Window {
        float wake, bed;
    };
    for (Window w : {Window{7, 23}, Window{9, 1}, Window{8, 8}})
        for (float hour : {0.5f, 3.0f, 7.5f, 12.0f, 22.0f, 23.5f})
            for (int day : {0, 1})
                for (int nights : {0, 1, 2})
                    for (int stay : {0, 1})
                        Forced(guest, [&] {
                            world->m_gameDayTime = hour;
                            world->m_gameDay = day;
                            guest->m_WakeTime = w.wake;
                            guest->m_BedTime = w.bed;
                            guest->m_NightsToStay = nights;
                            guest->m_StayingAt = stay ? hotel->m_id : -1;
                            CheckBedTime(guest);
                        });
    for (float happiness : {0.1f, 0.25f, 0.6f})
        for (int nights : {0, 2})
            for (float leaveAt : {5.0f, 20.0f})
                for (int flag : {0, 1})
                    Forced(guest, [&] {
                        world->m_gameDayTime = 12.0f;
                        guest->m_happiness = happiness;
                        guest->m_NightsToStay = nights;
                        guest->m_TimeToLeave = leaveAt;
                        guest->m_souvenirsDecided = static_cast<uint8_t>(flag);
                        guest->m_desires[10] = 0.3f;
                        CheckLeaveResort(guest);
                    });
    world->m_gameDayTime = savedHour;
    world->m_gameDay = savedDay;
}

// Guest::UpdateDrowning (script: STATE_GUEST_DROWNING, once per game second while the guest is in the water) and
// Guest::DecideSwimAction (STATE_GUEST_SWIMMING, picks the next swim target). Message ids: 53 = MT_DROWNING, 62 = MT_IMDEAD,
// 11 = MT_SWIM2SEAPOINT, 73 = MT_SPOT_OPINION.
//   UpdateDrowning: m_oxygen -= 1; every lifeguard (class 7) gets MT_IMDEAD when it reaches 0; the nearest lifeguard
//   (3-D distance, first wins ties) gets MT_DROWNING with the sea area of the guest as data; at 0 the guest is marked dead
//   (it stops sending drowning calls) and Guest::SendOutDeathMessage runs; a dead guest sends no MT_DROWNING.
//   DecideSwimAction: random point within +-SWIM_DIST of the guest, clamped to x 10..4000, y 10..8000, redrawn until
//   the tile is sea (flag 4) and not blocked (flag 0x10); posts MT_SWIM2SEAPOINT (sender = receiver = guest, data = truncated x, y).
GuestMethod g_updateDrowning, g_decideSwim;
HelperStats g_drown, g_swim;

// The grid of the running world.
map::Grid* GridInstance()
{
    return game::GetGrid();
}

unsigned TileFlags(float x, float y, float z)
{
    const grid_math::TileXY tile = GridInstance()->WorldPointToTile(x, y);
    return GridInstance()->Cell(tile.x, tile.y).m_flags;
}

int SeaAreaAt(float x, float y, float z)
{
    return GridInstance()->GetSeaAreaOfPoint(x, y, z);
}

std::vector<object::Base*> Lifeguards()
{
    object::Manager* manager = game::GetObjectManager();
    auto result = object::object_manager::GetListByClass(manager, 7);
    if (result.size() > 256) result.resize(256);
    return result;
}

void UpdateDrowning(object::Guest* guest)
{
    if (!g_helperChecks) return g_updateDrowning(guest);
    world_data::World* world = World();
    int oxygen = guest->m_oxygen;
    auto guards = Lifeguards();
    int nearest = -2;
    float best = 1e8f;
    for (object::Base* guard : guards) {
        float dx = guard->m_WorldXYZ[0] - guest->m_WorldXYZ[0];
        float dy = guard->m_WorldXYZ[1] - guest->m_WorldXYZ[1];
        float dz = guard->m_WorldXYZ[2] - guest->m_WorldXYZ[2];
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < best) {
            best = distance;
            nearest = guard->m_id;
        }
    }
    int sea = SeaAreaAt(guest->m_WorldXYZ[0], guest->m_WorldXYZ[1], guest->m_WorldXYZ[2]);
    int drowned = world->m_guestsDrowned, eaten = world->m_guestsEaten;
    int state = guest->m_state;
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_updateDrowning(guest);
    g_post.guest = nullptr;
    ++g_drown.calls;
    bool dead = oxygen - 1 == 0;
    ++g_drown.branch[dead ? 2 : (nearest == -2 ? 0 : 1)];
    Expect(g_drown, "oxygen", guest->m_oxygen == oxygen - 1, guest);
    int deadMessages = 0, drowning = 0;
    for (int i = 0; i < std::min(g_post.logged, 64); ++i) {
        const int* m = g_post.log[i];
        if (m[2] == MT_IMDEAD) {
            ++deadMessages;
            Expect(g_drown, "imdead: sender", m[0] == static_cast<int>(guest->m_id), guest);
        } else if (m[2] == MT_DROWNING) {
            ++drowning;
            Expect(g_drown, "drowning: receiver is the nearest lifeguard", m[1] == nearest, guest);
            Expect(g_drown, "drowning: sea area", m[4] == sea && m[3] == 0 && m[0] == static_cast<int>(guest->m_id), guest);
        } else if (m[2] == MT_SPOT_OPINION && dead) {
            Expect(g_drown, "death: spot opinion kind", m[4] == (state == 0x36 ? 8 : 10), guest);
        }
    }
    Expect(g_drown, "imdead count", deadMessages == (dead ? static_cast<int>(guards.size()) : 0), guest);
    Expect(g_drown, "drowning count", drowning == (!dead && nearest != -2 ? 1 : 0), guest);
    if (dead) {
        // Guest::SendOutDeathMessage: one world counter goes up (drowned, or eaten in state 0x36)
        Expect(g_drown, "dead flag", guest->m_waitingForDeath == 1, guest);
        Expect(g_drown, "death counter",
                state == 0x36 ? world->m_guestsEaten == eaten + 1 && world->m_guestsDrowned == drowned
                              : world->m_guestsDrowned == drowned + 1 && world->m_guestsEaten == eaten,
                guest);
    }
}

void DecideSwimAction(object::Guest* guest)
{
    if (!g_helperChecks) return g_decideSwim(guest);
    float radius = static_cast<float>(Def(guest)->m_SwimDist);
    float x = guest->m_WorldXYZ[0], y = guest->m_WorldXYZ[1], z = guest->m_WorldXYZ[2];
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_decideSwim(guest);
    g_post.guest = nullptr;
    ++g_swim.calls;
    bool posted = g_post.logged == 1 && g_post.log[0][2] == MT_SWIM2SEAPOINT;
    Expect(g_swim, "one MT_SWIM2SEAPOINT", posted, guest);
    if (!posted) return;
    const int* m = g_post.log[0];
    ++g_swim.branch[radius <= 100 ? 0 : 1];
    Expect(g_swim, "sender and receiver are the guest", m[0] == static_cast<int>(guest->m_id) && m[1] == m[0], guest);
    float tx = static_cast<float>(m[3]), ty = static_cast<float>(m[4]);
    Expect(g_swim, "within the clamped swim range",
            tx >= 10 && tx <= 4000 && ty >= 10 && ty <= 8000 && tx >= x - radius - 1 && tx <= x + radius + 1 && ty >= y - radius - 1 &&
                    ty <= y + radius + 1,
            guest);
    unsigned flags = TileFlags(tx + 0.5f, ty + 0.5f, z);
    Expect(g_swim, "target tile is free sea", (flags & 4) && !(flags & 0x10), guest);
}

// A swimmer needs a sea tile to start from: scan the world for the first free sea point.
bool FindSeaPoint(float z, float& x, float& y)
{
    for (float sy = 100; sy < 8000; sy += 50)
        for (float sx = 100; sx < 4000; sx += 50) {
            unsigned flags = TileFlags(sx, sy, z);
            if ((flags & 4) && !(flags & 0x10)) {
                x = sx;
                y = sy;
                return true;
            }
        }
    return false;
}

void ForceSwimming(object::Guest* guest)
{
    float x = 0, y = 0;
    if (!FindSeaPoint(guest->m_WorldXYZ[2], x, y)) {
        OBL_LOG("autotest: FAILED swim-force found no sea tile");
        return;
    }
    auto def = Def(guest);
    float savedRadius = def->m_SwimDist;
    for (float radius : {20.0f, 300.0f, 3000.0f})
        for (int i = 0; i < 40; ++i)
            Forced(guest, [&] {
                def->m_SwimDist = radius;
                guest->m_WorldXYZ[0] = x;
                guest->m_WorldXYZ[1] = y;
                DecideSwimAction(guest);
            });
    def->m_SwimDist = savedRadius;
    // drowning at several oxygen levels (1 = the death step), normal and "eaten" state; world counters restored afterwards
    world_data::World* world = World();
    int drowned = world->m_guestsDrowned, eaten = world->m_guestsEaten;
    for (int oxygen : {100, 40, 2, 1})
        for (int state : {0, 0x36})
            Forced(guest, [&] {
                guest->m_WorldXYZ[0] = x;
                guest->m_WorldXYZ[1] = y;
                guest->m_oxygen = oxygen;
                guest->m_StayingAt = -1;
                guest->m_state = state;
                UpdateDrowning(guest);
            });
    world->m_guestsDrowned = drowned;
    world->m_guestsEaten = eaten;
}

// StartStomachIllness (the ill branch of SearchDesire): returns 0 unless the guest is
// ill (m_imIll). An ill guest draws a random walkable point (world::FindRandomWalkPoint: x 1..3999, y 1..7999, tile flags
// & 0x10000082 set and & 0x8010 clear) into Walkpoint, sends itself MT_STATECHANGE (type 3) to
// STATE_GUEST_BAD_GUTS (0x38), adds a GUEST_THOUGHT_NOT_WELL<1-3> thought and
// returns 1.
using Predicate = bool (*)(object::Guest*);
Predicate g_startBadGuts;
HelperStats g_badGuts;

bool StartStomachIllness(object::Guest* guest)
{
    if (!g_helperChecks) return g_startBadGuts(guest);
    bool ill = guest->m_imIll != 0;
    int thoughts[3] = {guest->m_Thoughts[0], guest->m_Thoughts[1], guest->m_Thoughts[2]};
    float before[2] = {guest->m_walkpoints[0][0], guest->m_walkpoints[0][1]};
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    bool result = g_startBadGuts(guest);
    g_post.guest = nullptr;
    ++g_badGuts.calls;
    ++g_badGuts.branch[ill ? 1 : 0];
    Expect(g_badGuts, "result", (result & 0xff) == (ill ? 1u : 0u), guest);
    if (!ill) {
        Expect(g_badGuts, "healthy: nothing changes",
                g_post.logged == 0 && guest->m_walkpoints[0][0] == before[0] && guest->m_walkpoints[0][1] == before[1] &&
                        guest->m_Thoughts[0] == thoughts[0],
                guest);
        return result;
    }
    Expect(g_badGuts, "one state change to BAD_GUTS",
            g_post.logged == 1 && g_post.log[0][2] == MT_STATECHANGE && g_post.log[0][4] == 0x38 && g_post.log[0][3] == 0 &&
                    g_post.log[0][0] == static_cast<int>(guest->m_id) && g_post.log[0][1] == g_post.log[0][0],
            guest);
    float x = guest->m_walkpoints[0][0], y = guest->m_walkpoints[0][1];
    Expect(g_badGuts, "walkpoint range", x >= 1 && x <= 3999 && y >= 1 && y <= 7999, guest);
    unsigned flags = TileFlags(x, y, 0);
    Expect(g_badGuts, "walkpoint tile", (flags & 0x10000082) && !(flags & 0x8010), guest);
    bool shifted = guest->m_Thoughts[1] == thoughts[0] && guest->m_Thoughts[2] == thoughts[1];
    bool unchanged = guest->m_Thoughts[0] == thoughts[0] && guest->m_Thoughts[1] == thoughts[1] && guest->m_Thoughts[2] == thoughts[2];
    Expect(g_badGuts, "thought pushed", shifted || unchanged, guest);
    return result;
}

// The thought functions (src/game/objects/guest_thoughts.h): natural play never reaches them (no detention, no full building), so the scratch guest is sent to a real
// building and each is called directly. It runs inside the real game with its effects (the building's thought, the ticker line, the sound).
void ForceThoughts(object::Guest* guest, Building* building)
{
    void (Guest::* const kFunctions[])() = {&Guest::AddIllPurchaseThought, &Guest::AddCommodBeachClosed, &Guest::AddThoughtIDidnt,
            &Guest::AddThoughtIDidIt, &Guest::AddDrowningThought, &Guest::NoRouteToLodging, &Guest::AddBuildingFullThought,
            &Guest::DemolishedAsleep, &Guest::GenerateBlockedMessage,
            &Guest::AlterDesires}; // scenario 1 never calls the last one from the script
    float& tickerTime = game::LastNoPathTickerTime();
    const float savedTickerTime = tickerTime;
    for (auto function : kFunctions)
        for (int i = 0; i < 4; ++i)
            Forced(guest, [&] {
                guest->m_DestObjectID = guest->m_CurID = static_cast<int32_t>(building->m_id);
                guest->m_desired_commod = 0;
                guest->m_CurTypeID =
                        building->m_definition->m_id; // GenerateBlockedMessage names this building's key (an activity would not)
                guest->m_activity = 0;
                guest->m_Thoughts[0] = guest->m_Thoughts[1] = guest->m_Thoughts[2] = 0;
                tickerTime = i % 2 ? -1000.0f : 1e9f; // the ticker line is due on odd rounds only
                // The state change of AddBuildingFullThought is swallowed like the helpers' (it would run the scratch guest's script).
                g_post.guest = guest;
                g_post.seen = false;
                (guest->*function)();
                g_post.guest = nullptr;
            });
    tickerTime = savedTickerTime;
}

// The walking goals of src/game/objects/guest_walk_targets.h: natural play heads for no entry point and windsurfs and unloads nowhere, and each call ends in a Post
// that would run the scratch guest's script, so the messages are swallowed like the helpers'. A building stands in for the vehicle (its matching field is not
// the unload kind, so GotoWorldUnloadPoint takes the MT_MOVEIMHERE branch).
void ForceWalkTargets(object::Guest* guest, Building* building)
{
    float x = 0, y = 0;
    if (!FindSeaPoint(guest->m_WorldXYZ[2], x, y)) {
        OBL_LOG("autotest: FAILED walk-targets-force found no sea tile");
        return;
    }
    void (Guest::* const kFunctions[])() = {
            &Guest::HeadForEntryPoint, &Guest::GetSeaEntry, &Guest::GotoWorldUnloadPoint, &Guest::WalkToWindsurf, &Guest::WanderBeach};
    for (auto function : kFunctions)
        for (int i = 0; i < 3; ++i)
            Forced(guest, [&] {
                guest->m_WorldXYZ[0] = x;
                guest->m_WorldXYZ[1] = y;
                guest->m_ActivityPerson = static_cast<int32_t>(building->m_id);
                g_post.guest = guest;
                (guest->*function)();
                g_post.guest = nullptr;
            });
}

// The pool and slide functions of src/game/objects/guest_pool.h: natural play has no pool, so each call is made on the scratch guest next to the building given
// (its direction is 0..3 for every building of the map), in every action state of GetNextPoolPoint. Messages are swallowed. ChooseFunSlideAction swaps
// the guest's sprite, so the guest is not put back by bytes: the funpool sprite is released and one of the guest's own name made again.
void ForcePool(object::Guest* guest, Building* building)
{
    for (int32_t state : {0, 0x28, 0x29, 0x2a, 0x64, 1})
        Forced(guest, [&] {
            guest->m_DestObjectID = static_cast<int32_t>(building->m_id);
            guest->m_ActionState = state;
            g_post.guest = guest;
            guest->GetNextPoolPoint();
            g_post.guest = nullptr;
        });
    for (int32_t state : {5, 6})
        Forced(guest, [&] {
            guest->m_DestObjectID = static_cast<int32_t>(building->m_id);
            guest->m_ActionState = state;
            g_post.guest = guest;
            guest->SetupFunPoolSwim();
            g_post.guest = nullptr;
        });
    auto* sprite = static_cast<ui::SpriteInstance*>(guest->m_sprite);
    if (!sprite || !sprite->m_name) return;
    std::array<uint8_t, kGuestSize> snapshot;
    std::copy_n(reinterpret_cast<const uint8_t*>(guest), snapshot.size(), snapshot.begin());
    const ui::SpriteInstance savedSprite = *sprite;
    const std::string name = sprite->m_name;
    const float home[3] = {guest->m_WorldXYZ[0], guest->m_WorldXYZ[1], guest->m_WorldXYZ[2]};
    guest->m_DestObjectID = static_cast<int32_t>(building->m_id);
    g_post.guest = guest;
    g_post.swallow = true;
    guest->ChooseFunSlideAction();
    // The way back from the slide: the action state the slide left (1..4) makes SetupFunPoolSwim swap the funpool sprite for the swimming one.
    guest->SetupFunPoolSwim();
    g_post.swallow = false;
    g_post.guest = nullptr;
    guest->Teleport(home[0], home[1], home[2]);
    void* bank = ui::GetSpriteBank();
    (static_cast<ui::SpriteBank*>(bank))->ReleaseInstance(guest->m_sprite);
    auto* restored = (static_cast<ui::SpriteBank*>(bank))->CreateObservedInstance(name.c_str());
    if (restored) {
        char* ownName = restored->m_name; // the saved one was freed with its instance
        *restored = savedSprite;
        restored->m_name = ownName;
    }
    std::copy(snapshot.begin(), snapshot.end(), reinterpret_cast<uint8_t*>(guest));
    guest->m_sprite = restored;
}

// The grid walkers and the bin of src/game/objects/guest_walk_routes.h and guest_walk_targets.h: natural play neither litters into a bin on demand, nor walks off the
// beach by StartPathWalk, nor windsurfs, nor dances (the map has no dance floor), so each call is made on the scratch guest with its messages swallowed. The calls
// that would not end (no ray meets a tile, no candidate fits) are checked with the same pure search first; the built flag of a dance floor and the sprite
// direction of the guest are put back afterwards.
void ForceWalkRoutes(object::Guest* guest, Building* building)
{
    float x = 0, y = 0;
    if (!FindSeaPoint(guest->m_WorldXYZ[2], x, y)) {
        OBL_LOG("autotest: FAILED walk-routes-force found no sea tile");
        return;
    }
    map::Grid* grid = GridInstance();
    const auto tileOf = [&](object::Base* object) {
        int tile[2] = {};
        ui::MapView::WorldToTile(tile, object->m_WorldXYZ);
        return guest_walk::TileXY{static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1])};
    };
    const auto call = [&](void (Guest::*function)()) {
        g_post.guest = guest;
        (guest->*function)();
        g_post.guest = nullptr;
    };
    for (int i = 0; i < 3; ++i) Forced(guest, [&] { call(&Guest::HeadForBin); });
    // WalkAlongPath from where the scratch guest stands: a plain walk, then the desperate one that thinks about the building and about the sea.
    const auto walk = [&] {
        g_post.guest = guest;
        guest->WalkAlongPath();
        g_post.guest = nullptr;
    };
    for (int i = 0; i < 3; ++i) Forced(guest, walk);
    for (uint8_t activity : {0, 1})
        Forced(guest, [&] {
            guest->m_state = 0x3a;
            std::strcpy(guest->m_WalkAnimation, "D_WALK_DESPERATE");
            guest->m_activity = activity;
            guest->m_desired_commod = 2;
            guest->m_CurID = static_cast<int32_t>(building->m_id);
            guest->m_CurTypeID = Def(building)->m_id;
            walk();
        });
    // From where the scratch guest stands.
    if (guest_walk::FindGetonTile(*grid, tileOf(guest)))
        for (int i = 0; i < 3; ++i) Forced(guest, [&] { call(&Guest::StartPathWalk); });
    else
        OBL_LOG("autotest: FAILED walk-routes-force no ray of StartPathWalk meets a tile from the guest");
    // From the sea.
    for (int i = 0; i < 3; ++i)
        Forced(guest, [&] {
            guest->m_WorldXYZ[0] = x;
            guest->m_WorldXYZ[1] = y;
            call(&Guest::SurfToShore);
        });
    // Windsurfing needs free water ahead (the turn-round branch plays animations for real): the first direction that has some.
    if (auto* sprite = static_cast<ui::SpriteInstance*>(guest->m_sprite)) {
        const int32_t savedDirection = sprite->m_direction;
        guest->m_WorldXYZ[0] = x;
        guest->m_WorldXYZ[1] = y;
        const guest_walk::TileXY here = tileOf(guest);
        int direction = -1;
        for (int d = 0; d < 8 && direction < 0; ++d)
            if (guest_walk::WaterAhead(*grid, here, d) > 0) direction = d;
        if (direction >= 0) {
            sprite->m_direction = direction;
            for (int i = 0; i < 3; ++i) Forced(guest, [&] { call(&Guest::WindSurfStraight); });
            sprite->m_direction = savedDirection;
        } else {
            OBL_LOG("autotest: FAILED walk-routes-force no free water ahead of the sea point");
        }
    }
    // A dance floor: the building, its footprint carrying the built flag.
    const guest_walk::TileXY anchor = tileOf(building);
    const guest_walk::DanceFloor floor = guest_walk::DanceFloorOf(anchor, static_cast<ui::SpriteInstance*>(building->m_sprite)->m_direction,
            Def(building)->m_GridWidth, Def(building)->m_GridHeight);
    struct Painted {
        uint32_t x, y, flags;
    };
    std::vector<Painted> painted;
    guest_walk::ForEachFloorTile(*grid, floor, [&](guest_walk::TileXY tile) {
        painted.push_back({tile.x, tile.y, grid->Cell(tile.x, tile.y).m_flags});
        grid->Cell(tile.x, tile.y).m_flags |= guest_walk::kDanceFloorFlag;
    });
    bool danced = false;
    for (int attempt = 0; attempt < 3 && !danced; ++attempt) {
        const grid_math::WorldPoint spot =
                grid_math::TileToWorldPoint(grid->m_tileSize, anchor.x + attempt, anchor.y, guest_walk::kDanceTilePoint);
        const float savedXY[2] = {guest->m_WorldXYZ[0], guest->m_WorldXYZ[1]};
        guest->m_WorldXYZ[0] = spot.x;
        guest->m_WorldXYZ[1] = spot.y;
        uint32_t seed = 12345u + static_cast<uint32_t>(attempt);
        const auto trial = guest_walk::FindDanceTile(
                *grid, tileOf(guest), floor, [&] { return static_cast<int32_t>((seed = seed * 1664525u + 1013904223u) >> 17); }, 3000);
        if (trial) {
            for (int i = 0; i < 3; ++i)
                Forced(guest, [&] {
                    guest->m_DestObjectID = static_cast<int32_t>(building->m_id);
                    call(&Guest::GotoDancePoint);
                });
            danced = true;
        }
        guest->m_WorldXYZ[0] = savedXY[0];
        guest->m_WorldXYZ[1] = savedXY[1];
    }
    for (auto it = painted.rbegin(); it != painted.rend(); ++it) grid->Cell(it->x, it->y).m_flags = it->flags;
    if (!danced) OBL_LOG("autotest: FAILED walk-routes-force no dance tile in reach of the building");
}

// The lookups of src/game/objects/guest_lookups.h: natural play seldom sends a guest to a first-aid point, a bench or the complaints hut, and each call ends in
// Posts that would run the scratch guest's script, so the messages are swallowed like the helpers'. The buildings must exist on the map (the test script
// spawns the missing ones: without a COMPLAINTHUT or WHARF/BUSSTOP the game ends), and the guest stands on the first bench for the bench call.
// CreateLitter runs with litter forbidden (see below); its animation name is restored with the rest of the guest.
void ForceLookups(object::Guest* guest)
{
    const auto first = [&](const char* name) -> object::Base* {
        const auto list = object::object_manager::FindObjectsByName(game::GetObjectManager(), name);
        const auto count = static_cast<uint32_t>(list.size());
        return count ? list[0] : nullptr;
    };
    const auto call = [&](void (Guest::*function)()) {
        g_post.guest = guest;
        (guest->*function)();
        g_post.guest = nullptr;
    };
    for (int i = 0; i < 3; ++i)
        Forced(guest, [&] {
            guest->m_StayingAt = i == 0 ? -1 : static_cast<int32_t>(guest->m_id);
            call(&Guest::LookForIllPoint);
        });
    if (object::Base* bench = first("BENCH"))
        for (int i = 0; i < 3; ++i)
            Forced(guest, [&] {
                guest->m_HasSatDown = 0;
                guest->m_WorldXYZ[0] = bench->m_WorldXYZ[0];
                guest->m_WorldXYZ[1] = bench->m_WorldXYZ[1];
                call(&Guest::LookForBench);
            });
    // GetConsumePoint (natural play reaches it rarely) on the bench's tile, so that the random search has beach around it: once in the consume state (nobody
    // waits next to the scratch guest, or a guest of the crowd does), and in another state for the bench search and the point.
    if (object::Base* bench = first("BENCH"))
        for (int32_t state : {guest_lookup::kStateConsume, 0, 0, guest_lookup::kStateConsume})
            Forced(guest, [&] {
                guest->m_state = state;
                guest->m_WorldXYZ[0] = bench->m_WorldXYZ[0];
                guest->m_WorldXYZ[1] = bench->m_WorldXYZ[1];
                call(&Guest::GetConsumePoint);
            });
    // FindFlirtTarget: with and without the desire to flirt (a bar walk or the way back to the desires) next to whoever is around the scratch guest. MoveActionPerson
    // on another guest of the crowd, in state 0x27 and not (the person's sprite, position and animation are put back).
    for (float desire : {0.0f, 0.5f, 0.5f})
        Forced(guest, [&] {
            guest->m_desires[20] = desire;
            call(&Guest::FindFlirtTarget);
        });
    object::Manager* crowd = game::GetObjectManager();
    for (uint32_t i = 0; i < crowd->m_guests.size(); ++i) {
        auto* partner = static_cast<object::Guest*>(crowd->m_guests[i]);
        if (partner == guest || !partner->m_sprite || !guest->m_sprite) continue;
        auto* sprite = static_cast<ui::SpriteInstance*>(partner->m_sprite);
        const ui::SpriteInstance savedSprite = *sprite;
        const float savedPosition[3] = {partner->m_WorldXYZ[0], partner->m_WorldXYZ[1], partner->m_WorldXYZ[2]};
        for (int32_t state : {0xd, guest_lookup::kStateHoldHands})
            Forced(guest, [&] {
                guest->m_state = state;
                guest->m_ActivityPerson = static_cast<int32_t>(partner->m_id);
                call(&Guest::MoveActionPerson);
            });
        *sprite = savedSprite;
        partner->SetPosition(savedPosition);
        break;
    }
    if (first("COMPLAINTHUT"))
        for (int i = 0; i < 3; ++i) Forced(guest, [&] { call(&Guest::QueueAtComplaints); });
    if (first("WHARF") || first("BUSSTOP"))
        for (int i = 0; i < 3; ++i) Forced(guest, [&] { call(&Guest::QueueAtWharf); });
    // A litter placed on the scratch guest's tile has no usable sprite and crashes the next draw (so does the game), so the world forbids litter for these
    // calls: the animation choice and its draws run, the placement itself happens in natural play.
    world_data::World* world = game::GetWorld();
    const uint8_t savedAllow = world->m_allowLitter;
    world->m_allowLitter = 0;
    for (const char* animation :
            {"D_STAND_DROPDRINK", "D_WALK_DRINK", "D_STAND_DROPICE", "D_STAND_DROPBURGER", "D_WALK_BURGER", "D_STAND_VOMIT"})
        Forced(guest, [&] {
            std::strncpy(guest->m_CurrAnimation, animation, sizeof(guest->m_CurrAnimation) - 1);
            call(&Guest::CreateLitter);
        });
    world->m_allowLitter = savedAllow;
}

void ForceBadGuts(object::Guest* guest)
{
    for (int ill : {0, 1, 1, 1, 1})
        for (int i = 0; i < 20; ++i)
            Forced(guest, [&] {
                guest->m_imIll = static_cast<uint8_t>(ill);
                StartStomachIllness(guest);
            });
}

// Fighting (STATE_GUEST_LOOK_FOR_FIGHT / STATE_GUEST_FIGHTING in guest.ble). Message ids are the real ones (n - 1): 3 = MT_STATECHANGE,
// 5 = MT_MOVE2POINT, 8 = MT_PAVEMENT2POINT, 0x15 = MT_MOVESTOP, 0x49 = MT_SPOT_OPINION, 0x54 = MT_LOOK_AROUND; states 0xc =
// SEARCH_DESIRE, 0xd = HEADING_DESIRE, 0x1f = LOOK_FOR_FIGHT, 0x20 = FIGHTING.
//   FindFightPoint: DesireFight <= 0 -> STATE_GUEST_SEARCH_DESIRE. Otherwise the nearest (tile distance, first
//   wins ties, list[0] when there is none) OPEN building that sells an ALCOHOL-family commodity gets a random walkable tile within
//   +-7 of its tile (redrawn until walkable and inside 0..127); posts MT_PAVEMENT2POINT with the truncated world origin of it.
//   FindFightPerson: first object within 2 tiles that is a guest (class 3), not the guest itself, of the same GENDER
//  , in state HEADING_DESIRE or LOOK_FOR_FIGHT and standing on a walkable tile (evaluated at x - 20) becomes
//   FightPerson, ActionState = 1; the target gets MT_MOVESTOP and MT_STATECHANGE(FIGHTING) from the guest, the guest gets
//   MT_MOVE2POINT to (trunc(target.x - 20), trunc(target.y)) and the target's sprite is turned towards the guest. Only the first match.
//   TellNearbyOfFight: every guest within 1 tile (including the guest itself) gets MT_SPOT_OPINION kind 3 (a fight
//   seen); the guest sends itself MT_LOOK_AROUND delayed by 2 s.
HelperStats g_fightPoint, g_fightPerson, g_fightMsgs;
GuestMethod g_findFightPoint, g_findFightPerson, g_sendFightMsgs;
struct DelayedCapture {
    int count = 0;
    int msg[5]{};
    float delay = 0;
} g_delayed;
messages::Handler* g_queueDelivery = nullptr;
bool PostDelayed(messages::Handler*, messages::Message* message, float delay)
{
    if (g_queueDelivery && message->type == MT_MOVE2QUEUEPOINT) {
        // Keep the real dequeue messages in an isolated clock-driven delivery queue.
        auto* delivery = std::exchange(g_queueDelivery, nullptr);
        messages::message_facade::PostDelayed(delivery, message, delay);
        g_queueDelivery = delivery;
        return true;
    }
    if (g_post.guest) {
        ++g_delayed.count;
        g_delayed.msg[0] = message->sender;
        g_delayed.msg[1] = message->receiver;
        g_delayed.msg[2] = message->type;
        g_delayed.msg[3] = message->flags;
        g_delayed.msg[4] = message->data;
        g_delayed.delay = delay;
        if (g_post.swallow) return true;
    }
    return false;
}

std::vector<object::Base*> ObjectsNear(object::Base* object, int radius)
{
    object::Manager* manager = game::GetObjectManager();
    int tile[2] = {};
    ui::MapView::WorldToTile(tile, object->m_WorldXYZ);
    auto result = object::object_manager::GetObjectsNearTile(manager, tile[0], tile[1], radius);
    if (result.size() > 4096) result.resize(4096);
    return result;
}

unsigned TileFlagsAt(uint32_t tx, uint32_t ty)
{
    return GridInstance()->Cell(tx, ty).m_flags;
}

// the walkable-ground test shared by FindFightPerson and FindFightPoint
bool FightWalkable(unsigned f)
{
    return ((f & 0x10000080) != 0 && (f & 0x8000) == 0) || ((f & 2) != 0 && (f & 0x8010) == 0);
}

bool Sent(int sender, int receiver, int type, int d0, int d1)
{
    for (int i = 0; i < std::min(g_post.logged, 64); ++i) {
        const int* m = g_post.log[i];
        if (m[0] == sender && m[1] == receiver && m[2] == type && m[3] == d0 && m[4] == d1) return true;
    }
    return false;
}

int SentOfType(int type)
{
    int n = 0;
    for (int i = 0; i < std::min(g_post.logged, 64); ++i) n += g_post.log[i][2] == type;
    return n;
}

void FindFightPoint(object::Guest* guest)
{
    if (!g_helperChecks) return g_findFightPoint(guest);
    int id = guest->m_id;
    float desire = guest->m_desires[kDesireFight];
    // nearest open building with an ALCOHOL-family commodity (the guest does not move during the call)
    int bx = 0, by = 0;
    object::Manager* manager = game::GetObjectManager();
    tables::WorldParams* params = game::GetWorldParams();
    const auto list = object::object_manager::GetListByClass(manager, 2);
    std::vector<Building*> buildings;
    for (size_t i = 0; i < list.size() && i < 512; ++i) buildings.push_back(static_cast<Building*>(list[i]));
    if (buildings.empty()) return g_findFightPoint(guest);
    int family = world::world_params::GetCommodityFamilyId(params, "ALCOHOL");
    int mine[2] = {};
    ui::MapView::WorldToTile(mine, guest->m_WorldXYZ);
    float best = 50000.0f;
    Building* chosen = buildings[0];
    for (Building* building : buildings) {
        if (!object::building_api::IsOpen(building)) continue;
        auto def = Def(building);
        int theirs[2] = {};
        ui::MapView::WorldToTile(theirs, building->m_WorldXYZ);
        for (int i = 0; i < def->m_NumCommodities; ++i) {
            if (world::world_params::GetCommodityFamilyOf(params, def->m_Commodity[i]) != family) continue;
            int dx = mine[0] - theirs[0], dy = mine[1] - theirs[1];
            float distance = std::sqrt(static_cast<float>(static_cast<uint32_t>(dy * dy + dx * dx)));
            if (distance < best) {
                best = distance;
                chosen = building;
            }
        }
    }
    int chosenTile[2] = {};
    ui::MapView::WorldToTile(chosenTile, chosen->m_WorldXYZ);
    bx = chosenTile[0];
    by = chosenTile[1];

    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_findFightPoint(guest);
    g_post.guest = nullptr;
    ++g_fightPoint.calls;
    if (desire <= 0) {
        ++g_fightPoint.branch[0];
        Expect(g_fightPoint, "no desire: back to SEARCH_DESIRE", g_post.logged == 1 && Sent(id, id, MT_STATECHANGE, 0, 0xc), guest);
        return;
    }
    ++g_fightPoint.branch[best < 50000.0f ? 1 : 2];
    Expect(g_fightPoint, "one MT_PAVEMENT2POINT", g_post.logged == 1 && SentOfType(MT_PAVEMENT2POINT) == 1, guest);
    if (g_post.logged != 1 || g_post.log[0][2] != MT_PAVEMENT2POINT) return;
    const int* m = g_post.log[0];
    Expect(g_fightPoint, "sender and receiver are the guest", m[0] == id && m[1] == id, guest);
    // the target must be the truncated world origin of a walkable tile within 7 tiles of the chosen building
    bool found = false;
    for (int ty = by - 7; ty <= by + 7 && !found; ++ty)
        for (int tx = bx - 7; tx <= bx + 7 && !found; ++tx) {
            if (tx < 0 || tx >= 0x80 || ty < 0 || ty >= 0x80 || !FightWalkable(TileFlagsAt(tx, ty))) continue;
            const grid_math::WorldPoint origin = GridInstance()->TileToWorldOrigin(tx, ty);
            found = static_cast<int>(origin.x) == m[3] && static_cast<int>(origin.y) == m[4];
        }
    Expect(g_fightPoint, "target tile near the nearest alcohol building", found, guest);
}

void FindFightPerson(object::Guest* guest)
{
    if (!g_helperChecks) return g_findFightPerson(guest);
    int id = guest->m_id;
    object::Base* expected = nullptr;
    float target[3] = {};
    for (object::Base* other : ObjectsNear(guest, 2)) {
        if (static_cast<int>(other->m_id) == id || other->m_type != object::ObjectType::Guest) continue;
        if (Def(static_cast<object::Guest*>(other))->m_Gender != Def(guest)->m_Gender) continue;
        int state = other->m_state;
        if (state != 0xd && state != 0x1f) continue;
        if (!FightWalkable(TileFlags(other->m_WorldXYZ[0] - 20.0f, other->m_WorldXYZ[1], other->m_WorldXYZ[2]))) continue;
        expected = other;
        for (int i = 0; i < 3; ++i) target[i] = other->m_WorldXYZ[i];
        break;
    }
    int actionState = guest->m_ActionState, fightPerson = guest->m_FightPerson;
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_findFightPerson(guest);
    g_post.guest = nullptr;
    ++g_fightPerson.calls;
    ++g_fightPerson.branch[expected ? 1 : 0];
    if (!expected) {
        Expect(g_fightPerson, "no target: nothing happens",
                g_post.logged == 0 && guest->m_ActionState == actionState && guest->m_FightPerson == fightPerson, guest);
        return;
    }
    int other = expected->m_id;
    Expect(g_fightPerson, "ActionState = 1, FightPerson set", guest->m_ActionState == 1 && guest->m_FightPerson == other, guest);
    Expect(g_fightPerson, "MT_MOVESTOP to the target", Sent(id, other, MT_MOVESTOP, 0, 0), guest);
    Expect(g_fightPerson, "MT_STATECHANGE(FIGHTING) to the target", Sent(id, other, MT_STATECHANGE, 0, 0x20), guest);
    Expect(g_fightPerson, "MT_MOVE2POINT next to the target",
            Sent(id, id, MT_MOVE2POINT, static_cast<int>(target[0] - 20.0f), static_cast<int>(target[1])), guest);
    Expect(g_fightPerson, "exactly three messages", g_post.logged == 3, guest);
}

void TellNearbyOfFight(object::Guest* guest)
{
    if (!g_helperChecks) return g_sendFightMsgs(guest);
    int id = guest->m_id;
    std::vector<int> receivers;
    for (object::Base* other : ObjectsNear(guest, 1))
        if (other->m_type == object::ObjectType::Guest) receivers.push_back(other->m_id);
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_delayed = {};
    g_sendFightMsgs(guest);
    g_post.guest = nullptr;
    ++g_fightMsgs.calls;
    ++g_fightMsgs.branch[receivers.size() > 1 ? 1 : 0];
    bool all = true;
    for (int receiver : receivers) all = all && Sent(id, receiver, MT_SPOT_OPINION, 0, 3);
    Expect(g_fightMsgs, "MT_SPOT_OPINION(FIGHT) to every guest around",
            all && SentOfType(MT_SPOT_OPINION) == static_cast<int>(receivers.size()), guest);
    Expect(g_fightMsgs, "one delayed MT_LOOK_AROUND to itself after 2 s",
            g_delayed.count == 1 && g_delayed.msg[0] == id && g_delayed.msg[1] == id && g_delayed.msg[2] == 0x54 && g_delayed.msg[3] == 0 &&
                    g_delayed.msg[4] == 0 && g_delayed.delay == 2.0f,
            guest);
}

// A guest with same-gender neighbours within 2 tiles (the object tile lists cannot be faked, so use the live crowd) drives the
// FindFightPerson branches by overriding the neighbours' states; the states are restored afterwards.
void ForceFight(object::Guest* guest)
{
    for (float desire : {0.0f, -1.0f, 0.4f, 1.0f})
        for (int i = 0; i < 25; ++i)
            Forced(guest, {{{&guest->m_desires[kDesireFight], desire}, {nullptr, 0.0f}}}, [&] { FindFightPoint(guest); });
    // The baseline save has no open building that sells alcohol (FindFightPoint then falls back to list[0]), so lend one open
    // building an ALCOHOL commodity in its first slot (definition record, restored afterwards) to cover the "nearest bar" branch.
    {
        tables::WorldParams* params = game::GetWorldParams();
        int family = world::world_params::GetCommodityFamilyId(params, "ALCOHOL");
        int commodity = -1;
        for (int c = 0; c < 64 && commodity < 0; ++c)
            if (world::world_params::GetCommodityFamilyOf(params, c) == family) commodity = c;
        object::Manager* manager = game::GetObjectManager();
        const auto list = object::object_manager::GetListByClass(manager, 2);
        Building* bar = nullptr;
        for (size_t i = 0; i < list.size() && !bar; ++i)
            if (object::building_api::IsOpen(static_cast<Building*>(list[i]))) bar = static_cast<Building*>(list[i]);
        if (commodity < 0 || !bar) {
            OBL_LOG("autotest: FAILED fight-force needs an ALCOHOL commodity and an open building");
        } else {
            auto def = Def(bar);
            int savedCount = def->m_NumCommodities;
            int savedSlot = def->m_Commodity[0];
            def->m_NumCommodities = std::max(savedCount, 1);
            def->m_Commodity[0] = commodity;
            for (int i = 0; i < 40; ++i)
                Forced(guest, {{{&guest->m_desires[kDesireFight], 0.5f}, {nullptr, 0.0f}}}, [&] { FindFightPoint(guest); });
            def->m_NumCommodities = savedCount;
            def->m_Commodity[0] = savedSlot;
        }
    }
    object::Manager* manager = game::GetObjectManager();
    object::Guest* host = nullptr;
    std::vector<object::Base*> around;
    for (int i = 0; manager && i < static_cast<int>(manager->m_guests.size()) && !host; ++i) {
        auto candidate = static_cast<object::Guest*>(manager->m_guests[i]);
        auto crowd = ObjectsNear(candidate, 2);
        int guests = 0;
        for (object::Base* other : crowd) guests += other->m_type == object::ObjectType::Guest && other != candidate;
        int adjacent = 0;
        for (object::Base* other : ObjectsNear(candidate, 1)) adjacent += other->m_type == object::ObjectType::Guest && other != candidate;
        if (guests >= 2 && adjacent >= 1) {
            host = candidate;
            around = crowd;
        }
    }
    if (!host) {
        OBL_LOG("autotest: FAILED fight-force found no guest with neighbours");
        return;
    }
    std::vector<std::pair<object::Base*, int>> saved;
    for (object::Base* other : around)
        if (other != host) saved.emplace_back(other, other->m_state);
    for (int mode = 0; mode < 6; ++mode) {
        for (size_t k = 0; k < saved.size(); ++k) {
            int state = 0;
            if (mode == 1) state = 0xd;
            if (mode == 2) state = 0x1f;
            if (mode == 3) state = k == 0 ? 0 : 0x1f; // only a later neighbour qualifies
            if (mode == 4) state = k % 2 ? 0xd : 0x1f; // several qualify: the first one wins
            if (mode == 5) state = 0x16; // wrong state
            saved[k].first->m_state = state;
        }
        Forced(host, [&] { FindFightPerson(host); });
        Forced(host, [&] { TellNearbyOfFight(host); });
    }
    for (auto [other, state] : saved) other->m_state = state;
}

// Vandalism (STATE_GUEST_VANDALISING in guest.ble): LookForVandalItem on entry, VandaliseBin when the guest arrives (MT_MOVEIMHERE).
// Message ids: 3 = MT_STATECHANGE, 5 = MT_MOVE2POINT, 0x3c = MT_PLAYANIMATION, 0x54 = MT_LOOK_AROUND.
//   LookForVandalItem: looks at the static objects named "Bin", then "Bench", then "STREET_LAMP" (object list order) and
//   keeps the last one whose metric |dx| + dy^2 (tile offsets; the intent is a distance but only sqrt(dx * dx) is taken) is <= the best so far (start 10, so later kinds win ties) that is not in state 4
//   and not claimed (by a vandal, a cleaner or a mechanic). Found: the object gets MT_STATECHANGE(4) from the guest,
//   the guest gets MT_MOVE2POINT to (trunc(x + A[dir]), trunc(y + B[dir])) with dir = the object sprite's direction (A: bin 0,-15,-30,-15,0,15,
//   30,15; bench/lamp 0,-15,-20,-15,0,15,20,15; B: 20,15,0,-15,-20,-15,0,15), ActionPerson = the object and ActionState
//   = 2 for a full bin (fill level at the definition's maximum) / 3 for another bin / 4 bench / 5 lamp. Nothing found: Vandalise is
//   added to Fight (capped at 1), Vandalise = 0, ActionState = 1, ActionPerson = -1.
//   VandaliseBin: no object for ActionPerson -> nothing. Otherwise the object gets the vandal claim (claim flags set, the sprite
//   remembered, self-sent MT_LOOK_AROUND, only when it was not claimed yet), then MT_PLAYANIMATION from the guest with the
//   animation name (ActionState 2 FULLBIN_VANDALISE, 3 EMPTYBIN_VANDALISE, 4 BENCH_VANDALISE, 5/6 STREETLAMP_VANDALISE_1/2;
//   any other ActionState posts an all-zero message), and MyOffence = 3 with the offence time (the world time).
HelperStats g_vandalLook, g_vandalBin;
GuestMethod g_lookForVandal, g_vandaliseBin;

std::vector<Scenery*> StaticsNamed(const char* name)
{
    object::Manager* manager = game::GetObjectManager();
    const auto list = object::object_manager::FindObjectsByName(manager, name);
    std::vector<Scenery*> result;
    for (size_t i = 0; i < list.size() && i < 512; ++i) result.push_back(static_cast<Scenery*>(list[i]));
    return result;
}

bool VandalClaimed(const Scenery* object)
{
    return object->m_state == 4 || object->m_vandalised || object->m_cleaning || object->m_repairing;
}

void LookForVandalItem(object::Guest* guest)
{
    if (!g_helperChecks) return g_lookForVandal(guest);
    int id = guest->m_id;
    int mine[2] = {};
    ui::MapView::WorldToTile(mine, guest->m_WorldXYZ);
    Scenery* chosen = nullptr;
    int kind = 4, best = 10;
    const char* names[3] = {"Bin", "Bench", "STREET_LAMP"};
    for (int k = 0; k < 3; ++k)
        for (Scenery* object : StaticsNamed(names[k])) {
            int theirs[2] = {};
            ui::MapView::WorldToTile(theirs, object->m_WorldXYZ);
            int dx = mine[0] - theirs[0], dy = mine[1] - theirs[1];
            int d2 = std::abs(dx) + dy * dy; // sqrt(dx * dx) + dy * dy, i.e. |dx| + dy^2
            if (d2 <= best && !VandalClaimed(object)) {
                chosen = object;
                best = d2;
                kind = k;
            }
        }
    float vandal = guest->m_desires[kDesireVandalise], fight = guest->m_desires[kDesireFight];
    int direction = chosen ? static_cast<ui::SpriteInstance*>(chosen->m_sprite)->m_direction : 0;
    float x = chosen ? chosen->m_WorldXYZ[0] : 0, y = chosen ? chosen->m_WorldXYZ[1] : 0;
    bool full = chosen && kind == 0 && chosen->m_NumItems == Def(chosen)->m_Capacity;

    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_lookForVandal(guest);
    g_post.guest = nullptr;
    ++g_vandalLook.calls;
    ++g_vandalLook.branch[kind == 4 ? 2 : (kind == 0 ? 0 : 1)];
    if (!chosen) {
        float expected = std::min(fight + vandal, 1.0f);
        Expect(g_vandalLook, "nothing found: desire moves to Fight",
                g_post.logged == 0 && guest->m_desires[kDesireVandalise] == 0 && guest->m_desires[kDesireFight] == expected &&
                        guest->m_ActionState == 1 && guest->m_ActivityPerson == -1,
                guest);
        return;
    }
    static const int kBinA[8] = {0, -15, -30, -15, 0, 15, 30, 15}, kOtherA[8] = {0, -15, -20, -15, 0, 15, 20, 15};
    static const int kB[8] = {20, 15, 0, -15, -20, -15, 0, 15};
    int other = chosen->m_id;
    int a = (kind == 0 ? kBinA : kOtherA)[direction & 7], b = kB[direction & 7];
    Expect(g_vandalLook, "sprite direction in range", direction >= 0 && direction < 8, guest);
    Expect(g_vandalLook, "MT_STATECHANGE(4) to the object", Sent(id, other, MT_STATECHANGE, 0, 4), guest);
    Expect(g_vandalLook, "MT_MOVE2POINT beside the object", Sent(id, id, MT_MOVE2POINT, static_cast<int>(x + a), static_cast<int>(y + b)),
            guest);
    Expect(g_vandalLook, "two messages", g_post.logged == 2, guest);
    int actionState = kind == 0 ? (full ? 2 : 3) : (kind == 1 ? 4 : 5);
    Expect(g_vandalLook, "ActionState and ActionPerson", guest->m_ActionState == actionState && guest->m_ActivityPerson == other, guest);
}

void VandaliseBin(object::Guest* guest)
{
    if (!g_helperChecks) return g_vandaliseBin(guest);
    int id = guest->m_id;
    object::Manager* manager = game::GetObjectManager();
    world_data::World* world = World();
    auto object = static_cast<Scenery*>(object::object_manager::ObjectById(manager, guest->m_ActivityPerson));
    int actionState = guest->m_ActionState;
    uint32_t offence = guest->m_OffenceState;
    float offenceTime = guest->m_timeOfOffence;
    bool wasClaimed = object && object->m_vandalised;
    g_post.guest = guest;
    g_post.count = g_post.logged = 0;
    g_vandaliseBin(guest);
    g_post.guest = nullptr;
    ++g_vandalBin.calls;
    ++g_vandalBin.branch[!object ? 0 : (actionState >= 2 && actionState <= 6 ? 1 : 2)];
    if (!object) {
        Expect(g_vandalBin, "no object: nothing happens",
                g_post.logged == 0 && guest->m_OffenceState == offence && guest->m_timeOfOffence == offenceTime, guest);
        return;
    }
    static const char* kAnimations[5] = {
            "FULLBIN_VANDALISE", "EMPTYBIN_VANDALISE", "BENCH_VANDALISE", "STREETLAMP_VANDALISE_1", "STREETLAMP_VANDALISE_2"};
    Expect(g_vandalBin, "object claimed", object->m_vandalised == 1, guest);
    Expect(g_vandalBin, "message count", g_post.logged == (wasClaimed ? 0 : 1) + 1, guest);
    if (!wasClaimed)
        Expect(g_vandalBin, "claim wakes the object (MT_LOOK_AROUND)", Sent(object->m_id, object->m_id, MT_LOOK_AROUND, 0, 0), guest);
    const int* last = g_post.log[std::max(std::min(g_post.logged, 64) - 1, 0)];
    if (actionState >= 2 && actionState <= 6) {
        const char* name = script::text::Of(last[4]);
        Expect(g_vandalBin, "MT_PLAYANIMATION with the animation name",
                last[0] == id && last[1] == static_cast<int>(object->m_id) && last[2] == 0x3c && last[3] == 0 && name &&
                        std::strcmp(name, kAnimations[actionState - 2]) == 0,
                guest);
    } else {
        Expect(g_vandalBin, "unrecognized ActionState posts a zero message",
                last[0] == 0 && last[1] == 0 && last[2] == 0 && last[3] == 0 && last[4] == 0, guest);
    }
    Expect(g_vandalBin, "offence recorded", guest->m_OffenceState == 3 && guest->m_timeOfOffence == world->m_gameTime, guest);
}

// Teleports the scratch guest next to each Bin/Bench/STREET_LAMP (tile lookups read the position fields), drives all eight sprite
// directions, full/not-full bins, claimed objects and a far-away spot; then VandaliseBin for every ActionState.
void ForceVandal(object::Guest* guest)
{
    const char* names[3] = {"Bin", "Bench", "STREET_LAMP"};
    std::vector<Scenery*> objects;
    for (auto name : names) {
        auto list = StaticsNamed(name);
        for (size_t i = 0; i < list.size() && i < 3; ++i) objects.push_back(list[i]);
    }
    if (objects.empty()) {
        OBL_LOG("autotest: FAILED vandal-force found no bin/bench/street lamp");
        return;
    }
    for (Scenery* object : objects) {
        auto* sprite = static_cast<ui::SpriteInstance*>(object->m_sprite);
        int savedDirection = sprite->m_direction, savedFill = object->m_NumItems;
        uint8_t savedClaim = object->m_vandalised;
        int fills[2] = {Def(object)->m_Capacity, 0};
        for (int direction = 0; direction < 8; ++direction)
            for (int fill : fills)
                for (uint8_t claim : {0, 1}) {
                    sprite->m_direction = direction;
                    object->m_NumItems = fill;
                    object->m_vandalised = claim;
                    Forced(guest, {{{&guest->m_desires[kDesireVandalise], 0.4f}, {&guest->m_desires[kDesireFight], 0.7f}}}, [&] {
                        guest->m_WorldXYZ[0] = object->m_WorldXYZ[0];
                        guest->m_WorldXYZ[1] = object->m_WorldXYZ[1];
                        guest->m_WorldXYZ[2] = object->m_WorldXYZ[2];
                        LookForVandalItem(guest);
                    });
                }
        sprite->m_direction = savedDirection;
        object->m_NumItems = savedFill;
        object->m_vandalised = savedClaim;
        // VandaliseBin for this object: every ActionState, before and after the claim
        for (int state = 0; state <= 7; ++state)
            for (uint8_t claim : {0, 1}) {
                object->m_vandalised = claim;
                Forced(guest, [&] {
                    guest->m_ActivityPerson = object->m_id;
                    guest->m_ActionState = state;
                    VandaliseBin(guest);
                });
            }
        object->m_vandalised = savedClaim;
    }
    Forced(guest, {{{&guest->m_desires[kDesireVandalise], 0.6f}, {&guest->m_desires[kDesireFight], 0.7f}}},
            [&] { // far from everything, sum above the cap
                guest->m_WorldXYZ[0] = 1.0f;
                guest->m_WorldXYZ[1] = 1.0f;
                LookForVandalItem(guest);
            });
    Forced(guest, {{{&guest->m_desires[kDesireVandalise], 0.2f}, {&guest->m_desires[kDesireFight], 0.1f}}}, [&] {
        guest->m_WorldXYZ[0] = 1.0f;
        guest->m_WorldXYZ[1] = 1.0f;
        LookForVandalItem(guest);
    });
    Forced(guest, [&] { // no such object
        guest->m_ActivityPerson = -1;
        VandaliseBin(guest);
    });
}

// Guest::SetRepeatAction: called from Guest::SearchDesire once a provider was
// found, with the winning desire and its value (0..1). RepeatAction = trunc(MAX_<desire> * value) where MAX_<desire> is
// the definition's per-desire maximum (only for the desires the switch lists; Headache, Sunbathing, Swimming, Stroll and
// Sleep keep the previous value), never below 1. The MAX_* fields are therefore read after all (they are 1 in every shipped
// Guests*.ble, so natural play always gets 1; the force run lends other values).
using RepeatActionCallback = void (*)(object::Guest*, int, float);
RepeatActionCallback g_setRepeat;
HelperStats g_setRepeatStats;

constexpr bool RepeatUsesMax(int desire)
{
    return (desire >= 0 && desire <= 3) || (desire >= 5 && desire <= 11) || desire == 14 || desire == 15 || (desire >= 17 && desire <= 20);
}

void SetRepeatAction(object::Guest* guest, int desire, float value)
{
    if (!g_helperChecks) return g_setRepeat(guest, desire, value);
    auto def = Def(guest);
    unsigned expected = guest->m_RepeatAction;
    bool mapped = RepeatUsesMax(desire);
    if (mapped) expected = static_cast<unsigned>(static_cast<int>(static_cast<float>(def->m_Max[desire]) * value));
    if (expected == 0) expected = 1;
    g_setRepeat(guest, desire, value);
    ++g_setRepeatStats.calls;
    ++g_setRepeatStats.branch[mapped ? 1 : 0];
    Expect(g_setRepeatStats, "RepeatAction", static_cast<unsigned>(guest->m_RepeatAction) == expected, guest);
}

// Beach / sea areas: Grid::m_beachAreas and m_seaAreas list them in script order. An area holds its polygon (m_points, tile pairs),
// the inclusive bounding rectangle and the m_shut flag that GuestFindSwimPoint requires to be false.
using map::Area;

void TileWorld(uint32_t x, uint32_t y, float* out, bool origin)
{
    const grid_math::WorldPoint point = origin ? GridInstance()->TileToWorldOrigin(x, y) : GridInstance()->TileToWorldPoint(x, y, 4);
    out[0] = point.x;
    out[1] = point.y;
    out[2] = point.z;
}

void Neighbour(uint32_t x, uint32_t y, int direction, uint32_t* out)
{
    const grid_math::TileXY next = GridInstance()->GetNeighbour(x, y, direction);
    out[0] = next.x;
    out[1] = next.y;
}

double TileDistance(uint32_t x, uint32_t y, const float* from)
{
    float w[3];
    TileWorld(x, y, w, true);
    double dx = w[0] - from[0], dy = w[1] - from[1], dz = w[2] - from[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Vertex {
    uint32_t x = 0, y = 0;
    const Area* node = nullptr;
};

bool Clear(uint32_t x, uint32_t y)
{
    return (TileFlagsAt(x, y) & 0x10) == 0;
}

// The shore vertex GuestFindSwimPoint accepts: its own tile and the four diagonal neighbours (directions 0, 2, 4, 6) free of buildings.
bool SwimVertexOk(uint32_t x, uint32_t y)
{
    if (!Clear(x, y)) return false;
    for (int direction : {0, 2, 4, 6}) {
        uint32_t n[2];
        Neighbour(x, y, direction, n);
        if (!Clear(n[0], n[1])) return false;
    }
    return true;
}

// Finds the tile of the vertex node's rectangle whose world point equals `world` (x, y).
bool TileInRect(const Vertex& v, const float* world, bool origin, uint32_t& tx, uint32_t& ty)
{
    int x0 = v.node->m_minX, y0 = v.node->m_minY;
    int x1 = v.node->m_maxX, y1 = v.node->m_maxY;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float w[3];
            TileWorld(x, y, w, origin);
            if (w[0] == world[0] && w[1] == world[1]) {
                tx = x;
                ty = y;
                return true;
            }
        }
    return false;
}

// FindSunbathePoint (SearchDesire desire 12 "Sunbathing"): the beach
// polygon vertex nearest to the guest (strict "closer" over every vertex of every node of the beach chain) picks the node; a random
// tile of that node's rectangle is redrawn until it and its eight neighbours are sand (flag 2) with no building and no
// 0x8100 (up to 1000 draws, then the vertex itself). Static result buffer: [0..2] the vertex point, [3..5] the tile point; the
// caller walks the guest to [3],[4] (Walkpoint).
using SunbathePointCallback = float* (*)(object::Guest*);
SunbathePointCallback g_findSunbathe;
HelperStats g_sunbathe;

bool SandOk(uint32_t x, uint32_t y)
{
    auto good = [](unsigned f) { return (f & 2) && !(f & 0x10) && !(f & 0x8100); };
    if (!good(TileFlagsAt(x, y))) return false;
    for (int direction = 0; direction < 8; ++direction) {
        uint32_t n[2];
        Neighbour(x, y, direction, n);
        if (!good(TileFlagsAt(n[0], n[1]))) return false;
    }
    return true;
}

float* FindSunbathePoint(object::Guest* guest)
{
    if (!g_helperChecks) return g_findSunbathe(guest);
    float from[3] = {guest->m_WorldXYZ[0], guest->m_WorldXYZ[1], guest->m_WorldXYZ[2]};
    Vertex best;
    bool have = false;
    for (const Area& node : GridInstance()->m_beachAreas)
        for (const map::TilePoint& vertex : node.m_points) {
            uint32_t x = vertex.x, y = vertex.y;
            if (!have || TileDistance(x, y, from) < TileDistance(best.x, best.y, from)) {
                best = {x, y, &node};
                have = true;
            }
        }
    float* result = g_findSunbathe(guest);
    ++g_sunbathe.calls;
    if (!have) {
        ++g_sunbathe.skipped;
        return result;
    }
    float vertex[3];
    TileWorld(best.x, best.y, vertex, false);
    Expect(g_sunbathe, "vertex point", result[0] == vertex[0] && result[1] == vertex[1] && result[2] == vertex[2], guest);
    uint32_t tx = 0, ty = 0;
    bool fallback = result[3] == vertex[0] && result[4] == vertex[1];
    ++g_sunbathe.branch[fallback ? 1 : 0];
    if (!fallback) {
        bool found = TileInRect(best, result + 3, false, tx, ty);
        Expect(g_sunbathe, "tile inside the vertex node's rectangle", found, guest);
        if (found) Expect(g_sunbathe, "tile is free sand", SandOk(tx, ty), guest);
    }
    return result;
}

// GuestFindSwimPoint (x, y, z; SearchDesire desire 13 "Swimming", only 9:00-18:00):
// over the sea chain, starting at its first node with flag 0: the first vertex whose own tile and diagonal neighbours are
// free of buildings (moving on to the next node - which is *not* filtered by the flag - when a node runs out), improved by every
// later vertex of that node and of every later flag-0 node that is strictly nearer and free. A random tile of the winning node's
// rectangle is redrawn until it is water (flag 4, no building) and the tiles two steps away in directions 0/2/4/6 are all
// water or sand. Static result buffer: [0..2] the vertex point (shore), [3..5] the tile origin (swim target); the caller
// walks to [0],[1] and then to [3],[4].
using SwimPointCallback = float* (*)(float, float, float);
SwimPointCallback g_findSwim;
HelperStats g_swim2;

// Quirk: the first draw steps direction 6 twice like the other three directions, but every redraw recomputes it with a
// single step (the loop head re-derives it from the tile itself), so a tile is accepted if either variant passes.
bool WaterOk(uint32_t x, uint32_t y, int steps6)
{
    unsigned f = TileFlagsAt(x, y);
    if (!(f & 4) || (f & 0x10)) return false;
    for (int direction : {0, 2, 4, 6}) {
        uint32_t n[2];
        Neighbour(x, y, direction, n);
        if (direction != 6 || steps6 == 2) Neighbour(n[0], n[1], direction, n);
        if (!(TileFlagsAt(n[0], n[1]) & 0xc)) return false;
    }
    return true;
}

float* FindSwimPoint(float x, float y, float z)
{
    if (!g_helperChecks) return g_findSwim(x, y, z);
    float from[3] = {x, y, z};
    Vertex best;
    bool have = false, modelled = true;
    const std::vector<Area>& areas = GridInstance()->m_seaAreas;
    size_t firstIndex = 0;
    while (firstIndex < areas.size() && areas[firstIndex].m_shut) ++firstIndex;
    const Area* first = firstIndex < areas.size() ? &areas[firstIndex] : nullptr;
    if (first) {
        size_t cur = firstIndex, i = 0;
        if (areas[cur].m_points.empty()) modelled = false; // the game reads past the empty list
        while (modelled && !SwimVertexOk(areas[cur].m_points[i].x, areas[cur].m_points[i].y)) {
            if (++i == areas[cur].m_points.size()) {
                ++cur;
                i = 0;
                if (cur == areas.size() || areas[cur].m_points.empty()) modelled = false; // the game ends in FatalError
            }
        }
        if (modelled) {
            best = {areas[cur].m_points[i].x, areas[cur].m_points[i].y, &areas[cur]};
            have = true;
            auto consider = [&](const Area& node, size_t from_index) {
                for (size_t j = from_index; j < node.m_points.size(); ++j) {
                    uint32_t vx = node.m_points[j].x, vy = node.m_points[j].y;
                    if (TileDistance(vx, vy, from) < TileDistance(best.x, best.y, from) && SwimVertexOk(vx, vy)) best = {vx, vy, &node};
                }
            };
            consider(areas[cur], i + 1);
            for (size_t other = cur + 1; other < areas.size(); ++other)
                if (!areas[other].m_shut) consider(areas[other], 0);
        }
    }
    float* result = g_findSwim(x, y, z);
    ++g_swim2.calls;
    if (!first) {
        ++g_swim2.branch[2];
        Expect(g_swim2, "no flag-0 area -> null", result == nullptr, nullptr);
        return result;
    }
    if (!have) {
        ++g_swim2.skipped;
        return result;
    }
    ++g_swim2.branch[0];
    float vertex[3];
    TileWorld(best.x, best.y, vertex, false);
    Expect(g_swim2, "vertex point", result && result[0] == vertex[0] && result[1] == vertex[1] && result[2] == vertex[2], nullptr);
    uint32_t tx = 0, ty = 0;
    bool found = result && TileInRect(best, result + 3, true, tx, ty);
    Expect(g_swim2, "tile inside the vertex node's rectangle", found, nullptr);
    if (found) Expect(g_swim2, "tile is open water", WaterOk(tx, ty, 1) || WaterOk(tx, ty, 2), nullptr);
    return result;
}

// Force run: RepeatAction with lent MAX_* values, and the two finders from a few positions (the tile draws are random: several
// calls each).
void ForceRepeatAndFinders(object::Guest* guest)
{
    auto def = Def(guest);
    std::array<uint8_t, kGuestSize> snapshot;
    std::copy_n(reinterpret_cast<const uint8_t*>(guest), snapshot.size(), snapshot.begin());
    const std::vector<int> saved = def->m_Max;
    for (int max : {1, 4, 100})
        for (int desire = 0; desire < 26; ++desire) {
            std::fill(def->m_Max.begin(), def->m_Max.end(), max);
            for (float value : {0.0f, 0.3f, 0.99f, 1.0f}) {
                guest->m_RepeatAction = 7;
                SetRepeatAction(guest, desire, value);
            }
        }
    def->m_Max = saved;
    for (float position : {0.0f, 500.0f, 2000.0f, 4000.0f}) {
        guest->m_WorldXYZ[0] = position;
        guest->m_WorldXYZ[1] = position * 1.5f;
        for (int i = 0; i < 5; ++i) {
            FindSunbathePoint(guest);
            FindSwimPoint(position, position * 1.5f, 0.0f);
        }
    }
    std::copy(snapshot.begin(), snapshot.end(), reinterpret_cast<uint8_t*>(guest));
}

void ForceOpinionHelpers(object::Guest* guest)
{
    Building* queued = FindBuilding(true);
    if (!queued) {
        OBL_LOG("autotest: FAILED opinion-helpers-force needs a building with a queue");
        return;
    }
    std::array<uint8_t, kGuestSize> snapshot;
    std::copy_n(reinterpret_cast<const uint8_t*>(guest), snapshot.size(), snapshot.begin());
    const float big = 1e9f;
    auto setBuilding = [&](Building* building) { guest->m_DestObjectID = building->m_id; };
    setBuilding(queued);
    guest->m_desired_commod = 0;
    // queue: long / short / between the limits
    Forced(guest, {{{&guest->m_Tolerance[2], -2.0f}, {&guest->m_Tolerance[3], -1.0f}}}, [&] { AlterQueueOpinion(guest); });
    Forced(guest, {{{&guest->m_Tolerance[2], big}, {&guest->m_Tolerance[3], big}}}, [&] { AlterQueueOpinion(guest); });
    Forced(guest, {{{&guest->m_Tolerance[2], -big}, {&guest->m_Tolerance[3], big}}}, [&] { AlterQueueOpinion(guest); });
    // a building without a queue is always "short"
    if (Building* plain = FindBuilding(false)) {
        setBuilding(plain);
        Forced(guest, {{{&guest->m_Tolerance[2], 0.5f}, {&guest->m_Tolerance[3], 3.0f}}}, [&] { AlterQueueOpinion(guest); });
        setBuilding(queued);
    }
    // safety: bad (not staying / staying here), above the upper limit, between the limits
    Forced(guest, {{{&guest->m_Tolerance[12], 5.0f}, {&guest->m_Tolerance[13], 6.0f}}}, [&] { AlterSafetyOpinion(guest); });
    Forced(guest, {{{&guest->m_Tolerance[12], 5.0f}, {&guest->m_Tolerance[13], 6.0f}}}, [&] {
        guest->m_StayingAt = guest->m_DestObjectID;
        AlterSafetyOpinion(guest);
    });
    Forced(guest, {{{&guest->m_Tolerance[12], -5.0f}, {&guest->m_Tolerance[13], -4.0f}}}, [&] { AlterSafetyOpinion(guest); });
    Forced(guest, {{{&guest->m_Tolerance[12], -5.0f}, {&guest->m_Tolerance[13], 5.0f}}}, [&] { AlterSafetyOpinion(guest); });
    // price: cheap / not cheap, dear / not dear
    Forced(guest, {{{&guest->m_Tolerance[6], big}, {nullptr, 0.0f}}}, [&] { UpdatePriceScore(guest); });
    Forced(guest, {{{&guest->m_Tolerance[6], -big}, {nullptr, 0.0f}}}, [&] { UpdatePriceScore(guest); });
    // The purchase record only exists while a purchase is being processed (it holds the price paid), so lend a synthetic one.
    void* savedMessage = guest->m_currentMessage;
    static messages::Message record{};
    record.floatData = 5.0f;
    guest->m_currentMessage = &record;
    Forced(guest, {{{&guest->m_Tolerance[7], -big}, {nullptr, 0.0f}}}, [&] { AlterPriceOpinion(guest); });
    Forced(guest, {{{&guest->m_Tolerance[7], big}, {nullptr, 0.0f}}}, [&] { AlterPriceOpinion(guest); });
    guest->m_currentMessage = savedMessage;
    ForcePurchaseAndSpot(guest, queued);
    ForceLifecycle(guest, queued);
    ForceSwimming(guest);
    ForceBadGuts(guest);
    ForceFight(guest);
    ForceVandal(guest);
    ForceThoughts(guest, queued);
    ForceWalkTargets(guest, queued);
    ForcePool(guest, queued);
    ForceWalkRoutes(guest, queued);
    ForceLookups(guest);
    ForceRepeatAndFinders(guest);
    object::ForceGuestAppearance();
    object::ForceGuestPlacement();
    object::ForceGuestSelection();
    object::ForceGuestVariableText();
    void* liveSprite = guest->m_sprite; // ForcePool replaced the one in the snapshot
    std::copy(snapshot.begin(), snapshot.end(), reinterpret_cast<uint8_t*>(guest));
    guest->m_sprite = liveSprite;
}
} // namespace

bool RegisterGuestDesireTrace()
{
    messages::message_facade::g_postProbe = &PostMessage;
    messages::message_facade::g_postDelayedProbe = &PostDelayed;
    weather::runtime::g_scheduleProbe = &UpdateSchedule;
    g_updateDesires = std::exchange(object::g_guestSeams.updateDesires, &UpdateDesires);
    g_updateOpinions = std::exchange(object::g_guestSeams.updateOpinions, &UpdateOpinions);
    g_alterQueue = std::exchange(object::g_guestSeams.alterQueueOpinion, &AlterQueueOpinion);
    g_alterSafety = std::exchange(object::g_guestSeams.alterSafetyOpinion, &AlterSafetyOpinion);
    g_updatePriceScore = std::exchange(object::g_guestSeams.updatePriceScore, &UpdatePriceScore);
    g_alterPrice = std::exchange(object::g_guestSeams.alterPriceOpinion, &AlterPriceOpinion);
    g_updatePurchase = std::exchange(object::g_guestSeams.updatePurchaseOpinion, &UpdatePurchaseOpinion);
    g_doSpot = std::exchange(object::g_guestSeams.doSpotOpinion, &ReactToSpottedEvent);
    g_checkBedTime = std::exchange(object::g_guestSeams.checkBedTime, &CheckBedTime);
    g_checkLeave = std::exchange(object::g_guestSeams.checkLeaveResort, &CheckLeaveResort);
    g_updateDrowning = std::exchange(object::g_guestSeams.updateDrowning, &UpdateDrowning);
    g_decideSwim = std::exchange(object::g_guestSeams.decideSwimAction, &DecideSwimAction);
    g_startBadGuts = std::exchange(object::g_guestSeams.startBadGuts, &StartStomachIllness);
    g_lookForVandal = std::exchange(object::g_guestSeams.lookForVandalItem, &LookForVandalItem);
    g_vandaliseBin = std::exchange(object::g_guestSeams.vandaliseBin, &VandaliseBin);
    g_findFightPoint = std::exchange(object::g_guestSeams.findFightPoint, &FindFightPoint);
    g_findFightPerson = std::exchange(object::g_guestSeams.findFightPerson, &FindFightPerson);
    g_sendFightMsgs = std::exchange(object::g_guestSeams.sendOutFightingMsgs, &TellNearbyOfFight);
    g_setRepeat = std::exchange(object::g_guestSeams.setRepeatAction, &SetRepeatAction);
    g_findSunbathe = std::exchange(object::g_guestSeams.findSunbathePoint, &FindSunbathePoint);
    g_findSwim = std::exchange(object::g_guestSeams.findSwimPoint, &FindSwimPoint);
    g_applyWeather = std::exchange(object::g_guestSeams.applyWeatherModifiers, &ApplyWeather);
    return true;
}

bool ExecuteGuestDesireCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name == "guest-queue-probe") {
        // Disposable L0 fixture: reload the world after this command, before natural play.
        auto* building = FindBuilding(true);
        auto* queue = building ? static_cast<object::BuildingQueue*>(building->m_queue) : nullptr;
        if (!queue || !GuestAt(2) || !queue->m_qlQueue.empty()) {
            OBL_LOG("autotest: FAILED guest-queue-probe needs three fresh guests and an empty queue");
            return true;
        }
        auto* handler = messages::message_facade::Current();
        auto send = [&](object::Guest* guest, int type, int data) {
            messages::Message message{};
            message.sender = building->m_id;
            message.receiver = guest->m_id;
            message.type = type;
            message.data = data;
            messages::message_facade::Post(handler, &message);
        };
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            auto* guest = GuestAt(i);
            guest->m_state = 14; // REACHED_DESIRE, without requesting another purchase.
            guest->m_DestObjectID = building->m_id;
            guest->m_desired_commod = 0;
            ok &= object::BuildingQueueEnqueue(queue, guest->m_id, 0, true);
        }
        messages::Handler delivery;
        g_queueDelivery = &delivery;
        uint32_t head = 0;
        int32_t commodity = 0;
        ok &= object::BuildingQueueDequeue(queue, &head, &commodity);
        g_queueDelivery = nullptr;
        ok &= head == GuestAt(0)->m_id && delivery.delayed.size() == 2;
        const float now = game::GetWorld()->m_gameTime;
        for (const auto& message : delivery.delayed) {
            ok &= message.receiver == static_cast<int>(GuestAt(1)->m_id) || message.receiver == static_cast<int>(GuestAt(2)->m_id);
            ok &= Close(message.time, now + 3.0f);
            OBL_LOG("autotest: guest-queue-scheduled guest=%d x=%d y=%d delay=%g", message.receiver, message.flags, message.data,
                    message.time - now);
        }
        // The first remaining guest is admitted before the pending move-up arrives.
        send(GuestAt(1), MT_IM_READY, building->m_id);
        ok &= GuestAt(1)->m_state == 27;
        send(GuestAt(1), MT_STATECHANGE, 15); // same WAITING_DESIRE state used while being served inside.
        GuestAt(1)->m_insideBuilding = true;
        std::array<std::array<float, 2>, 2> before{};
        for (int i = 0; i < 2; ++i) std::copy_n(GuestAt(i + 1)->m_DestXYZ, 2, before[i].begin());
        game::GetWorld()->m_gameTime = now + 2.98f;
        messages::message_facade::Update(delivery);
        ok &= delivery.delayed.size() == 2;
        for (int i = 0; i < 2; ++i) ok &= std::equal(before[i].begin(), before[i].end(), GuestAt(i + 1)->m_DestXYZ);
        const auto pending = delivery.delayed;
        game::GetWorld()->m_gameTime = now + 3.02f;
        messages::message_facade::Update(delivery);
        game::GetWorld()->m_gameTime = now;
        ok &= delivery.delayed.empty();
        for (const auto& message : pending) {
            auto* guest = message.receiver == static_cast<int>(GuestAt(1)->m_id) ? GuestAt(1) : GuestAt(2);
            const bool inside = guest == GuestAt(1);
            const bool changed = guest->m_DestXYZ[0] != before[inside ? 0 : 1][0] || guest->m_DestXYZ[1] != before[inside ? 0 : 1][1];
            const bool matches = guest->m_DestXYZ[0] == message.flags && guest->m_DestXYZ[1] == message.data;
            ok &= inside ? !changed && guest->m_insideBuilding : changed && matches;
            OBL_LOG("autotest: guest-queue-sample guest=%u inside=%d state=%d changed=%d target-x=%g target-y=%g expected-x=%d "
                    "expected-y=%d",
                    guest->m_id, inside, guest->m_state, changed, guest->m_DestXYZ[0], guest->m_DestXYZ[1], message.flags, message.data);
        }
        OBL_LOG("autotest: %s guest-queue-probe", ok ? "PASS" : "FAILED");
    } else if (name == "desire-report") {
        OBL_LOG("autotest: desire-model calls=%u mismatches=%u atCap=%u maxError=%g %s", g_update.calls, g_update.mismatches,
                g_update.atCap, g_update.maxError, g_update.firstBad.c_str());
        OBL_LOG("autotest: %s desire-model", g_update.calls && !g_update.mismatches ? "PASS" : "FAILED");
        g_update = {};
    } else if (name == "opinion-report") {
        bool ok = g_opinions.calls > g_opinions.skipped && !g_opinions.happinessBad && !g_opinions.beautyBad && !g_opinions.cleanBad &&
                g_opinions.beautyChecked;
        OBL_LOG("autotest: opinion-model calls=%u skipped=%u happinessBad=%u (maxError %g) beauty %u/%u bad, "
                "cleanliness %u/%u bad",
                g_opinions.calls, g_opinions.skipped, g_opinions.happinessBad, g_opinions.maxHappinessError, g_opinions.beautyBad,
                g_opinions.beautyChecked, g_opinions.cleanBad, g_opinions.cleanChecked);
        OBL_LOG("autotest: %s opinion-model", ok ? "PASS" : "FAILED");
        g_opinions = {};
    } else if (name == "opinion-helpers-report") {
        if (args.size() == 1 && args[0] == "queue") {
            OBL_LOG("autotest: queue-opinion-coverage calls=%u short=%u normal=%u long=%u bad=%u", g_queue.calls, g_queue.branch[0],
                    g_queue.branch[1], g_queue.branch[2], g_queue.bad);
            return true;
        }
        ReportHelper("queue-opinion", g_queue, "short/ok/long");
        ReportHelper("safety-opinion", g_safety, "good/ok/bad");
        ReportHelper("price-score", g_price, "normal/cheap/-");
        ReportHelper("purchase-opinion", g_purchase, "missed/normal/stench");
        std::ostringstream kinds;
        for (unsigned k : g_spotKind) kinds << k << " ";
        OBL_LOG("autotest: spot-opinion events by kind 0..10,other: %s", kinds.str().c_str());
        ReportHelper("spot-opinion", g_spot, "-/-/-", false);
        ReportHelper("bed-time", g_bed, "idle/night/last-night", false);
        ReportHelper("leave-resort", g_leave, "unhappy/souvenirs/other", false);
        ReportHelper("drowning", g_drown, "no-lifeguard/alive/dead", false);
        ReportHelper("swim-action", g_swim, "near/far/-", false);
        ReportHelper("bad-guts", g_badGuts, "healthy/ill/-", false);
        ReportHelper("fight-point", g_fightPoint, "no-desire/building/fallback", false);
        ReportHelper("fight-person", g_fightPerson, "none/found/-", false);
        ReportHelper("fight-msgs", g_fightMsgs, "alone/crowd/-", false);
        ReportHelper("vandal-look", g_vandalLook, "bin/bench-or-lamp/none", false);
        ReportHelper("vandalise-bin", g_vandalBin, "no-object/animation/unrecognized-state", false);
        ReportHelper("repeat-action", g_setRepeatStats, "keeps/MAX*value/-", false);
        ReportHelper("sunbathe-point", g_sunbathe, "tile/fallback/-", false);
        ReportHelper("swim-point", g_swim2, "found/-/no-area", false);
        std::fill(std::begin(g_spotKind), std::end(g_spotKind), 0u);
        ReportHelper("price-opinion", g_expensive, "normal/dear/-");
    } else if (name == "opinion-helpers" && args.size() == 1 && (args[0] == "on" || args[0] == "off")) {
        g_helperChecks = args[0] == "on";
    } else if (name == "opinion-helpers-force") {
        // opinion-helpers-force [first [count]]
        int first = args.empty() ? 0 : std::stoi(args[0]), count = args.size() > 1 ? std::stoi(args[1]) : 1;
        if (!GuestAt(first)) OBL_LOG("autotest: FAILED opinion-helpers-force needs a guest");
        bool checks = std::exchange(g_helperChecks, true);
        for (int i = first; i < first + count && GuestAt(i); ++i) ForceOpinionHelpers(GuestAt(i));
        g_helperChecks = checks;
    } else if (name == "desire-dump") {
        object::Guest* guest = GuestAt(args.empty() ? 0 : std::stoi(args[0]));
        world_data::World* world = World();
        if (!guest || !world) {
            OBL_LOG("autotest: FAILED desire-dump needs a guest");
            return true;
        }
        std::ostringstream out;
        for (int i = 0; i < kDesireCount; ++i) out << " " << guest->m_desires[i];
        OBL_LOG("autotest: desires id=%d dt=%g state=0x%x drunk=%g foodill=%g:%s", guest->m_id, world->m_gameDeltaTime, guest->m_state,
                guest->m_drunk, guest->m_foodIll, out.str().c_str());
        std::ostringstream rates;
        for (auto [index, person] : kGrowth) rates << " " << index << ":" << guest->m_person[person];
        OBL_LOG("autotest: growth rates%s sleep=%g", rates.str().c_str(), guest->m_person[12]);
    } else if (name == "weather-insert" && args.size() >= 2) {
        // weather-insert <kind 0..5> <SoftDrinks modifier> [hours]: a schedule entry that is already due, so the next
        // weather::runtime::UpdateSchedule activates it (the scenario 1 fixture has an empty schedule); with `hours` it falls that many game
        // hours from now (negative = in the past, the default is -0.1; past midnight it moves to the next day). Only after the last save of a
        // script: the save keeps m_eventCount but not the entries, so a load rebuilds fewer entries than counted and the weather load, then
        // weather::runtime::UpdateSchedule, walk past the deque (the game crashes the same way; the game inserts only from STATE_WORLD_STARTUP).
        weather::Weather* weather = game::GetWeather();
        world_data::World* world = World();
        if (!weather || !world) return true;
        struct Entry {
            int kind;
            float hour;
            int day;
            float modifiers[kDesireCount];
        } entry{};
        entry.kind = std::stoi(args[0]);
        const float offset = args.size() > 2 ? std::stof(args[2]) : -0.1f;
        entry.hour = std::max(world->m_gameDayTime + offset, 0.0f);
        entry.day = world->m_gameDay;
        if (entry.hour >= 24.0f) {
            entry.hour -= 24.0f;
            ++entry.day;
        }
        entry.modifiers[9] = std::stof(args[1]);
        weather::WeatherEntry stored{};
        stored.m_kind = entry.kind;
        stored.m_hour = entry.hour;
        stored.m_day = entry.day;
        for (int i = 0; i < kDesireCount; ++i) stored.m_desireModifiers[i] = entry.modifiers[i];
        weather::runtime::InsertEntry(weather, stored);
        OBL_LOG("autotest: weather-insert kind=%d softdrinks=%g", entry.kind, entry.modifiers[9]);
    } else if (name == "power-state") {
        // World power totals (demand of all consumers, installed generation, output of open
        // generators, demand of consumers not shed) recomputed from the building list, plus shed buildings.
        world_data::World* world = World();
        object::Manager* manager = game::GetObjectManager();
        if (!world || !manager) return true;
        float demandAll = 0, capacity = 0, output = 0, demand = 0;
        std::ostringstream out;
        int count = static_cast<int>(manager->m_buildings.size());
        for (int i = 0; i < count; ++i) {
            auto building = static_cast<Building*>(manager->m_buildings[i]);
            auto def = Def(building);
            float power = static_cast<float>(def->m_Power);
            bool shed = building->m_closedForPower != 0, closed = building->m_OpenStatus != 0;
            if (power >= 0) {
                demandAll += power;
                if (!shed) demand += power;
            } else {
                capacity -= power; // (build/demolish jobs zero the contribution; not modelled here)
                if (!closed) output -= power;
            }
            if (power != 0 && (shed || closed || power < 0))
                out << " " << def->m_key << "(power=" << power << " shed=" << shed << " closed=" << closed << ")";
        }
        OBL_LOG("autotest: power world demandAll=%g capacity=%g output=%g demand=%g | recomputed recDemandAll=%g "
                "recCapacity=%g recOutput=%g recDemand=%g | buildings=%d%s",
                world->m_powerUse, world->m_maxPower, world->m_currentPower, world->m_powerDemand, demandAll, capacity, output, demand,
                count, out.str().c_str());
    } else if (name == "power-generators" && args.size() == 1) {
        // The player's Close/Open on every POWERGEN (what the building dialog does): output drops to 0 while
        // the installed capacity stays, which is what makes the power shedding shed load.
        object::Manager* manager = game::GetObjectManager();
        if (!manager) return true;
        int changed = 0;
        for (int i = 0; i < static_cast<int>(manager->m_buildings.size()); ++i) {
            auto building = static_cast<Building*>(manager->m_buildings[i]);
            auto def = Def(building);
            if (def->m_Power >= 0) continue;
            (args[0] == "close" ? object::BuildingClose : object::BuildingOpen)(building);
            ++changed;
        }
        OBL_LOG("autotest: power-generators %s (%d)", args[0].c_str(), changed);
    } else if (name == "weather-state") {
        const weather::Weather* weather = game::GetWeather();
        world_data::World* world = World();
        if (!weather || !world) return true;
        std::ostringstream out;
        for (int i = 0; i < weather->m_eventCount; ++i) {
            const weather::WeatherEntry& entry = *weather->m_schedule[i];
            out << " (kind=" << entry.m_kind << " day=" << entry.m_day << " hour=" << entry.m_hour << " mods"
                << Modifiers(entry.m_desireModifiers) << ")";
        }
        // `cur`/`events` are separate report keys: the entries' own `kind=` values overwrite the first one for expect-last.
        OBL_LOG("autotest: weather now kind=%u next=%d/%d day=%d hour=%g cur=%u events=%d%s", weather->m_currentState, weather->m_nextEvent,
                weather->m_eventCount, world->m_gameDay, world->m_gameDayTime, weather->m_currentState, weather->m_eventCount,
                out.str().c_str());
    } else {
        return false;
    }
    return true;
}
} // namespace obl::test
