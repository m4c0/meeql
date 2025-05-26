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

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing file");

  auto pom = cavan::read_pom(file);
  cavan::eff_pom(pom);

  for (auto &[d, _]: pom->deps) {
    auto file = cavan::path_of(*d.grp, d.art, *d.ver, "jar");
    if (mtime::of(file.begin()) != 0) continue;

    auto mod = find_module(pom, d);
    if (mod != nullptr) continue;

    // TODO: Dive transitively
    errln("missing jar: ", file);
  }
} catch (...) {
  return 13;
}
