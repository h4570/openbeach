#include "platform/headless_main.h"
#include "game/game/game_api.h"

#include "game/game/game_accessors.h"
#include "game/game/game_parameters.h"
#include "core/log.h"

namespace obl::platform {
namespace {
HeadlessDriver s_driver = nullptr;
} // namespace

void SetHeadlessDriver(HeadlessDriver driver)
{
    s_driver = driver;
}

void DestroyGame(game::GameParameters* parameters)
{
    game::game_api::Destroy();
    if (parameters) {
        game::game_parameters::Destruct(parameters);
        delete parameters;
    }
}

int RunHeadlessGame(void* instance)
{
    auto* parameters = new game::GameParameters{};
    game::game_parameters::Construct(parameters, "data/script/gameparameters.pis");
    game::SetWindowedMode(1);
    game::game_api::Create(instance, nullptr);
    OBL_LOG("platform-main: C++ WinMain in headless mode (no window)");
    const int code = s_driver ? s_driver() : kExitHeadlessFailed;
    DestroyGame(parameters);
    return code;
}

} // namespace obl::platform
