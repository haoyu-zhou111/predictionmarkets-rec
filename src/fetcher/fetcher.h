#pragma once

#include "rec/context.h"

namespace predictionmarkets_rec {

namespace fetcher {

bool init();

void fetch_user_context(Context& ctx);

} // namespace fetcher

} // namespace predictionmarkets_rec
