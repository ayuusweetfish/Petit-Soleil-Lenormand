// Sources

import * as sources from './sources.js'

const keyedSources = {
  'FY Geostationary IR 10.8u': sources.fy_geos_ir,
  'FY Geostationary WV 7u': sources.fy_geos_wv,
  'FY-4B Geo Color': sources.fy4b_color,
  'FY-2H IR1 Color': sources.fy2h_color,
  'GOES-19 GeoColor': sources.goes19,
  'GOES-18 GeoColor': sources.goes18,
  'Himawari IR B13': sources.himawari_b13,
  'Himawari WV B08': sources.himawari_b08,
  'Himawari TCR': sources.himawari_tcr,
  'Meteosat IR 10.5u': sources.meteosat_ir105,
  'Meteosat IR 0.39u': sources.meteosat_ir039,
  'INSAT-3DS IR1 10.8u': sources.insat_ir1,
  'INSAT-3DS MIR 3.9u': sources.insat_mir,
  'GK2A RGB DAYNIGHT': sources.gk2a_rgb_daynight,
  'GK2A IR 8.7u': sources.gk2a_ir087,
  'Elektro-L 2': sources.elektro_l_2,
  'Elektro-L 3': sources.elektro_l_3,
  'Elektro-L 4': sources.elektro_l_4,
  'Arktika-M 1': sources.arktika_m_1,
  'Arktika-M 2': sources.arktika_m_2,
}

// Cryptographic primitives

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

// Application logic

const beaconPulseTimestamp = (t) => {
  const timestamp = (t || Date.now()) - 9 * 60 * 60000
  return timestamp - timestamp % (60 * 60000)
}

const fetchSources = async (records, timestamp) => {
  const zip = (...as) => [...as[0]].map((_, i) => as.map((a) => a[i]))

  const missingSources = Object.entries(keyedSources)
    .filter(([key, fn]) => !records[key] || !records[key].digest)
  const promises = missingSources.map(([_, fn]) => fn(timestamp))
  const results = await Promise.allSettled(promises)
  for (const [[key, _], result] of zip(missingSources, results)) {
    if (result.status === 'fulfilled') {
      console.log(key, result.value, result)
      records[key] = {
        digest: sha3_224(result.value).toHex(),
        length: result.value.length,
        url: result.value._url,
        message:
          result.value._modifiedAt ?
          result.value._modifiedAt.toISOString() : '-',
      }
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

Deno.test('fetchSources', async () => {
  const t = beaconPulseTimestamp()
  console.log(t)
  const c = {}
  await fetchSources(c, t)
  console.log(c)
  await fetchSources(c, t)
  console.log(c)
})
