import assert from 'node:assert/strict'
import { generateKeyPairSync, verify } from 'node:crypto'
import {
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import test from 'node:test'

const TEST_DIR = dirname(fileURLToPath(import.meta.url))
const SIGNER = resolve(TEST_DIR, '..', 'sign_runtime_manifest.mjs')

function createKeyFiles(directory, prefix) {
  const { privateKey, publicKey } = generateKeyPairSync('ed25519')
  const privateKeyPath = join(directory, `${prefix}-private.pem`)
  const publicKeyPath = join(directory, `${prefix}-public.pem`)
  writeFileSync(
    privateKeyPath,
    privateKey.export({ type: 'pkcs8', format: 'pem' }),
    { mode: 0o600 },
  )
  writeFileSync(
    publicKeyPath,
    publicKey.export({ type: 'spki', format: 'pem' }),
  )
  return { privateKeyPath, publicKeyPath, publicKey }
}

function runSigner(args) {
  const childEnvironment = { ...process.env }
  delete childEnvironment.NODE_TEST_CONTEXT
  return spawnSync(process.execPath, [SIGNER, ...args], {
    encoding: 'utf8',
    env: childEnvironment,
  })
}

test('signs raw manifest bytes and detects any byte change', () => {
  const directory = mkdtempSync(join(tmpdir(), 'runtime-manifest-signing-'))
  try {
    const keys = createKeyFiles(directory, 'release')
    const manifestPath = join(directory, 'manifest.json')
    const signaturePath = `${manifestPath}.sig`
    const manifestBytes = Buffer.from('{\n  "schema_version": 1\n}\n', 'utf8')
    writeFileSync(manifestPath, manifestBytes)

    const preflight = runSigner([
      '--check-key-pair',
      '--private-key-file', keys.privateKeyPath,
      '--public-key-file', keys.publicKeyPath,
    ])
    assert.equal(preflight.status, 0, preflight.stderr)

    const result = runSigner([
      '--manifest', manifestPath,
      '--key-id', 'czur-runtime-release-2026',
      '--private-key-file', keys.privateKeyPath,
      '--public-key-file', keys.publicKeyPath,
    ])
    assert.equal(result.status, 0, result.stderr)

    const envelopeBytes = readFileSync(signaturePath)
    assert.equal(envelopeBytes.at(-1), 0x0a)
    const envelope = JSON.parse(envelopeBytes.toString('utf8'))
    assert.deepEqual(
      {
        schema_version: envelope.schema_version,
        algorithm: envelope.algorithm,
        key_id: envelope.key_id,
      },
      {
        schema_version: 1,
        algorithm: 'ed25519',
        key_id: 'czur-runtime-release-2026',
      },
    )
    const signature = Buffer.from(envelope.signature, 'base64')
    assert.equal(verify(null, manifestBytes, keys.publicKey, signature), true)
    assert.equal(
      verify(null, Buffer.concat([manifestBytes, Buffer.from(' ')]), keys.publicKey, signature),
      false,
    )

    const otherKeys = createKeyFiles(directory, 'other')
    assert.equal(verify(null, manifestBytes, otherKeys.publicKey, signature), false)
  } finally {
    rmSync(directory, { recursive: true, force: true })
  }
})

test('rejects a mismatched public key before writing a signature', () => {
  const directory = mkdtempSync(join(tmpdir(), 'runtime-manifest-key-mismatch-'))
  try {
    const releaseKeys = createKeyFiles(directory, 'release')
    const otherKeys = createKeyFiles(directory, 'other')
    const preflight = runSigner([
      '--check-key-pair',
      '--private-key-file', releaseKeys.privateKeyPath,
      '--public-key-file', otherKeys.publicKeyPath,
    ])
    assert.equal(preflight.status, 2, JSON.stringify(preflight))

    const manifestPath = join(directory, 'manifest.json')
    writeFileSync(manifestPath, '{}\n')

    const result = runSigner([
      '--manifest', manifestPath,
      '--key-id', 'czur-runtime-release-2026',
      '--private-key-file', releaseKeys.privateKeyPath,
      '--public-key-file', otherKeys.publicKeyPath,
    ])
    assert.equal(result.status, 2, JSON.stringify(result))
  } finally {
    rmSync(directory, { recursive: true, force: true })
  }
})
