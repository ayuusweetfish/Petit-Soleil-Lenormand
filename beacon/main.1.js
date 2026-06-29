import { createHash } from 'node:crypto'

const sha3_224 = (a) => createHash('sha3-224').update(a).digest()

const parallelOracle = (N, local, files) => {
  const a = new Uint8Array(N)
  const l = local.slice()
  for (let i = 0; i < N / 64; i++) {
    const h = createHash('sha3-512')
    l[0] = (local[0] + i) % 256
    h.update(l)
    for (const f of files) h.update(f)
    a.set(h.digest(), 64 * i)
  }
  return a
}
if (0) {
  console.log(parallelOracle(4096,
    new Uint8Array([0x15, 0xb0, 0x3a, 0xb5, 0xe6, 0x93, 0x43, 0xbb, 0xf8, 0xc2, 0xb4, 0x67, 0xaf, 0x2b, 0xe0, 0x78, 0xf0, 0x4f, 0x6d, 0x63, 0xef, 0x4c, 0x96, 0xe1, 0xe2, 0xdb, 0xc4, 0x64, 0xcc, 0x2d, 0xe7, 0xc1, 0xc7, 0x54, 0x19, 0x06, 0xcf, 0x40, 0x31, 0x11, 0x69, 0x78, 0x16, 0x5b, 0x50, 0x68, 0x0f, 0x7f, 0x11, 0x29, 0xab, 0x41, 0xc8, 0x0e, 0xb1, 0x80, 0xa1, 0xab, 0x7a, 0x2a, 0xf5, 0x6b, 0x64, 0xfe]),
    [new TextEncoder().encode('hello')]
  ).toHex())
  Deno.exit(0)
}

const fetchImage = async (url, modifiedAfter, modifiedBefore) => {
  console.log(`> ${url}`)
  const resp = await fetch(url)
  const modifiedAt = (resp.headers.has('Last-Modified') ?
    new Date(resp.headers.get('Last-Modified')) : undefined)
  if (modifiedAfter !== undefined || modifiedBefore !== undefined) {
    if (modifiedAt === undefined) {
      throw new Error(`! ${url} -> Do not know when last modified`)
    }
    if ((modifiedAfter && modifiedAt < modifiedAfter) ||
        (modifiedBefore && modifiedAt > modifiedBefore)) {
      throw new Error(`! ${url} -> Modification timestamp ` +
        `${modifiedAt.toISOString()} not in ` +
        `${modifiedAfter.toISOString()}/${modifiedBefore.toISOString()}`)
    }
  }
  const payload = await resp.blob()
  if (resp.status >= 400 || !payload.type.startsWith('image/')) {
    throw new Error(`! ${url} -> Received status ` +
      `${resp.status}, type ${payload.type}`)
  }
  const arr = new Uint8Array(await payload.arrayBuffer())
  arr._url = url
  console.log(`* ${url} -> size ${arr.length}, modification ` +
    `${modifiedAt ? modifiedAt.toISOString() : 'not given'}`)
  return arr
}

// Fengyun-4B (2021), -2H (2018), etc.
const src_fy_geos = (type) => async (timestamp) => {
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
const src_fy_geos_ir = src_fy_geos('IRX')
const src_fy_geos_wv = src_fy_geos('WVX')
const src_fy4b_color = async (timestamp) => {
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
const src_fy2h_color = async (timestamp) => {
  timestamp -= timestamp % (60 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const payload = await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/FY2H/NOM/ETV/FY2H_ETV_NOM_${dateStr}_${hourStr}00.jpg`)
  return payload
}

// GOES-19 (2024)
const src_goes = (sat) => async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const dayOfYear = Math.floor((date - Date.UTC(date.getUTCFullYear(), 0, 0)) / 86400_000)
  const yearDayStr =
    date.getUTCFullYear().toString() +
    dayOfYear.toString().padStart(3, '0')
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://cdn.star.nesdis.noaa.gov/${sat}/ABI/FD/GEOCOLOR/${yearDayStr}${hourMinStr}_${sat}-ABI-FD-GEOCOLOR-1808x1808.jpg`)
  return payload
}
const src_goes19 = src_goes('GOES19')
const src_goes18 = src_goes('GOES18')

// Himawari-9 (2016)
const src_himawari = (type) => async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://www.data.jma.go.jp/mscweb/data/himawari/img/fd_/fd__${type}_${hourMinStr}.jpg`,
    new Date(timestamp - 120 * 60000),
    new Date(timestamp + 120 * 60000))
  return payload
}
const src_himawari_b13 = src_himawari('b13')
const src_himawari_b08 = src_himawari('b08')
const src_himawari_tcr = src_himawari('trm')

// Meteosat Second Generation (2015, etc.), Third Generation (2025, etc.)
const src_meteosat = (type) => async (timestamp) => {
  const date = new Date(timestamp)
  const dateTimeStr = date.toISOString()
  return await fetchImage(`https://view.eumetsat.int/geoserver/wms?service=WMS&version=1.3.0&request=GetMap&layers=${type},backgrounds:ne_10m_coastline&styles=&format=image/jpeg&srs=AUTO:97004,9001,0,0&bbox=-5450000,-5450000,5450000,5450000&width=1800&height=1800&time=${dateTimeStr}`)
}
const src_meteosat_ir105 = src_meteosat('mtg_fd:ir105_hrfi')
const src_meteosat_ir039 = src_meteosat('msg_fes:ir039')

// INSAT-3DS (2024)
const src_insat = (type) => async (timestamp) => {
  timestamp -= timestamp % (30 * 60000)
  const date = new Date(timestamp)
  const yearStr = date.getUTCFullYear().toString()
  const monthAbbrs =
    ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC']
  const monthDayStr =
    date.getUTCDate().toString().padStart(2, '0') +
    monthAbbrs[date.getUTCMonth()]
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const minuteStr = date.getUTCMinutes().toString().padStart(2, '0')
  return await fetchImage(`https://mosdac.gov.in/look/3S_IMG/preview/${yearStr}/${monthDayStr}/3SIMG_${monthDayStr}${yearStr}_${hourStr}${minuteStr}_L1B_STD_${type}_V01R00.jpg`)
}
const src_insat_ir1 = src_insat('IR1')
const src_insat_mir = src_insat('MIR')

// GEO-KOMPSAT 2A (2018)
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

// Elektro-L 2 (2015), 3 (2019), 4 (2023)
// Arktika-M 1 (2021), 2 (2023)
const src_ntsomz = (series, type) => async (timestamp) => {
  timestamp -= timestamp % (30 * 60000)
  timestamp += 3 * 60 * 60000   // In UTC+3
  const date = new Date(timestamp)
  const dateTimeStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0') + '-' +
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  return await fetchImage(`https://electro.ntsomz.ru/i/${type}/${dateTimeStr}.jpg`)
}
const src_elektro_l_2 = src_ntsomz('electro', 'splash')
const src_elektro_l_3 = src_ntsomz('electro', 'splash_l3')
const src_elektro_l_4 = src_ntsomz('electro', 'splash_l4')
const src_arktika_m_1 = src_ntsomz('arctic', 'splash')
const src_arktika_m_2 = src_ntsomz('arctic', 'splash_a2')

const sources = {
  'FY Geostationary IR 10.8u': src_fy_geos_ir,
  'FY Geostationary WV 7u': src_fy_geos_wv,
  'FY-4B Geo Color': src_fy4b_color,
  'FY-2H IR1 Color': src_fy2h_color,
  'GOES-19 GeoColor': src_goes19,
  'GOES-18 GeoColor': src_goes18,
  'Himawari IR B13': src_himawari_b13,
  'Himawari WV B08': src_himawari_b08,
  'Himawari TCR': src_himawari_tcr,
  'Meteosat IR 10.5u': src_meteosat_ir105,
  'Meteosat IR 0.39u': src_meteosat_ir039,
  'INSAT-3DS IR1 10.8u': src_insat_ir1,
  'INSAT-3DS MIR 3.9u': src_insat_mir,
  'GK2A RGB DAYNIGHT': src_gk2a_rgb_daynight,
  'GK2A IR 8.7u': src_gk2a_ir087,
  'Elektro-L 2': src_elektro_l_2,
  'Elektro-L 3': src_elektro_l_3,
  'Elektro-L 4': src_elektro_l_4,
  'Arktika-M 1': src_arktika_m_1,
  'Arktika-M 2': src_arktika_m_2,
}

const t = +new Date('2026-06-28T06:00:00.000Z')
// await Promise.all(Object.entries(sources).map(([key, fn]) => fn(t)))
for (const [key, fn] of Object.entries(sources)) console.log(sha3_224(await fn(t)).toHex())
