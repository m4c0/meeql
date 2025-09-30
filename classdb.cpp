#pragma leco tool
import jojo;
import jute;
import meeql;
import mtime;
import popen;
import print;
import tora;

static void help() {
  put(R"(
Available commands (in alphabetical order):
  * jar <fqn>      - Finds a jar file for a given class
  * javap <fqn>    - Outputs a URI in a format that can be passed to javap
  * help           - This command, list all commands
  * imports <java> - Lists JAR files found for each of a Java source file
                     import
  * load           - Recreates the database from the ~/.m2/repository folder
  * search <term>  - Searches the class table for <term>. Outputs <fqn> for
                     the first 1000 results.
Where:
  * fqn   - fully-qualified class name in forward-slash notation.
            Example: com/mycompany/Test$Inner
  * java  - any Java source file.
            Example: src/main/java/com/mycompany/Test.java
  * term  - any search term compatible with Sqlite's FTS5 notation.
            When in doubt, use words separated with space.
            Example: com mycompany Test Inner
)");
}

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

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

  p::proc p { "unzip", "-qq", "-l", jar_path.begin(), "*.class" };
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

static void add_jar(tora::stmt * stmt, jute::view pom_path) {
  auto name = pom_path.rsplit('/').after.rsplit('.').before;
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  if (!mtime::of(jar_path.cstr().begin())) return;

  stmt->reset();
  stmt->bind(1, jar_path.cstr());
  stmt->bind(2, name);
  stmt->step();
}

static void load(tora::db & db) {
  db.exec(R"(
    DROP TABLE IF EXISTS class;
    DROP TABLE IF EXISTS class_fts;
    DROP TABLE IF EXISTS jar;

    CREATE TABLE jar (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      path  TEXT NOT NULL UNIQUE,
      name  TEXT NOT NULL
    ) STRICT;

    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      jar   TEXT NOT NULL,
      fqn   TEXT NOT NULL
    ) STRICT;
  )");

  db.exec("BEGIN TRANSACTION");
  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO jar (path, name)
    VALUES (?, ?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));
  db.exec("END TRANSACTION");

  db.exec("BEGIN TRANSACTION");
  stmt = db.prepare("INSERT INTO class (jar, fqn) VALUES (?, ?)");
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
  auto stmt = db.prepare(R"(
    SELECT * FROM class_fts
    WHERE fqn MATCH ?
    ORDER BY rank
    LIMIT 1000
  )");
  stmt.bind(1, term);
  while (stmt.step()) putln(stmt.column_view(0));
}

static void jar(tora::db & db, jute::view term) {
  if (term == "") die("missing search term");

  auto stmt = db.prepare(R"(
    SELECT j.path
    FROM class AS c
    JOIN jar AS j ON j.name = c.jar
    WHERE c.fqn = ?
  )");
  stmt.bind(1, term);
  if (!stmt.step()) die("class or jar not found: [", term, "]");
  putln(stmt.column_view(0));
}

static void javap(tora::db & db, jute::view term) {
  if (term == "") die("missing search term");

  // TODO: inner classes
  // TODO: copy this to others?
  auto t = term.cstr();
  for (auto & c : t) if (c == '.') c = '/';
  term = t;

  auto stmt = db.prepare(R"(
    SELECT j.path
    FROM class AS c
    JOIN jar AS j ON j.name = c.jar
    WHERE c.fqn = ?
  )");
  stmt.bind(1, term);
  if (!stmt.step()) die("class or jar not found: [", term, "]");
  auto jar = stmt.column_view(0);
  putln("jar:file://", jar, "!/", term, ".class");
}

static void imports(tora::db & db, jute::view term) {
  if (term == "") die("missing file name");

  db.exec(R"(
    CREATE TEMPORARY TABLE search (
      fqn TEXT NOT NULL
    ) STRICT;
  )");
  auto stmt = db.prepare("INSERT INTO search VALUES (?)");

  jojo::readlines(term, [&](auto line) {
    auto [l, r] = line.split(' ');
    if (l != "import") return;
  
    auto [l1, r1] = r.split(' ');
    auto stc = l1 == "static";
    auto cls = stc ? r1 : r;
    cls = cls.rsplit(';').before;

    if (cls.starts_with("java")) return;
  
    auto cls_s = cls.cstr();
    for (auto & c : cls_s) if (c == '.') c = '/';
    if (stc) {
      const auto cc = cls_s.begin();
      for (auto i = cls_s.size() - 1; i > 0; i--) {
        if (cc[i] != '/') continue;
        if (cc[i + 1] >= 'A' && cc[i + 1] <= 'Z') {
          cc[i] = '$';
        } else {
          cc[i] = 0;
        }
        break;
      }
    }
    stmt.bind(1, jute::view::unsafe(cls_s.begin()));
    stmt.step();
    stmt.reset();
  });

  stmt = db.prepare(R"(
    SELECT DISTINCT j.path
    FROM class AS c
    JOIN jar AS j ON j.name = c.jar
    JOIN search AS s ON s.fqn = c.fqn
    GROUP BY s.fqn
    HAVING j.id = MAX(j.id)
  )");
  while (stmt.step()) putln(stmt.column_view(0));
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  auto file = meeql::m2_dir() + "/meeql-classdb.sqlite\0";
  tora::db db { file.cstr().begin() };
  meeql::spellfix_init(db);

  auto cmd = shift();
  auto param = shift();
       if (cmd == "")         help();
  else if (cmd == "imports")  imports(db, param);
  else if (cmd == "jar")      jar(db, param);
  else if (cmd == "javap")    javap(db, param);
  else if (cmd == "help")     help();
  else if (cmd == "load")     load(db);
  else if (cmd == "search")   search(db, param);
  else die("Unknown command: ", cmd);
} catch (...) {
  return 1;
}
