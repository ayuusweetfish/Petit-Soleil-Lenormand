import { encodeHex, decodeHex } from 'https://deno.land/std@0.220.1/encoding/hex.ts'
import { sha3_224, sha3_512, keccakP } from 'npm:@noble/hashes@1.4.0/sha3'
import { u32, isLE, byteSwap32 } from 'npm:@noble/hashes@1.4.0/utils'
import { serveFile } from 'https://deno.land/std@0.220.1/http/file_server.ts'

// --unstable-kv
const isOnDenoDeploy = (Deno.env.get('DENO_DEPLOYMENT_ID') !== undefined)
const kv = await Deno.openKv(isOnDenoDeploy ? undefined : 'kv.sqlite')

let lastMonotonicDateStr = null
let lastMonotonicCount = 0
const monotonicDate = () => {
  const s = (new Date()).toISOString()
  if (s === lastMonotonicDateStr) {
    lastMonotonicCount++
  } else {
    lastMonotonicDateStr = s
    lastMonotonicCount = 0
  }
  return s + ',' + lastMonotonicCount.toString().padStart(3, '0')
}
// for (let i = 0; i < 1000; i++) console.log(monotonicDate())
const persistLog = (s) => {
  const dateStr = monotonicDate()
  kv.set(['log', dateStr], s)
  console.log(dateStr, s)
}

// Beacons
const src_drand = async (timestamp) => {
  // https://api.drand.sh/8990e7a9aaed2ffed73dbd7092123d6f289930540d7651336225dc172e51b2ce/info
  const round = Math.floor((timestamp - 1595431050000) / 30000) + 1
  const payload = await (await fetch(`https://api.drand.sh/public/${round}`)).json()
  return payload['randomness']
}
const src_irb = (baseUrl, extraTime) => async (timestamp) => {
  timestamp -= timestamp % 60000
  const payload = await (await fetch(`${baseUrl}${timestamp + (extraTime || 0)}`)).json()
  return payload['pulse']['outputValue']
}
const src_irb_nist = src_irb('https://beacon.nist.gov/beacon/2.0/pulse/time/')
// --unsafely-ignore-certificate-errors=beacon.inmetro.gov.br
const src_irb_inmetro_br = src_irb('https://beacon.inmetro.gov.br/beacon/2.1/pulse/time/')
const src_irb_uchile = src_irb('https://random.uchile.cl/beacon/2.1-beta/pulse?timeGE=')

const src_beacon_multi = (fn, interval, count) => async (timestamp) => {
  const promises = []
  for (let i = 0; i < count; i++) {
    promises.push(fn(timestamp))
    timestamp -= interval
  }
  const results = await Promise.all(promises)
  return decodeHex(results.join(''))
}
const src_drand_m = src_beacon_multi(src_drand, 30000, 20)
const src_irb_nist_m = src_beacon_multi(src_irb_nist, 60000, 1)
const src_irb_inmetro_br_m = src_beacon_multi(src_irb_inmetro_br, 60000, 20)
const src_irb_uchile_m = src_beacon_multi(src_irb_uchile, 60000, 20)

const fetchImage = async (url, modifiedAfter, modifiedBefore) => {
  console.log('fetch', url)
  const resp = await fetch(url)
  if (modifiedAfter !== undefined) {
    if (!resp.headers.has('Last-Modified')) {
      throw new Error(`Do not know when last modified (${url})`)
    }
    const modifiedAt = new Date(resp.headers.get('Last-Modified'))
    if (modifiedAt < modifiedAfter || modifiedAt > modifiedBefore) {
      throw new Error(`Modification timestamp ${modifiedAt.toISOString()} not in ${modifiedAfter.toISOString()}/${modifiedBefore.toISOString()} (${url})`)
    }
  }
  const payload = await resp.blob()
  if (resp.status >= 400 || !payload.type.startsWith('image/')) {
    throw new Error(`Received status ${resp.status}, type ${payload.type} (${url})`)
  }
  const arr = new Uint8Array(await payload.arrayBuffer())
  arr._url = url
  return arr
}

// Satellite images
const src_fy_geostationary = (type) => async (timestamp) => {
  timestamp -= timestamp % (60 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const payload = await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/GEOS/MOS/${type}/PIC/GBAL/${dateStr}/GEOS_IMAGR_GBAL_L2_MOS_${type}_GLL_${dateStr}_${hourStr}00_10KM_MS.jpg`)
  return payload
}
const src_fy_geostationary_ir = src_fy_geostationary('IRX')
const src_fy_geostationary_wv = src_fy_geostationary('WVX')
const src_fy4b_disk = async (timestamp) => {
  timestamp -= timestamp % (15 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const minute = date.getUTCMinutes()
  const payload = await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/FY4B/AGRI/GCLR/DISK/FY4B-_AGRI--_N_DISK_1050E_L2-_GCLR_MULT_NOM_${dateStr}${hourStr}${minute.toString().padStart(2, '0')}00_${dateStr}${hourStr}${minute + 14}59_1000M_V0001.JPG`)
  return payload
}
const src_goes18_noaa = async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const dayOfYear = Math.floor((date - Date.UTC(date.getUTCFullYear(), 0, 0)) / 86400_000)
  const yearDayStr =
    date.getUTCFullYear().toString() +
    dayOfYear.toString().padStart(3, '0')
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://cdn.star.nesdis.noaa.gov/GOES18/ABI/FD/GEOCOLOR/${yearDayStr}${hourMinStr}_GOES18-ABI-FD-GEOCOLOR-1808x1808.jpg`)
  return payload
}
const src_himawari = (type) => async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://www.data.jma.go.jp/mscweb/data/himawari/img/fd_/fd__${type}_${hourMinStr}.jpg`,
    new Date(timestamp - 60 * 60000),
    new Date(timestamp + 60 * 60000))
  return payload
}
const src_himawari_b13 = src_himawari('b13')
const src_himawari_trm = src_himawari('trm')
const src_meteosat_eumetsat = (type) => async (timestamp) => {
  timestamp -= timestamp % (15 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCDate().toString().padStart(2, '0') + '/' +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') + '/' +
    (date.getUTCFullYear() % 100).toString().padStart(2, '0')
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') + ':' +
    date.getUTCMinutes().toString().padStart(2, '0')

  const page = await (await fetch(`https://eumetview.eumetsat.int/static-images/MSG/IMAGERY/${type}/BW/FULLDISC/`)).text()

  const reStr1 = `<option value=['"](\\d+)['"]>\\s*${dateStr}\\s+${hourMinStr}\\s+UTC\\s*</option>`
  const matched1 = page.match(new RegExp(reStr1))
  if (matched1 === null) {
    throw new Error('Timestamped image not found (maybe not uploaded)')
  }
  const imageIndex = +matched1[1]
  const reStr2 = `array_nom_imagen\\[${imageIndex}\\].+['"](\\w+)['"]`
  const matched2 = page.match(new RegExp(reStr2))
  if (matched2 === null) {
    throw new Error('Cannot extract image name from page')
  }
  const imageName = matched2[1]

  const payload = await fetchImage(`https://eumetview.eumetsat.int/static-images/MSG/IMAGERY/${type}/BW/FULLDISC/IMAGESDisplay/${imageName}`)
  return payload
}
const src_meteosat_ir039 = src_meteosat_eumetsat('IR039')
const src_meteosat_ir108 = src_meteosat_eumetsat('IR108')

const src_imd = (type) => async (timestamp) => {
  timestamp -= timestamp % (15 * 60000)
  return await fetchImage(`http://satellite.imd.gov.in/imgr/globe_${type}.jpg`,
    new Date(timestamp),
    new Date(timestamp + 30 * 60000))
}
const src_imd_ir1 = src_imd('ir1')
const src_imd_mp = src_imd('mp')

const src_gk2a = (type) => async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const yearMonthStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0')
  const dayStr = date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const minuteStr = date.getUTCMinutes().toString().padStart(2, '0')
  return await fetchImage(`https://nmsc.kma.go.kr/IMG/GK2A/AMI/PRIMARY/L1B/COMPLETE/FD/${yearMonthStr}/${dayStr}/${hourStr}/gk2a_ami_le1b_${type}_fd020ge_${yearMonthStr}${dayStr}${hourStr}${minuteStr}.srv.png`)
}
const src_gk2a_rgb_daynight = src_gk2a('rgb-daynight')
const src_gk2a_ir087 = src_gk2a('ir087')

const src_elektro_l = (type) => async (timestamp) => {
  timestamp -= timestamp % (30 * 60000)
  timestamp += 3 * 60 * 60000   // In UTC+3
  const date = new Date(timestamp)
  const dateTimeStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0') + '-' +
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  return await fetchImage(`http://electro.ntsomz.ru/i/${type}/${dateTimeStr}.jpg`)
}
const src_elektro_l2 = src_elektro_l('splash')
const src_elektro_l3 = src_elektro_l('splash_l3')

const src_sdo = (type) => async (timestamp) => {
  return await fetchImage(`https://sdo.gsfc.nasa.gov/assets/img/latest/latest_1024_${type}.jpg`,
    new Date(timestamp - 20 * 60000),
    new Date(timestamp + 20 * 60000))
}
const src_sdo_193 = src_sdo('0193')

const src_random_org = (n) => async (timestamp) => {
  const content = await (await fetch(`https://www.random.org/cgi-bin/randbyte?nbytes=${n}&format=h`)).text()
  return new Uint8Array(content.matchAll(/[0-9a-fA-F]{2}/g).map(([w]) => parseInt(w, 16)))
}

const src_log_digest = async () => {
  const content = await (await fetch(`http://log-digest.ayu.land/`)).text()
  return decodeHex(content.match(/^[0-9a-fA-F]{128}/)[0])
}

const src_opensky_network = async (timestamp) => {
  const payload = await (await fetch(`https://opensky-network.org/api/states/all`)).blob()
  return new Uint8Array(await payload.arrayBuffer())
}

// ====== Common utility functions ======

const zip = (...as) => [...as[0]].map((_, i) => as.map((a) => a[i]))

const keccakPrng = () => {
  const state = new Uint8Array(200)
  const stateWords = u32(state)
  const p = (isLE ? () => {
    keccakP(stateWords, 24)
  } : () => {
    byteSwap32(stateWords)
    keccakP(stateWords, 24)
    byteSwap32(stateWords)
  })
  // Rate = 512 b = 64 B (capacity = 1088 b)
  const feed = function* (buf) {
    let pos = 0
    for (let i = 0; i < buf.length; i++) {
      state[pos++] ^= buf[i]
      if (pos === 64) {
        p()
        yield state.subarray(0, 64)
        pos = 0
      }
    }
    // Pad 10*1
    state[pos] ^= 0x80
    state[63] ^= 0x01
    p()
    yield state.subarray(0, 64)
  }
  const fetchBlockInto = (out) => {
    p()
    if (!out) out = new Uint8Array(64)
    for (let i = 0; i < 64; i++) out[i] ^= state[i]
    return out
  }
  return {
    feed,
    fetchBlockInto,
  }
}
/*
const r = keccakPrng()
const u = new Uint8Array(100)
// r.feed(u).forEach(() => {})
for (const block of r.feed(u)) console.log(block)
Deno.exit(0)
*/

const hashAllEntries = (entries) => {
  entries.sort((a, b) => a[0].localeCompare(b[0]))
  const breakdown = []
  const prng = keccakPrng()
  for (const [key, value] of entries) {
    // persistLog(`${key}\t${value.length},SHA-3-224=${encodeHex(sha3_224(value))}`)
    prng.feed(value).forEach(() => {})
    breakdown.push([key, value.length, encodeHex(sha3_224(value))])
  }
  const result = new Uint8Array(4096)
  // Initial whitening
  for (let i = 0; i < 4096; i += 64)
    prng.fetchBlockInto(result.subarray(i, i + 64))
  let curIndex = 0
  for (const [key, value] of entries) {
    for (let block of prng.feed(value)) {
      for (let i = 0; i < 64; i++) result[curIndex + i] ^= block[i]
      curIndex = (curIndex + 64) % 4096
    }
  }
  // Final whitening
  for (let i = 0; i < 4096; i += 64)
    prng.fetchBlockInto(result.subarray(i, i + 64))
  return [result, breakdown]
}
/*
const [result, breakdown] = hashAllEntries([
  // ['source 1', new Uint8Array(100)],
  // ['source 2', new Uint8Array(100)],
  // ['hash.c', await Deno.readFile('page/hash.c')],
  // ['hash.c', await Deno.readFile('page/hash.c')],
  ['font-subset.py', await Deno.readFile('page/font-subset.py')],
  ['index.html', await Deno.readFile('page/index.html')],
])
console.log(encodeHex(result))
Deno.exit(0)
*/

// ====== Miscellaneous sources for local randomness ======

const miscSources = {
  'drand': src_drand_m,
  'NIST beacon': src_irb_nist_m,
  'INMETRO beacon': src_irb_inmetro_br_m,
  'UChile beacon': src_irb_uchile_m,
  'SDO/AIA 193': src_sdo_193,
  'RANDOM.ORG': src_random_org(256),
  'Ayu.land digest': src_log_digest,
  'OpenSky Network': src_opensky_network,
}
const miscSourcesConstruct = async (timestampRef) => {
  timestampRef -= 60 * 60000
  const results = await Promise.allSettled(
    Object.entries(miscSources).map(([key, fn]) => fn(timestampRef))
  )
  const resultsKeyed = zip(Object.keys(miscSources), results)
  const rejects = resultsKeyed
    .filter(([key, result]) => result.status === 'rejected')
    .map(([key, result]) => [key, result.reason.message])
  if (rejects.length > 0)
    persistLog('rejects ' + rejects.map(([key, message]) => `<${key}>: ${message}`).join('; '))
  const entries = resultsKeyed
    .filter(([key, result]) => result.status === 'fulfilled')
    .map(([key, result]) => [key, result.value])
  const [digestBlock, _] = hashAllEntries(entries)
  const localRand = new Uint8Array(4096)
  crypto.getRandomValues(localRand)
  for (let i = 0; i < 4096; i++) digestBlock[i] ^= localRand[i]
  return digestBlock
}

const miscSourceBlockForTimestamp = async (timestamp, construct) => {
  // Try to load from KV
  const miscSourcesBlockSaved = (await kv.get(['misc-block', timestamp])).value
  if (miscSourcesBlockSaved) {
    return decodeHex(miscSourcesBlockSaved)
  } else {
    persistLog(`Misc entropy block for ${timestamp} not saved, ${construct ? 'building' : 'randomly generating'}`)
    let miscSourcesBlock
    if (construct) {
      miscSourcesBlock = await miscSourcesConstruct(timestamp)
    } else {
      miscSourcesBlock = new Uint8Array(4096)
      crypto.getRandomValues(miscSourcesBlock)
    }
    await kv.set(['misc-block', timestamp], encodeHex(miscSourcesBlock))
    return miscSourcesBlock
  }
}
const miscSourceBlockHashForTimestamp = async (timestamp) => {
  const block = await miscSourceBlockForTimestamp(timestamp, true)
  return sha3_512(block)
}

// ====== Public sources ======

const sources = {
  'FY Geostationary IR 10.8u': src_fy_geostationary_ir,
  'FY Geostationary WV 7u': (timestamp) => src_fy_geostationary_wv(timestamp - 60 * 60000),
  // 'FY-4B Geo Color': (timestamp) => src_fy4b_disk(timestamp - 30*60000),
  'GOES-18 GeoColor': src_goes18_noaa,
  'Himawari-9 IR B13': src_himawari_b13,
  'Himawari-9 True Color Reproduction': src_himawari_trm,
  'Meteosat IR 0.39u': src_meteosat_ir039,
  'Meteosat IR 10.8u': src_meteosat_ir108,
  'INSAT-3D IR1 10.8u': src_imd_ir1,
  'INSAT-3D TIR BT': src_imd_mp,
  'GK2A RGB DAYNIGHT': src_gk2a_rgb_daynight,
  'GK2A IR 8.7u': src_gk2a_ir087,
  'Elektro-L 2': (timestamp) => src_elektro_l2(timestamp - 60*60000),
  'Elektro-L 3': (timestamp) => src_elektro_l3(timestamp - 30*60000),
}
const current = {}

const tryUpdateCurrent = async (timestamp) => {
  timestamp -= timestamp % (60 * 60000)
  const promises = []
  for (const [key, fn] of Object.entries(sources)) {
    if (current[key] === undefined) {
      promises.push(fn(timestamp))
    } else {
      promises.push(null)
    }
  }
  const results = await Promise.allSettled(promises)
  const rejects = {}
  for (const [[key, _], result] of zip(Object.entries(sources), results)) {
    if (result.status === 'fulfilled') {
      if (result.value !== null) current[key] = result.value
    } else {
      rejects[key] = result.reason.message
    }
  }
  return rejects
}
const digestCurrent = () => {
  const entries = Object.entries(current)
  return hashAllEntries(entries)
}
const clearCurrent = () => {
  for (const key of Object.getOwnPropertyNames(current)) {
    delete current[key]
  }
}

const loadOutput = async (timestamp) => {
  const savedOutputStr = (await kv.get(['output', timestamp])).value
  if (!savedOutputStr) return undefined
  return savedOutputStr
}
const outputDetailsArr = async (entries) => {
  // entries: [[length (number), digest (string), message or url (string | undefined)]]
  const entriesArr = []
  const keysArr = Object.keys(sources)
  keysArr.sort((a, b) => a.localeCompare(b))
  for (const key of keysArr) {
    const [length, digest, message] = entries[key] || [null, null, undefined]
    entriesArr.push({ key, length, digest, message })
  }
  return entriesArr
}
const loadOutputDetails = async (timestamp) => {
  const details = (await kv.get(['output', timestamp, 'breakdown'])).value
  if (!details) return undefined

  const messagesStr = (await kv.get(['output', 'messages', timestamp])).value
  const messages = (messagesStr ? JSON.parse(messagesStr) : undefined)

  const entries = {}
  for (const entry of details.match(/[^;]+/g)) {
    const [_, name, length, digest] =
      entry.match(/^(.+) ([0-9]+) ([0-9a-f]+)$/)
    const message = (messages ? (messages[name] || null) : undefined)
    entries[name] = [+length, digest, message]
  }
  for (const name of Object.keys(sources))
    if (!entries[name]) {
      const message = (messages ? (messages[name] || null) : undefined)
      entries[name] = [null, null, message]
    }
  return await outputDetailsArr(entries)
}

let currentFinalizedDigest = null
let currentFinalizedDigestTimestamp = null

const currentPulseTimestamp = (absolute) => {
  return 1711641600000
  const timestamp = Date.now() + (absolute ? 0 : 3 * 60000)
  return timestamp - timestamp % (60 * 60000)
}
const latestPulseTimestamp = () => currentPulseTimestamp(true) - 60 * 60000

const checkUpdate = async (finalize, timestamp) => {
  timestamp = timestamp || currentPulseTimestamp()
  if (currentFinalizedDigest !== null && currentFinalizedDigestTimestamp === timestamp)
    return

  persistLog('checking')
  const rejects = Object.entries(await tryUpdateCurrent(timestamp))

  if (finalize || rejects.length === 0) {
    const [digest, breakdown] = digestCurrent()
    currentFinalizedDigest = digest
    currentFinalizedDigestTimestamp = timestamp

    // Mix in miscellaneous sources
    const miscSourcesBlock = await miscSourceBlockForTimestamp(timestamp)
    for (let i = 0; i < 4096; i++)
      currentFinalizedDigest[i] ^= miscSourcesBlock[i]
    // Mix in previous output
    const previousOutputStr = (await kv.get(['output', timestamp - 60 * 60000])).value
    if (previousOutputStr) {
      const previousOutput = decodeHex(previousOutputStr)
      for (let i = 0; i < 4096; i++)
        currentFinalizedDigest[i] ^= previousOutput[i]
    } else {
      persistLog('previous output not found, not mixing')
    }

    const op = kv.atomic()

    const miscSourcesNextHash = await miscSourceBlockHashForTimestamp(timestamp + 60 * 60000)
    persistLog('finalized!' + ' ' +
      encodeHex(currentFinalizedDigest).substring(0, 16) + ' ' +
      encodeHex(miscSourcesBlock).substring(0, 16) + ' ' +
      encodeHex(miscSourcesNextHash))
    op.set(['output', timestamp], encodeHex(currentFinalizedDigest))

    const breakdownStr = breakdown.map(
      ([key, length, digest]) => `${key} ${length} ${digest}`
    ).join(';')
    op.set(['output', timestamp, 'breakdown'], breakdownStr)

    const messages = {}
    for (const [key, value] of Object.entries(current)) messages[key] = value._url
    for (const [key, message] of rejects) messages[key] = message
    op.set(['output', 'messages', timestamp], JSON.stringify(messages))

    // Remove obsolete messages
    console.log(['output', 'messages', timestamp - (23 * 60 * 60000 + 1)])
    for await (const { key } of kv.list({
      prefix: ['output', 'messages'],
      end: ['output', 'messages', timestamp - (23 * 60 * 60000 + 1)],
    })) {
      op.delete(key)
    }

    await op.commit()
  } else {
    console.log('rejects ' + rejects.map(([key, message]) => `<${key}>: ${message}`).join('; '))
  }

  const currentEntries = {}
  for (const [key, value] of Object.entries(current)) {
    currentEntries[key] = [
      value.length,
      encodeHex(sha3_224(value)),
      value._url,
    ]
  }
  for (const [key, message] of rejects) {
    currentEntries[key] = [
      null, null, message,
    ]
  }
  await kv.set(['current'], JSON.stringify(currentEntries))
}
const initializeStates = async (timestamp) => {
  clearCurrent()
  currentFinalizedDigest = null
  // Try loading
  timestamp = timestamp || currentPulseTimestamp()
  const output = await loadOutput(timestamp)
  if (output !== undefined) {
    currentFinalizedDigest = decodeHex(output)
    currentFinalizedDigestTimestamp = timestamp
    persistLog(`Loaded output block at ${timestamp}`)
  }
}
const initializeUpdate = async () => {
  await initializeStates()
  await checkUpdate()
}

/*
for (let t  = +new Date('2024-03-20T04:00:00.000Z');
         t <= +new Date('2024-03-21T02:00:00.000Z');
         t += 60 * 60000) {
  await initializeStates(t)
  await checkUpdate(true, t)
}
Deno.exit(0)
*/

await initializeStates()
// await checkUpdate()
// --unstable-cron
Deno.cron('Initialize updates', '0 * * * *', initializeUpdate)
Deno.cron('Check updates', '5-44/5 * * * *', () => checkUpdate(false))
Deno.cron('Finalize updates', '45 * * * *', () => checkUpdate(true))

const jsonResp = (o) =>
  new Response(JSON.stringify(o), { headers: { 'Content-Type': 'application/json' } })

const savedPulseResp = async (timestamp, format) => {
  timestamp -= timestamp % (60 * 60000)
  if (timestamp > currentPulseTimestamp()) {
    if (format === 'object') return null
    return new Response('Pulse is in the future', { status: 404 })
  }
  const output = await loadOutput(timestamp)
  if (output === undefined) {
    if (format === 'object') return null
    return new Response('Pulse is not recorded', { status: 404 })
  }
  if (format === 'short') return jsonResp({ timestamp, output })
  if (format === 'plain') return new Response(output)
  const details = await loadOutputDetails(timestamp)
  const local = encodeHex(await miscSourceBlockForTimestamp(timestamp))
  const precommit = encodeHex(await miscSourceBlockHashForTimestamp(timestamp + 60 * 60000))
  const obj = { timestamp, output, details, local, precommit }
  if (format === 'object') return obj
  return jsonResp(obj)
}

const renderTemplate = (s, lookup, extra) => {
  return s.replaceAll(/{{~(.*)\s*([0-9A-Za-z_]+)\s*([^]*\S)\s*\1~(?:}}|-}}\s*)/gm, (_, _delim, key, w) => {
    const list = lookup[key] || []
    return list.map((entry, index) =>
      renderTemplate(w, entry, { index: (index + 1).toString().padStart(2, '0') })
    ).join('')
  }).replaceAll(/{{\s*([0-9A-Za-z_]+)\s*}}/g, (_, w) => {
    return lookup[w] !== undefined ? lookup[w].toString() :
           extra[w] !== undefined ? extra[w].toString() : ''
  })
}

Deno.serve({
  port: 3321,
}, async (req, info) => {
  const url = new URL(req.url)
  if (req.method === 'GET') {
    if (url.pathname === '/current') {
      const currentTimestamp = currentPulseTimestamp(true)
      const currentEntries = JSON.parse((await kv.get(['current'])).value || '[]')
      const details = await outputDetailsArr(currentEntries)
      const respObject = {}
      Object.assign(respObject, await savedPulseResp(currentTimestamp, 'object'))
      respObject.timestamp = currentTimestamp
      respObject.output = respObject.output || null
      respObject.details = details
      return jsonResp(respObject)
    }
    const urlPartsLatest = url.pathname.match(/^\/([0-9]{1,15}|latest)(|\/short|\/plain)$/)
    if (urlPartsLatest) {
      let [_, timestamp, format] = urlPartsLatest
      if (timestamp === 'latest')
        timestamp = latestPulseTimestamp()
      else timestamp = +timestamp
      return await savedPulseResp(timestamp, (format || '').substring(1))
    }
    if (url.pathname === '/') {
      const timestamp = latestPulseTimestamp()
      const output = await loadOutput(timestamp)
      const details = await loadOutputDetails(timestamp)
      for (const entry of details) {
        const match = entry.message.match(/\.([^/\.]+)$/)
        entry.extension = (match ? match[1] :
          (entry.message.startsWith('https://eumetview.eumetsat.int') ? 'jpg' : 'bin'))
      }
      const outputArray = decodeHex(output)
      const localRandomnessArray = await miscSourceBlockForTimestamp(timestamp)
      for (let i = 0; i < 4096; i++) outputArray[i] ^= localRandomnessArray[i]
      const prefixSuffix = (s) => {
        if (s instanceof Uint8Array) s = encodeHex(s)
        return (s.substring(0, 16) + '...' + s.substring(s.length - 16))
      }
      const lookup = !output ? {} : {
        // 'latestPrefix': output.substring(0, 16) + '...',
        'latestPrefixSuffix': prefixSuffix(output),
        'contentHashPrefixSuffix': prefixSuffix(outputArray),
        'localRandomnessPrefixSuffix': prefixSuffix(localRandomnessArray),
        'precommitment': prefixSuffix(await miscSourceBlockHashForTimestamp(timestamp)),
        'details': details.filter((e) => e.length !== null),
      }
      const template = await Deno.readTextFile('page/index.html')
      const content = renderTemplate(template, lookup)
      return new Response(content, {
        headers: { 'Content-Type': 'text/html; encoding=utf-8' }
      })
    }
    // Try static files
    const tryStat = async (path) => {
      try {
        return (await Deno.stat(path)).isFile
      } catch (e) {
        if (e instanceof Deno.errors.NotFound) return false
        throw e
      }
    }
    if (await tryStat('page/' + url.pathname.substring(1))) {
      const path = url.pathname.substring(1)
      return await serveFile(req, 'page/' + path)
    }
  } else {
    return new Response('Unsupported method', { status: 405 })
  }
  return new Response('Void space, please return', { status: 404 })
})
