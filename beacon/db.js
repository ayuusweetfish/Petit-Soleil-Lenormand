const createDb = async (path, token) => {
  let run, rwtxn, rotxn
  if (path && path.startsWith('libsql:')) {
    const module = 'npm:@libsql/client/web'
    const { createClient } = await import(module)
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
  output TEXT
)`)

export const getPulse = async (pulse) => {
  const result = await db.run(`SELECT * FROM pulses WHERE pulse = ?`, pulse)
  const toUint8 = (a) => a ? new Uint8Array(a) : null
  return result.length > 0 ? {
    details: JSON.parse(result[0].details), // Handles null already
    output: toUint8(result[0].output),
  } : {
    details: null,
    output: null,
  }
}
export const setBeaconOutput = async (pulse, details, output) => {
  await db.run(`
    INSERT INTO pulses (pulse, details, output) VALUES (?, ?, ?)
      ON CONFLICT (pulse) DO UPDATE
      SET details = excluded.details, output = excluded.output
  `, pulse, JSON.stringify(details), output)
}
export const getLatestPulseTimestsamp = async () => {
  const result = await db.run(`SELECT MAX(pulse) AS pulse FROM pulses`)
  return result[0]['pulse'] || null
}
export const getPreviousPulseTimestsamp = async (before) => {
  const result = await db.run(`SELECT MAX(pulse) AS pulse FROM pulses WHERE pulse < ?`, before)
  return result[0]['pulse'] || null
}

Deno.test('Application database operations', async () => {
  for (let i = 10000000; i <= 10036000; i += 3600) {
    await setBeaconOutput(i, { a: 'a' + i }, new Uint8Array([5, 6, 7, 8, i % 97]))
    const p = await getPulse(i)
    if (p.details.a !== 'a' + i) throw new Error('-')
    if (p.output[4] !== i % 97) throw new Error('-')
  }
})
