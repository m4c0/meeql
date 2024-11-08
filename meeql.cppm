#pragma leco add_impl meeql_eff
module;
#include <stdlib.h>

export module meeql;
export import tora;

import jute;

static const auto home_dir = jute::view::unsafe(getenv("HOME"));

namespace meeql {
  export auto db() {
    auto file = (home_dir + "/.m2/meeql").cstr();
    return tora::db { file.begin() };
  }

  export int eff(tora::db & db, jute::view group_id, jute::view artefact_id, jute::view version, int depth);
}
