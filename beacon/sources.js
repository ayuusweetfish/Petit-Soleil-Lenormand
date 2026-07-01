const fetchImage = async (url, modifiedAfter, modifiedBefore) => {
  console.log(`> ${url}`)
  try {
    const resp = await fetch(url, { signal: AbortSignal.timeout(60000) })
    const modifiedAt = (resp.headers.has('Last-Modified') ?
      new Date(resp.headers.get('Last-Modified')) : undefined)
    if (modifiedAfter !== undefined || modifiedBefore !== undefined) {
      if (modifiedAt === undefined) {
        throw new Error(`Do not know when last modified`)
      }
      if ((modifiedAfter && modifiedAt < modifiedAfter) ||
          (modifiedBefore && modifiedAt > modifiedBefore)) {
        throw new Error(`Modification timestamp ` +
          `${modifiedAt.toISOString()} not in ` +
          `${modifiedAfter.toISOString()}/${modifiedBefore.toISOString()}`)
      }
    }
    const payload = await resp.blob()
    if (resp.status >= 400 || !payload.type.startsWith('image/')) {
      throw new Error(`Received status ` +
        `${resp.status}, type ${payload.type}`)
    }
    const arr = new Uint8Array(await payload.arrayBuffer())
    arr._url = url
    arr._modifiedAt = modifiedAt
    console.log(`* ${url} -> size ${arr.length}, modification ` +
      `${modifiedAt ? modifiedAt.toISOString() : 'not given'}`)
    return arr
  } catch (e) {
    const tagged = new Error(`${e.message}`)
    tagged._url = url
    throw tagged
  }
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
  return await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/GEOS/MOS/${type}/PIC/GBAL/${dateStr}/GEOS_IMAGR_GBAL_L2_MOS_${type}_GLL_${dateStr}_${hourStr}00_10KM_MS.jpg`)
}
export const fy_geos_ir = src_fy_geos('IRX')
export const fy_geos_wv = src_fy_geos('WVX')
export const fy4b_color = async (timestamp) => {
  timestamp -= timestamp % (15 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const minute = date.getUTCMinutes()
  return await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/FY4B/AGRI/GCLR/DISK/FY4B-_AGRI--_N_DISK_1050E_L2-_GCLR_MULT_NOM_${dateStr}${hourStr}${minute.toString().padStart(2, '0')}00_${dateStr}${hourStr}${minute + 14}59_1000M_V0001.JPG`)
}
export const fy2h_color = async (timestamp) => {
  timestamp -= timestamp % (60 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  return await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/FY2H/NOM/ETV/FY2H_ETV_NOM_${dateStr}_${hourStr}00.jpg`)
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
  return await fetchImage(`https://cdn.star.nesdis.noaa.gov/${sat}/ABI/FD/GEOCOLOR/${yearDayStr}${hourMinStr}_${sat}-ABI-FD-GEOCOLOR-1808x1808.jpg`)
}
export const goes19 = src_goes('GOES19')
export const goes18 = src_goes('GOES18')

// Himawari-9 (2016)
const src_himawari = (type) => async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  return await fetchImage(`https://www.data.jma.go.jp/mscweb/data/himawari/img/fd_/fd__${type}_${hourMinStr}.jpg`,
    new Date(timestamp - 120 * 60000),
    new Date(timestamp + 120 * 60000))
}
export const himawari_b13 = src_himawari('b13')
export const himawari_b08 = src_himawari('b08')
export const himawari_tcr = src_himawari('trm')

// Meteosat Second Generation (2015, etc.), Third Generation (2025, etc.)
const src_meteosat = (type) => async (timestamp) => {
  const date = new Date(timestamp)
  const dateTimeStr = date.toISOString()
  return await fetchImage(`https://view.eumetsat.int/geoserver/wms?service=WMS&version=1.3.0&request=GetMap&layers=${type},backgrounds:ne_10m_coastline&styles=&format=image/jpeg&srs=AUTO:97004,9001,0,0&bbox=-5450000,-5450000,5450000,5450000&width=1800&height=1800&time=${dateTimeStr}`)
}
export const meteosat_ir105 = src_meteosat('mtg_fd:ir105_hrfi')
export const meteosat_ir039 = src_meteosat('msg_fes:ir039')

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
export const insat_ir1 = src_insat('IR1')
export const insat_mir = src_insat('MIR')

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
export const gk2a_rgb_daynight = src_gk2a('rgb-daynight')
export const gk2a_ir087 = src_gk2a('ir087')

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
  return await fetchImage(`https://${series}.ntsomz.ru/i/${type}/${dateTimeStr}.jpg`)
}
export const elektro_l_2 = src_ntsomz('electro', 'splash')
export const elektro_l_3 = src_ntsomz('electro', 'splash_l3')
export const elektro_l_4 = src_ntsomz('electro', 'splash_l4')
export const arktika_m_1 = src_ntsomz('arctic', 'splash')
export const arktika_m_2 = src_ntsomz('arctic', 'splash_a2')
