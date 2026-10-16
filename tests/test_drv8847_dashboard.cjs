// Feed actual native protocol-harness telemetry through the unmodified site.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const telemetry = JSON.parse(fs.readFileSync('build/native-tests/drv8847-telemetry.json', 'utf8'));
const html = fs.readFileSync(require.resolve('../dist/single-motor.html'), 'utf8');
const nodes = {};
for (const [, id] of html.matchAll(/id="([^"]+)"/g)) nodes[id] = {
  value: id === 'jogSpeed' ? '150' : id === 'jogResolution' ? 'half' : '',
  checked: false, classList: {toggle() {}}, addEventListener() {},
  replaceChildren(...children) {this.children = children;}, blur() {}
};
const arrows = [...html.matchAll(/data-arrow="([^"]+)"/g)].map(([, arrow]) => ({
  dataset: {arrow}, classList: {toggle() {}}, addEventListener() {}
}));
const context = {console, setTimeout, clearTimeout, setInterval() {}, navigator: {serial: {}},
  window: {localStorage: {getItem() {return null;}, setItem() {}}, addEventListener() {}},
  document: {createElement() {return {};}, body: {dataset: {singleMotor: 'true'}}, hidden: false,
    getElementById(id) {assert.ok(nodes[id], id); return nodes[id];},
    querySelectorAll() {return arrows;}, addEventListener() {}}
};
let source = fs.readFileSync(require.resolve('../dist/gantry.js'), 'utf8');
source = source.replace(' render();\n if(!navigator.serial)',
  ' globalThis.testApi={axes,jog,positions};\n render();\n if(!navigator.serial)');
vm.runInNewContext(source, context);
const {axes, jog, positions} = context.testApi;
const axis = axes.X, calls = [];
axis.port = {};
axis.receive(JSON.stringify(telemetry));
axis.send = async (cmd, fields) => {
  calls.push({cmd, fields});
  assert.ok(!['set_led', 'set_pd_voltage', 'arm_power', 'test_pulses'].includes(cmd));
  if (cmd === 'jog') axis.last.motor_enabled = true;
  if (cmd === 'stop' || cmd === 'decelerate') axis.last.motor_enabled = false;
  if (cmd === 'start') axis.move = fields;
  if (cmd === 'run_move') {
    axis.last.position_full_steps += axis.move.direction * axis.move.steps /
      axis.last.capabilities.resolutions[axis.move.resolution];
    axis.last.motor_enabled = false;
    axis.last.stop_reason = 'Step move complete';
  }
};
(async () => {
  jog.update();
  assert.deepEqual(Array.from(nodes.jogResolution.children, o => o.value), ['full', 'half']);
  assert.equal(nodes.hardwareX.hidden, true);
  assert.equal(nodes.setLedX.disabled, true);
  assert.equal(nodes.setPdX.disabled, true);
  assert.match(nodes.windingCurrent.textContent, /unavailable/);
  assert.match(nodes.stepNote.textContent, /four bridge inputs/);
  assert.ok(arrows.every(a => !a.disabled));
  jog.press('ArrowRight');
  await jog.tick(150, {}, 'half');
  await new Promise(r => setImmediate(r));
  assert.equal(calls.findLast(c => c.cmd === 'jog').fields.resolution, 'half');
  await jog.release('ArrowRight');
  await jog.stop();
  axis.last.position_full_steps = 1.5;
  assert.equal(positions.save(0), true);
  axis.last.position_full_steps = 5;
  assert.equal(await positions.goto(0, 150, 'full'), true);
  const move = calls.findLast(c => c.cmd === 'start').fields;
  assert.equal(move.resolution, 'half');
  assert.equal(move.steps, 7);
  assert.equal(move.direction, -1);
  assert.equal(move.speed_sps, 300);
  assert.equal(move.ramp_ms, 400);
  assert.equal(move.defer, true);
  assert.equal(axis.last.position_full_steps, 1.5);
  console.log('PASS actual DRV8847 C telemetry on the unchanged single-motor page: capability controls, jogging and exact half-step GOTO');
})().catch(e => {console.error(e); process.exitCode = 1;});
