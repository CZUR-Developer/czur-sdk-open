#!/usr/bin/env node

/**
 * Create the detached Ed25519 signature envelope consumed by @czur/open-cli.
 *
 * The manifest is signed as raw bytes. Do not parse, reformat, or append to the
 * manifest after this command has run. The private key is read from a CI secret
 * file (preferred) or an environment variable and is never written to disk.
 */

import {
  createPrivateKey,
  createPublicKey,
  sign,
  verify,
} from 'node:crypto'
import {
  chmodSync,
  existsSync,
  readFileSync,
  renameSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs'
import { resolve } from 'node:path'

function usage() {
  return `Usage:
  node src/sdk_open/scripts/release/sign_runtime_manifest.mjs --manifest <path> --key-id <id> [options]

Required:
  --manifest <path>             Manifest JSON to sign (raw bytes are preserved)
  --key-id <id>                 Public-key identifier written to the envelope

Private key input (one required; file is preferred for Jenkins credentials):
  --private-key-file <path>     PEM Ed25519 private key file
  --private-key-env <name>      Environment variable containing PEM (default: RUNTIME_MANIFEST_PRIVATE_KEY)

Optional:
  --output <path>               Signature output (default: <manifest>.sig)
  --public-key-file <path>      Verify the generated signature before writing it
  --check-key-pair              Validate an Ed25519 private/public key pair only
  -h, --help                    Show this help
`
}

function fail(message) {
  console.error(`Error: ${message}`)
  console.error(usage())
  process.exitCode = 2
}

function valueAfter(args, index, option) {
  const value = args[index + 1]
  if (!value || value.startsWith('--')) throw new Error(`${option} requires a value`)
  return value
}

function parseArgs(args) {
  const options = {
    privateKeyEnv: 'RUNTIME_MANIFEST_PRIVATE_KEY',
  }

  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index]
    switch (arg) {
      case '--manifest':
        options.manifest = valueAfter(args, index++, arg)
        break
      case '--key-id':
        options.keyId = valueAfter(args, index++, arg)
        break
      case '--private-key-file':
        options.privateKeyFile = valueAfter(args, index++, arg)
        break
      case '--private-key-env':
        options.privateKeyEnv = valueAfter(args, index++, arg)
        break
      case '--output':
        options.output = valueAfter(args, index++, arg)
        break
      case '--public-key-file':
        options.publicKeyFile = valueAfter(args, index++, arg)
        break
      case '--check-key-pair':
        options.checkKeyPair = true
        break
      case '-h':
      case '--help':
        options.help = true
        break
      default:
        throw new Error(`unknown option: ${arg}`)
    }
  }

  return options
}

function readPrivateKey(options) {
  if (options.privateKeyFile && process.env[options.privateKeyEnv]) {
    throw new Error('provide only one private key source')
  }

  if (options.privateKeyFile) {
    if (!existsSync(options.privateKeyFile)) {
      throw new Error(`private key file does not exist: ${options.privateKeyFile}`)
    }
    return createPrivateKey(readFileSync(options.privateKeyFile))
  }

  const pem = process.env[options.privateKeyEnv]
  if (!pem) throw new Error(`private key environment variable is empty: ${options.privateKeyEnv}`)
  return createPrivateKey(pem)
}

function readPublicKey(path) {
  if (!path) throw new Error('--public-key-file is required')
  if (!existsSync(path)) throw new Error(`public key file does not exist: ${path}`)
  const publicKey = createPublicKey(readFileSync(path))
  if (publicKey.asymmetricKeyType !== 'ed25519') {
    throw new Error('public key must be an Ed25519 key')
  }
  return publicKey
}

function checkKeyPair(options) {
  const privateKey = readPrivateKey(options)
  if (privateKey.asymmetricKeyType !== 'ed25519') {
    throw new Error('private key must be an Ed25519 key')
  }
  const suppliedPublicKey = readPublicKey(options.publicKeyFile)
  const derivedPublicKey = createPublicKey(privateKey)
  const suppliedDer = suppliedPublicKey.export({ type: 'spki', format: 'der' })
  const derivedDer = derivedPublicKey.export({ type: 'spki', format: 'der' })
  if (!suppliedDer.equals(derivedDer)) {
    throw new Error('private and public Ed25519 keys do not match')
  }
  console.log('Ed25519 key pair: valid')
}

function writeAtomically(path, bytes) {
  const temporaryPath = `${path}.tmp-${process.pid}-${Date.now().toString(36)}`
  try {
    writeFileSync(temporaryPath, bytes, { encoding: 'utf8', mode: 0o600, flag: 'wx' })
    renameSync(temporaryPath, path)
    chmodSync(path, 0o644)
  } finally {
    rmSync(temporaryPath, { force: true })
  }
}

function main() {
  const options = parseArgs(process.argv.slice(2))
  if (options.help) {
    console.log(usage())
    return
  }
  if (options.checkKeyPair) {
    checkKeyPair(options)
    return
  }
  if (!options.manifest) throw new Error('--manifest is required')
  if (!options.keyId) throw new Error('--key-id is required')
  if (options.keyId.length > 128 || !/^[A-Za-z0-9._-]+$/.test(options.keyId)) {
    throw new Error('--key-id must contain only letters, numbers, dot, underscore, or hyphen')
  }

  const manifestPath = resolve(options.manifest)
  const outputPath = resolve(options.output ?? `${manifestPath}.sig`)
  const manifestInfo = statSync(manifestPath)
  if (!manifestInfo.isFile()) throw new Error(`manifest is not a regular file: ${manifestPath}`)

  const manifestBytes = readFileSync(manifestPath)
  if (manifestBytes.length === 0) throw new Error('manifest is empty')

  const privateKey = readPrivateKey(options)
  if (privateKey.asymmetricKeyType !== 'ed25519') {
    throw new Error('private key must be an Ed25519 key')
  }

  const signature = sign(null, manifestBytes, privateKey)
  if (options.publicKeyFile) {
    const publicKey = readPublicKey(options.publicKeyFile)
    if (!verify(null, manifestBytes, publicKey, signature)) {
      throw new Error('generated signature could not be verified with the supplied public key')
    }
  }

  const envelope = `${JSON.stringify(
    {
      schema_version: 1,
      algorithm: 'ed25519',
      key_id: options.keyId,
      signature: signature.toString('base64'),
    },
    null,
    2,
  )}\n`

  writeAtomically(outputPath, envelope)
  console.log(`Signed manifest: ${manifestPath}`)
  console.log(`Manifest bytes: ${manifestBytes.length}`)
  console.log(`Signature: ${outputPath}`)
  console.log(`Key ID: ${options.keyId}`)
  if (options.publicKeyFile) console.log('Verification: valid')
}

try {
  main()
} catch (error) {
  fail(error instanceof Error ? error.message : String(error))
}
