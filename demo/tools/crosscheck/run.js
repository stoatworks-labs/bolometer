// The page's half of the crosscheck: fetch the cases and the harness's card
// frames from crosscheck.py's server, run each case through a fresh instance
// (window.bolometerDemo.begin), frame by frame on the times and cues the job
// gives, and post every frame back, with what the camera said it did on each
// (read out or not, frozen or not, FFCs done), the page's table of controls,
// the page's own card for comparison with the harness's, and any uniform the
// page set that a program lacks or a program has that nothing set.
// crosscheck.py compares; nothing is judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.bolometerDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.bolometerDemo) throw new Error('window.bolometerDemo never appeared: the page did not start');
  const demo = window.bolometerDemo;
  const job = await (await fetch('/xc/job.json')).json();
  const sequences = {};
  const sequence = async (key) => {
    if (!sequences[key]) sequences[key] = new Uint8Array(await (await fetch(`/xc/frames/${key}`)).arrayBuffer());
    return sequences[key];
  };
  await post('/xc/table', JSON.stringify(demo.table));
  const errors = [];
  const refused = [];
  for (const c of job.cases) {
    const frames = await sequence(c.cards);
    const size = c.width * c.height * 4;
    const out = new Uint8Array(size * c.times.length);
    const log = [];
    const run = demo.begin({ settings: c.settings, width: c.width, height: c.height, input: job.input });
    try {
      // One task: the page's own frames cannot interleave with a case's.
      for (let n = 0; n < c.times.length; n += 1) {
        const index = c.indices[n];
        const card = frames.subarray(index * size, (index + 1) * size);
        const r = run.frame(card, c.times[n], c.changes[n] ?? {});
        out.set(r.pixels, n * size);
        log.push([r.readout ? 1 : 0, r.frozen ? 1 : 0, r.ffcs, r.index]);
        if (r.error) errors.push(`${c.name} frame ${index}: GL error ${r.error}`);
        if (!r.ok) refused.push(`${c.name} frame ${index}`);
      }
    } finally {
      run.end();
    }
    await post(`/xc/out/${c.name}`, out);
    await post(`/xc/log/${c.name}`, JSON.stringify(log));
  }
  for (const k of job.cards) {
    await post(`/xc/out/card-${k.name}`, demo.clip('card', k.width, k.height, k.time));
  }
  await post('/xc/missing', JSON.stringify({ ...demo.uniforms(), errors, refused }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
