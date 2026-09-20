/* Helpers shared by the demo pages (user.html and system.html).
 *
 * Loaded as a classic (non-deferred) script before each page's inline script,
 * so the definitions below are visible to it.
 */

/* Minimal ustar parser. Handles regular files and directory entries; tar
 * produced via `tar -czf x.tar.gz -C dir .` prefixes names with "./" which
 * is stripped before extraction. Returns the number of files written. */
function extractTarToMemfs(data, destPrefix) {
  const decoder = new TextDecoder('ascii');
  let offset = 0;
  let count = 0;
  while (offset + 512 <= data.length) {
    const header = data.subarray(offset, offset + 512);
    let allZero = true;
    for (let i = 0; i < 512; i++) if (header[i] !== 0) { allZero = false; break; }
    if (allZero) break;
    let name = decoder.decode(header.subarray(0, 100)).replace(/\0.*$/, '');
    if (name.startsWith('./')) name = name.substring(2);
    const sizeOctal = decoder.decode(header.subarray(124, 136)).replace(/\0.*$/, '').trim();
    const size = sizeOctal ? parseInt(sizeOctal, 8) : 0;
    const type = String.fromCharCode(header[156] || 0x30);
    offset += 512;
    // Skip macOS AppleDouble metadata entries (._foo). They're useless
    // to the guest and would pollute /etc/timidity with junk.
    const components = name.split('/');
    const base = components[components.length - 1];
    const isAppleDouble = base && base.startsWith('._');
    // Reject anything that would escape destPrefix: an absolute member name
    // or a ".." component lets a tampered archive overwrite unrelated MEMFS
    // files such as /Image or /rootfs.cpio.
    const escapesDest = name.startsWith('/') || components.includes('..');
    if (escapesDest)
      console.warn(`skipping unsafe tar entry: ${name}`);
    if (name && !isAppleDouble && !escapesDest) {
      const isDir = type === '5';
      const fullPath = destPrefix + '/' + name;
      const parts = fullPath.split('/').filter(p => p && p !== '.');
      const dirDepth = isDir ? parts.length : parts.length - 1;
      let cur = '';
      for (let i = 0; i < dirDepth; i++) {
        cur += '/' + parts[i];
        try { Module.FS.mkdir(cur); } catch (e) {
          const code = e && (e.code || e.errno);
          if (code !== 'EEXIST' && code !== 20 && code !== 17 &&
              !String(e).includes('exist')) throw e;
        }
      }
      if (!isDir && (type === '0' || type === '\0' || type === '')) {
        try {
          Module.FS.writeFile(fullPath, data.subarray(offset, offset + size));
          count++;
        } catch (e) {
          throw new Error(`writeFile ${fullPath} failed: ` +
              `${e?.message || e?.code || e}`);
        }
      }
    }
    offset += Math.ceil(size / 512) * 512;
  }
  return count;
}

function formatError(e) {
  if (!e) return 'undefined error';
  if (typeof e === 'string') return e;
  const parts = [];
  if (e.name) parts.push(e.name);
  if (e.code) parts.push(`code=${e.code}`);
  if (e.errno !== undefined) parts.push(`errno=${e.errno}`);
  if (e.status) parts.push(`status=${e.status}`);
  const msg = e.message || e.toString?.() || '';
  if (msg && msg !== '[object Object]') parts.push(msg);
  return parts.length ? parts.join(' ') : '[object Object]';
}
