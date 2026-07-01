const createDb = async (path, token) => {
  let run, rwtxn, rotxn
  if (path && path.startsWith('libsql:')) {
    const { createClient } = await import('npm:@libsql/client/web')
    const client = createClient({
      url: path,
      authToken: token,
      timeout: 5000,
    })
    run = async (s, ...args) => {
      return (await client.execute({ sql: s, args: args })).rows
    }
    const txn = (type) => async (fn) => {
      const txn = await client.transaction(type)
      try {
        await fn(async (s, ...args) => {
          return (await txn.execute({ sql: s, args: args })).rows
        })
        await txn.commit()
      } catch (e) {
        await txn.rollback()
        throw e
      }
    }
    rwtxn = txn('write')
    rotxn = txn('read')
  } else {
    const { DatabaseSync } = await import('node:sqlite')
    const db = new DatabaseSync(path || ':memory:')
    const cachedStmts = {}
    const stmt = (s) => (cachedStmts[s] || (cachedStmts[s] = db.prepare(s)))
    run = async (s, ...args) => stmt(s).all(...args)
    const txn = (type) => async (fn) => {
      db.exec(type)
      try {
        await fn(run)
        db.exec('COMMIT')
      } catch (e) {
        db.exec('ROLLBACK')
        throw e
      }
    }
    rwtxn = txn('BEGIN IMMEDIATE TRANSACTION')
    rotxn = txn('BEGIN DEFERRED TRANSACTION')
  }
  return { run, rwtxn, rotxn }
}

export default createDb

Deno.test('Database', async () => {
  const d = await createDb()
  const r = async (s, ...a) => console.log(await d.run(s, ...a))
  await r(`SELECT 1 + ? AS three, DATETIME(?) AS now`, 2, 'now')
  await r(`CREATE TABLE IF NOT EXISTS t1 (s TEXT)`)
  await r(`INSERT INTO t1(s) VALUES ('aaa'), ('bbb') RETURNING rowid`)
  await r(`SELECT CHANGES()`)
  await r(`INSERT INTO t1(s) VALUES ('ccc')`)
  await r(`SELECT COUNT(*) FROM t1`)
  await r(`UPDATE t1 SET s = 'www' WHERE rowid = 1`)
  await r(`SELECT CHANGES()`)
  await r(`SELECT 1 AS toString`)
  await d.rwtxn(async (run) => {
    const { rowid, s } = (await run(`SELECT rowid, s FROM t1 ORDER BY rowid DESC LIMIT 1`))[0]
    await run(`UPDATE t1 SET s = ? WHERE rowid = ?`, s + '!!', rowid)
    console.log(await run(`SELECT * FROM t1 WHERE s = ?`, s + '!!'))
  })
  try {
    await d.rwtxn(async (run) => {
      await run(`UPDATE t1 SET s = 'n'`)
      await run(`UPDATE t1 SET t = 'n'`)
    })
  } catch (e) {
  }
  await d.rotxn(async (run) => {
    console.log(await run(`SELECT COUNT(*) FROM t1 WHERE s = ?`, 'bbb'))
  })
  await r(`SELECT * FROM t1 ORDER BY rowid DESC LIMIT 5`)
})
