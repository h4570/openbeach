#include "core/char16.h"
#include "feedback_manager.h"

#include "game/feedback/feedback_manager.h"
#include "game/game/game_accessors.h"
#include "game/ui/font.h"
#include "game/ui/gui_manager_types.h"
#include "game/ui/gui_types.h"
#include "game/ui/sprite.h"
#include "game/feedback/feedback_state.h"
#include "core/log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace obl::test {

bool ExecuteFeedbackCommand(const std::string& name, const std::vector<std::string>& args)
{
    if (name != "feedback-report" && name != "feedback-fixture") return false;
    layout::World* world = game::GetWorld();
    if (!world || !world->m_feedbackManager) {
        OBL_LOG("autotest: FAILED %s: no world", name.c_str());
        return true;
    }
    feedback::Manager& manager = *world->m_feedbackManager;
    if (name == "feedback-report") {
        OBL_LOG("autotest: feedback-report %s pending=%d history=%d suppressed=%d last_review=%.3f", args.empty() ? "-" : args[0].c_str(),
                static_cast<int>(manager.pending.size()), static_cast<int>(manager.history.size()),
                static_cast<int>(manager.suppressedCount), static_cast<double>(manager.lastReviewHour));
        return true;
    }

    // feedback-fixture: the GUI keeps its balloon for the first notices, so they wait; later ones find it free again.
    int count = args.size() > 0 ? std::stoi(args[0]) : 20;
    feedback::Feedback feedback(&manager);
    static const char* const kKeys[] = {"FEEDBACK_NO_CLEANERS", "FEEDBACK_TOILET", "FEEDBACK_SHOWER", "FEEDBACK_BUILDING_BROKEN"};
    ui::GuiManager* gui = game::GetGuiManager();
    for (int i = 0; i < count; ++i) {
        if (gui && i == count / 2) gui->m_balloonActive = 0; // the balloon times out half-way
        if (i % 2 == 0)
            feedback.AddKey(kKeys[i % 4], i % 3 == 0 ? -2 : i, 5);
        else
            feedback.AddText(u"The pump is broken", i, 5);
    }
    OBL_LOG("autotest: feedback-fixture added=%d pending=%d history=%d", count, static_cast<int>(manager.pending.size()),
            static_cast<int>(manager.history.size()));
    return true;
}

} // namespace obl::test
