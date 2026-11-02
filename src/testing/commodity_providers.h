#pragma once

#include <string>
#include <vector>

namespace obl::test {

// Test-only checks of the commodity provider layer: a model of the commodity_providers::FindProvider rules
// that judges every answer of the game (observer of game/commodity_providers), the slot score wrapper and the forced fixture behind
// `provider-force` / `provider-report` (tests/commodity_providers.txt).
bool RegisterCommodityProviderTrace();
bool ExecuteCommodityProviderCommand(const std::string& name, const std::vector<std::string>& args);

} // namespace obl::test
