#pragma once

#include "context.h"
#include <nlohmann/json.hpp>

namespace ratus_rec {
namespace rec {

nlohmann::json recommend(Context& ctx);

}
}
