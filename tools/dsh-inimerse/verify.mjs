/**
 * Standalone verification for the dsh-inimerse bundle.
 *
 * Loads the plugin outside DSH, captures every tool it registers, asserts each
 * output schema belongs to the registry's supported JSON Schema subset (the
 * exact check `ctx.tools.register` runs), then exercises the tools against the
 * real checkout and validates each returned value against its own schema.
 *
 * This exists because a schema the registry rejects aborts activation, and a
 * value the schema rejects turns a successful call into INVALID_TOOL_OUTPUT —
 * both are cheap to catch here and expensive to discover mid-session.
 *
 * Usage:
 *   node tools/dsh-inimerse/verify.mjs [--live]
 *
 * Without --live it runs only the schema and offline-status checks.
 */

import { readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const LIVE = process.argv.includes('--live');

/** The registry's own schema validator, loaded from the running dsh install. */
const TOOLS_MODULE = process.env.DSH_TOOLS_MODULE
    ?? '/home/sakiko/.nvm/versions/node/v24.20.0/lib/node_modules/@deepseek-ai/dsh/node_modules/@deepseek-ai/dsh-tools/lib/index.js';

const { assertSupportedJsonSchema, validateJsonSchemaValue } = await import(pathToFileURL(TOOLS_MODULE).href);

const failures = [];
const checks = [];

/** Record one assertion outcome. */
function check(name, condition, detail) {
    checks.push({ name, ok: Boolean(condition), detail });
    if (!condition) failures.push(`${name}${detail === undefined ? '' : `: ${detail}`}`);
}

/* ── load the plugin and capture its registrations ─────────────────── */
const mod = await import(pathToFileURL(join(HERE, 'index.js')).href);
check('exports apply()', typeof mod.apply === 'function');
check('declares name', mod.name === 'dsh-inimerse', String(mod.name));
check('injects tools', Array.isArray(mod.inject) && mod.inject.includes('tools'));

const registered = [];
const ctx = {
    logger: { info() {}, warn() {}, error() {} },
    effect(fn) {
        const disposer = fn();
        return () => { if (typeof disposer === 'function') disposer(); };
    },
    tools: {
        register(definition) {
            registered.push(definition);
            return () => {};
        },
    },
};

/* The checkout under test is the one this file lives in.  `cordis.patch.yml`
 * names an absolute path for the *installed* plugin, and reading it here was a
 * trap: run from a worktree or a fresh clone, this stage then exercised whatever
 * engine that path held -- and a stale engine there passes while the tree under
 * test is never touched.  The config is still read, only to report a mismatch. */
const CHECKOUT = resolve(HERE, '..', '..');
const configuredRoot = /repoRoot:\s*(\S+)/.exec(readFileSync(join(HERE, 'cordis.patch.yml'), 'utf8'))?.[1] ?? null;
const repoRoot = CHECKOUT;

mod.apply(ctx, { repoRoot });
if (configuredRoot !== null && resolve(configuredRoot) !== CHECKOUT) {
    console.log(`note: cordis.patch.yml names ${configuredRoot}, but this verifier lives in ${CHECKOUT}; verifying the checkout under test.`);
}
check('registers five tools', registered.length === 5, `got ${registered.length}`);

const expected = ['inim_build', 'inim_run', 'inim_status', 'inim_test', 'inim_verse'];
check(
    'registers the expected names',
    expected.every((n) => registered.some((d) => d.name === n)),
    registered.map((d) => d.name).join(', '),
);

/* ── every declared schema must pass the registry's own assertion ──── */
for (const definition of registered) {
    try {
        assertSupportedJsonSchema(definition.output.schema);
        check(`${definition.name}: output schema accepted`, true);
    } catch (error) {
        check(`${definition.name}: output schema accepted`, false, String(error.message));
    }
    check(`${definition.name}: declares a renderer`, typeof definition.output.render === 'function');
    check(`${definition.name}: declares execute()`, typeof definition.execute === 'function');
    const parameters = definition.parameters;
    check(
        `${definition.name}: parameters are an object root`,
        parameters !== null && typeof parameters === 'object' && parameters.type === 'object',
        JSON.stringify(parameters?.type),
    );
    try {
        assertSupportedJsonSchema(parameters);
        check(`${definition.name}: parameter schema accepted`, true);
    } catch (error) {
        check(`${definition.name}: parameter schema accepted`, false, String(error.message));
    }
}

/** Run one tool and validate both the schema and the value. */
async function call(name, args = {}) {
    const definition = registered.find((d) => d.name === name);
    const value = await definition.execute(args, { signal: new AbortController().signal });
    const violations = validateJsonSchemaValue(definition.output.schema, value, 'value');
    check(`${name}: value matches its schema`, violations.length === 0, violations.join('; '));
    return value;
}

/* ── offline checks ────────────────────────────────────────────────── */
const status = await call('inim_status', {});
check('inim_status: reports ok', status.ok === true, JSON.stringify(status).slice(0, 200));
check('inim_status: found the checkout', status.repoRoot === repoRoot, String(status.repoRoot));
check('inim_status: parsed a version', typeof status.engineVersion === 'string', String(status.engineVersion));
check('inim_status: detected the server binary', status.binaries?.server === true);
check('inim_status: parsed a CTest count', Number.isInteger(status.ctestDeclared), String(status.ctestDeclared));

const badRun = await call('inim_run', {});
check('inim_run: rejects a call with neither script nor source', badRun.ok === false && badRun.code === 'bad_request', JSON.stringify(badRun));

const missing = await call('inim_run', { script: 'definitely-not-here.im' });
check('inim_run: reports a missing script', missing.ok === false && missing.code === 'script_missing', JSON.stringify(missing));

const ensure = await call('inim_verse', { verse_id: 'verify', create: true, ops: [{ op: 'status' }] });
check('inim_verse: hello succeeded', ensure.ok === true, JSON.stringify(ensure).slice(0, 300));
check('inim_verse: status returned cells', Array.isArray(ensure.responses?.[1]?.cells), JSON.stringify(ensure.responses?.[1]));

/* ── live checks (opt-in: they build, test and mutate) ─────────────── */
if (LIVE) {
    const inline = await call('inim_run', { source: 'print("dsh-inimerse probe");\n' });
    /* Read `stdout` defensively: on an early return (`engine_missing`,
     * `bad_request`) the tool answers without it, and `undefined.includes` used
     * to crash the whole verifier -- which reports nothing at all, and so is
     * worse than the named failure the check above already records. */
    const inlineOut = typeof inline.stdout === 'string' ? inline.stdout : '';
    check('inim_run: executed inline source', inline.exitCode === 0, JSON.stringify(inline).slice(0, 300));
    check('inim_run: captured program output', inlineOut.includes('dsh-inimerse probe'),
        inlineOut === '' ? JSON.stringify(inline).slice(0, 300) : inlineOut);

    /* Start from a wiped layer: sequence numbers are state, so a leftover root
     * would make the very first `put` return a sequence other than 1. */
    const verseRoot = join(HERE, 'verify-root');
    rmSync(verseRoot, { recursive: true, force: true });
    const roundTrip = await call('inim_verse', {
        verse_id: 'loop',
        root: verseRoot,
        create: true,
        ops: [
            { op: 'put', key: 'k1', cell: 'alpha', value: 7 },
            { op: 'undo', key: 'k2', target: 1 },
            { op: 'status' },
        ],
    });
    const putResponse = roundTrip.responses?.[1];
    const undoResponse = roundTrip.responses?.[2];
    const statusResponse = roundTrip.responses?.[3];
    check('inim_verse: put acknowledged with a server sequence', putResponse?.ok === true && putResponse.seq === 1, JSON.stringify(putResponse));
    check('inim_verse: undo acknowledged', undoResponse?.ok === true, JSON.stringify(undoResponse));
    check('inim_verse: alpha reverted to its previous value', statusResponse?.cells?.some((c) => c.cell === 'alpha' && c.value === 0), JSON.stringify(statusResponse?.cells));

    const refused = await call('inim_verse', { verse_id: 'loop', root: verseRoot, create: true, ops: [{ op: 'put', key: 'k3', cell: 'beta', value: 1, seq: 99 }] });
    check('inim_verse: client-supplied authority is refused', refused.responses?.[1]?.code === 'client_authority', JSON.stringify(refused.responses?.[1]));

    const filtered = await call('inim_test', { filter: 'ed25519_probe' });
    check('inim_test: filtered run passed', filtered.ok === true, JSON.stringify(filtered).slice(0, 300));
    check('inim_test: parsed one test, zero failures', filtered.total === 1 && filtered.failed === 0, `total=${filtered.total} failed=${filtered.failed}`);

    /* A full rebuild is opt-in even within --live: it is minutes, not seconds. */
    if (process.argv.includes('--build')) {
        const built = await call('inim_build', { skip_configure: true });
        check('inim_build: incremental build succeeded', built.ok === true, JSON.stringify(built).slice(0, 300));
        check('inim_build: counted diagnostics', Number.isInteger(built.warnings) && Number.isInteger(built.errors), JSON.stringify({ warnings: built.warnings, errors: built.errors }));
    }
}

/* ── report ────────────────────────────────────────────────────────── */
rmSync(join(HERE, 'verify-root'), { recursive: true, force: true });
const passed = checks.filter((c) => c.ok).length;
for (const entry of checks) {
    if (!entry.ok) console.log(`FAIL  ${entry.name}${entry.detail === undefined ? '' : ` — ${entry.detail}`}`);
}
console.log(`\ndsh-inimerse verify: ${passed}/${checks.length} checks passed${LIVE ? ' (live)' : ''}`);
process.exit(failures.length === 0 ? 0 : 1);
