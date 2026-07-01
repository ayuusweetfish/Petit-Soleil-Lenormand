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

Deno.test('createDb', async () => {
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


const db = 1 ? await createDb('/tmp/abcde.sqlite') : await createDb('libsql://01KWDYF9W67SADNW25W8XRQ8HD-testdb1.lite.bunnydb.net/', await Deno.readTextFile('token.txt'))

await db.run(`CREATE TABLE IF NOT EXISTS pulses (
  pulse INTEGER NOT NULL PRIMARY KEY,
  details TEXT,
  output TEXT,
  local_entropy TEXT
)`)

export const getPulse = async (pulse) => {
  const result = await db.run(`SELECT * FROM pulses WHERE pulse = ?`, pulse)
  return result.length > 0 ? {
    details: JSON.parse(result[0].details),
    output: new Uint8Array(result[0].output),
    local_entropy: new Uint8Array(result[0].local_entropy),
  } : null
}
export const setBeaconOutput = async (pulse, details, output) => {
  await db.run(`
    INSERT INTO pulses (pulse, details, output) VALUES (?, ?, ?)
      ON CONFLICT (pulse) DO UPDATE
      SET details = excluded.details, output = excluded.output
  `, pulse, JSON.stringify(details), output)
}
export const setLocalEntropy = async (pulse, local_entropy) => {
  await db.run(`
    INSERT INTO pulses (pulse, local_entropy) VALUES (?, ?)
      ON CONFLICT (pulse) DO UPDATE
      SET local_entropy = COALESCE(local_entropy, excluded.local_entropy)
  `, pulse, local_entropy)
}

Deno.test('Application database operations', async () => {
  for (let i = 10000000; i <= 10036000; i += 3600) {
    if (i % 7200 === 0) await setLocalEntropy(i, new Uint8Array([1, 2, 3, 4, i % 256]))
    await setBeaconOutput(i, { a: 'a' + i }, new Uint8Array([5, 6, 7, 8, i % 97]))
    await setLocalEntropy(i, new Uint8Array([1, 2, 3, 4, i % 256 + 1]))
      // Do nothing if already existing
    const p = await getPulse(i)
    if (p.details.a !== 'a' + i) throw new Error('-')
    if (p.local_entropy[4] !== i % 256 + +!!(i % 7200)) throw new Error('-')
    if (p.output[4] !== i % 97) throw new Error('-')
  }
})
