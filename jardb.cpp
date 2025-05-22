#pragma leco tool
import jute;
import meeql;
import mtime;
import print;
import tora;

static void help() {
  putln(R"(
Available commands (in alphabetical order):
  * mvndep <name>   - Outputs a snippet for a maven dependency
  * search <prefix> - Search jars matching a given prefix

Where:
  * name  -  The name of a maven package.
             Example: MyPackage would match com.mycompany:MyPackage
  * prefix - A text query prefix for a maven package name.
             Example: My would match com.mycompany:MyPackage
)");
}

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static void add_jar(tora::stmt * stmt, jute::view pom_path) {
  auto name = pom_path.rsplit('/').after.rsplit('.').before;
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  if (!mtime::of((*jar_path).cstr().begin())) return;

  stmt->reset();
  stmt->bind(1, *jar_path);
  stmt->bind(2, name);
  stmt->step();
}

static auto init() {
  tora::db db { ":memory:" };
  meeql::spellfix_init(db);

  db.exec(R"(
    CREATE TABLE jar (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      path  TEXT NOT NULL UNIQUE,
      name  TEXT NOT NULL
    ) STRICT;
  )");
  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO jar (path, name)
    VALUES (?, ?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));

  db.exec(R"(
    CREATE VIRTUAL TABLE jar_sfx USING spellfix1;
    INSERT INTO jar_sfx(word) SELECT name FROM jar;
  )");

  return db;
}

static void cmd_mvndep(tora::db & db, jute::view param) {
  if (param == "") die("missing mvn dep parameter");
  auto stmt = db.prepare("SELECT path FROM jar WHERE name = ?");
  stmt.bind(1, param);
  if (!stmt.step()) die("dependency not found: ", param);

  auto [ver_r, ver] = stmt.column_view(0).rsplit('/').before.rsplit('/');
  auto [path, art] = ver_r.rsplit('/');
  auto grp = path.subview(meeql::repo_dir().size() + 1).after.cstr();
  for (auto & c : grp) if (c == '/') c = '.';

  putln("    <dependency>");
  putln("      <groupId>", grp, "</groupId>");
  putln("      <artifactId>", art, "</artifactId>");
  putln("      <version>", ver, "</version>");
  putln("    </dependency>");
}

static void cmd_search(tora::db & db, jute::view param) {
  if (param == "") die("missing search parameter");
  auto stmt = db.prepare("SELECT word FROM jar_sfx WHERE word MATCH ? || '*'");
  stmt.bind(1, param);
  while (stmt.step()) putln(stmt.column_view(0));
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  auto db = init();

  auto cmd = shift();
  auto param = shift();
  if (cmd == "") help();
  else if (cmd == "mvndep") cmd_mvndep(db, param);
  else if (cmd == "search") cmd_search(db, param);
  else die("Unknown command: ", cmd);
} catch (...) {
  return 1;
}
