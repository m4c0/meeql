#pragma leco tool
import tora;

int main(int argc, char ** argv) try {
  tora::db db { ":memory:" };

  db.exec(R"(
    CREATE TABLE grp (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL UNIQUE
    ) STRICT;

    CREATE TABLE art (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      grp   INTEGER NOT NULL REFERENCES grp (id),
      UNIQUE (grp, name)
    ) STRICT;

    CREATE TABLE ver (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      art   INTEGER NOT NULL REFERENCES art (id),
      UNIQUE (art, name)
    ) STRICT;
  )");

} catch (...) {
  return 1;
}
