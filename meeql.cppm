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
}
