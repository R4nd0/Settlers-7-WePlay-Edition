'use strict';
// Keep resource locations and standard nil-member diagnostics, never arbitrary
// game error strings, assertion values, paths outside Script/, or account data.
const S7LuaError = text => {
  if (typeof text !== 'string' || text.length > 512) return null;
  const nilMember=/(?:^|:\s)(attempt to (call|index) (field|method|global) '([A-Za-z_][A-Za-z0-9_]{0,63})' \(a nil value\))$/.exec(text);
  const diagnostic=nilMember?{operation:nilMember[2],member:nilMember[4]}:null;
  const match = /^(?:\[string ")?@?([A-Za-z0-9_./\\-]+)(?:"\])?:(\d+):\s*([^\r\n]*)$/.exec(text);
  if (!match) return diagnostic;
  let source = match[1].replace(/\\/g, '/');
  if (/^(?:local|Shared|tutorial)\//.test(source)) source='Script/'+source;
  if (!source.startsWith('Script/') || source.split('/').some(p => p === '.' || p === '..' || !p)) return diagnostic;
  const line = Number(match[2]);
  if (!Number.isSafeInteger(line) || line < 1 || line > 10000000) return null;
  const result = { source, line };
  if (diagnostic) Object.assign(result,diagnostic);
  return result;
};
if (typeof module !== 'undefined') module.exports = S7LuaError;
