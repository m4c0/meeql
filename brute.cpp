#pragma leco tool

import cavan;
import jojo;
import jute;
import meeql;
import mtime;
import pprent;
import popen;
import print;
import tora;

using namespace jute::literals;

static void help() {
  errln(R"(
Available commands (in alphabetical order):
  * help - This command, list all commands
  * load - Loads the current project dependencies into the database.
)");
}

static void load(tora::stmt & stmt, jute::view path) {
  if (!mtime::of((path + "/pom.xml").cstr().begin())) return;
  for (auto d : pprent::list(path.cstr().begin())) {
    if (d[0] == '.') continue;

    auto dp = (path + "/" + jute::view::unsafe(d)).cstr();
    load(stmt, dp);

    auto deps = (dp + "/target/meeql-brute"_s).cstr();
    if (!mtime::of(deps.begin())) continue;

    auto src = jojo::read_cstr(deps);
    jute::view rest = src;
    while (rest.size()) {
      auto [l, r] = rest.split('\n');
      rest = r;
      if (!l.starts_with("   ")) continue;

      auto [grp, rg] = l.trim().split(':');
      auto [art, ra] = rg.split(':');
      auto [typ, rt] = ra.split(':');
      auto [ver, sc] = rt.split(':');
      auto jar = cavan::path_of(grp, art, ver, "jar");

      stmt.reset();
      stmt.bind(1, jar);
      stmt.bind(2, grp);
      stmt.bind(3, art);
      stmt.bind(4, ver);
      stmt.step();
    }
  }
}
static void load(tora::db & db) {
  db.exec(R"(
    CREATE TABLE IF NOT EXISTS dep (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      path  TEXT NOT NULL UNIQUE,
      grp   TEXT NOT NULL,
      art   TEXT NOT NULL,
      ver   TEXT NOT NULL,
      UNIQUE (grp, art)
    ) STRICT;
  )");
  auto stmt = db.prepare(R"(
    INSERT INTO dep (path, grp, art, ver) VALUES (?, ?, ?, ?)
    ON CONFLICT (grp, art) DO
    UPDATE SET ver=excluded.ver, path=excluded.path
  )");

  p::proc p {
    "mvn",
    "dependency:collect",
    "-DoutputFile=target/meeql-brute",
  };
  bool erred = false;
  while (p.gets()) {}
  while (p.gets_err()) {
    erred = true;
    putln(p.last_line_read());
  }
  if (erred) die("mvn emitted some error");

  load(stmt, ".");
}

int main(int argc, char ** argv) {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  auto file = meeql::m2_dir() + "/meeql-brute.sqlite\0";
  tora::db db { file.begin() };

  auto cmd = shift();
       if (cmd == "")     help();
  else if (cmd == "load") load(db);
}
