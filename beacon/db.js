const createDb = async (path, token) => {
  let runSql
  if (path && path.startsWith('libsql:')) {
    const { createClient } = await import('npm:@libsql/client/web')
    const client = createClient({
      url: path,
      authToken: token,
      timeout: 5000,
    })
    runSql = async (s, ...args) => {
      return (await client.execute({ sql: s, args: args })).rows
    }
  } else {
    const { DatabaseSync } = await import('node:sqlite')
    const db = new DatabaseSync(path || ':memory:')
    const cachedStmts = {}
    const stmt = (s) => (cachedStmts[s] || (cachedStmts[s] = db.prepare(s)))
    runSql = async (s, ...args) => stmt(s).all(...args)
  }
  return runSql
}

export default createDb

Deno.test('Database', async () => {
  const d = await createDb()
  const r = async (s, ...a) => console.log(await d(s, ...a))
  await r(`SELECT 1 + ? AS three, DATETIME(?) AS now`, 2, 'now')
  await r(`CREATE TABLE IF NOT EXISTS t1 (s TEXT)`)
  await r(`INSERT INTO t1(s) VALUES ('aaa'), ('bbb') RETURNING rowid`)
  await r(`SELECT CHANGES()`)
  await r(`INSERT INTO t1(s) VALUES ('ccc')`)
  await r(`SELECT COUNT(*) FROM t1`)
  await r(`UPDATE t1 SET s = 'www' WHERE rowid = 1`)
  await r(`SELECT CHANGES()`)
  await r(`SELECT 1 AS toString`)
})
