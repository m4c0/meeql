module meeql;
import cavan;
import jojo;
import print;
import sysstd;

[[nodiscard]] static auto init_db() {
  auto fname = meeql::m2_dir() + "/meeql-javac.sqlite\0";
  tora::db db { fname.begin() }; // TODO: move to .m2
  db.exec(R"(
    CREATE TABLE IF NOT EXISTS rdep (
      s_pom TEXT NOT NULL,
      t_jar TEXT NOT NULL
    ) STRICT
  )");
  return db;
}

[[nodiscard]] static auto resolve_deps(auto & fname, bool use_cache) {
  auto db = init_db();
  if (!use_cache) {
    auto stmt = db.prepare("DELETE FROM rdep WHERE s_pom = ?");
    stmt.bind(1, fname);
    stmt.step();
  }

  auto stmt = db.prepare("SELECT t_jar FROM rdep WHERE s_pom = ?");
  stmt.bind(1, fname);

  hai::chain<hai::cstr> res { 1000 };
  while (stmt.step()) {
    res.push_back(stmt.column_view(0).cstr());
  }
  if (res.size()) return res;

  cavan::file_reader = jojo::read_cstr;
  auto pom = cavan::read_pom(fname);
  auto deps = cavan::resolve_classpath(pom);

  const auto m2dir = meeql::repo_dir();

  stmt = db.prepare("INSERT INTO rdep (s_pom, t_jar) VALUES (?, ?)");
  for (auto & p : deps) {
    stmt.reset();
    stmt.bind(1, fname);
    stmt.bind(2, p);
    stmt.step();

    res.push_back(traits::move(p));
  }
  return res;
}

hai::cstr meeql::resolve_classpath(const char * any_file_in_repo, bool use_cache = true) {
  hai::cstr realpath { 10240 };
  sysstd::fullpath(any_file_in_repo, realpath.begin(), realpath.size());
  auto root = jute::view::unsafe(realpath.begin());
  hai::cstr pom {};
  while (root != "") {
    auto [l, r] = root.rsplit('/');
    pom = (l + "/pom.xml").cstr();
    if (mtime::of(pom.begin())) break;
    root = l;
  }

  if (root == "") die("pom.xml not found in file's parents");

  auto jars = resolve_deps(pom, use_cache);
  unsigned sz = 0;
  for (auto & p : jars) sz += p.size() + 1;
  hai::cstr cp { sz };
  auto cpp = cp.begin();
  for (auto & p : jars) {
    *cpp++ = ':';
    for (auto c : p) *cpp++ = c;
  }

  return cp;
}
