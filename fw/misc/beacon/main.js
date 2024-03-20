import { encodeHex, decodeHex } from 'https://deno.land/std@0.220.1/encoding/hex.ts'
import { sha3_224, sha3_512 } from 'npm:@noble/hashes@1.3.0/sha3'
import { keccakprg } from 'npm:@noble/hashes@1.3.0/sha3-addons'

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
      throw new Error(`Do not know when last modified`)
    }
    const modifiedAt = new Date(resp.headers.get('Last-Modified'))
    if (modifiedAt < modifiedAfter || modifiedAt > modifiedBefore) {
      throw new Error(`Modification timestamp ${modifiedAt.toISOString()} not in ${modifiedAfter.toISOString()}/${modifiedBefore.toISOString()}`)
    }
  }
  const payload = await resp.blob()
  if (resp.status >= 400 || !payload.type.startsWith('image/')) {
    throw new Error(`Received status ${resp.status}, type ${payload.type}`)
  }
  return new Uint8Array(await payload.arrayBuffer())
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

const src_sdo = (type) => async () => {
  return await fetchImage(`https://sdo.gsfc.nasa.gov/assets/img/latest/latest_1024_${type}.jpg`)
}
const src_sdo_193 = src_sdo('0193')
// const src_imd_ir1 = () => fetchImage(`http://satellite.imd.gov.in/imgr/globe_ir1.jpg`)
// const src_imd_mp = () => fetchImage(`http://satellite.imd.gov.in/imgr/globe_mp.jpg`)

// ====== Common utility functions ======

const zip = (...as) => [...as[0]].map((_, i) => as.map((a) => a[i]))

const hashAllEntries = (entries) => {
  entries.sort((a, b) => a[0].localeCompare(b[0]))
  const prng = keccakprg(510)
  for (const [key, value] of entries) {
    persistLog(key + '\t' +
      (value.length <= 32 ? encodeHex(value) :
      `${value.length},SHA-3-224=${encodeHex(sha3_224(value))}`))
    prng.feed(value)
  }
  const result = prng.fetch(4096)
  let curIndex = 0
  for (const [key, value] of entries) {
    for (let i = 0; i < value.length; i += 64) {
      prng.feed(value.slice(i, i + 64))
      const digestBlock = prng.fetch(64)
      curIndex += 1
      for (let i = 0; i < 64; i++) {
        result[curIndex] ^= digestBlock[i]
        curIndex = (curIndex + 1) % 4096
      }
    }
  }
  const whiten = prng.fetch(4096)
  for (let i = 0; i < 4096; i++) result[4095 - i] ^= whiten[i]
  return result
}

// ====== Miscellaneous sources for local randomness ======

const miscSources = {
  'drand': src_drand_m,
  'NIST beacon': src_irb_nist_m,
  'INMETRO beacon': src_irb_inmetro_br_m,
  'UChile beacon': src_irb_uchile_m,
  'SDO/AIA 193': src_sdo_193,
}
const miscSourcesConstruct = async (timestampRef) => {
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
  const digestBlock = hashAllEntries(entries)
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
  // 'FY Geostationary WV 7u': src_fy_geostationary_wv,
  // 'FY-4B Geo Color': (timestamp) => src_fy4b_disk(timestamp - 30*60000),
  'GOES-18 GeoColor': src_goes18_noaa,
  'Himawari-9 IR B13': src_himawari_b13,
  'Himawari-9 True Color Reproduction': src_himawari_trm,
  'Meteosat IR 0.39u': src_meteosat_ir039,
  'Meteosat IR 10.8u': src_meteosat_ir108,
  'INSAT-3D IR1 10.8u': src_imd_ir1,
  'INSAT-3D TIR BT': src_imd_mp,
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
    // console.log(key, result)
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

let currentFinalizedDigest = null

const currentPulseTimestamp = () => {
  const timestamp = Date.now() + 3 * 60000
  return timestamp - timestamp % (60 * 60000)
}

const checkUpdate = async (finalize) => {
  if (currentFinalizedDigest !== null) return
  persistLog('checking')
  const timestamp = currentPulseTimestamp()
  const rejects = Object.entries(await tryUpdateCurrent(timestamp))
  if (finalize || rejects.length === 0) {
    currentFinalizedDigest = digestCurrent()
    if (rejects.length > 0) console.log(rejects)

    // Mix in miscellaneous sources
    const miscSourcesBlock = await miscSourceBlockForTimestamp(timestamp)
    for (let i = 0; i < 4096; i++)
      currentFinalizedDigest[i] ^= miscSourcesBlock[i]

    const miscSourcesNextHash = await miscSourceBlockHashForTimestamp(timestamp + 60 * 60000)
    persistLog('finalized!' + ' ' +
      encodeHex(currentFinalizedDigest).substring(0, 16) + ' ' +
      encodeHex(miscSourcesBlock).substring(0, 16) + ' ' +
      encodeHex(miscSourcesNextHash))
  } else {
    persistLog('rejects ' + rejects.map(([key, message]) => `<${key}>: ${message}`).join('; '))
  }
}
const initializeUpdate = async () => {
  clearCurrent()
  currentFinalizedDigest = null
  await checkUpdate()
}

await initializeUpdate()
// --unstable-cron
Deno.cron('Initialize updates', '0 * * * *', initializeUpdate)
Deno.cron('Check updates', '5,44/5 * * * *', () => checkUpdate(false))
Deno.cron('Finalize updates', '45 * * * *', () => checkUpdate(true))
