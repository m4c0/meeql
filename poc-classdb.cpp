#pragma leco tool
import jute;
import meeql;
import mtime;
import popen;
import print;
import tora;

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static auto d(jute::view str) { return str.cstr(); }

static constexpr bool is_lambda(jute::view cls) {
  auto [a, b] = cls.rsplit('$');
  if (a == "") return false;
  if (b == "") return true; // No idea if this case exists
  for (auto c : b) if (c < '0' || c > '9') return false;
  return true;
}
static_assert(!is_lambda("Abc"));
static_assert(!is_lambda("Abc$Def"));
static_assert(!is_lambda("Abc$Def123"));
static_assert(is_lambda("Abc$123"));
static_assert(is_lambda("Abc$1"));

static void unjar(tora::stmt * stmt, jute::view pom_path) {
  auto art_name = pom_path.rsplit('/').after.rsplit('.').before;
  auto jar_path = (pom_path.rsplit('.').before + ".jar").cstr();
  if (!mtime::of(jar_path.begin())) return;

  auto unzip = d("unzip");
  auto qq    = d("-qq");
  auto l     = d("-l");
  auto cls   = d("*.class");
  char * args[] { unzip.begin(), qq.begin(), l.begin(), jar_path.begin(), cls.begin(), 0 };
  p::proc p { args };
  while (p.gets()) {
    auto fqn = jute::view::unsafe(p.last_line_read())
      .rsplit(' ').after
      .rsplit('.').before;

    if (is_lambda(fqn)) continue;

    stmt->bind(1, art_name);
    stmt->bind(2, fqn);
    stmt->step();
    stmt->reset();
  }
}

static void load(tora::db & db) {
  db.exec(R"(
    DROP TABLE IF EXISTS class;
    DROP TABLE IF EXISTS class_fts;

    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      jar   TEXT NOT NULL,
      fqn   TEXT NOT NULL
    ) STRICT;
  )");
  db.exec("BEGIN TRANSACTION");
  auto stmt = db.prepare("INSERT INTO class (jar, fqn) VALUES (?, ?)");
  meeql::recurse_repo_dir(curry(unjar, &stmt));
  db.exec("END TRANSACTION");

  stmt = db.prepare("SELECT COUNT(*) FROM class");
  stmt.step();
  putln("got ", stmt.column_int(0), " classes");

  db.exec(R"(
    CREATE VIRTUAL TABLE class_fts USING fts5 (fqn);
    INSERT INTO class_fts SELECT DISTINCT(fqn) FROM class;
  )");
  putln("indexing done");
}

static void search(tora::db & db, jute::view term) {
  if (term == "") die("missing search term");
  auto stmt = db.prepare("SELECT * FROM class_fts WHERE fqn MATCH ? ORDER BY rank");
  stmt.bind(1, term);
  while (stmt.step()) putln(stmt.column_view(0));
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  tora::db db { "out/classdb.sqlite" };
  meeql::spellfix_init(db);

  auto cmd = shift();
  auto param = shift();
  if (cmd == "") die("missing command");
  else if (cmd == "load") load(db);
  else if (cmd == "search") search(db, param);
  else die("Unknown command: ", cmd);
} catch (...) {
  return 1;
}
