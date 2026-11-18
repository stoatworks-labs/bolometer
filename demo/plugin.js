/**
 * SW Bolometer (BO01, an FFGL effect), in a browser.
 *
 * A thermal camera on an uncooled microbolometer core, looking at the clip.
 * Every pixel is a thermal mass with a 12 ms time constant, read out at the
 * export-limited 9 Hz (or 30 or 60), frozen for 23/26 s by each flat-field
 * correction, drifting between shutters, with a narcissus spot at the centre;
 * plateau equalisation and an unsharp mask make the picture, through one of
 * four palettes.
 *
 * Three files, and they are not equally faithful:
 *
 *   **shaders.js is the plugin's own GLSL**, unedited: spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, and the same
 *   script fails tools/verify.sh if a character, or the way the pieces are
 *   joined, drifts. palettes.js is Palettes.cpp's two tables, spliced the same
 *   way, with the licence that travels with them.
 *
 *   **port.js is a hand port of the CPU half, and only a reader checks it**:
 *   Model.h, Controls.cpp, Camera.cpp (all time, in double), Palettes.cpp's
 *   Build() and Bolometer.cpp's ProcessOpenGL. demo/tools/crosscheck/ compares
 *   its pictures with bmtest's, frame by frame, on the cases it lists.
 *
 *   **This file is the page**: the parameter panel, the harness's clip ported
 *   to GLSL, the line under the picture, the page's presets and the hook the
 *   crosscheck drives. None of it is the plugin.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **The host is the page.** The kit's clock is seconds from
 * requestAnimationFrame deltas, capped at a tenth of a second a frame, and it
 * stands still while paused; it is handed to the plugin through SetTime every
 * frame, as a host hands its own. The plugin's vote on whether a host means
 * seconds or milliseconds is not ported (this host's unit is known), so the
 * camera is not restarted a few frames in as it would be in Arena. Restart
 * sends the clock back to 0, which the camera takes as a host clock that ran
 * backwards: it starts again, settled, as the plugin's does.
 *
 * **Shutter** is FF_TYPE_EVENT in the plugin. The kit has no event control,
 * so it is a toggle the page lets go of after one frame, as a host lets go of
 * an event button; the plugin counts the rising edge.
 *
 * **The default clip is the harness's own** (bmtest's buildCard: schlieren's
 * look card and a white ball crossing it every 4 s), ported to GLSL below,
 * so the page opens on the picture the README's hero frame shows.
 *
 * **The About block is not a parameter here**, as on every page in the suite.
 */
import { mountDemo } from './vendor/demo.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import { PassBuffer } from './vendor/gl.js';
import {
  f32, PT, TABLE, Kind, FrameRate, Span, Palette, Bolometer, compilePrograms,
  ShutterIntervalFromParam, ParamFromShutterInterval, DriftFromParam, ParamFromDrift,
  NarcissusFromParam, ParamFromNarcissus, PlateauFromParam, ParamFromPlateau, LevelFromParam,
  ParamFromLevel, WidthFromParam, ParamFromWidth, HaloFromParam,
} from './port.js';

//===========================================================================
// The harness's clip: tools/bmtest/main.cpp's buildCard, in GLSL. The look
// card (a sunset sky, a sun, a skyline with lit windows, a stripe of 75% bars
// and a white title block) is haze's port of the same card; bmtest adds a
// white ball crossing it once every 4 s, so a camera has something to see
// move. Rows bottom first, as there.
//===========================================================================
const CARD = `
uniform float Time;
uniform vec2 Resolution;
in vec2 uv;
out vec4 fragColor;

uint pcgCard( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float hash01( uint a, uint b )
{
	return float( pcgCard( a * 2654435761u ^ pcgCard( b + 0x9e3779b9u ) ) ) * ( 1.0 / 4294967296.0 );
}

void main()
{
	vec2 px  = floor( gl_FragCoord.xy );
	float w  = Resolution.x;
	float h  = Resolution.y;
	float s  = min( w, h );
	float bx = -0.9 + 1.8 * fract( Time / 4.0 );
	float by = -0.2 + 0.1 * sin( Time * 2.0 );
	float u  = ( px.x + 0.5 - 0.5 * w ) / s;
	float v  = ( px.y + 0.5 - 0.5 * h ) / s;
	float k  = clamp( 0.5 + v, 0.0, 1.0 );
	vec3 c   = vec3( 0.95 - 0.8 * k, 0.45 - 0.3 * k, 0.25 + 0.5 * k );
	float d  = sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
	if( d < 0.2 )
		c = vec3( 1.0, 0.85 - 0.5 * d, 0.35 );
	int block = int( floor( ( u + 2.0 ) * 11.0 ) );
	float top = -0.12 + 0.16 * hash01( uint( block ), 7u );
	if( v < top )
	{
		c = vec3( 0.05 );
		int wx = int( floor( ( u + 2.0 ) * 70.0 ) );
		int wy = int( floor( v * 60.0 + 100.0 ) );
		if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( uint( wx * 131 + wy ), 9u ) > 0.55 )
			c = vec3( 1.0, 0.9, 0.55 );
	}
	if( v < -0.36 )
	{
		int bar = int( clamp( floor( ( px.x + 0.5 ) / w * 7.0 ), 0.0, 6.0 ) );
		vec3 b  = vec3( 0.0, 0.0, 1.0 );
		if( bar == 0 ) b = vec3( 1.0, 1.0, 1.0 );
		else if( bar == 1 ) b = vec3( 1.0, 1.0, 0.0 );
		else if( bar == 2 ) b = vec3( 0.0, 1.0, 1.0 );
		else if( bar == 3 ) b = vec3( 0.0, 1.0, 0.0 );
		else if( bar == 4 ) b = vec3( 1.0, 0.0, 1.0 );
		else if( bar == 5 ) b = vec3( 1.0, 0.0, 0.0 );
		c = 0.75 * b;
	}
	if( abs( u - 0.42 ) < 0.28 && abs( v - 0.3 ) < 0.06 )
		c = vec3( 0.95 );
	float bd = sqrt( ( u - bx ) * ( u - bx ) + ( v - by ) * ( v - by ) );
	if( bd < 0.07 )
		c = vec3( 1.0 );
	fragColor = vec4( c, 1.0 );
}
`;

if (!SOURCES.some((s) => s.id === 'card')) {
  SOURCES.unshift({
    id: 'card',
    name: 'The harness’s card (a ball crossing)',
    hint: 'The plugin harness’s own clip (buildCard in bmtest), drawn in the page: a sunset sky, a sun, a skyline with lit windows, a stripe of bars, a white block, and a white (hot) ball crossing every 4 s. It is the clip behind the README’s picture.',
    fragment: CARD,
  });
}

//===========================================================================
// The parameters: the plugin's own, in kTable's order and groups, with its
// names, element lists and defaults. Hints from the README and AGENTS.md.
//===========================================================================

const sig = (n, d = 3) => Number(n.toPrecision(d)).toString();
const V = (v) => f32(v);

const SPEC = [
  ['frameRate', PT.FRAME_RATE, null, 'How often the core is read out. 9 Hz is the export limit (EAR 6A003.b.4.b Note 3) every small thermal core ships at: the picture changes only at sensor instants exactly 1/9 s apart in host time, a 6-7-7 rhythm of 60 fps frames, and is held in between. 30 and 60 Hz are the unrestricted cores’ rates.'],
  ['shutterInterval', PT.SHUTTER_INTERVAL, (v) => { const s = ShutterIntervalFromParam(V(v)); return Number.isFinite(s) ? `${s >= 100 ? s.toFixed(0) : s.toFixed(1)} s` : 'Off'; }, 'Seconds between flat-field corrections, 5 to 300, geometric; the top of the slider is Off. Each FFC closes the shutter and freezes the last good frame for 23/26 s, then the picture comes back with its drift wiped. The first comes the interval after the camera starts.'],
  ['shutter', PT.SHUTTER, null, 'An FFC now, at the next sensor instant (ignored while one is in progress). FF_TYPE_EVENT in the plugin; a toggle the page lets go of after one frame here.'],
  ['drift', PT.DRIFT, (v) => `${sig(DriftFromParam(V(v)))} K/min`, 'How fast the calibration drifts between shutters, 0 to 1 K a minute: a fixed pattern, half in whole columns and half per pixel, growing linearly with the time since the last FFC (capped at 600 s) and replaced by a new one at each FFC. In host pixels.'],
  ['narcissus', PT.NARCISSUS, (v) => `${sig(NarcissusFromParam(V(v)))} K`, 'The narcissus spot’s depth, 0 to 2 K: the detector seeing its own cold reflection in the lens, a dark disc 0.15 frame heights in radius with an edge 0.1 wide, dead centre. The shutter sits behind the lens, so an FFC cannot remove it.'],
  ['span', PT.SPAN, null, 'Auto: plateau equalisation, the AGC of every small core, which hands each temperature its share of the 256 levels by histogram and damps the change by 0.85 a sensor frame. Manual: Level and Width, a fixed window.'],
  ['plateau', PT.PLATEAU, (v) => { const p = PlateauFromParam(V(v)); return p >= 0.01 ? `${sig(p * 100)}%` : `${p.toExponential(1)}`; }, 'The plateau, as a share of the histogram’s 36,864 samples, 1e-5 to 1, geometric: no temperature bin may claim more than this much of the scale. 100% is plain histogram equalisation; Boson’s factory value is 7%, the plugin’s 2%, chosen because a flat background otherwise takes the scale.'],
  ['level', PT.LEVEL, (v) => `${LevelFromParam(V(v)).toFixed(2)} °C`, 'Manual only: the temperature at the middle of the window, 15 to 40 °C.'],
  ['width', PT.WIDTH, (v) => `${sig(WidthFromParam(V(v)))} K`, 'Manual only: the window’s width, 0.5 to 30 K, geometric. 25 K at 27.5 °C spans the whole mapped range.'],
  ['palette', PT.PALETTE, null, 'White Hot (the core’s own 8-bit grey), Black Hot, Ironbow and Rainbow, the last two from Pure Engineering’s LeptonModule (BSD-2-Clause; demo/palettes.js carries its notice), not FLIR’s own.'],
  ['halo', PT.HALO, (v) => `a ${sig(HaloFromParam(V(v)))}`, 'The detail enhancement’s unsharp gain, 0 to 3: y + a (y − Gy), with a Gaussian of 0.01 frame heights, before the 8-bit output. Hot things ring dark and cold things light.'],
  ['mix', PT.MIX, (v) => `×${V(v).toFixed(2)}`, 'The camera’s picture over the clip. At 1 it is opaque, as a camera’s is; 0 hands the clip through untouched.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const row = TABLE[slot];
  const types = { [Kind.Standard]: 'standard', [Kind.Option]: 'option', [Kind.Event]: 'boolean' };
  const p = { id, slot, name: row.name, type: types[row.kind], default: row.defaultValue, group: row.group, hint };
  if (row.options) p.elements = [...row.options];
  if (display) p.display = display;
  return p;
});
if (PARAMS.length !== PT.ABOUT_TEXT || PARAMS.some((p, i) => p.slot !== i)) {
  throw new Error('the page’s parameters are not the plugin’s table, in order');
}

//===========================================================================
// For demo/tools/crosscheck/ and headless checks.
//===========================================================================

/** std::lround( clamp( v, 0, 1 ) x 255 ): bmtest's own rounding of its float output. */
const toCode = (v) => {
  const x = Math.min(1, Math.max(0, v)) * 255;
  return Math.floor(x + 0.5);
};

function installHook(gl, programs, clips) {
  const byName = new Map(TABLE.map((row) => [row.name, row]));
  const setByName = (instance, name, value) => {
    const row = byName.get(name);
    if (!row) throw new Error(`no parameter called '${name}'`);
    const index = row.options ? row.options.indexOf(String(value)) : -1;
    const number = index >= 0 ? index : Number(value);
    if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number or an option of ${name}`);
    instance.SetFloatParameter(row.id, number);
  };

  /** A float RGBA target, read back and rounded as bmtest rounds its own, top row first. */
  const readCodes = (fbo, width, height) => {
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const floats = new Float32Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, floats);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const row = width * 4;
    const out = new Uint8Array(row * height);
    for (let y = 0; y < height; y += 1) {
      const src = (height - 1 - y) * row;
      const dst = y * row;
      for (let x = 0; x < row; x += 1) out[dst + x] = toCode(floats[src + x]);
    }
    return out;
  };

  /** An RGBA8 target (what the page's canvas is), read back as it stands, top row first. */
  const readBytes = (fbo, width, height) => {
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const read = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const row = width * 4;
    const out = new Uint8Array(read.length);
    for (let y = 0; y < height; y += 1) out.set(read.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
    return out;
  };

  /** The uniforms a program declares that nothing has set: each would sit at 0. */
  const unsetUniforms = (program) => {
    const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
    const out = [];
    const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < count; i += 1) {
      const name = gl.getActiveUniform(program.program, i).name;
      if (!set.has(name)) out.push(name);
    }
    return out;
  };

  window.bolometerDemo = {
    table: TABLE.map((row) => ({ name: row.name, kind: row.kind, value: row.defaultValue, elements: row.options ?? [] })),

    /**
     * A FRESH instance (bmtest's is one too), on the seconds clock, with the
     * settings by the plugin's display name (an option by its element's name).
     *
     * Each frame() hands the plugin one picture -- an RGBA8 one, top row first,
     * as bmtest --pipe reads it, or with `clip` set the page's own clip of that
     * id drawn at `time` -- hands it `time` through SetTime, runs ProcessOpenGL
     * into the host framebuffer and reads it back, top row first. `host` picks
     * that framebuffer: 'float', RGBA32F and rounded as bmtest rounds its own
     * (bmtest's is RGBA32F), or 'rgba8', what the page's canvas is. `changes`
     * are set before the frame, as bmtest's --script cues are.
     */
    begin({ settings = {}, width, height, host = 'float', clip = null, input = 'rgba8' }) {
      const run = new Bolometer(gl, programs);
      run.InitGL();
      for (const [name, value] of Object.entries(settings)) setByName(run, name, value);
      const source = clip ? SOURCES.find((s) => s.id === clip) : null;
      if (clip && !source) throw new Error(`no clip '${clip}'`);
      // The page's clips are RGBA8 textures. bmtest's is RGBA32F, holding the
      // same codes / 255: `input: 'float'` uploads it that way, to tell a
      // difference of representation from one of arithmetic.
      const floats = input === 'float';
      const upload = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, upload);
      gl.texStorage2D(gl.TEXTURE_2D, 1, floats ? gl.RGBA32F : gl.RGBA8, width, height);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.bindTexture(gl.TEXTURE_2D, null);
      const target = new PassBuffer(gl, { filter: 'nearest' }).ensure(width, height, host === 'rgba8' ? gl.RGBA8 : gl.RGBA32F);
      const flipped = new Uint8Array(width * height * 4);
      const asFloats = floats ? new Float32Array(width * height * 4) : null;
      const row = width * 4;
      return {
        frame(rgba, time, changes = {}) {
          for (const [name, value] of Object.entries(changes)) setByName(run, name, value);
          let input;
          if (source) {
            input = clips.render(source, width, height, time);
          } else {
            for (let y = 0; y < height; y += 1) flipped.set(rgba.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
            gl.bindTexture(gl.TEXTURE_2D, upload);
            gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            if (floats) {
              // in[ ... ] / 255.0f, as bmtest --pipe divides (a float division, correctly rounded).
              for (let i = 0; i < flipped.length; i += 1) asFloats[i] = Math.fround(Math.fround(flipped[i]) / Math.fround(255));
              gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.FLOAT, asFloats);
            } else {
              gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, flipped);
            }
            gl.bindTexture(gl.TEXTURE_2D, null);
            input = { texture: upload, width, height };
          }
          target.clearTo(0, 0, 0, 0);
          run.SetTime(time);
          const ok = run.ProcessOpenGL(input, target.fbo, [0, 0, width, height]);
          const f = run.lastFrame;
          return {
            ok,
            pixels: host === 'rgba8' ? readBytes(target.fbo, width, height) : readCodes(target.fbo, width, height),
            readout: f.readout,
            index: f.readoutIndex,
            frozen: f.frozen,
            ffcs: f.ffcsCompleted,
            error: gl.getError(),
          };
        },
        end() {
          run.DeInitGL();
          target.dispose();
          gl.deleteTexture(upload);
        },
      };
    },

    /** Uniforms the page set that a program lacks, and uniforms a program has that nothing set. */
    uniforms() {
      return {
        missing: Object.entries(programs).flatMap(([k, p]) => [...p.missing].map((u) => `${k}: ${u}`)),
        unset: Object.entries(programs).flatMap(([k, p]) => unsetUniforms(p).map((u) => `${k}: ${u}`)),
      };
    },

    /** A clip of the page's own (the harness's card by default), RGBA8, top row first. */
    clip(id, width, height, time) {
      const source = SOURCES.find((s) => s.id === id);
      if (!source) throw new Error(`no clip '${id}'`);
      clips.render(source, width, height, time);
      return readBytes(clips.buffer.fbo, width, height);
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

const REPO = 'https://github.com/stoatworks-labs/bolometer';
const page = { mounted: null, instance: null, stats: '' };

function describe(instance, time) {
  const f = instance.lastFrame;
  const s = instance.Settings();
  const cam = instance.camera;
  const parts = [`${s.rate} Hz sensor`];
  const k = f.latestIndex;
  if (f.frozen) {
    parts.push(`FFC: instant ${k} frozen, the last good frame held${f.shutterClosed ? ', shutter closed' : ', shutter open, the bolometers relaxing back'}`);
  } else if (f.readout && !f.prime) {
    parts.push(`instant ${f.readoutIndex} read out at ${f.readoutTime.toFixed(3)} s (${f.fresh} since the last frame)`);
  } else if (f.prime) {
    parts.push('the camera starts (settled)');
  } else {
    parts.push(`holding instant ${cam.lastK}, the next at ${cam.InstantTime(cam.lastK + 1).toFixed(3)} s`);
  }
  const used = instance.lastPasses ?? {};
  if (used.keep !== undefined) parts.push(`AGC kept ${used.keep.toFixed(3)} of the last transfer`);
  if (f.readout) parts.push(`drift ${f.driftAmplitude.toFixed(4)} K RMS`);
  const next = cam.NextFfcStart();
  if (Number.isFinite(next) && !f.frozen) parts.push(`next FFC at ${next.toFixed(2)} s (in ${Math.max(0, next - time).toFixed(1)} s)`);
  else if (!Number.isFinite(next)) parts.push('Shutter Interval Off');
  parts.push(`${f.ffcsCompleted} FFC${f.ffcsCompleted === 1 ? '' : 's'} done`);
  parts.push(`host clock ${time.toFixed(3)} s`);
  return parts.join(' · ');
}

function createRenderer(gl, quad) {
  const programs = compilePrograms(gl);
  const instance = new Bolometer(gl, programs);
  instance.InitGL();
  page.instance = instance;
  // The kit's clip renderer is private to mountDemo; the crosscheck draws the
  // page's clips through one of its own, on the same context and quad.
  installHook(gl, programs, new SourceRenderer(gl, quad));

  return {
    render({ input, params, time }) {
      // The host restates every parameter each frame.
      for (const p of PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));
      // Shutter is a toggle here: the plugin counts its rising edge, and the
      // page lets go of it after this frame, as a host does with an event.
      if (params.get('shutter') > 0.5) queueMicrotask(() => params.set('shutter', 0));

      instance.SetTime(time);
      instance.ProcessOpenGL(input, null);
      page.stats = describe(instance, time);
    },
  };
}

const COMMON = {
  es: 'Precision. The kit gives every fragment stage highp float, int and samplers, so the PCG hash’s 32-bit unsigned arithmetic wraps as GLSL 4.10 says it does. In the vertex stage it raises float and int only, and GLSL ES 3.00 reads a vertex-stage sampler2D at lowp by default; the histogram’s vertex shader reads each sample’s temperature through one, so the page adds one line to that stage, precision highp sampler2D, and nothing else to the plugin’s text (demo/tools/check_shaders.py pins it). A desktop GPU runs every precision at 32 bits either way. This page was checked on a desktop GPU only: on a phone it is untested.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  about: 'The About block — a text line and link buttons for the host’s panel — is absent: a web page has links of its own. So are the plugin’s log file and its harness’s negative controls (every model uniform is 0, as shipped). There is no audio path, so nothing here is missing for want of one.',
};

const mounted = mountDemo({
  name: 'SW Bolometer',
  pluginId: 'BO01',
  tagline:
    'A thermal camera on an uncooled microbolometer core, looking at your clip. Every pixel is a tiny thermal mass with a 12 ms time constant, read out at the export-limited 9 Hz, corrected by a calibration a shutter refreshes now and then, and plateau equalisation decides which colour each temperature gets. Nothing is drawn: the stutter, the freeze, the contrast that jumps, the halos, the stripes and the dark centre all follow from the camera. Temperature is the clip’s luma × alpha, 15–40 °C.',
  repo: REPO,
  blurb:
    'It is SW Bolometer’s own GLSL — all nine of its passes, ported from the repository to WebGL2 unedited — run by a JavaScript port of the plugin’s CPU half that only a reader checks: the camera’s clock (the sensor instants, the shutter’s schedule, the exact weights of the thermal lag, the drift’s age), the AGC’s damping, the pass orchestration and every conversion. The clip is the plugin harness’s own card, one of the kit’s generated clips, or a picture of your own.',
  sources: ['card', 'spot', 'scene', 'ramp', 'bars', 'grid', 'detail', 'alpha'],
  params: PARAMS,
  // The plugin declares no presets. These are the page's: combinations of
  // the plugin's own parameters and nothing else.
  presets: {
    'White Hot: the core’s own 8-bit grey': { palette: Palette.WhiteHot },
    'Black Hot': { palette: Palette.BlackHot },
    '30 Hz sensor': { frameRate: FrameRate.Hz30 },
    'A shutter every 5 s': { shutterInterval: ParamFromShutterInterval(5.0) },
    'Shutter Off: the drift builds for 10 minutes': { shutterInterval: 1 },
    'Drift 1 K a minute: the stripes grow fast': { drift: ParamFromDrift(1.0) },
    'Narcissus 1 K: the dark centre': { narcissus: ParamFromNarcissus(1.0) },
    'Plateau 7%: Boson’s factory AGC': { plateau: ParamFromPlateau(0.07) },
    'Plateau 100%: plain histogram equalisation': { plateau: 1 },
    'Manual span, 20 to 35 °C': { span: Span.Manual, level: ParamFromLevel(27.5), width: ParamFromWidth(15.0) },
    'No halo': { halo: 0 },
    'Halo 3: heavy ringing': { halo: 1 },
    'Mix 0.5: half the clip, half the camera': { mix: 0.5 },
  },
  needFloat: true,
  needFloatBlend: true,
  showBackdrop: true,
  differences: [
    'The CPU half is a port, and only a reader checks it. The nine shaders are the plugin’s, unedited. What runs them is translated to JavaScript by hand in demo/port.js: Model.h’s constants, Controls.cpp’s table and conversions, Camera.cpp’s clock (the sensor instants k/f from an epoch, the flat-field correction’s schedule and its 23/26 s windows, the exact weights of the 12 ms lag over the host’s frame, the frozen instants, the drift’s age and pattern, all in double as the C++ is), Palettes.cpp’s Build() and Bolometer.cpp’s ProcessOpenGL: readout, state, histogram, LUT, display, blur twice, shown and composite on a frame with a new sensor instant, state and composite otherwise, with every uniform, and the resample when the raster changes. demo/tools/crosscheck/ ran the page and bmtest, the plugin’s own harness, frame by frame from the same frames in 21 cases (every palette and sensor rate, Manual, the plateau’s and the halo’s ends, drift and narcissus, Mix, a Shutter press, a 5 s Shutter Interval, Frame Rate changes mid-run, host rates of 24, 30, 60 and 144 fps, three rasters): 18 came out identical to the bit, with the same sensor instants read out and the same FFCs. In the other three, one pixel in each of a few frames, or at Mix 0.5 the outputs that land exactly half-way between two 8-bit codes, rounded one code the other way. Outside those cases, only a reader checks the port. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders or the palettes drifts, or if the C++ the port was made from changes.',
    'The histogram is the plugin’s: one point per sample of a fixed 256 × 144 grid, blended additively into a 512 × 1 32-bit float target. WebGL2 can do that only with two extensions, EXT_color_buffer_float and EXT_float_blend; a browser without either gets a message naming the missing one in place of the picture, rather than a histogram that silently stops counting.',
    'The clock is the page’s. It is the kit’s: seconds from the browser’s requestAnimationFrame, capped at a tenth of a second a frame and standing still while paused, handed to the plugin every frame as a host hands its own. The plugin’s vote on whether a host means seconds or milliseconds (Resolume has been seen sending both) is not ported, because this host’s unit is known; in Arena the camera starts again once that vote settles, a few frames in, and here it does not. Restart sends the clock back to 0, which the camera takes as a host clock that ran backwards: it starts again, settled, with the AGC re-primed, as the plugin’s does.',
    'Paused, nothing is read out. The camera only changes its picture at a sensor instant, and the clock does not move while paused, so a control that acts at a readout — every one but Mix — shows when you press Play or Step (one 60 fps frame, which may or may not reach the next instant).',
    'Shutter is FF_TYPE_EVENT in the plugin; here it is a toggle the page lets go of after one frame, as a host lets go of an event button. The plugin counts its rising edge, so one press is one FFC at the next sensor instant.',
    'Alpha. The kit’s clips and a picture of your own are premultiplied, as the kit takes Resolume’s clips to be. The plugin’s temperature is luma × alpha and its composite assumes straight alpha (its AGENTS.md says so, and lists the output’s alpha as an open question), so over the Shape on transparency clip a half-transparent edge reads colder here than its colour would, and with Mix below 1 the canvas, which is premultiplied, shows the plugin’s straight-alpha output over the backdrop. Every other clip is opaque.',
    'The default clip is the plugin harness’s own card (bmtest’s buildCard, the clip behind the README’s picture), ported to a small GLSL shader of the page’s own; the crosscheck compares it with bmtest’s card. The kit’s clips are not thermal scenes: here, as in the plugin, temperature is a clip’s encoded luma × alpha, 15 °C for black or clear and 40 °C for white, which is a choice, not a measurement.',
    'The drift’s stripes, the narcissus spot and the halo are in host pixels, as the plugin’s are, so they are finer at 1920 × 1080 than at 640 × 360. Changing the Composition mid-run carries the bolometers and the held frame to the new raster the plugin’s way (a bilinear resample, never a clear); that path is ported and not cross-checked.',
    'A video of your own plays at the browser’s pace, not on the page’s clock: the camera samples whatever frame the browser has decoded at each page frame, so Pause stops the camera but not the video.',
    COMMON.es,
    'The line under the picture is not the plugin. It reports what the port computed for this frame — the sensor instant read out or held, the FFC, the AGC’s damping, the drift and the next shutter — and the plugin draws no such thing.',
    COMMON.presets,
    COMMON.about,
    'What the harness measured, and this page did not: the bolometers within 1.5e-12 K of an RK4 integration of their own lag; over 10 minutes at nine host rates and three sensor rates, 1,954,476 host frames reading out exactly the sensor instants integer arithmetic gives; the histogram exact to the count; 37 deliberately wrong models and 27 one-character mutants of the shipped code all caught. That is tools/bmtest in the repository, and it, not this page, is the reason to believe the camera. When this page was made no host had loaded the plugin, not Arena and not Avenue; its README says what has been checked since.',
  ],
  createRenderer,
});

page.mounted = mounted ?? null;

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status bo-status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'bolometer-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== page.stats) statLine.textContent = page.stats;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
window.bolometerPage = page;
