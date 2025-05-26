#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import mtime;
import print;
import tora;

static constexpr bool operator==(const cavan::pom & a, const cavan::dep & b) {
  return a.grp == b.grp && a.art == b.art && a.ver == b.ver;
}

static cavan::pom * find_module(const cavan::pom * p, const cavan::dep & d) {
  while (p) {
    for (auto pc : cavan::read_modules(p)) {
      if (*pc == d) return pc; 
    }
    p = p->ppom;
  }
  return nullptr;
}

[[nodiscard]] static bool check_dep(const cavan::dep & d, const cavan::pom * owner) {
  auto file = cavan::path_of(*d.grp, d.art, *d.ver, "jar");
  if (mtime::of(file.begin()) != 0) return true;

  auto mod = find_module(owner, d);
  if (mod != nullptr) return true;

  errln("missing jar: ", file);
  return false;
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing file");

  auto pom = cavan::read_pom(file);
  cavan::eff_pom(pom);

  // TODO: Dive transitively
  for (auto &[d, _]: pom->deps) {
    if (!check_dep(d, pom)) return 42;
  }
} catch (...) {
  return 13;
}
