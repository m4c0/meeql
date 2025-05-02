#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import print;
import tora;

static constexpr const char * dbname = "out/test.db";

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

// TODO: consider splitting dep_a and dep_v into different tables
static void init_db() {
  tora::db { dbname }.exec(R"(
    DROP TABLE IF EXISTS grp;
    CREATE TABLE grp (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL UNIQUE
    ) STRICT;

    DROP TABLE IF EXISTS art;
    CREATE TABLE art (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      grp   INTEGER NOT NULL REFERENCES grp (id),
      UNIQUE (grp, name)
    ) STRICT;

    DROP TABLE IF EXISTS ver;
    CREATE TABLE ver (
      id     INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name   TEXT NOT NULL,
      art    INTEGER NOT NULL REFERENCES art (id),
      parent INTEGER REFERENCES ver (id),
      pom    TEXT,
      UNIQUE (art, name)
    ) STRICT;

    DROP VIEW IF EXISTS gav;
    CREATE VIEW gav AS
    SELECT ver.id   AS id
         , grp.name AS grp
         , art.name AS art
         , ver.name AS ver
    FROM ver
    JOIN art ON ver.art = art.id
    JOIN grp ON art.grp = grp.id;

    DROP TABLE IF EXISTS dep_mgmt;
    CREATE TABLE dep_mgmt (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;

    DROP TABLE IF EXISTS dep_mgmt_imports;
    CREATE TABLE dep_mgmt_imports (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;

    DROP TABLE IF EXISTS dep;
    CREATE TABLE dep (
      from_ver INTEGER NOT NULL REFERENCES ver (id),
      to_art   INTEGER NOT NULL REFERENCES art (id),
      to_ver   INTEGER REFERENCES ver (id)
    ) STRICT;
  )");
}

struct version_not_found {};
class db {
  tora::db m_db { dbname };

  tora::stmt m_ins_grp_stmt = m_db.prepare(R"(
    INSERT INTO grp (name) VALUES (?) 
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");
  tora::stmt m_ins_art_stmt = m_db.prepare(R"(
    INSERT INTO art (grp, name) VALUES (?, ?) 
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");
  tora::stmt m_ins_ver_stmt = m_db.prepare(R"(
    INSERT INTO ver (art, name) VALUES (?, ?)
    ON CONFLICT DO
    UPDATE SET id = id
    RETURNING id
  )");

  tora::stmt m_upd_ver_stmt = m_db.prepare(R"(
    UPDATE ver
    SET parent = ?, pom = ?
    WHERE id = ?
  )");

  tora::stmt m_ins_dm_stmt = m_db.prepare(R"(
    INSERT INTO dep_mgmt (from_ver, to_ver) VALUES (?, ?)
  )");
  tora::stmt m_ins_dmi_stmt = m_db.prepare(R"(
    INSERT INTO dep_mgmt_imports (from_ver, to_ver) VALUES (?, ?)
  )");

  tora::stmt m_ins_depv_stmt = m_db.prepare(R"(
    INSERT INTO dep (from_ver, to_art, to_ver) VALUES (?, ?, ?)
  )");
  tora::stmt m_ins_depa_stmt = m_db.prepare(R"(
    INSERT INTO dep (from_ver, to_art) VALUES (?, ?)
  )");

public:
  [[nodiscard]] constexpr auto * handle() { return &m_db; }

  unsigned insert(jute::view grp, jute::view art) {
    m_ins_grp_stmt.reset();
    m_ins_grp_stmt.bind(1, grp);
    m_ins_grp_stmt.step();

    m_ins_art_stmt.reset();
    m_ins_art_stmt.bind(1, m_ins_grp_stmt.column_int(0));
    m_ins_art_stmt.bind(2, art);
    m_ins_art_stmt.step();

    return m_ins_art_stmt.column_int(0);
  }
  unsigned insert(jute::view grp, jute::view art, jute::view ver) {
    m_ins_grp_stmt.reset();
    m_ins_grp_stmt.bind(1, grp);
    m_ins_grp_stmt.step();

    m_ins_art_stmt.reset();
    m_ins_art_stmt.bind(1, m_ins_grp_stmt.column_int(0));
    m_ins_art_stmt.bind(2, art);
    m_ins_art_stmt.step();

    m_ins_ver_stmt.reset();
    m_ins_ver_stmt.bind(1, m_ins_art_stmt.column_int(0));
    m_ins_ver_stmt.bind(2, ver);
    m_ins_ver_stmt.step();

    return m_ins_ver_stmt.column_int(0);
  }

  void update_ver(unsigned id, unsigned parent, jute::view pom_path) {
    m_upd_ver_stmt.reset();
    if (parent == 0) m_upd_ver_stmt.bind(1);
    else m_upd_ver_stmt.bind(1, parent);
    m_upd_ver_stmt.bind(2, pom_path);
    m_upd_ver_stmt.bind(3, id);
    m_upd_ver_stmt.step();
  }

  void add_dep_mgmt(unsigned from, unsigned to) {
    m_ins_dm_stmt.reset();
    m_ins_dm_stmt.bind(1, from);
    m_ins_dm_stmt.bind(2, to);
    m_ins_dm_stmt.step();
  }
  void add_dep_mgmt_import(unsigned from, unsigned to) {
    m_ins_dmi_stmt.reset();
    m_ins_dmi_stmt.bind(1, from);
    m_ins_dmi_stmt.bind(2, to);
    m_ins_dmi_stmt.step();
  }
  void add_dep_a(unsigned from, unsigned to_a) {
    m_ins_depv_stmt.reset();
    m_ins_depv_stmt.bind(1, from);
    m_ins_depv_stmt.bind(2, to_a);
    m_ins_depv_stmt.step();
  }
  void add_dep_v(unsigned from, unsigned to_v) {
    m_ins_depv_stmt.reset();
    m_ins_depv_stmt.bind(1, from);
    m_ins_depv_stmt.bind(2, to_v);
    m_ins_depv_stmt.step();
  }
};

static void try_load(db * db, jute::view pom_path) try {
  auto pom = cavan::read_pom(pom_path);
  cavan::read_parent_chain(pom);
  cavan::merge_props(pom);

  auto from = db->insert(pom->grp, pom->art, pom->ver);
  auto parent = pom->ppom ? db->insert(pom->ppom->grp, pom->ppom->art, pom->ppom->ver) : 0;
  db->update_ver(from, parent, pom_path);

  for (auto &[d, _]: pom->deps_mgmt) {
    auto grp = cavan::apply_props(pom, d.grp);
    auto ver = cavan::apply_props(pom, d.ver);
    auto to = db->insert(*grp, d.art, *ver);
    if (d.scp == "import") db->add_dep_mgmt_import(from, to);
    else db->add_dep_mgmt(from, to);
  }
  for (auto &[d, _]: pom->deps) {
    auto grp = cavan::apply_props(pom, d.grp);
    auto ver = cavan::apply_props(pom, d.ver);
    if (*ver == "") db->add_dep_a(from, db->insert(*grp, d.art));
    else db->add_dep_v(from, db->insert(*grp, d.art, *ver));
  }
} catch (...) {
  // TODO: have more catchable errors in cavan
}
static void load() {
  init_db();

  db db {};
  meeql::recurse_repo_dir(curry(try_load, &db));

  auto stmt = db.handle()->prepare("SELECT COUNT(*) FROM ver");
  stmt.step();
  putln("found ", stmt.column_int(0), " vers");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM art");
  stmt.step();
  putln("found ", stmt.column_int(0), " arts");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM grp");
  stmt.step();
  putln("found ", stmt.column_int(0), " grps");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep_mgmt");
  stmt.step();
  putln("found ", stmt.column_int(0), " dm links");

  stmt = db.handle()->prepare("SELECT COUNT(*) FROM dep");
  stmt.step();
  putln("found ", stmt.column_int(0), " dep links");
}

static unsigned insert_parent_chain(db * db, cavan::pom * pom) {
  auto id = db->insert(pom->grp, pom->art, pom->ver);
  auto parent = pom->ppom ? insert_parent_chain(db, pom->ppom) : 0;
  db->update_ver(id, parent, pom->filename);
  return id;
}

static void pomcp(jute::view pom_path) {
  if (pom_path == "") die("missing pom filename");

  auto pom = cavan::read_pom(pom_path);
  cavan::read_parent_chain(pom);
  cavan::merge_props(pom);

  db db {};
  auto ver = insert_parent_chain(&db, pom);

  auto stmt = db.handle()->prepare(R"(
    CREATE TEMPORARY TABLE dm_res AS
    WITH RECURSIVE pc(id, depth) AS (
      VALUES(?, 0)
      UNION
      SELECT parent, pc.depth + 1
      FROM ver
      JOIN pc ON pc.id = ver.id
    )
    SELECT ver.*
    FROM pc
    JOIN dep_mgmt dm ON dm.from_ver = pc.id
    JOIN ver ON dm.to_ver = ver.id
    GROUP BY ver.art
    HAVING pc.depth = MIN(pc.depth)
  )");
  stmt.bind(1, ver);
  stmt.step();

  stmt = db.handle()->prepare(R"(
    SELECT r.id, r.pom
    FROM dm_res AS r
    JOIN art ON art.id = r.art
    JOIN grp ON grp.id = art.grp
    WHERE grp.name = ?
      AND art.name = ?
  )");
  // TODO: BOMs (at least)
  for (auto &[d, _]: pom->deps) {
    if (*d.ver == "") {
      stmt.reset();
      stmt.bind(1, *d.grp);
      stmt.bind(2, d.art);
      stmt.step();
      putln(stmt.column_int(0), " ", stmt.column_view(1));
    } else {
      putln("> ", *d.grp, ":", d.art, ":", *d.ver);
    }
  }
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto cmd = shift();
       if (cmd == ""     ) die("missing command");
  else if (cmd == "load" ) load();
  else if (cmd == "pomcp") pomcp(shift());
  else die("invalid command: ", cmd);
} catch (...) {
  return 13;
}
