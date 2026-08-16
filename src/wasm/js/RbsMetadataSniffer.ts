/**
 * Pure TypeScript .rbs header/metadata sniffer.
 *
 * Extracts portable song metadata (title, author, version) without the WASM
 * engine. Mirrors the C++ parser's HEAD / GLOB / USRI chunk handling.
 */

export interface SniffedRbsMetadata {
  title: string;
  author: string;
  infoText: string;
  creatorUrl: string;
  versionLabel: string;
  headVersion: number | null;
  globSubFormat: number | null;
  bpm: number;
  fileSize: number;
  valid: boolean;
  error?: string;
}

const CONTAINER_MAGIC = 'CAT ';
const FORMAT_MARKER = 'RB40';
const HEAD_SIGNATURE = '[T[T';
const DEFAULT_BPM = 125;

function readU32BE(view: DataView, offset: number): number {
  return view.getUint32(offset, false);
}

function matchId(bytes: Uint8Array, offset: number, expected: string): boolean {
  if (offset + 4 > bytes.length) return false;
  for (let i = 0; i < 4; i++) {
    if (bytes[offset + i] !== expected.charCodeAt(i)) return false;
  }
  return true;
}

function readCString(bytes: Uint8Array, start: number): { value: string; next: number } {
  let end = start;
  while (end < bytes.length && bytes[end] !== 0) end++;
  const slice = bytes.subarray(start, end);
  const value = latin1ToUtf8(slice);
  return { value, next: end < bytes.length ? end + 1 : end };
}

function latin1ToUtf8(bytes: Uint8Array): string {
  let out = '';
  for (let i = 0; i < bytes.length; i++) {
    const c = bytes[i];
    if (c < 0x80) {
      out += String.fromCharCode(c);
    } else {
      out += String.fromCharCode(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
    }
  }
  return out;
}

function trim(value: string): string {
  return value.trim();
}

function versionLabel(headVersion: number | null): string {
  if (headVersion === 0x01) return 'v2.0';
  if (headVersion === 0x02) return 'v2.0.1';
  if (headVersion != null) return `v2.x (${headVersion})`;
  return 'unknown';
}

interface ParseState {
  title: string;
  author: string;
  infoText: string;
  creatorUrl: string;
  headVersion: number | null;
  globSubFormat: number | null;
}

function parseChunks(
  data: Uint8Array,
  offset: number,
  end: number,
  state: ParseState,
  isRoot: boolean
): string | null {
  let pos = offset;

  if (isRoot) {
    if (!matchId(data, pos, CONTAINER_MAGIC)) return "Missing root 'CAT ' container magic";
    const catSize = readU32BE(new DataView(data.buffer, data.byteOffset, data.byteLength), pos + 4);
    pos += 8;
    if (!matchId(data, pos, FORMAT_MARKER)) return "Missing 'RB40' format marker";
    pos += 4;
    if (catSize + 8 > data.length) return 'Root container size exceeds file size';
    void catSize;
  }

  while (pos + 8 <= end) {
    const id = String.fromCharCode(data[pos], data[pos + 1], data[pos + 2], data[pos + 3]);
    const chunkSize = readU32BE(
      new DataView(data.buffer, data.byteOffset, data.byteLength),
      pos + 4
    );
    pos += 8;

    if (chunkSize > end - pos) return 'Chunk size exceeds container bounds';

    const chunkStart = pos;
    const chunkEnd = pos + chunkSize;

    if (id === 'HEAD') {
      const err = parseHead(data, chunkStart, chunkSize, state);
      if (err) return err;
    } else if (id === 'GLOB') {
      const err = parseGlob(data, chunkStart, chunkSize, state);
      if (err) return err;
    } else if (id === 'USRI') {
      const err = parseUsri(data, chunkStart, chunkSize, state);
      if (err) return err;
    } else if (id === CONTAINER_MAGIC) {
      if (chunkSize < 4) return 'Nested CAT container too small';
      const err = parseChunks(data, chunkStart + 4, chunkEnd, state, false);
      if (err) return err;
    }

    pos = chunkEnd;
    if (chunkSize & 1) pos += 1;
  }

  return null;
}

function parseHead(
  data: Uint8Array,
  start: number,
  size: number,
  state: ParseState
): string | null {
  if (size < 0x1c) return 'HEAD chunk too small';
  if (!matchId(data, start, HEAD_SIGNATURE)) return 'Invalid HEAD signature';
  state.headVersion = data[start + 0x06];
  return null;
}

function parseGlob(
  data: Uint8Array,
  start: number,
  size: number,
  state: ParseState
): string | null {
  if (size < 0x20) return 'GLOB chunk too small';
  state.globSubFormat = data[start + 0x03];
  const titleOffset = start + 0x0f;
  if (titleOffset >= start + size) return 'GLOB chunk too small for title';
  const title = readCString(data, titleOffset);
  state.title = trim(title.value);

  const urlOffset = start + 0x119;
  if (urlOffset < start + size) {
    const url = readCString(data, urlOffset);
    state.creatorUrl = trim(url.value);
  }
  return null;
}

function parseUsri(
  data: Uint8Array,
  start: number,
  size: number,
  state: ParseState
): string | null {
  if (size < 2) return 'USRI chunk too small';
  const author = readCString(data, start);
  state.author = trim(author.value);

  let infoPos = author.next;
  while (infoPos < start + size && data[infoPos] === 0) infoPos++;
  if (infoPos < start + size) {
    const info = readCString(data, infoPos);
    state.infoText = trim(info.value);
  }
  return null;
}

/**
 * Sniff metadata from a raw .rbs file buffer.
 * Returns partial metadata even when the file is incomplete.
 */
export function sniffRbsMetadata(buffer: ArrayBuffer): SniffedRbsMetadata {
  const bytes = new Uint8Array(buffer);
  const state: ParseState = {
    title: '',
    author: '',
    infoText: '',
    creatorUrl: '',
    headVersion: null,
    globSubFormat: null,
  };

  if (bytes.length < 16) {
    return {
      ...state,
      versionLabel: 'unknown',
      bpm: DEFAULT_BPM,
      fileSize: bytes.length,
      valid: false,
      error: 'File too small to contain a valid ReBirth container',
    };
  }

  const parseError = parseChunks(bytes, 0, bytes.length, state, true);

  return {
    title: state.title || 'Untitled',
    author: state.author || 'Unknown',
    infoText: state.infoText,
    creatorUrl: state.creatorUrl,
    headVersion: state.headVersion,
    globSubFormat: state.globSubFormat,
    versionLabel: versionLabel(state.headVersion),
    bpm: DEFAULT_BPM,
    fileSize: bytes.length,
    valid: !parseError && state.title.length > 0,
    error: parseError ?? undefined,
  };
}
