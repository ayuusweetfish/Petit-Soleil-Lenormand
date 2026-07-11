// ECVRF-EDWARDS25519-SHA512-TAI
// https://www.ietf.org/rfc/rfc9381.html
// 5.5. ECVRF Ciphersuites, #section-5.5-5

// Ported & edited from draft Python implementation at
// https://github.com/nccgroup/draft-irtf-cfrg-vrf-06.git

import crypto from 'node:crypto'
import { Point } from 'npm:@noble/ed25519'

// Constants
const ORDER = 2n ** 252n + 27742317777372353535851937790883648493n
const COFACTOR = 8n

// Saner modulus operation
const mod = (n, m) => {
  return ((n % m) + m) % m
}

// Bytes <-> BigInt conversion
const bytesToBigIntLE = (buf) => {
  let val = 0n
  for (let i = buf.length - 1; i >= 0; i--) {
    val = (val << 8n) + BigInt(buf[i])
  }
  return val
}

const bigIntToBytesLE = (val, width) => {
  const buf = Buffer.alloc(width)
  for (let i = 0; i < width; i++) {
    buf[i] = Number(val & 0xffn)
    val >>= 8n
  }
  return buf
}

const _hash = (message) => {
  return crypto.createHash('sha512').update(message).digest()
}

const _get_secret_scalar = (sk) => {
  const h = _hash(sk).subarray(0, 32)
  h[31] = (h[31] & 0x7f) | 0x40
  h[0] = h[0] & 0xf8
  return bytesToBigIntLE(h)
}

// Internal functions

const SUITE_STRING = Buffer.from([0x03])

// Section 5.4.1.1. ECVRF_encode_to_curve_try_and_increment
const _ecvrf_hash_to_curve_tai = (suite_string, y, alpha_string) => {
  for (let ctr = 0; ctr < 256; ctr++) {
    const ctr_string = Buffer.from([ctr])

    const hash_input = Buffer.concat([
      suite_string,
      Buffer.from([0x01]),
      y,
      alpha_string,
      ctr_string,
      Buffer.from([0x00]),
    ])
    const h_string = _hash(hash_input)

    try {
      const h_prelim = Point.fromBytes(h_string.subarray(0, 32))

      const h = h_prelim.multiply(COFACTOR)
      if (h.is0()) continue

      return h.toBytes()
    } catch (e) {
      if (e.message.startsWith('bad point'))
        continue
      throw e   // Should be unexpected
    }
  }

  // Chances of reaching here are astronomically low (2^-256).
  // While the spec says the loop continues indefinitely, we would simply bail out
  return 'INVALID'
}

const _ecvrf_nonce_generation_rfc8032 = (sk, h_string) => {
  const hashed_sk_string = _hash(sk)
  const truncated_hashed_sk_string = hashed_sk_string.subarray(32)
  const k_string = _hash(Buffer.concat([truncated_hashed_sk_string, h_string]))
  return bytesToBigIntLE(k_string) % ORDER
}

const _ecvrf_hash_points = (y, p1, p2, p3, p4) => {
  const string = Buffer.concat([
    SUITE_STRING,
    Buffer.from([0x02]),
    y,
    p1.toBytes(),
    p2.toBytes(),
    p3.toBytes(),
    p4.toBytes(),
    Buffer.from([0x00]),
  ])
  const c_string = _hash(string)
  return bytesToBigIntLE(c_string.subarray(0, 16))
}

// Public API

// Section 5.1. ECVRF Proving
export const ecvrf_prove = (sk, alpha_string) => {
  const secret_scalar_x = _get_secret_scalar(sk)
  const public_key_y = get_public_key(sk)

  const h_string = _ecvrf_hash_to_curve_tai(SUITE_STRING, public_key_y, alpha_string)
  if (h_string === 'INVALID') return ['INVALID', Buffer.alloc(0)]

  let h_point
  try {
    h_point = Point.fromBytes(h_string)
  } catch {
    return ['INVALID', Buffer.alloc(0)]
  }

  const gamma = h_point.multiply(mod(secret_scalar_x, ORDER))
  const k = _ecvrf_nonce_generation_rfc8032(sk, h_string)

  const k_b = Point.BASE.multiply(k)
  const k_h = h_point.multiply(k)
  const c = _ecvrf_hash_points(public_key_y, h_point, gamma, k_b, k_h)

  const s = mod(k + c * secret_scalar_x, ORDER)

  const pi_string = Buffer.concat([
    gamma.toBytes(),
    bigIntToBytesLE(c, 16),
    bigIntToBytesLE(s, 32),
  ])

  return ['VALID', pi_string]
}

// Section 5.2. ECVRF Proof To Hash
export const ecvrf_proof_to_hash = (pi_string) => {
  if (pi_string.length !== 80) return ['INVALID', Buffer.alloc(0)]

  const gamma_string = pi_string.subarray(0, 32)
  const c_string = pi_string.subarray(32, 48) // Unused
  const s_string = pi_string.subarray(48)     // Unused

  let gamma
  try {
    gamma = Point.fromBytes(gamma_string)
  } catch (e) {
    return ['INVALID', Buffer.alloc(0)]
  }

  const cofactor_gamma = gamma.multiply(COFACTOR)

  const beta_string = _hash(Buffer.concat([
    SUITE_STRING,
    Buffer.from([0x03]),
    cofactor_gamma.toBytes(),
    Buffer.from([0x00]),
  ]))

  return ['VALID', beta_string]
}

// Section 5.3. ECVRF Verifying
export const ecvrf_verify = (y, alpha_string, pi_string) => {
  if (pi_string.length !== 80) return ['INVALID', Buffer.alloc(0)]

  let y_point
  try {
    y_point = Point.fromBytes(y)
  } catch (e) {
    return ['INVALID', Buffer.alloc(0)]
  }

  const gamma_string = pi_string.subarray(0, 32)
  const c_string = pi_string.subarray(32, 48)
  const s_string = pi_string.subarray(48, 80)

  const c = bytesToBigIntLE(c_string)
  const s = bytesToBigIntLE(s_string)

  let gamma
  try {
    gamma = Point.fromBytes(gamma_string)
  } catch (e) {
    return ['INVALID', Buffer.alloc(0)]
  }

  const h_string = _ecvrf_hash_to_curve_tai(SUITE_STRING, y, alpha_string)
  if (h_string === 'INVALID') return ['INVALID', Buffer.alloc(0)]

  let h_point
  try {
    h_point = Point.fromBytes(h_string)
  } catch (e) {
    return ['INVALID', Buffer.alloc(0)]
  }

  // U = s*B - c*Y
  const U = Point.BASE.multiply(s).subtract((y_point.multiply(c)))
  // V = s*H - c*Gamma
  const V = h_point.multiply(s).subtract(gamma.multiply(c))

  const derived_c = _ecvrf_hash_points(y, h_point, gamma, U, V)
  if (c !== derived_c) return ['INVALID', Buffer.alloc(0)]

  return ecvrf_proof_to_hash(pi_string)
}

export const get_public_key = (sk) => {
  const secret_int = _get_secret_scalar(sk)
  const public_point = Point.BASE.multiply(mod(secret_int, ORDER))
  return public_point.toBytes()
}

Deno.test('ECVRF-EDWARDS25519-SHA512-TAI', () => {
  const test = (secret_key, public_key_expected, input, proof_str_expected, output_str_expected) => {
    console.log('Secret key', secret_key, 'Input', input)

    const public_key = get_public_key(Buffer.fromHex(secret_key)).toHex()
    console.log('Public key', public_key)
    if (public_key !== public_key_expected) throw new Error('public key')

    const proof_str = ecvrf_prove(Buffer.fromHex(secret_key), Buffer.fromHex(input))[1].toHex()
    console.log('Proof', proof_str)
    if (proof_str !== proof_str_expected) throw new Error('proof')

    const output_str = ecvrf_proof_to_hash(Buffer.fromHex(proof_str))[1].toHex()
    console.log('Output', output_str)
    if (output_str !== output_str_expected) throw new Error('output')

    const verify_result = ecvrf_verify(
      Buffer.fromHex(public_key),
      Buffer.fromHex(input),
      Buffer.fromHex(proof_str)
    )
    if (verify_result[0] !== 'VALID') throw new Error('verify')
    if (verify_result[1].toHex() !== output_str) throw new Error('verify')
  }

  // Test vectors from B.3. ECVRF-EDWARDS25519-SHA512-TAI
  test(
    '9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60',
    'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a',
    '',
    '8657106690b5526245a92b003bb079ccd1a92130477671f6fc01ad16f26f723f26f8a57ccaed74ee1b190bed1f479d9727d2d0f9b005a6e456a35d4fb0daab1268a1b0db10836d9826a528ca76567805',
    '90cf1df3b703cce59e2a35b925d411164068269d7b2d29f3301c03dd757876ff66b71dda49d2de59d03450451af026798e8f81cd2e333de5cdf4f3e140fdd8ae'
  )
  test(
    '4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb',
    '3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c',
    '72',
    'f3141cd382dc42909d19ec5110469e4feae18300e94f304590abdced48aed5933bf0864a62558b3ed7f2fea45c92a465301b3bbf5e3e54ddf2d935be3b67926da3ef39226bbc355bdc9850112c8f4b02',
    'eb4440665d3891d668e7e0fcaf587f1b4bd7fbfe99d0eb2211ccec90496310eb5e33821bc613efb94db5e5b54c70a848a0bef4553a41befc57663b56373a5031',
  )
  test(
    'c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7',
    'fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025',
    'af82',
    '9bc0f79119cc5604bf02d23b4caede71393cedfbb191434dd016d30177ccbf8096bb474e53895c362d8628ee9f9ea3c0e52c7a5c691b6c18c9979866568add7a2d41b00b05081ed0f58ee5e31b3a970e',
    '645427e5d00c62a23fb703732fa5d892940935942101e456ecca7bb217c61c452118fec1219202a0edcf038bb6373241578be7217ba85a2687f7a0310b2df19f',
  )
})
