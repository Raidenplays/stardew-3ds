// A small zip writer for the worker: files are stored as they are (the converted
// files are already compressed), and the zip is handed to the page as a Blob made of
// the pieces, so nothing is copied into one big buffer.
  const TABLE = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    TABLE[n] = c >>> 0;
  }
  function crcUpdate(crc, bytes) {
    for (let i = 0; i < bytes.length; i++) crc = TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
    return crc;
  }
  const enc = new TextEncoder();

  class StoreZip {
    constructor() {
      this.parts = [];
      this.entries = [];
      this.offset = 0;
    }

    // data: a Uint8Array, or a Blob/File (read in slices for its checksum, then used as it is)
    add(name, data) {
      let crc = 0xffffffff;
      let size;
      if (data instanceof Blob) {
        const reader = new FileReaderSync();
        const STEP = 4 << 20;
        for (let at = 0; at < data.size; at += STEP)
          crc = crcUpdate(crc, new Uint8Array(reader.readAsArrayBuffer(data.slice(at, at + STEP))));
        size = data.size;
      } else {
        crc = crcUpdate(crc, data);
        size = data.length;
      }
      crc = (crc ^ 0xffffffff) >>> 0;
      if (size > 0xfffffffe || this.offset > 0xfffffffe) throw new Error("the converted files are too big for one zip");
      const nameBytes = enc.encode(name);
      const head = new DataView(new ArrayBuffer(30));
      head.setUint32(0, 0x04034b50, true);
      head.setUint16(4, 20, true);        // version needed
      head.setUint16(6, 0x0800, true);    // names are UTF-8
      head.setUint16(8, 0, true);         // stored
      head.setUint16(10, 0, true);        // time
      head.setUint16(12, 0x21, true);     // date: 1980-01-01
      head.setUint32(14, crc, true);
      head.setUint32(18, size, true);
      head.setUint32(22, size, true);
      head.setUint16(26, nameBytes.length, true);
      head.setUint16(28, 0, true);
      this.parts.push(head.buffer, nameBytes, data);
      this.entries.push({ nameBytes, crc, size, offset: this.offset });
      this.offset += 30 + nameBytes.length + size;
    }

    finish() {
      const start = this.offset;
      let dirSize = 0;
      for (const e of this.entries) {
        const c = new DataView(new ArrayBuffer(46));
        c.setUint32(0, 0x02014b50, true);
        c.setUint16(4, 20, true);
        c.setUint16(6, 20, true);
        c.setUint16(8, 0x0800, true);
        c.setUint16(10, 0, true);
        c.setUint16(12, 0, true);
        c.setUint16(14, 0x21, true);
        c.setUint32(16, e.crc, true);
        c.setUint32(20, e.size, true);
        c.setUint32(24, e.size, true);
        c.setUint16(28, e.nameBytes.length, true);
        c.setUint32(42, e.offset, true);
        this.parts.push(c.buffer, e.nameBytes);
        dirSize += 46 + e.nameBytes.length;
      }
      if (this.entries.length > 0xfffe) throw new Error("too many files for one zip");
      const end = new DataView(new ArrayBuffer(22));
      end.setUint32(0, 0x06054b50, true);
      end.setUint16(8, this.entries.length, true);
      end.setUint16(10, this.entries.length, true);
      end.setUint32(12, dirSize, true);
      end.setUint32(16, start, true);
      this.parts.push(end.buffer);
      return new Blob(this.parts, { type: "application/zip" });
    }
  }

export { StoreZip };
