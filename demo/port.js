/**
 * SW Bolometer's CPU half, ported by hand from the C++ to JavaScript.
 *
 * **Only a reader checks this file against the C++** (demo/tools/crosscheck/
 * compares the page's pictures with bmtest's on the cases it lists; nothing
 * checks the rest). It is, section by section:
 *
 *   - Model.h: every constant, with the same values.
 *   - Controls.h / Controls.cpp: the table of controls (names, groups, kinds,
 *     element lists, defaults as the host's floats) and every conversion from
 *     the host's 0..1 to hertz, seconds, kelvin, a share of the samples and the
 *     unsharp gain.
 *   - Camera.h / Camera.cpp: the camera's clock, on the CPU, in double (a JS
 *     number is a double): the sensor instants k/f from an epoch, the FFC
 *     schedule and its windows, the exact weights of the thermal lag
 *     (weightsAt), the frozen instants, the readouts' count and the drift's
 *     age and pattern. The negative-control hooks (Camera::Hooks) are left
 *     out: the shipped path is the one ported.
 *   - Palettes.cpp: Build(), the four 256-entry rows, from the two tables in
 *     palettes.js (spliced from the C++ by check_shaders.py --write).
 *   - Bolometer.cpp: the constructor's defaults, InitGL (the nine programs,
 *     the palette texture), Settings(), SetFloatParameter (Shutter on its
 *     rising edge), ensureBuffers (the raster change: the state and the held
 *     frame resampled, never cleared), resample, and ProcessOpenGL with
 *     render(): readout -> state -> histogram -> LUT -> display -> blur ->
 *     shown -> composite on a frame with a new sensor instant, state ->
 *     composite otherwise, with every uniform.
 *
 * Not ported: the clock-unit vote in UpdateClock (the page hands the plugin
 * seconds, as bmtest does with SetClockScaleForTest( 1.0 ); see the page's
 * disclosure), the log (Diag.cpp), the About block, the host-state save and
 * restore (GLState.h's ScopedGLState: the page owns its context), and the
 * harness's negative controls (TestHooks and Camera::Hooks: every model
 * uniform here is 0, as shipped).
 *
 * demo/tools/check_shaders.py holds this file's shader assembly to
 * Shaders.cpp's and Bolometer.cpp's verbatim, every uniform name to the
 * shaders' declarations, and the C++ it was ported from to the SHA-256 it had
 * when it was ported: a change there fails tools/verify.sh until a reader
 * carries it here.
 */
import { PassBuffer, Program, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import { RAINBOW, IRON } from './palettes.js';

/** static_cast< float >: what a host's float, or a glUniform1f, keeps of a double. */
export const f32 = Math.fround;

const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));
/** std::lround: half away from zero, which Math.round is not. */
const lround = (v) => (v < 0 ? -Math.floor(-v + 0.5) : Math.floor(v + 0.5));

// ==== PORT: Model.h ===========================================================

export const model = Object.freeze({
  kPi: 3.14159265358979323846,
  kTau: 0.012,
  kTempLow: 15.0,
  kTempSpan: 25.0,
  kFfcDuration: 23.0 / 26.0,
  kShutterMargin: 3.0 / 26.0,
  kShutterTemp: 27.5,
  kDriftAgeCap: 600.0,
  kBins: 512,
  kHistLow: 5.0,
  kHistHigh: 50.0,
  kGridW: 256,
  kGridH: 144,
  kGridCount: 256 * 144,
  kDamping: 0.85,
  kNarcissusR: 0.15,
  kNarcissusB: 0.10,
  kHaloSigma: 0.01,
  kHaloCut: 4.0,
});

// ==== PORT: Controls.h and Controls.cpp =======================================

/** ParamId: the host's indices. The About block (PT_ABOUT_TEXT on) is not here. */
export const PT = Object.freeze({
  FRAME_RATE: 0,
  SHUTTER_INTERVAL: 1,
  SHUTTER: 2,
  DRIFT: 3,
  NARCISSUS: 4,
  SPAN: 5,
  PLATEAU: 6,
  LEVEL: 7,
  WIDTH: 8,
  PALETTE: 9,
  HALO: 10,
  MIX: 11,
  ABOUT_TEXT: 12,
});

export const Kind = Object.freeze({ Standard: 'standard', Option: 'option', Event: 'event' });
export const FrameRate = Object.freeze({ Export: 0, Hz30: 1, Hz60: 2, Count: 3 });
export const Span = Object.freeze({ Auto: 0, Manual: 1, Count: 2 });
export const Palette = Object.freeze({ WhiteHot: 0, BlackHot: 1, Ironbow: 2, Rainbow: 3, Count: 4 });

const clamp01 = (v) => clamp(v, 0.0, 1.0);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = Math.min(low, high);
  const hi = Math.max(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
const linear = (v, low, high) => low + (high - low) * clamp01(v);
const inverseLinear = (value, low, high) => f32(clamp((value - low) / (high - low), 0.0, 1.0));

const kIntervalLow = 5.0;
const kIntervalHigh = 300.0;
const kIntervalOff = f32(0.999);
const kDriftHigh = 1.0;
const kNarcHigh = 2.0;
const kPlateauLow = 1e-5;
const kPlateauHigh = 1.0;
const kLevelLow = 15.0;
const kLevelHigh = 40.0;
const kWidthLow = 0.5;
const kWidthHigh = 30.0;
const kHaloHigh = 3.0;

const kRateNames = ['9 Hz', '30 Hz', '60 Hz'];
const kSpanNames = ['Auto', 'Manual'];
const kPaletteNames = ['White Hot', 'Black Hot', 'Ironbow', 'Rainbow'];

export function OptionIndex(value, count) {
  return clamp(lround(value), 0, count - 1);
}

export function FrameRateHz(option) {
  switch (clamp(option, 0, FrameRate.Count - 1)) {
    case FrameRate.Export: return 9.0;
    case FrameRate.Hz30: return 30.0;
    case FrameRate.Hz60: return 60.0;
    default: return 9.0;
  }
}

export function ShutterIntervalFromParam(v) {
  if (v >= kIntervalOff) return Infinity;
  return geometric(v, kIntervalLow, kIntervalHigh);
}
export function ParamFromShutterInterval(seconds) {
  if (!Number.isFinite(seconds)) return 1.0;
  return Math.min(inverseGeometric(seconds, kIntervalLow, kIntervalHigh), f32(0.998));
}
export const DriftFromParam = (v) => linear(v, 0.0, kDriftHigh);
export const ParamFromDrift = (k) => inverseLinear(k, 0.0, kDriftHigh);
export const NarcissusFromParam = (v) => linear(v, 0.0, kNarcHigh);
export const ParamFromNarcissus = (k) => inverseLinear(k, 0.0, kNarcHigh);
export const PlateauFromParam = (v) => geometric(v, kPlateauLow, kPlateauHigh);
export const ParamFromPlateau = (f) => inverseGeometric(f, kPlateauLow, kPlateauHigh);
export const LevelFromParam = (v) => linear(v, kLevelLow, kLevelHigh);
export const ParamFromLevel = (c) => inverseLinear(c, kLevelLow, kLevelHigh);
export const WidthFromParam = (v) => geometric(v, kWidthLow, kWidthHigh);
export const ParamFromWidth = (k) => inverseGeometric(k, kWidthLow, kWidthHigh);
export const HaloFromParam = (v) => linear(v, 0.0, kHaloHigh);
export const ParamFromHalo = (a) => inverseLinear(a, 0.0, kHaloHigh);

const standard = (id, name, group, value) => ({ id, name, group, kind: Kind.Standard, defaultValue: f32(value), options: null });
const option = (id, name, group, value, names) => ({ id, name, group, kind: Kind.Option, defaultValue: f32(value), options: names });
const event = (id, name, group) => ({ id, name, group, kind: Kind.Event, defaultValue: 0.0, options: null });

/** kTable, in ParamId order. */
const kTable = Object.freeze([
  option(PT.FRAME_RATE, 'Frame Rate', 'Sensor', 0.0, kRateNames),
  standard(PT.SHUTTER_INTERVAL, 'Shutter Interval', 'Sensor', ParamFromShutterInterval(30.0)),
  event(PT.SHUTTER, 'Shutter', 'Sensor'),
  standard(PT.DRIFT, 'Drift', 'Sensor', ParamFromDrift(0.1)),
  standard(PT.NARCISSUS, 'Narcissus', 'Sensor', ParamFromNarcissus(0.1)),

  option(PT.SPAN, 'Span', 'Span', 0.0, kSpanNames),
  standard(PT.PLATEAU, 'Plateau', 'Span', ParamFromPlateau(0.02)),
  standard(PT.LEVEL, 'Level', 'Span', ParamFromLevel(27.5)),
  standard(PT.WIDTH, 'Width', 'Span', ParamFromWidth(25.0)),

  option(PT.PALETTE, 'Palette', 'Look', Palette.Ironbow, kPaletteNames),
  standard(PT.HALO, 'Halo', 'Look', ParamFromHalo(1.0)),
  standard(PT.MIX, 'Mix', 'Look', 1.0),
]);
if (kTable.length !== PT.ABOUT_TEXT || kTable.some((row, i) => row.id !== i)) {
  throw new Error('every control needs a row in the table, in ParamId order');
}

export function InfoOf(id) {
  return kTable[Math.min(id, PT.ABOUT_TEXT - 1)];
}
export const TABLE = kTable;

// ==== PORT: Camera.h and Camera.cpp ===========================================

/** A millionth of a sensor period: an instant this close to a host frame is at or before it. */
const kInstantSlack = 1e-6;

const weights = (prev = 1.0, scene = 0.0, shutter = 0.0) => ({ prev, scene, shutter });

/** CameraFrame, with its defaults. */
function cameraFrame() {
  return {
    prime: false,
    state: weights(),
    readout: false,
    sample: weights(),
    fresh: 0,
    driftAmplitude: 0.0,
    driftSeed: 0,
    readoutIndex: -1,
    readoutTime: 0.0,
    latestIndex: 0,
    frozen: false,
    shutterClosed: false,
    ffcsCompleted: 0,
  };
}

export class Camera {
  constructor() {
    this.primed = false;
    this.epoch = 0.0;
    this.rate = 9.0;
    this.interval = 30.0;
    this.lastT = 0.0;
    this.lastK = 0;
    this.nextStart = 0.0; // Infinity: none scheduled
    this.lastEnd = 0.0; // the end of the last FFC, or the epoch
    this.startTime = 0.0;
    this.seed = 1; // uint32_t
    this.completed = 0;
    this.hostFrames = 0;
  }

  /** Start again from the next frame. */
  Reprime() {
    this.primed = false;
  }

  Tau() {
    return model.kTau;
  }

  FfcDuration() {
    return model.kFfcDuration;
  }

  IndexAtOrBefore(t) {
    return Math.floor((t - this.epoch) * this.rate + kInstantSlack);
  }

  IndexAtOrAfter(t) {
    return Math.ceil((t - this.epoch) * this.rate - kInstantSlack);
  }

  InstantTime(k) {
    return this.epoch + k / this.rate;
  }

  NextFfcStart() {
    return this.nextStart;
  }

  LastFfcEnd() {
    return this.lastEnd;
  }

  inProgress(t) {
    return Number.isFinite(this.nextStart) && t >= this.nextStart && t < this.nextStart + this.FfcDuration();
  }

  reschedule() {
    if (!Number.isFinite(this.interval)) {
      this.nextStart = Infinity;
      return;
    }
    this.nextStart = this.InstantTime(Math.max(this.IndexAtOrAfter(this.lastEnd + this.interval), this.lastK + 1));
  }

  prime(t, settings) {
    this.primed = true;
    this.epoch = t;
    this.rate = settings.rate > 0.0 ? settings.rate : 9.0;
    this.interval = settings.interval;
    this.lastT = t;
    this.lastK = 0;
    this.lastEnd = t;
    this.startTime = t;
    this.completed = 0;
    this.reschedule();

    const f = cameraFrame();
    f.prime = true;
    f.state = weights(0.0, 1.0, 0.0);
    f.readout = true;
    f.sample = weights(0.0, 1.0, 0.0);
    f.fresh = 0;
    f.driftSeed = this.seed;
    f.readoutIndex = 0;
    f.readoutTime = t;
    f.latestIndex = 0;
    return f;
  }

  weightsAt(x, windows) {
    const tau = this.Tau();
    const w = weights();
    w.prev = Math.exp(-(x - this.lastT) / tau);
    w.shutter = 0.0;
    for (const win of windows) {
      const a = Math.max(win.start, this.lastT);
      const b = Math.min(win.open, x);
      if (b > a) w.shutter += Math.exp(-(x - b) / tau) - Math.exp(-(x - a) / tau);
    }
    w.scene = Math.max(0.0, 1.0 - w.prev - w.shutter);
    return w;
  }

  frozenIndex(k, windows) {
    for (const win of windows) {
      if (k >= this.IndexAtOrAfter(win.start) && k <= this.IndexAtOrAfter(win.end) - 1) return true;
    }
    return false;
  }

  /**
   * The next host frame, at `t` seconds. `press` starts an FFC at the next
   * sensor instant (ignored while one is in progress).
   */
  Advance(t, settings, press) {
    this.hostFrames += 1;
    if (!this.primed || !(t >= this.lastT) || !Number.isFinite(t)) return this.prime(t, settings);

    // A new rate starts a new grid of instants at the last host frame (whose
    // readout is already shown); an FFC in progress keeps its time window.
    if (settings.rate > 0.0 && settings.rate !== this.rate) {
      const busy = this.inProgress(this.lastT);
      this.epoch = this.lastT;
      this.rate = settings.rate;
      this.lastK = 0;
      if (!busy) this.reschedule();
    }
    if (settings.interval !== this.interval) {
      this.interval = settings.interval;
      if (!this.inProgress(this.lastT)) this.reschedule();
    }
    if (press && !this.inProgress(this.lastT)) this.nextStart = Math.min(this.nextStart, this.InstantTime(this.lastK + 1));

    // The FFCs that touch ( lastT, t ]: one in progress, one starting, and --
    // across a long gap -- any that start and end inside it.
    const D = this.FfcDuration();
    const endBefore = this.lastEnd;
    const seedBefore = this.seed;
    const windows = [];
    const completions = [];
    while (this.nextStart <= t) {
      const win = { start: this.nextStart, open: this.nextStart + D - model.kShutterMargin, end: this.nextStart + D };
      windows.push(win);
      if (win.end > t) break;
      this.lastEnd = win.end;
      this.seed = (this.seed + 1) >>> 0;
      this.completed += 1;
      completions.push({ end: win.end, seed: this.seed });
      this.reschedule();
    }

    const f = cameraFrame();
    f.state = this.weightsAt(t, windows);
    for (const win of windows) f.shutterClosed = f.shutterClosed || (t >= win.start && t < win.open);

    // The latest instant no FFC has frozen, and how many were read out.
    const kNew = this.IndexAtOrBefore(t);
    f.frozen = this.frozenIndex(kNew, windows);
    let j = kNew;
    for (let i = windows.length - 1; i >= 0; i -= 1) {
      const lo = this.IndexAtOrAfter(windows[i].start);
      const hi = this.IndexAtOrAfter(windows[i].end) - 1;
      if (j >= lo && j <= hi) j = lo - 1;
    }
    let fresh = kNew - this.lastK;
    for (const win of windows) {
      const lo = Math.max(this.IndexAtOrAfter(win.start), this.lastK + 1);
      const hi = Math.min(this.IndexAtOrAfter(win.end) - 1, kNew);
      if (hi >= lo) fresh -= hi - lo + 1;
    }
    if (j <= this.lastK) j = -1;
    let readAt = t;
    if (j >= 0) readAt = this.InstantTime(j);

    if (j >= 0) {
      f.readout = true;
      f.sample = this.weightsAt(Math.min(readAt, t), windows);
      f.fresh = Math.max(1, fresh);
      f.readoutIndex = j;
      f.readoutTime = readAt;

      // The drift at the readout: its age since the last FFC that had ended
      // by then, and that FFC's pattern.
      let since = endBefore;
      let pattern = seedBefore;
      for (const c of completions) {
        if (c.end <= readAt) {
          since = c.end;
          pattern = c.seed;
        }
      }
      const age = clamp(readAt - since, 0.0, model.kDriftAgeCap);
      f.driftAmplitude = settings.driftRate * age;
      f.driftSeed = pattern;
    }
    f.latestIndex = kNew;
    f.ffcsCompleted = this.completed;

    this.lastT = t;
    this.lastK = kNew;
    return f;
  }
}

// ==== PORT: Palettes.cpp, Build() =============================================

export const kEntries = 256;
export const kIronEntries = 128;
export const kPalettes = 4;

/** The four palettes as RGBA8 rows, alpha 255: [palette][entry][rgba], flat. */
export function BuildPalettes() {
  const out = new Uint8Array(kPalettes * kEntries * 4);
  for (let i = 0; i < kEntries; i += 1) {
    const grey = i;
    const inv = 255 - i;
    // Ironbow: the 128 iron entries spread over 256 by linear interpolation,
    // rounded half up; entry 0 is iron 0 and entry 255 is iron 127.
    const s = (i * (kIronEntries - 1)) / (kEntries - 1);
    const i0 = Math.floor(s);
    const i1 = i0 + 1 < kIronEntries ? i0 + 1 : i0;
    const f = s - i0;
    const iron = [0, 0, 0];
    for (let c = 0; c < 3; c += 1) iron[c] = Math.floor(IRON[i0][c] * (1.0 - f) + IRON[i1][c] * f + 0.5);
    const rows = [null, null, iron, RAINBOW[i]];
    for (let p = 0; p < kPalettes; p += 1) {
      const o = (p * kEntries + i) * 4;
      if (p === 0) out[o] = out[o + 1] = out[o + 2] = grey;
      else if (p === 1) out[o] = out[o + 1] = out[o + 2] = inv;
      else for (let c = 0; c < 3; c += 1) out[o + c] = rows[p][c];
      out[o + 3] = 255;
    }
  }
  return out;
}

// ==== PORT: Shaders.cpp's assembly and Bolometer.cpp's InitGL =================

export const PASSES = ['readout', 'state', 'histogram', 'lut', 'display', 'blur', 'shown', 'composite', 'resample'];

/**
 * The one line the page adds to the plugin's GLSL, and only in the browser.
 * The kit's port() gives the fragment stage highp samplers, but the vertex
 * stage only highp float and int, and GLSL ES 3.00's default for a vertex
 * sampler2D is lowp: the histogram's vertex shader reads each sample's
 * temperature (an R32F texel, 5 to 50 degC) through one. A desktop GPU runs
 * every precision at 32 bits; a phone GPU that honoured lowp would bin the
 * temperatures in steps of whole kelvin or worse. Desktop GLSL 4.10 accepts
 * and ignores a precision statement, so this changes nothing there.
 */
const kVertexSamplers = 'precision highp sampler2D;\n';

function AssembleVertex(pass) {
  if (pass === 'histogram') return S.VERSION + kVertexSamplers + S.HISTOGRAM_VERTEX;
  return S.VERSION + S.QUAD_VERTEX;
}

function Assemble(pass) {
  const fragments = {
    readout: S.VERSION + S.COMMON + S.READOUT_FRAGMENT,
    state: S.VERSION + S.COMMON + S.STATE_FRAGMENT,
    histogram: S.VERSION + S.HISTOGRAM_FRAGMENT,
    lut: S.VERSION + S.LUT_FRAGMENT,
    display: S.VERSION + S.DISPLAY_FRAGMENT,
    blur: S.VERSION + S.BLUR_FRAGMENT,
    shown: S.VERSION + S.SHOWN_FRAGMENT,
    composite: S.VERSION + S.COMPOSITE_FRAGMENT,
    resample: S.VERSION + S.RESAMPLE_FRAGMENT,
  };
  return fragments[pass];
}

/** The nine programs, compiled once and shared by every instance on the page. */
export function compilePrograms(gl) {
  const programs = {};
  for (const pass of PASSES) {
    programs[pass] = new Program(gl, AssembleVertex(pass), Assemble(pass), pass);
  }
  return programs;
}

// ==== PORT: Bolometer.h and Bolometer.cpp =====================================

/** glUniform2i by name, counted as missing the way the kit's setters count. */
function setIvec2(program, name, a, b) {
  const location = program.location(name);
  if (location === null) {
    program.missing.add(name);
    return;
  }
  program.gl.uniform2i(location, a | 0, b | 0);
}

/** GLState.h's bindUnit and unbindTextureUnits. */
function bindUnit(gl, unit, texture) {
  bindTexture(gl, unit, texture);
}
function unbindTextureUnits(gl, count) {
  for (let unit = count - 1; unit >= 0; unit -= 1) {
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
}

/**
 * One SW Bolometer instance. `params` is the plugin's float params[], as a
 * Float32Array so that a value rounds as the host's float does.
 */
export class Bolometer {
  constructor(gl, programs) {
    this.gl = gl;
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (let id = 0; id < PT.ABOUT_TEXT; id += 1) this.params[id] = InfoOf(id).defaultValue;

    this.camera = new Camera();
    this.lastFrame = cameraFrame();
    this.programs = programs;
    this.vao = null;
    this.paletteTexture = null;
    const nearest = () => new PassBuffer(gl, { filter: 'nearest' });
    this.state = [nearest(), nearest()];
    this.current = 0;
    this.raw = nearest();
    this.display = nearest();
    this.blurred = nearest();
    this.shown = nearest();
    this.histogram = nearest();
    this.lut = [nearest(), nearest()];
    this.currentLut = 0;
    this.primeLut = false;
    this.bufferWidth = 0;
    this.bufferHeight = 0;

    // The clock. UpdateClock's vote on seconds or milliseconds is not
    // ported: the page hands the plugin seconds, as bmtest does
    // (SetClockScaleForTest( 1.0 )).
    this.hostTime = -1.0;
    this.clockScale = 1.0;
    this.wallStart = -1.0;
    this.now = 0.0;

    this.shutterPresses = 0;
    this.shutterHeld = false;

    // What the last frame handed the shaders, for the line under the picture.
    this.lastPasses = null;
  }

  InitGL() {
    const gl = this.gl;
    // A core profile draws nothing without a vertex array bound; every pass
    // makes its vertices from gl_VertexID.
    this.vao = gl.createVertexArray();

    // The palettes: one 256-entry row each, read by texelFetch.
    const table = BuildPalettes();
    this.paletteTexture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.paletteTexture);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, kEntries, kPalettes, 0, gl.RGBA, gl.UNSIGNED_BYTE, table);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.bindTexture(gl.TEXTURE_2D, null);

    this.camera.Reprime();
    this.bufferWidth = 0;
    this.bufferHeight = 0;
    return true;
  }

  DeInitGL() {
    const gl = this.gl;
    if (this.vao) gl.deleteVertexArray(this.vao);
    if (this.paletteTexture) gl.deleteTexture(this.paletteTexture);
    this.vao = null;
    this.paletteTexture = null;
    for (const b of [...this.state, this.raw, this.display, this.blurred, this.shown, this.histogram, ...this.lut]) b.dispose();
    this.bufferWidth = 0;
    this.bufferHeight = 0;
  }

  /** What the camera is set to, from the controls. */
  Settings() {
    const p = this.params;
    return {
      rate: FrameRateHz(OptionIndex(p[PT.FRAME_RATE], FrameRate.Count)),
      interval: ShutterIntervalFromParam(p[PT.SHUTTER_INTERVAL]),
      driftRate: DriftFromParam(p[PT.DRIFT]) / 60.0,
    };
  }

  /** The About buttons are not here: a web page has links of its own. */
  SetFloatParameter(index, value) {
    if (index < 0 || index >= PT.ABOUT_TEXT) return false;
    // Shutter acts on the press, once: an event arrives as 1 on press and 0
    // on release, and a host is free to restate either.
    if (index === PT.SHUTTER) {
      const down = f32(value) >= 0.5;
      if (down && !this.shutterHeld) this.shutterPresses += 1;
      this.shutterHeld = down;
    }
    this.params[index] = value;
    return true;
  }

  GetFloatParameter(index) {
    return index >= 0 && index < PT.ABOUT_TEXT ? this.params[index] : 0.0;
  }

  SetTime(time) {
    this.hostTime = time;
    return true;
  }

  /** UpdateClock's last lines, with the unit settled on seconds (the vote is not ported). */
  UpdateClock() {
    const wallNow = performance.now() / 1000;
    if (this.wallStart < 0.0) this.wallStart = wallNow;
    const raw = this.hostTime;
    this.now = raw >= 0.0 && this.clockScale !== 0.0 ? raw * this.clockScale : wallNow - this.wallStart;
  }

  draw(target) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, target.width, target.height);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  resample(from, to) {
    const gl = this.gl;
    const program = this.programs.resample;
    program.use();
    bindUnit(gl, 0, from.texture);
    program.setSampler('Src', 0);
    setIvec2(program, 'SrcSize', from.width, from.height);
    setIvec2(program, 'DstSize', to.width, to.height);
    this.draw(to);
  }

  ensureBuffers(width, height) {
    const gl = this.gl;
    this.histogram.ensure(model.kBins, 1, gl.R32F);
    this.lut[0].ensure(model.kBins, 1, gl.RGBA32F);
    this.lut[1].ensure(model.kBins, 1, gl.RGBA32F);

    const resized = this.bufferWidth > 0 && (width !== this.bufferWidth || height !== this.bufferHeight);
    if (resized && this.state[this.current].texture && this.shown.texture) {
      // The thermal state and the held frame survive a raster change: each is
      // resampled into a buffer of the new size, which then replaces it.
      const nextState = new PassBuffer(gl, { filter: 'nearest' }).ensure(width, height, gl.R32F);
      const nextShown = new PassBuffer(gl, { filter: 'nearest' }).ensure(width, height, gl.RGBA8);
      gl.bindVertexArray(this.vao);
      this.resample(this.state[this.current], nextState);
      this.resample(this.shown, nextShown);
      this.state[this.current].dispose();
      this.state[this.current] = nextState;
      this.shown.dispose();
      this.shown = nextShown;
      this.state[1 - this.current].dispose();
    }

    this.state[0].ensure(width, height, gl.R32F);
    this.state[1].ensure(width, height, gl.R32F);
    this.shown.ensure(width, height, gl.RGBA8);
    this.raw.ensure(width, height, gl.R32F);
    this.display.ensure(width, height, gl.R32F);
    this.blurred.ensure(width, height, gl.R32F);
    this.bufferWidth = width;
    this.bufferHeight = height;
    return true;
  }

  render(input, hostViewport, hostFBO, width, height) {
    const gl = this.gl;
    const f = this.lastFrame;
    const P = this.programs;
    const used = { readout: f.readout };
    gl.bindVertexArray(this.vao);

    const lumaModel = 0;
    const setScene = (p, w) => {
      p.setSampler('InputTexture', 0);
      p.setSampler('State', 1);
      p.set('Weights', w.prev, w.scene, w.shutter);
      p.set('ShutterTemp', model.kShutterTemp);
      p.set('TempLow', model.kTempLow);
      p.set('TempSpan', model.kTempSpan);
      p.setInt('LumaModel', lumaModel);
    };

    // The readout, from the state at the last host frame (before it moves).
    if (f.readout) {
      const p = P.readout;
      p.use();
      bindUnit(gl, 0, input.texture);
      bindUnit(gl, 1, this.state[this.current].texture);
      setScene(p, f.sample);
      const depth = NarcissusFromParam(this.params[PT.NARCISSUS]);
      const radius = model.kNarcissusR * height;
      const cx = 0.5 * width;
      const cy = 0.5 * height;
      p.set('DriftAmp', f.driftAmplitude);
      p.setUint('DriftSeed', f.driftSeed);
      p.setInt('DriftModel', 0);
      p.set('NarcDepth', depth);
      p.set('NarcCentre', cx, cy);
      p.set('NarcR', radius);
      p.set('NarcB', model.kNarcissusB * height);
      this.draw(this.raw);
      used.narcissus = depth;
    }

    // The bolometers at the host frame's time, into the other half.
    {
      const p = P.state;
      p.use();
      bindUnit(gl, 0, input.texture);
      bindUnit(gl, 1, this.state[this.current].texture);
      setScene(p, f.state);
      this.draw(this.state[1 - this.current]);
      this.current = 1 - this.current;
    }

    if (f.readout) {
      const histScale = model.kBins / (model.kHistHigh - model.kHistLow);

      // The histogram of the readout on the fixed grid: exact counts.
      {
        const p = P.histogram;
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.histogram.fbo);
        gl.viewport(0, 0, model.kBins, 1);
        gl.clearColor(0.0, 0.0, 0.0, 0.0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        p.use();
        bindUnit(gl, 0, this.raw.texture);
        bindUnit(gl, 1, null);
        p.setSampler('Raw', 0);
        setIvec2(p, 'Size', width, height);
        setIvec2(p, 'Grid', model.kGridW, model.kGridH);
        p.set('HistLow', model.kHistLow);
        p.set('HistScale', histScale);
        p.setInt('Bins', model.kBins);
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ONE, gl.ONE);
        // GL_PROGRAM_POINT_SIZE has no WebGL2 switch: gl_PointSize is always read.
        gl.drawArrays(gl.POINTS, 0, model.kGridCount);
        gl.disable(gl.BLEND);
      }

      // The transfer, damped against the last one by d^m.
      {
        const p = P.lut;
        p.use();
        bindUnit(gl, 0, this.histogram.texture);
        bindUnit(gl, 1, this.lut[this.currentLut].texture);
        let keep = Math.pow(model.kDamping, f.fresh);
        if (f.prime || this.primeLut) keep = 0.0;
        const plateau = PlateauFromParam(this.params[PT.PLATEAU]) * model.kGridCount;
        p.setSampler('Histogram', 0);
        p.setSampler('PrevLut', 1);
        p.set('Plateau', plateau);
        p.set('Keep', keep);
        p.setInt('Bins', model.kBins);
        p.setInt('PlateauModel', 0);
        this.draw(this.lut[1 - this.currentLut]);
        this.currentLut = 1 - this.currentLut;
        this.primeLut = false;
        used.keep = keep;
        used.plateau = plateau;
      }

      // The display level.
      {
        const p = P.display;
        p.use();
        bindUnit(gl, 0, this.raw.texture);
        bindUnit(gl, 1, this.lut[this.currentLut].texture);
        const autoSpan = OptionIndex(this.params[PT.SPAN], Span.Count) === Span.Auto;
        const widthK = WidthFromParam(this.params[PT.WIDTH]);
        p.setSampler('Raw', 0);
        p.setSampler('Lut', 1);
        p.setInt('Auto', autoSpan ? 1 : 0);
        p.set('Level', LevelFromParam(this.params[PT.LEVEL]));
        p.set('Width', widthK);
        p.set('HistLow', model.kHistLow);
        p.set('HistScale', histScale);
        p.setInt('Bins', model.kBins);
        this.draw(this.display);
        used.autoSpan = autoSpan;
      }

      // The halo (the detail enhancement's unsharp mask), the core's 8-bit
      // output and the palette, into the held frame.
      const gain = HaloFromParam(this.params[PT.HALO]);
      const sigma = model.kHaloSigma * height;
      const radius = Math.ceil(model.kHaloCut * sigma);
      if (gain !== 0.0) {
        const p = P.blur;
        p.use();
        bindUnit(gl, 0, this.display.texture);
        bindUnit(gl, 1, null);
        p.setSampler('Src', 0);
        setIvec2(p, 'Dir', 1, 0);
        p.setInt('Radius', radius);
        p.set('Sigma', sigma);
        setIvec2(p, 'Size', width, height);
        this.draw(this.blurred);
      }
      {
        const p = P.shown;
        p.use();
        bindUnit(gl, 0, this.display.texture);
        bindUnit(gl, 1, this.blurred.texture);
        bindUnit(gl, 2, this.paletteTexture);
        p.setSampler('Y', 0);
        p.setSampler('BlurH', 1);
        p.setSampler('PaletteTable', 2);
        p.setInt('Radius', radius);
        p.set('Sigma', sigma);
        setIvec2(p, 'Size', width, height);
        p.set('HaloGain', gain);
        p.setInt('PaletteRow', OptionIndex(this.params[PT.PALETTE], Palette.Count));
        p.setInt('PaletteModel', 0);
        this.draw(this.shown);
      }
      used.gain = gain;
      used.sigma = sigma;
      used.radius = radius;
    }

    // The picture, into the host's framebuffer and viewport.
    {
      const p = P.composite;
      gl.bindFramebuffer(gl.FRAMEBUFFER, hostFBO);
      gl.viewport(hostViewport[0], hostViewport[1], hostViewport[2], hostViewport[3]);
      p.use();
      bindUnit(gl, 0, input.texture);
      bindUnit(gl, 1, this.shown.texture);
      bindUnit(gl, 2, null);
      p.setSampler('InputTexture', 0);
      p.setSampler('Shown', 1);
      setIvec2(p, 'ViewOrigin', hostViewport[0], hostViewport[1]);
      p.set('MixAmount', clamp(this.params[PT.MIX], 0.0, 1.0));
      p.setInt('AlphaModel', 0);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    unbindTextureUnits(gl, 3);
    gl.activeTexture(gl.TEXTURE0);
    gl.useProgram(null);
    gl.bindVertexArray(null);
    this.lastPasses = used;
  }

  /**
   * ProcessOpenGL. `input` is { texture, width, height }, the host's clip;
   * `hostFBO` is the host's framebuffer (null: the canvas) and `hostViewport`
   * its viewport. Returns false where the plugin returns FF_FAIL.
   */
  ProcessOpenGL(input, hostFBO = null, hostViewport = null) {
    const gl = this.gl;
    if (!input || !input.texture) return false;
    gl.disable(gl.BLEND);

    const width = input.width | 0;
    const height = input.height | 0;
    if (width <= 0 || height <= 0) return false;
    const viewport = hostViewport ?? [0, 0, width, height];

    this.UpdateClock();
    const press = this.shutterPresses > 0;
    this.shutterPresses = 0;

    if (!this.ensureBuffers(width, height)) return false;
    this.lastFrame = this.camera.Advance(this.now, this.Settings(), press);
    this.render(input, viewport, hostFBO, width, height);
    return true;
  }
}
