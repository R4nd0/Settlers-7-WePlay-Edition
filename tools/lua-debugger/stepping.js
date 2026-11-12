'use strict';

// Shared by the Frida agent and the Node policy tests.
class S7Stepping {
  constructor() { this.breakpoints = []; this.mode = 'continue'; this.depth = 0; }
  static source(value) { return String(value).replace(/^@/, '').replace(/\\/g, '/'); }
  setBreakpoints(items) {
    if (!Array.isArray(items) || items.length > 32) throw new Error('breakpoint-limit');
    this.breakpoints = items.map(item => {
      const source = S7Stepping.source(item.source);
      if (!source || source.length > 256 || !Number.isInteger(item.line) || item.line < 1 || item.line > 1000000)
        throw new Error('invalid-breakpoint');
      return { source, line: item.line };
    });
  }
  resume(mode, depth) {
    if (!['continue', 'stepIn', 'next', 'stepOut'].includes(mode)) throw new Error('invalid-mode');
    this.mode = mode; this.depth = depth;
  }
  reason(source, line, depth) {
    source = S7Stepping.source(source);
    if (this.breakpoints.some(b => b.source === source && b.line === line)) return 'breakpoint';
    if (this.mode === 'stepIn' || (this.mode === 'next' && depth <= this.depth)
        || (this.mode === 'stepOut' && depth < this.depth)) return this.mode;
    return null;
  }
}
if (typeof module !== 'undefined') module.exports = S7Stepping;
