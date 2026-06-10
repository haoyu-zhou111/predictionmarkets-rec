#pragma once

#include "rec/context.h"

namespace ratus_rec {

namespace fetcher {

bool init();

void fetch_user_context(Context& ctx);

} // namespace fetcher

} // namespace ratus_rec
