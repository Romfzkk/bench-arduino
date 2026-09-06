/**
 * Packages the Arduino library into the ZIP the IDE accepts.
 *
 * The library is not in the Arduino Library Manager index, so "Add .ZIP
 * Library" is how people install it. This produces that ZIP, and it is also
 * the file to attach to a GitHub release.
 *
 * Run: node tools/make-zip.mjs
 */

import { createWriteStream, mkdirSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { deflateRawSync, crc32 } from 'node:zlib'
import { dirname, join, relative, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..')
const LIB_DIR = ROOT
const OUT_DIR = join(ROOT, 'dist')

/** Every file under a directory, depth first, as absolute paths. */
const SKIP = new Set(['.git', '.github', 'dist', 'docs', 'tools', 'node_modules'])
const SKIP_FILES = new Set(['.gitignore', 'Bench.zip', 'CHANGELOG.md'])

function walk(dir) {
  const out = []
  for (const entry of readdirSync(dir)) {
    if (SKIP.has(entry) || SKIP_FILES.has(entry)) continue
    const full = join(dir, entry)
    if (statSync(full).isDirectory()) out.push(...walk(full))
    else out.push(full)
  }
  return out
}

/**
 * Minimal ZIP writer. Node has deflate and crc32 built in, and the archive
 * shape is simple enough that pulling in a dependency for it is not worth it.
 */
function createZip(entries) {
  const chunks = []
  const central = []
  let offset = 0

  for (const { name, data } of entries) {
    const nameBuf = Buffer.from(name, 'utf8')
    const compressed = deflateRawSync(data, { level: 9 })
    const checksum = crc32(data)

    const local = Buffer.alloc(30)
    local.writeUInt32LE(0x04034b50, 0) // local file header signature
    local.writeUInt16LE(20, 4) // version needed
    local.writeUInt16LE(0, 6) // flags
    local.writeUInt16LE(8, 8) // deflate
    local.writeUInt16LE(0, 10) // mod time
    local.writeUInt16LE(0x2921, 12) // mod date, arbitrary but valid
    local.writeUInt32LE(checksum, 14)
    local.writeUInt32LE(compressed.length, 18)
    local.writeUInt32LE(data.length, 22)
    local.writeUInt16LE(nameBuf.length, 26)
    local.writeUInt16LE(0, 28) // extra field length

    chunks.push(local, nameBuf, compressed)

    const dirEntry = Buffer.alloc(46)
    dirEntry.writeUInt32LE(0x02014b50, 0) // central directory signature
    dirEntry.writeUInt16LE(20, 4) // version made by
    dirEntry.writeUInt16LE(20, 6) // version needed
    dirEntry.writeUInt16LE(0, 8)
    dirEntry.writeUInt16LE(8, 10)
    dirEntry.writeUInt16LE(0, 12)
    dirEntry.writeUInt16LE(0x2921, 14)
    dirEntry.writeUInt32LE(checksum, 16)
    dirEntry.writeUInt32LE(compressed.length, 20)
    dirEntry.writeUInt32LE(data.length, 24)
    dirEntry.writeUInt16LE(nameBuf.length, 28)
    dirEntry.writeUInt16LE(0, 30)
    dirEntry.writeUInt16LE(0, 32)
    dirEntry.writeUInt16LE(0, 34)
    dirEntry.writeUInt16LE(0, 36)
    dirEntry.writeUInt32LE(0, 38) // external attributes
    dirEntry.writeUInt32LE(offset, 42)

    central.push(dirEntry, nameBuf)
    offset += local.length + nameBuf.length + compressed.length
  }

  const centralBuf = Buffer.concat(central)
  const end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50, 0)
  end.writeUInt16LE(0, 4)
  end.writeUInt16LE(0, 6)
  end.writeUInt16LE(entries.length, 8)
  end.writeUInt16LE(entries.length, 10)
  end.writeUInt32LE(centralBuf.length, 12)
  end.writeUInt32LE(offset, 16)
  end.writeUInt16LE(0, 20)

  return Buffer.concat([...chunks, centralBuf, end])
}

const files = walk(LIB_DIR)
if (files.length === 0) {
  console.error('No library files found')
  process.exit(1)
}

// The IDE expects everything under a single top-level folder named after the
// library, otherwise "Add .ZIP Library" rejects it.
const entries = files.map((full) => ({
  name: `Bench/${relative(LIB_DIR, full).split(sep).join('/')}`,
  data: readFileSync(full),
}))

mkdirSync(OUT_DIR, { recursive: true })
const outFile = join(OUT_DIR, 'Bench.zip')
const zip = createZip(entries)
createWriteStream(outFile).end(zip)

console.log(`  dist/Bench.zip  (${(zip.length / 1024).toFixed(1)} kB)`)
console.log(`  ${entries.length} files:`)
for (const entry of entries) console.log(`    ${entry.name}`)
