'use strict';
const assert = require('node:assert/strict');
const parse = require('./lua_errors');
assert.deepEqual(parse('Script/local/menu/game.lua:93: attempt to call field \'create\' (a nil value)'),
  { source: 'Script/local/menu/game.lua', line: 93, operation: 'call', member: 'create' });
assert.deepEqual(parse('[string "@Script\\local\\menu\\hud.lua"]:20: attempt to index method \'getFrame\' (a nil value)'),
  { source: 'Script/local/menu/hud.lua', line: 20, operation: 'index', member: 'getFrame' });
assert.deepEqual(parse('Script/profiles.lua:1: assertion failed: private-value'), { source: 'Script/profiles.lua', line: 1 });
for (const text of ['C:/Users/private.lua:1: secret', 'Script/../../private.lua:1: secret',
  'Script//private.lua:1: secret', 'token=secret', 'Script/test.lua:0: secret',
  'Script/test.lua:10000001: secret', 'Script/test.lua:1: secret\nother', 'x'.repeat(513)])
  assert.equal(parse(text), null);
assert.deepEqual(parse('Script/test.lua:1: attempt to call field \'private-id-value\' (a nil value)'),
  { source: 'Script/test.lua', line: 1 });
assert.deepEqual(parse('local/menu/hud.lua:10: attempt to call field \'create\' (a nil value)'),
  {source:'Script/local/menu/hud.lua',line:10,operation:'call',member:'create'});
assert.deepEqual(parse('C:/private/user.lua:10: attempt to call field \'create\' (a nil value)'),
  {operation:'call',member:'create'});
assert.deepEqual(parse('[string "Script/local/menu/panel/game/gameChatMessages..."]:269: attempt to call a nil value'),
  {source:'Script/local/menu/panel/game/gameChatMessages...',line:269});
console.log('15 Lua error location/redaction checks passed');
