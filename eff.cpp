#pragma leco tool

import meeql;

int main() {
  auto db = meeql::db();

  db.exec(R"(
    DROP TABLE IF EXISTS f_pom_tree;
    CREATE TABLE f_pom_tree AS
    WITH RECURSIVE
      pom_chain(id, root, depth) AS (
        SELECT id, id, 0
        FROM pom
        UNION ALL
        SELECT pom.parent, pom_chain.root, pom_chain.depth + 1
        FROM pom
        JOIN pom_chain ON pom_chain.id = pom.id
        WHERE pom.parent IS NOT NULL
      )
    SELECT * FROM pom_chain;

    DROP INDEX IF EXISTS if_pom_tree_root;
    CREATE INDEX if_pom_tree_root ON f_pom_tree (root);

    DROP VIEW IF EXISTS f_prop;
    CREATE VIEW f_prop AS
    SELECT t.root, prop.*, t.depth
    FROM prop
    JOIN f_pom_tree t ON t.id = prop.owner_pom
    GROUP BY t.root, prop.key
    HAVING depth = MIN(depth);

    DROP VIEW IF EXISTS f_dep;
    CREATE VIEW f_dep AS
    SELECT t.root, dep.*, depth
    FROM dep
    JOIN f_pom_tree t ON t.id = dep.owner_pom
    GROUP BY t.root, dep.dep_mgmt, dep.group_id, dep.artefact_id
    HAVING depth = MIN(depth);
  )");
}
