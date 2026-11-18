"""The demo's shaders must be the plugin's shaders, character for character.

    python3 demo/tools/check_shaders.py            compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write    regenerate demo/shaders.js and demo/palettes.js
    python3 demo/tools/check_shaders.py --hashes   print the ported C++'s hashes (for a reader)

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

`demo/shaders.js` holds the GLSL pieces of `source/Shaders.cpp`: kVersion, the
two vertex stages (kQuadVertex, kHistogramVertex), kCommon (the clip's
temperature, the PCG hash and the unit variate) and the nine fragment passes --
readout, state, histogram, lut, display, blur, shown, composite and resample.
That is two copies of the same text, and two copies drift -- quietly, because a
thermal picture made by a *plausible* shader looks exactly like one made by the
right one. The whole claim of the page is that it runs the plugin's own
shaders, so the claim needs something enforcing it. `bmtest` drives the real
plugin class and has never heard of this page, and `tools/glslc.sh` compiles
the C++ copies and never looks at the JS one.

`demo/palettes.js` holds the two tables of `source/Palettes.cpp` (LeptonModule's
Rainbow and the iron half of its Ironbow, BSD-2-Clause) and the licence notice
that travels with them. They are compared number for number, and the notice
character for character.

------------------------------------------------------------------- what it does

Pulls each piece out of `source/Shaders.cpp` and the matching backtick literal
out of `demo/shaders.js`, and compares them exactly -- no whitespace
normalisation, no comment stripping: a comment updated on one side only is
exactly the drift worth catching. `kVersion`, a plain string, is compared too.
A piece may be several adjacent raw strings (MSVC refuses one literal over
about 16 KB); this joins them as the compiler does, with nothing between.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
Assemble() is kVersion + kCommon + the pass for the readout and the state and
kVersion + the pass for the other seven, AssembleVertex() is kVersion +
kHistogramVertex for the histogram and kVersion + kQuadVertex for the rest,
Bolometer.cpp's InitGL compiles those for every pass in the Pass order, and
demo/port.js must assemble them the same way -- with one addition, pinned
here to the character: `precision highp sampler2D;` in the histogram's
vertex stage, which GLSL ES 3.00 would otherwise read at lowp.

The page is more than the shaders. The CPU half -- Model.h's constants,
Controls.cpp's conversions and table, Camera.cpp's clock (the sensor instants,
the FFC schedule, the lag's weights, the drift's age) and Bolometer.cpp's
ProcessOpenGL orchestration (readout, state, histogram, LUT, display, blur,
shown, composite, and the resample on a raster change) -- is translated by
hand into demo/port.js. This cannot check a translation, but it can trip when
its source moves: PORTED_FROM holds the SHA-256 of every C++ file and function
the port was made from, and a change to any of them fails here until a reader
carries it across and updates the hash (`--hashes` prints the new ones).

And it fails when a piece declares a uniform that demo/port.js never names (a
uniform the page does not set sits at 0, a control quietly dead on the page and
live in the plugin), or when port.js hands an integer uniform -- int, ivec,
uint or a sampler -- to the kit's `.set()`, which always uploads a float and so
is refused by GL without a word.

The one transformation is a decode, not a normalisation: a backtick cannot
appear raw inside a template literal, so --write would escape one as \\`.
Shaders.cpp has none today. This undoes that escape and REJECTS any other
backslash or any `${` on the JS side; there are none in the C++ bodies, so
either could only be somebody hiding a difference.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half itself. demo/tools/crosscheck/ renders
bmtest and the page from the same frames and compares the pictures, frame by
frame; it needs Chrome and a GPU, so it is run by hand, not by verify.sh. When
you change the C++ the port came from, change demo/port.js too.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there. kVersion, a plain string, is handled on its own.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("HISTOGRAM_VERTEX", "kHistogramVertex"),
    ("COMMON", "kCommon"),
    ("READOUT_FRAGMENT", "kReadoutFragment"),
    ("STATE_FRAGMENT", "kStateFragment"),
    ("HISTOGRAM_FRAGMENT", "kHistogramFragment"),
    ("LUT_FRAGMENT", "kLutFragment"),
    ("DISPLAY_FRAGMENT", "kDisplayFragment"),
    ("BLUR_FRAGMENT", "kBlurFragment"),
    ("SHOWN_FRAGMENT", "kShownFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
    ("RESAMPLE_FRAGMENT", "kResampleFragment"),
]

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are port.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "std::string AssembleVertex( Pass pass )\n{\n"
     "\tif( pass == Pass::Histogram )\n"
     "\t\treturn std::string( kVersion ) + kHistogramVertex;\n"
     "\treturn std::string( kVersion ) + kQuadVertex;\n}"),
    ("source/Shaders.cpp",
     "\tcase Pass::Readout: return std::string( kVersion ) + kCommon + kReadoutFragment;\n"
     "\tcase Pass::State: return std::string( kVersion ) + kCommon + kStateFragment;\n"
     "\tcase Pass::Histogram: return std::string( kVersion ) + kHistogramFragment;\n"
     "\tcase Pass::Lut: return std::string( kVersion ) + kLutFragment;\n"
     "\tcase Pass::Display: return std::string( kVersion ) + kDisplayFragment;\n"
     "\tcase Pass::Blur: return std::string( kVersion ) + kBlurFragment;\n"
     "\tcase Pass::Shown: return std::string( kVersion ) + kShownFragment;\n"
     "\tcase Pass::Composite: return std::string( kVersion ) + kCompositeFragment;\n"
     "\tcase Pass::Resample: return std::string( kVersion ) + kResampleFragment;\n"),
    ("source/Shaders.h",
     "enum class Pass\n{\n\tReadout,\n\tState,\n\tHistogram,\n\tLut,\n\tDisplay,\n\tBlur,\n\tShown,\n"
     "\tComposite,\n\tResample,\n\tCount\n};"),
    ("source/Bolometer.cpp",
     'const char* const names[ kPrograms ] = { "readout", "state", "histogram", "lut", "display", "blur", '
     '"shown", "composite", "resample" };'),
    ("source/Bolometer.cpp", "const std::string vertex   = shaders::AssembleVertex( pass );"),
    ("source/Bolometer.cpp", "const std::string fragment = shaders::Assemble( pass );"),
    ("source/Bolometer.cpp", "if( programs[ i ].Compile( vertex.c_str(), fragment.c_str() ) )"),
    ("demo/port.js",
     "export const PASSES = ['readout', 'state', 'histogram', 'lut', 'display', 'blur', 'shown', "
     "'composite', 'resample'];"),
    # The page adds one precision statement to the histogram's vertex stage
    # (port.js says why); this pins it to exactly that and nothing else.
    ("demo/port.js", "const kVertexSamplers = 'precision highp sampler2D;\\n';"),
    ("demo/port.js",
     "function AssembleVertex(pass) {\n"
     "  if (pass === 'histogram') return S.VERSION + kVertexSamplers + S.HISTOGRAM_VERTEX;\n"
     "  return S.VERSION + S.QUAD_VERTEX;\n}"),
    ("demo/port.js",
     "    readout: S.VERSION + S.COMMON + S.READOUT_FRAGMENT,\n"
     "    state: S.VERSION + S.COMMON + S.STATE_FRAGMENT,\n"
     "    histogram: S.VERSION + S.HISTOGRAM_FRAGMENT,\n"
     "    lut: S.VERSION + S.LUT_FRAGMENT,\n"
     "    display: S.VERSION + S.DISPLAY_FRAGMENT,\n"
     "    blur: S.VERSION + S.BLUR_FRAGMENT,\n"
     "    shown: S.VERSION + S.SHOWN_FRAGMENT,\n"
     "    composite: S.VERSION + S.COMPOSITE_FRAGMENT,\n"
     "    resample: S.VERSION + S.RESAMPLE_FRAGMENT,\n"),
    ("demo/port.js", "programs[pass] = new Program(gl, AssembleVertex(pass), Assemble(pass), pass);"),
]

# What demo/port.js was translated from, by SHA-256: whole files, or one C++
# function from its signature to its closing brace. Not a proof of the port: a
# tripwire for its source. Run --hashes after carrying a change across.
PORTED_FROM = [
    ("source/Model.h", None, "72b149d9c7da8c4e085b05b4c8972a621d3504468564f8fbf5ac3134a1cc69e9"),
    ("source/Controls.h", None, "16df4400a74866709f3578029c393cfac9e4a840f938ab1d77fcfd7629cb009a"),
    ("source/Controls.cpp", None, "a9489fe8c13d530fa6256964f38b7ea6566bba06b1d75317526e36abece2de07"),
    ("source/Camera.h", None, "9d6172a42c97a612ef35c874f711b6a4042bab23ef3585054abb28db0be1d422"),
    ("source/Camera.cpp", None, "2c4c0c8168746073f4a30abff7cdf33c48402e8820946681b7633562b57b0189"),
    ("source/Palettes.cpp", "void Build( unsigned char out[ kPalettes ][ kEntries ][ 4 ] )", "f2fbf320237f09fd7f70bdeafb7d663fd8ecca663bef2e67ef6755446eda4be5"),
    ("source/Bolometer.cpp", "BolometerPlugin::BolometerPlugin()", "e16f5fed22b42632736aceaba6155b4a0e38c988df59ea0b9ca121bfb26f253b"),
    ("source/Bolometer.cpp", "CameraSettings BolometerPlugin::Settings() const", "293351095136ab639b853a430f4ba5561017165b805aa432c7f994e8c383388b"),
    ("source/Bolometer.cpp", "void BolometerPlugin::draw( PassBuffer& target )", "ef7bdb4abd28c9967d59551e57a0976842a20dae447aca069afc2ba003bbf87a"),
    ("source/Bolometer.cpp", "void BolometerPlugin::resample( PassBuffer& from, PassBuffer& to )", "a315ec9941e472e15b848dc7742afd1fb859194dc8ada9a1ac531c004eacebe7"),
    ("source/Bolometer.cpp", "bool BolometerPlugin::ensureBuffers( int width, int height )", "457aca90224522671028920f2390d9dee8460925f2aed3b9543a3f5b37660193"),
    ("source/Bolometer.cpp",
     "void BolometerPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, "
     "int width, int height )", "7ef66ebd35265983d550d2562ebf9b9f48939d467381d9a64240afe7bccfdf85"),
    # Read, and deliberately not ported (the page hands the plugin seconds, as
    # bmtest does): the page says so, and a change here may change what it says.
    ("source/Bolometer.cpp", "void BolometerPlugin::UpdateClock()", "068396f0efb5084127e1182ab21c2e7324e45035e649b172f60a7bddea2b8172"),
    ("source/Bolometer.cpp", "FFResult BolometerPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )", "613aba3267268d3f30c71c928a0648c0a5f6cd9eab17ef6e73884247b102a137"),
    ("source/Bolometer.cpp", "FFResult BolometerPlugin::InitGL( const FFGLViewportStruct* vp )", "bd7d4c4acbcc747cb9338f47127536d32a3ba8616e68e3c04b6968fc71081d9d"),
    ("source/Bolometer.cpp", "FFResult BolometerPlugin::SetFloatParameter( unsigned int index, float value )", "af2a389e1657c451d1d91311c922c9268971e8258331b1914e94d12d7120f932"),
]

# The pieces whose uniforms port.js must set (by name, quoted).
UNIFORM_PIECES = [symbol for _, symbol in SHADERS]

HEADER = """// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \\` for a backtick inside a comment.
"""

PALETTE_HEADER = """// GENERATED from source/Palettes.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a number of these tables, or a
// character of the notice below, differs from the plugin's. demo/port.js
// builds the four 256-entry palettes from them as Palettes.cpp's Build() does.
//
// The notice, as Palettes.cpp carries it:
//
"""


def read(*parts):
    with open(os.path.join(REPO, *parts), "r", encoding="utf-8") as handle:
        return handle.read()


def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them.

    Returns (text, count of raw strings), or (None, 0) if the symbol is not
    there or what follows its `=` is not a run of adjacent raw strings ending
    in `;`.
    """
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    """Every raw-string piece Shaders.cpp defines, by symbol."""
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def uniforms(glsl):
    """Every uniform a piece declares, as (type, name), comments stripped
    first. An array counts by its name."""
    code = re.sub(r"//[^\n]*", "", glsl)
    return re.findall(r"^\s*uniform\s+(\w+)\s+(\w+)\s*(?:\[\s*\d+\s*\])?\s*;", code, re.M)


INTEGER_TYPES = re.compile(r"^(int|uint|ivec[234]|uvec[234]|bool|bvec[234]|[iu]?sampler\w+)$")


# ---------------------------------------------------------------- palettes
def cpp_notice(source):
    """The licence block comment above the tables, verbatim."""
    match = re.search(r"/\*\n( \* The Rainbow table.*?)\n \*/", source, re.S)
    return None if match is None else match.group(1)


def cpp_table(source, symbol):
    match = re.search(r"const unsigned char " + symbol + r"\[ (\w+) \]\[ 3 \] = \{(.*?)\n\};", source, re.S)
    if match is None:
        return None
    return [tuple(int(v) for v in t) for t in re.findall(r"\{ (\d+), (\d+), (\d+) \}", match.group(2))]


def js_table(source, name):
    match = re.search(r"^export const " + name + r" = \[(.*?)\];$", source, re.S | re.M)
    if match is None:
        return None
    return [tuple(int(v) for v in t) for t in re.findall(r"\[(\d+), (\d+), (\d+)\]", match.group(1))]


def js_notice(source):
    lines = []
    for line in source.splitlines():
        if line.startswith("// |"):
            lines.append(line[4:] if len(line) > 4 else "")
    return "\n".join(lines) if lines else None


def write_palettes(cpp):
    notice = cpp_notice(cpp)
    if notice is None:
        print("FAIL  the licence notice was not found in source/Palettes.cpp")
        return 1
    out = [PALETTE_HEADER]
    out.extend(f"// |{line}\n" for line in notice.split("\n"))
    for name, symbol in (("RAINBOW", "kRainbow"), ("IRON", "kIron")):
        table = cpp_table(cpp, symbol)
        if not table:
            print(f"FAIL  {symbol} not found in source/Palettes.cpp")
            return 1
        rows = []
        for i in range(0, len(table), 8):
            rows.append("  " + ", ".join(f"[{r}, {g}, {b}]" for r, g, b in table[i:i + 8]) + ",")
        out.append(f"\n// {symbol}, source/Palettes.cpp: {len(table)} entries\n"
                   f"export const {name} = [\n" + "\n".join(rows) + "\n];\n")
    with open(os.path.join(REPO, "demo", "palettes.js"), "w", encoding="utf-8") as handle:
        handle.write("".join(out))
    print("wrote demo/palettes.js")
    return 0


def check_palettes(cpp):
    try:
        js = read("demo", "palettes.js")
    except FileNotFoundError:
        print("FAIL  demo/palettes.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = 0
    notice_cpp, notice_js = cpp_notice(cpp), js_notice(js)
    if notice_cpp is None or notice_js != notice_cpp:
        print("FAIL  the licence notice in demo/palettes.js is not Palettes.cpp's")
        problems += 1
    else:
        print(f"ok    {'palette notice':<20} matches Palettes.cpp's ({len(notice_cpp)} chars)")
    for name, symbol, want in (("RAINBOW", "kRainbow", 256), ("IRON", "kIron", 128)):
        a, b = cpp_table(cpp, symbol), js_table(js, name)
        if a is None or len(a) != want:
            print(f"FAIL  {symbol} in source/Palettes.cpp is not {want} entries")
            problems += 1
        elif a != b:
            bad = next((i for i in range(max(len(a), len(b or []))) if (a[i:i + 1] != (b or [])[i:i + 1])), None)
            print(f"FAIL  {name} in demo/palettes.js differs from {symbol} (first at entry {bad})")
            problems += 1
        else:
            print(f"ok    {name:<20} matches {symbol} ({len(a)} entries)")
    return problems


# ---------------------------------------------------------------- the port's source
def function_body(source, signature):
    """From `signature` to the brace that closes the body after it."""
    start = source.find(signature)
    if start < 0:
        return None
    brace = source.find("{", start + len(signature))
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    return None


def ported_hashes():
    out = []
    for path, signature, want in PORTED_FROM:
        try:
            text = read(*path.split("/"))
        except FileNotFoundError:
            out.append((path, signature, want, None))
            continue
        piece = text if signature is None else function_body(text, signature)
        got = None if piece is None else hashlib.sha256(piece.encode("utf-8")).hexdigest()
        out.append((path, signature, want, got))
    return out


def check_ported():
    problems = 0
    for path, signature, want, got in ported_hashes():
        what = path if signature is None else f"{path}: {signature.split('(')[0]}"
        if got is None:
            print(f"FAIL  ported from {what}: not found")
            problems += 1
        elif got != want:
            print(f"FAIL  ported from {what}: changed since demo/port.js was made from it "
                  f"(sha256 {got[:12]}, the port's {want[:12]}) -- carry the change across, then --hashes")
            problems += 1
        else:
            print(f"ok    ported from {what} ({got[:12]})")
    return problems


# ---------------------------------------------------------------- shaders.js
def write(cpp):
    out = [HEADER]
    version = cpp_version(cpp)
    if version is None:
        print("FAIL  kVersion not found in source/Shaders.cpp")
        return 1
    # kVersion is "#version 410 core\n" in the C++, the \n an escape there.
    out.append(f"export const VERSION = '{version}';\n")
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w", encoding="utf-8") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


def verbatim(entries, what):
    problems = 0
    for path, text in entries:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        if text in body:
            print(f"ok    {what}: {path} still says {text.strip().splitlines()[0][:60]}")
        else:
            print(f"FAIL  {what}: {path} no longer says: {text.strip().splitlines()[0]}")
            problems += 1
    return problems


def check(cpp, js):
    problems = 0
    version_cpp = cpp_version(cpp)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    problems += verbatim(ASSEMBLY, "assembly")
    problems += check_palettes(read("source", "Palettes.cpp"))
    problems += check_ported()

    try:
        port = read("demo", "port.js")
    except FileNotFoundError:
        port = ""

    for symbol in UNIFORM_PIECES:
        text, _ = from_cpp(cpp, symbol)
        if text is None:
            continue
        declared = uniforms(text)
        unset = [n for _, n in declared if f"'{n}'" not in port]
        floated = [n for t, n in declared
                   if INTEGER_TYPES.match(t) and re.search(r"\.set\(\s*'" + n + r"'", port)]
        if unset:
            print(f"FAIL  {symbol} declares {', '.join(unset)}, which demo/port.js never names -- "
                  "the page would leave it at 0")
            problems += 1
        elif floated:
            print(f"FAIL  {symbol}'s integer uniform(s) {', '.join(floated)} go through .set(), which uploads a "
                  "float that GL refuses -- use setInt, setUint, setSampler or gl.uniform2i")
            problems += 1
        elif declared:
            print(f"ok    uniforms: all {len(declared)} of {symbol}'s are named in demo/port.js, integers as integers")
    return problems


def main(argv):
    cpp = read("source", "Shaders.cpp")
    if "--write" in argv:
        return write(cpp) or write_palettes(read("source", "Palettes.cpp"))
    if "--hashes" in argv:
        for path, signature, _, got in ported_hashes():
            print(f"{got}  {path}{'' if signature is None else ': ' + signature}")
        return 0
    try:
        js = read("demo", "shaders.js")
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        return 1
    problems = check(cpp, js)
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted shader or "
              "palette copy (never edit shaders.js or palettes.js by hand); an assembly, ported-source, "
              "uniform or unlisted-piece failure needs a reader")
        return 1
    print(f"all {len(SHADERS) + 1} shader pieces, their assembly, the palettes, the ported C++ and the "
          "uniforms are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
