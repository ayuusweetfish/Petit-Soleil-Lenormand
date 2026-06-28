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

// GOES-19 (2024)
const src_goes19_noaa = async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const dayOfYear = Math.floor((date - Date.UTC(date.getUTCFullYear(), 0, 0)) / 86400_000)
  const yearDayStr =
    date.getUTCFullYear().toString() +
    dayOfYear.toString().padStart(3, '0')
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://cdn.star.nesdis.noaa.gov/GOES19/ABI/FD/GEOCOLOR/${yearDayStr}${hourMinStr}_GOES19-ABI-FD-GEOCOLOR-1808x1808.jpg`)
  return payload
}

// Himawari-9 (2016)
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

// Meteosat Second Generation (2015, etc.), Third Generation (2025, etc.)
const src_meteosat = (type) => async (timestamp) => {
  const date = new Date(timestamp)
  const dateTimeStr = date.toISOString()
  return await fetchImage(`https://view.eumetsat.int/geoserver/wms?service=WMS&version=1.3.0&request=GetMap&layers=${type},backgrounds:ne_10m_coastline&styles=&format=image/jpeg&srs=AUTO:97004,9001,0,0&bbox=-5450000,-5450000,5450000,5450000&width=1800&height=1800&time=${dateTimeStr}`)
}
const src_meteosat_ir105 = src_meteosat('mtg_fd:ir105_hrfi')
const src_meteosat_ir039 = src_meteosat('msg_fes:ir039')

// INSAT-3DS (2024)
const src_imd = (type) => async (timestamp) => {
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
const src_imd_ir1 = src_imd('IR1')
const src_imd_mir = src_imd('MIR')

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

// Elektro-L2 (2015), -L3 (2019), -L4 (2023)
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
  return await fetchImage(`https://electro.ntsomz.ru/i/${type}/${dateTimeStr}.jpg`)
}
const src_elektro_l2 = src_elektro_l('splash')
const src_elektro_l3 = src_elektro_l('splash_l3')

const sources = {
  'FY Geostationary IR 10.8u': src_fy_geostationary_ir,
  'FY Geostationary WV 7u': src_fy_geostationary_wv,
  'FY-4B Geo Color': src_fy4b_disk,
  'GOES-19 GeoColor': src_goes19_noaa,
  'Himawari-9 IR B13': src_himawari_b13,
  'Himawari-9 True Color Reproduction': src_himawari_trm,
  'Meteosat IR 10.5u': src_meteosat_ir105,
  'Meteosat IR 0.39u': src_meteosat_ir039,
  'INSAT-3DS IR1 10.8u': src_imd_ir1,
  'INSAT-3DS MIR 3.9u': src_imd_mir,
  'GK2A RGB DAYNIGHT': src_gk2a_rgb_daynight,
  'GK2A IR 8.7u': src_gk2a_ir087,
  'Elektro-L 2': src_elektro_l2,
  'Elektro-L 3': src_elektro_l3,
}

const t = +new Date('2026-06-28T06:00:00.000Z')
// await Promise.all(Object.entries(sources).map(([key, fn]) => fn(t)))
for (const [key, fn] of Object.entries(sources)) if (key.startsWith('Met')) await fn(t)
