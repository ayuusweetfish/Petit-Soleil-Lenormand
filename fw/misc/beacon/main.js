// Beacons
const src_drand = async (timestamp) => {
  // https://api.drand.sh/8990e7a9aaed2ffed73dbd7092123d6f289930540d7651336225dc172e51b2ce/info
  const round = Math.floor((timestamp - 1595431050000) / 30000) + 1
  const payload = await (await fetch(`https://api.drand.sh/public/${round}`)).json()
  return payload['randomness']
}
const src_irb = (baseUrl) => async (timestamp) => {
  timestamp -= timestamp % 60000
  const payload = await (await fetch(`${baseUrl}${timestamp}`)).json()
  return payload['pulse']['localRandomValue']
}
const src_irb_nist = src_irb('https://beacon.nist.gov/beacon/2.0/pulse/time/')
// --unsafely-ignore-certificate-errors=beacon.inmetro.gov.br
const src_irb_inmetro_br = src_irb('https://beacon.inmetro.gov.br/beacon/2.1/pulse/time/')
const src_irb_uchile = src_irb('https://random.uchile.cl/beacon/2.1-beta/pulse?timeGE=')

const fetchImage = async (url, modifiedAfter) => {
  console.log(url)
  const resp = await fetch(url)
  if (modifiedAfter !== undefined) {
    if (!resp.headers.has('Last-Modified')) {
      throw new Error(`Do not know when last modified`)
    }
    const modifiedAt = new Date(resp.headers.get('Last-Modified'))
    if (modifiedAt < modifiedAfter) {
      throw new Error(`Not updated since ${modifiedAt.toISOString()}`)
    }
  }
  const payload = await resp.blob()
  if (resp.status >= 400 || !payload.type.startsWith('image/')) {
    throw new Error(`Received status ${resp.status}, type ${payload.type}`)
  }
  return payload
}

// Satellite images
const src_fengyun = async (timestamp) => {
  timestamp -= timestamp % (60 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCFullYear().toString() +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') +
    date.getUTCDate().toString().padStart(2, '0')
  const hourStr = date.getUTCHours().toString().padStart(2, '0')
  const payload = await fetchImage(`https://img.nsmc.org.cn/CLOUDIMAGE/GEOS/MOS/IRX/PIC/GBAL/${dateStr}/GEOS_IMAGR_GBAL_L2_MOS_IRX_GLL_${dateStr}_${hourStr}00_10KM_MS.jpg`)
  console.log(payload)
}
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
  console.log(payload)
}
const src_noaa = async (timestamp) => {
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
  console.log(payload)
}
const src_himawari = async (timestamp) => {
  timestamp -= timestamp % (10 * 60000)
  const date = new Date(timestamp)
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') +
    date.getUTCMinutes().toString().padStart(2, '0')
  const payload = await fetchImage(`https://www.data.jma.go.jp/mscweb/data/himawari/img/fd_/fd__trm_${hourMinStr}.jpg`,
    new Date(timestamp - 60 * 60000))
  console.log(payload)
}
const src_meteosat_eumetsat = async (timestamp) => {
  timestamp -= timestamp % (15 * 60000)
  const date = new Date(timestamp)
  const dateStr =
    date.getUTCDate().toString().padStart(2, '0') + '/' +
    (date.getUTCMonth() + 1).toString().padStart(2, '0') + '/' +
    (date.getUTCFullYear() % 100).toString().padStart(2, '0')
  const hourMinStr =
    date.getUTCHours().toString().padStart(2, '0') + ':' +
    date.getUTCMinutes().toString().padStart(2, '0')

  const page = await (await fetch('https://eumetview.eumetsat.int/static-images/MSG/IMAGERY/IR039/BW/FULLDISC/')).text()

  const reStr1 = `<option value=['"](\\d+)['"]>\\s*${dateStr}\\s+${hourMinStr}\\s+UTC\\s*</option>`
  const matched1 = page.match(new RegExp(reStr1))
  if (matched1 === null) {
    throw new Error('Cannot extract time from page')
  }
  const imageIndex = +matched1[1]
  const reStr2 = `array_nom_imagen\\[${imageIndex}\\].+['"](\\w+)['"]`
  const matched2 = page.match(new RegExp(reStr2))
  if (matched2 === null) {
    throw new Error('Cannot extract image name from page')
  }
  const imageName = matched2[1]

  const payload = await fetchImage(`https://eumetview.eumetsat.int/static-images/MSG/IMAGERY/IR039/BW/FULLDISC/IMAGESDisplay/${imageName}`)
  console.log(payload)
}

const timestamp = 1710826860000 - 30*60000 // Date.now()
/*
console.log(await src_drand(timestamp))
console.log(await src_irb_nist(timestamp))
console.log(await src_irb_inmetro_br(timestamp))
console.log(await src_irb_uchile(timestamp))
*/
/*
console.log(await src_fengyun(timestamp))
console.log(await src_fy4b_disk(timestamp))
console.log(await src_noaa(timestamp))
console.log(await src_himawari(timestamp))
*/
console.log(await src_meteosat_eumetsat(timestamp))
