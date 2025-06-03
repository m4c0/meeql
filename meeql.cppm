#pragma leco add_impl spellfix
export module meeql;
import jute;
import hai;
import mtime;
import pprent;
import sysstd;
import tora;

extern "C" int sqlite3_spellfix_init(void * db, char ** err, const void * api);

namespace meeql {
  export inline jute::heap home_dir() { return jute::view::unsafe(sysstd::env("HOME")); }
  export inline jute::heap m2_dir() { return home_dir() + jute::view { "/.m2" }; }
  export inline jute::heap repo_dir() { return m2_dir() + jute::view { "/repository" }; }

  static void recurse(jute::view path, hai::fn<void, jute::view> fn) {
    auto r_dir = repo_dir();
    auto full_path = r_dir + path + "\0";
    auto marker = r_dir + path + "/_remote.repositories\0";
    if (mtime::of(marker.begin())) {
      auto [l_ver, ver] = path.rsplit('/');
      auto [l_art, art] = l_ver.rsplit('/');

      auto fname = r_dir + path + "/" + art + "-" + ver + ".pom";
      auto ftime = mtime::of((*fname).cstr().begin());
      // Some dependencies might not have a pom for reasons
      if (ftime == 0) return;
      return fn(*fname);
    }
    for (auto f : pprent::list(full_path.begin())) {
      if (f[0] == '.') continue;
      auto child = jute::heap { path } + "/" + jute::view::unsafe(f);
      recurse(*child, fn);
    }
  }
  export void recurse_repo_dir(hai::fn<void, jute::view> fn) { recurse("", fn); }

  struct spellfix_error { const char * msg; };
  export void spellfix_init(tora::db & db) {
    char * err {};
    sqlite3_spellfix_init(db.handle(), &err, nullptr);
    if (err) throw spellfix_error { err };
  }
}
