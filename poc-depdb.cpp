#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import mtime;
import print;
import tora;

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing file");

  auto pom = cavan::read_pom(file);
  cavan::eff_pom(pom);

  const auto repo = meeql::repo_dir();

  for (auto &[d, _]: pom->deps) {
    auto grp = (*d.grp).cstr();
    for (auto &c : grp) if (c == '.') c = '/';

    auto file = repo + "/" + grp + "/" + d.art + "/" + d.ver + "/" + d.art + "-" + d.ver + ".jar";
    if (mtime::of((*file).cstr().begin()) == 0) errln("missing jar: ", file);

    // TODO: Check for "reactor" deps
    // TODO: Dive transitively
  }
} catch (...) {
  return 13;
}
