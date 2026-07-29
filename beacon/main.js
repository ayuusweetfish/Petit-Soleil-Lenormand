// ============ Entropy sources ============ //

import * as sources from './sources.js'

const keyedSources = {
/*
  'FY Geostationary IR 10.8u': sources.fy_geos_ir,
  'FY Geostationary WV 7u': sources.fy_geos_wv,
  'FY-4B Geo Color': sources.fy4b_color,
  'FY-2H IR1 Color': sources.fy2h_color,
  'GOES-19 GeoColor': sources.goes19,
  'GOES-18 GeoColor': sources.goes18,
  'Himawari IR B13': sources.himawari_b13,
  'Himawari WV B08': sources.himawari_b08,
  'Himawari TCR': sources.himawari_tcr,
*/
  'Meteosat IR 10.5u': sources.meteosat_ir105,
  'Meteosat IR 0.39u': sources.meteosat_ir039,
/*
  'INSAT-3DS IR1 10.8u': sources.insat_ir1,
  'INSAT-3DS MIR 3.9u': sources.insat_mir,
  'GK2A RGB DAYNIGHT': sources.gk2a_rgb_daynight,
  'GK2A IR 8.7u': sources.gk2a_ir087,
  'Elektro-L 2': sources.elektro_l_2,
  'Elektro-L 3': sources.elektro_l_3,
  'Elektro-L 4': sources.elektro_l_4,
  'Arktika-M 1': sources.arktika_m_1,
  'Arktika-M 2': sources.arktika_m_2,
*/
}

// ============ Cryptographic primitives ============ //

import { createHash } from 'node:crypto'

const sha3_224 = (a) => createHash('sha3-224').update(a).digest()

const parallelOracle = (N, local, files) => {
  const a = new Uint8Array(N)
  for (let i = 0; i < N / 64; i++) {
    const h = createHash('sha3-512')
    h.update(new Uint8Array([(local[0] + i) % 256]))
    h.update(local.subarray(1))
    for (const f of files) h.update(f)
    a.set(h.digest(), 64 * i)
  }
  return a
}

Deno.test('parallelOracle', () => {
  const output = parallelOracle(4096,
    new Uint8Array([0x15, 0xb0, 0x3a, 0xb5, 0xe6, 0x93, 0x43, 0xbb, 0xf8, 0xc2, 0xb4, 0x67, 0xaf, 0x2b, 0xe0, 0x78, 0xf0, 0x4f, 0x6d, 0x63, 0xef, 0x4c, 0x96, 0xe1, 0xe2, 0xdb, 0xc4, 0x64, 0xcc, 0x2d, 0xe7, 0xc1, 0xc7, 0x54, 0x19, 0x06, 0xcf, 0x40, 0x31, 0x11, 0x69, 0x78, 0x16, 0x5b, 0x50, 0x68, 0x0f, 0x7f, 0x11, 0x29, 0xab, 0x41, 0xc8, 0x0e, 0xb1, 0x80, 0xa1, 0xab, 0x7a, 0x2a, 0xf5, 0x6b, 0x64, 0xfe]),
    [new TextEncoder().encode('hel'), new TextEncoder().encode('lo')]
  )
  if (output.toHex().substring(0, 8) !== 'daa90909') throw new Error('-')
  if (output.toHex().substring(8192 - 8) !== '7057e87e') throw new Error('-')
})

// ============ Application logic ============ //

const beaconPulseTimestamp = (offsetHours, t0) => {
  const timestamp = (t0 || Date.now()) + offsetHours * 60 * 60000
  return timestamp - timestamp % (60 * 60000)
}

const digestedBlocks = {}

const fetchSources = async (timestamp, records) => {
  records = records || {}

  const zip = (...as) => [...as[0]].map((_, i) => as.map((a) => a[i]))

  const missingSources = Object.entries(keyedSources)
    .filter(([key, fn]) => !records[key] || !records[key].digest)
  const promises = missingSources.map(([_, fn]) => fn(timestamp))
  const results = await Promise.allSettled(promises)
  for (const [[key, _], result] of zip(missingSources, results)) {
    if (result.status === 'fulfilled') {
      const digest = sha3_224(result.value).toHex()
      records[key] = {
        digest: digest,
        length: result.value.length,
        url: result.value._url,
        message:
          result.value._modifiedAt ?
          result.value._modifiedAt.toISOString() : '-',
      }
      digestedBlocks[digest] = result.value
    } else {
      records[key] = {
        digest: null,
        length: null,
        url: result.reason._url || null,
        message: result.reason.message,
      }
    }
  }
  return records
}

if (0) Deno.test('fetchSources', async () => {
  const t0 = beaconPulseTimestamp(0)
  const t = beaconPulseTimestamp(-9, t0)
  console.log(t)
  const c = await fetchSources(t)
  console.log(c)
  await fetchSources(t, c)
  console.log(c)
})

import * as db from './db.js'
import * as ecvrf from './ecvrf.js'

const findPulse = async (t) => {
  let pulseRecord = await db.getPulse(t)
  if (pulseRecord.output === null) return null
  pulseRecord.pulse = t
  return pulseRecord
}

const getPreviousPulse = async (t) => {
  const pulse = await db.getPreviousPulseTimestsamp(t)
  const output = pulse !== null ?
    (await findPulse(pulse)).output : new Uint8Array(4096)
  return { pulse, output }
}

const findOrCreatePulse = async (t) => {
  let pulseRecord = await db.getPulse(t)
  if (pulseRecord.output === null) {
    // Local VRF
    const vrfSecretKey = Buffer.fromHex('4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb')
    const vrfPublicKey = ecvrf.get_public_key(vrfSecretKey)
    const [, vrfProof] = ecvrf.ecvrf_prove(
      vrfSecretKey, new TextEncoder().encode(t.toString()))
    const [, vrfOutput] = ecvrf.ecvrf_proof_to_hash(vrfProof)

    // External sources
    const sourceDetails = await fetchSources(t)
    const sourceBlocks = Object.values(sourceDetails)
      .filter((o) => o.digest !== null)
      .map((o) => digestedBlocks[o.digest])
    const output = parallelOracle(4096, vrfOutput, sourceBlocks)

    // Previous pulse
    const prevOutput = (await getPreviousPulse(t)).output
    for (let i = 0; i < output.length; i++) output[i] ^= prevOutput[i]

    // Save
    const pulseDetails = {
      sources: sourceDetails,
      vrf_pk: vrfPublicKey.toHex(),
      vrf_proof: vrfProof.toHex(),
    }
    await db.setBeaconOutput(t, pulseDetails, output)
    pulseRecord.details = pulseDetails
    pulseRecord.output = output
    // TODO: Record source blocks?
  }
  pulseRecord.pulse = t
  return pulseRecord
}

const getLatestPulse = async () => {
  const t = await db.getLatestPulseTimestsamp()
  // Return null if no pulse has been constructed. This does not happen in normal operation
  return t !== null ? await findPulse(t) : null
}

const printPulse = (pulseRecord) => {
  const { pulse, details, output } = pulseRecord
  console.log(pulse)
  console.log('curl --parallel \\\n' + Object.values(details.sources)
    .filter((o) => o.digest !== null)
    .map((o, i) => `'${o.url}' -o ${i.toString().padStart(2, '0')}.bin`)
    .join(' \\\n')
  )
  console.log(`openssl dgst -sha3-224 *.bin`)
  console.log(Object.values(details.sources)
    .filter((o) => o.digest !== null)
    .map((o, i) => `SHA3-224(${i.toString().padStart(2, '0')}.bin)= ${o.digest}`)
    .join('\n')
  )
  const filesList = 
    Object.values(details.sources)
      .filter((o) => o.digest !== null)
      .map((o, i) => `${i.toString().padStart(2, '0')}.bin`)
      .join(' ')
  console.log(`
LOCAL=$(./vrf-verify ${details.vrf_pk} ${pulse} ${details.vrf_proof})
i=0; while [ $i -lt 64 ]; do echo $LOCAL | LC_ALL=C awk '{ for (i = 0; i < 256; i++) { hex[sprintf("%02x", i)] = i; } for (i = 1; i <= length($0); i += 2) { printf("%c", (hex[substr($0, i, 2)] + (i == 1 ? '"$i"' : 0)) % 256) } }' | cat - ${filesList} | openssl dgst -sha3-512 | awk '{ printf "%s", $2 }'; i=$((i + 1)); done
  `.trim())
  console.log(output.toHex())
}

Deno.test('findOrCreatePulse', async () => {
  const t0 = beaconPulseTimestamp(0)
  const pulseRecord = await findOrCreatePulse(beaconPulseTimestamp(-9, t0))
  printPulse(pulseRecord)
})

;(async () => {
  await findOrCreatePulse(beaconPulseTimestamp(-9))
})()

// ============ Web server ============ //

const log = (msg) => console.log(`${(new Date()).toISOString()} ${msg}`)

const renderTemplate = (s, args, lang, extra) => {
  extra = extra || {}
  extra.lang = (lang === 'zh' ? 'zh-Hans' : lang)
  return s.replaceAll(/^{{\s*@([a-zA-Z-]+)\s*}}(.+\n)/gm, (_, capturedLang, content) => {
    return (capturedLang === lang ? content : '')
  }).replaceAll(/{{~(.*)\s*([0-9A-Za-z_]+)\s*([^]*\S)\s*\1~(?:}}|-}}\s*)/gm, (_, _delim, key, w) => {
    const list = args[key] || []
    return list.map((entry, index) =>
      renderTemplate(w, entry, lang, { index: (index + 1).toString().padStart(2, '0') })
    ).join('')
  }).replaceAll(/{{\s*([0-9A-Za-z_]+)\s*}}/g, (_, w) => {
    return args[w] !== undefined ? args[w].toString() :
           extra[w] !== undefined ? extra[w].toString() : ''
  })
}

const parseCookies = (cookiesStr) => {
  const cookies = {}
  const regexp = /([A-Za-z0-9-_]+)=(.*?)(?:(?=;)|$)/g
  let result
  while ((result = regexp.exec(cookiesStr)) !== null) {
    const [_, key, value] = result
    cookies[decodeURIComponent(key)] = decodeURIComponent(value)
  }
  return cookies
}

const negotiateLang = (accept, supported) => {
  const list = accept.split(',').map((s) => {
    s = s.trim()
    let q = 1
    const pos = s.indexOf(';q=')
    if (pos !== -1) {
      const parsed = parseFloat(s.substring(pos + 3))
      if (isFinite(parsed)) q = parsed
      s = s.substring(0, pos).trim()
    }
    return { lang: s, q }
  })

  let bestScore = 0
  let bestLang = supported[0]
  for (const l of supported) {
    for (const { lang, q } of list) {
      if (lang.substring(0, 2) === l.substring(0, 2)) {
        const score = q + (lang === l ? 0.2 : 0)
        if (score > bestScore)
          [bestScore, bestLang] = [score, l]
      }
    }
  }
  return bestLang
}

const openFile = async (path, byteStart, byteEnd) => {
  let file

  try {
    file = await Deno.open(path)
  } catch (e) {
    if (e instanceof Deno.errors.NotFound) return null
    if (e.code === 'EISDIR') return null
    throw e
  }

  const fileInfo = await file.stat()
  if (fileInfo.isDirectory) return null

  const hash = (x, y, s) => {
    x = Math.imul(((x << 5) | (x >>> 27)) ^ y, 0x9e3779b9)
    for (let i = 0; i < s.length; i++)
      x = Math.imul(((x << 5) | (x >>> 27)) ^ s.charCodeAt(i), 0x9e3779b9)
    return (x >>> 1).toString(36).padStart(6, '0')
  }
  const etag = `"${hash(+fileInfo.mtime, fileInfo.size, path)}"`

  // Ranges
  const fileSize = fileInfo.size
  if (byteStart === undefined) byteStart = 0
  if (byteEnd === undefined) byteEnd = fileSize - 1
  if (!(byteStart >= 0 && byteEnd < fileSize && byteStart <= byteEnd))
    return { path, fileSize, etag, rangeValid: false }

  file.seek(byteStart, Deno.SeekMode.Start)

  return {
    path, stream: file.readable, fileSize, etag,
    byteStart, byteEnd, rangeValid: true,
  }
}
const mime = (s) => {
  const ext = s.substring(s.lastIndexOf('.') + 1).toLowerCase()
  switch (ext) {
    case 'html': return 'text/html; charset=UTF-8'
    case 'css': return 'text/css; charset=UTF-8'
    case 'js': return 'application/javascript; charset=UTF-8'
    case 'woff2': return 'font/woff2'
    case 'woff': return 'font/woff'
    case 'svg': return 'image/svg+xml'
    case 'png': return 'image/png'
    case 'jpeg': case 'jpg': return 'image/jpeg'
    case 'mp4': return 'video/mp4'
  }
  return 'application/octet-stream'
}
class Truncate extends TransformStream {
  constructor(limit) {
    super({
      transform(chunk, controller) {
        controller.enqueue(chunk.slice(0, limit))
        if ((limit -= chunk.length) <= 0) controller.terminate()
      },
    })
  }
}
const serveFile = async (req, path) => {
  const rangeHeader = req.headers.get('Range')
  const result = /bytes=(\d+)-(\d+)?/g.exec(rangeHeader)
  const reqByteStart = (result && result[1]) ? +result[1] : undefined
  const reqByteEnd = (result && result[2]) ? +result[2] : undefined

  const { path: realPath, stream, fileSize, etag, byteStart, byteEnd, rangeValid } =
    await openFile(path, reqByteStart, reqByteEnd)

  const headers = new Headers()
  headers.set('Content-Type', mime(realPath))
  headers.set('ETag', etag)
  headers.set('Accept-Ranges', 'bytes')
  if (!rangeValid) {
    headers.set('Content-Range', `bytes */${fileSize}`)
    return new Response(null, { status: 416, headers })
  }
  headers.set('Cache-Control', 'public, max-age=600')
  if (req.headers.get('If-None-Match') === etag) {
    headers.delete('Content-Type')
    return new Response(null, { status: 304, headers })
  }
  headers.set('Content-Length', (byteEnd - byteStart + 1).toString())
  if (reqByteStart !== undefined) {
    headers.set('Content-Range', `bytes ${byteStart}-${byteEnd}/${fileSize}`)
    return new Response(stream.pipeThrough(new Truncate(byteEnd - byteStart + 1)),
      { status: 206, headers })
  }
  return new Response(stream, { status: 200, headers })
}

const serveReq = async (req, info) => {
  const url = new URL(req.url)

  if (req.method === 'GET' &&
    (url.pathname === '/' || url.pathname === '/verify' || url.pathname === '/gallery')
  ) {
    const pageName = url.pathname.substring(1) || 'index'
    let selLang = url.search.substring(1)
    if (selLang) {
      const lang = negotiateLang(selLang, ['en', 'zh'])
      const redirectUrl = url.origin + url.pathname
      return new Response(
        `<html><body>Redirecting to <a href='${redirectUrl}'>${redirectUrl}</a></body></html>`,
        {
          status: 303,
          headers: {
            'Location': redirectUrl,
            'Set-Cookie': `lang=${lang}; SameSite=Strict; Path=/; Secure; Max-Age=86400`,
          },
        }
      )
    }
    let cookieLang
    if (!selLang) selLang = cookieLang = parseCookies(req.headers.get('Cookie') || '')['lang']
    if (!selLang) selLang = req.headers.get('Accept-Language')
    const lang = negotiateLang(selLang || '', ['en', 'zh'])

    const args = {}

    // Fill template arguments
    const pulseRecord = await getLatestPulse()

    // - Base, output
    const dateStrISO = (t) =>
      (new Date(t)).toISOString().replace('T', ' ').replace(':00.000Z', ' UTC')
    args.latestTimestamp = pulseRecord.pulse
    args.latestTimestampISO = dateStrISO(pulseRecord.pulse)

    const prefixSuffix = (s) => {
      if (s instanceof Uint8Array) s = encodeHex(s)
      return (s.substring(0, 16) + '...' + s.substring(s.length - 16))
    }
    args.latestOutputPrefixSuffix = prefixSuffix(pulseRecord.output.toHex())

    const emojis = Array.from(
      '🍇🍈🍉🍊🍋🍌🍍🥭🍎🍏🍐🍑🍒🍓🫐🥝🍅🫒🥥🥑🍆🥔🥕🌽🌶🫑🥒🥬🥦🧄🧅🥜🫘🌰🫚🫛🍦🍧🍨🍩🍪🎂🍰🧁🥧🍫🍬🍭🍮🍯' +
      '⭐🌟🌠🌌🦄💐🌸💮🪷🏵️🌹🌺🌻🌼🌷🪻🌱🪴🌲🌳🌴🌵🌾🌿☘️🍀🍁🍃🍄🪨🪵🌊' +
      '🎃🎆🎇🧨✨🎈🎉🎊🎐🎑🏮💖💟❣❤🩷🧡💛💚💙🩵💜🤎🖤🩶🤍🎵🎶🎹🥁🔮🪁🪄🎨🌈🫧💡🕯️🎀'
    ).filter((c) => c.codePointAt(0) !== 0xfe0f)
    // console.log(emojis.map((c) => c.codePointAt(0).toString(16)).sort().join('\n'))
    const minutes = Math.floor((Date.now() - pulseRecord.pulse) / 60000 - 9 * 60);
    let n = 0
    for (let i = minutes; i < pulseRecord.output.length; i += 120)
      n += pulseRecord.output[i]
    const char = emojis[n % emojis.length]
    args.randomEmoji = ` + <img class='emoji-icon' alt='${char}' src='/emoji/${char.codePointAt(0).toString(16)}.svg'>`

    // - VRF
    args.vrfPk = pulseRecord.details.vrf_pk
    args.vrfProof = pulseRecord.details.vrf_proof
    const [, vrfOutput] = ecvrf.ecvrf_proof_to_hash(Buffer.fromHex(pulseRecord.details.vrf_proof))
    args.vrfOutputPrefixSuffix = prefixSuffix(vrfOutput.toHex())

    // - Images
    // XXX: This is primitive. May need adaptation if sources are extended
    const detectExt = (url) =>
      url.toLowerCase().endsWith('.png') ? 'png' : 'jpg'
    args.details = Object.values(pulseRecord.details.sources).map(
      (o) => ({ url: o.url, extension: detectExt(o.url), digest: o.digest }))

    // - Previous
    const prevPulse = await getPreviousPulse(pulseRecord.pulse)
    args.previousTimestamp = prevPulse.pulse || 0
    args.previousTimestampISO = dateStrISO(prevPulse.pulse || 0)
    args.previousOutputPrefixSuffix = prefixSuffix(prevPulse.output.toHex())

    const curCombinedHash = new Uint8Array(pulseRecord.output.length)
    for (let i = 0; i < curCombinedHash.length; i++)
      curCombinedHash[i] = pulseRecord.output[i] ^ prevPulse.output[i]
    args.currentCombinedPrefixSuffix = prefixSuffix(curCombinedHash.toHex())

    // Render the page
    const templateFrame = await Deno.readTextFile('page/frame.html')
    const templateContent = await Deno.readTextFile(`page/${pageName}.html`)
    let content = renderTemplate(templateContent, args, lang)
    let title
    content = content.replace(/^<title>(.+)<\/title>\n/, (_, matchedTitle) => {
      title = matchedTitle
      return ''
    })
    title = (title ? (title + ' — ') : '')
    Object.assign(args, { title, content })
    const page = renderTemplate(templateFrame, args, lang)
    const headers = {
      'Content-Type': 'text/html; encoding=utf-8',
    }
    if (cookieLang !== lang) {
      headers['Set-Cookie'] =
        `lang=${lang}; SameSite=Strict; Path=/; Secure; Max-Age=86400`
    }
    return new Response(page, { headers })
  }

  if (req.method === 'GET') {
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
  }

  throw new Error(`[404] Void space, please return`)
}

const port = 3321
const server = Deno.serve({
  port,
  onListen: () => log(`Running at http://localhost:${port}/`),
}, async (req, info) => {
  try {
    return await serveReq(req, info)
  } catch (e) {
    let status = 500
    const message = e.message.replace(/^\[([0-9]{3})\] /, (_, n) => ((status = +n), ''))
    if (status === 500) log(e), console.log(e)
    return new Response(message, { status })
  }
})
