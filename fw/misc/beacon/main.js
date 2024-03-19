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

const timestamp = 1710826860000 // Date.now()
console.log(await src_drand(timestamp))
console.log(await src_irb_nist(timestamp))
console.log(await src_irb_inmetro_br(timestamp))
console.log(await src_irb_uchile(timestamp))
