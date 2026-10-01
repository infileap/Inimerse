/**
 * dsh-inimerse — Host half of the Inimerse / Infiverse engine bridge.
 *
 * The plugin turns the Inimerse engine from "something a session shells out to
 * with ad-hoc bash" into a first-class harness capability: five declarative
 * tools over one shared, configurable runner.  Every tool returns structured
 * JSON, so a session can reason over build warnings, failing test names and
 * canonical verse state instead of scraping a terminal.
 *
 * Design rules held to on purpose:
 *   - No imports beyond Node builtins, no dependency, no build step: this is a
 *     Host-only bundle, so the loaded module must run as written.
 *   - No tool ever throws for an expected condition.  A missing binary, a
 *     failing build, a timed-out test run and a refused protocol op are all
 *     ordinary values with `ok: false` and a machine-readable `code`.  Thrown
 *     errors would hide the engine's own diagnostics from the model.
 *   - Nothing infers state it did not observe.  `inim_verse` reports the exact
 *     JSON lines the server produced, in order, and never synthesises a summary
 *     of a mutation it did not see acknowledged.
 *
 * @see ./README.md
 */

import { spawn } from 'node:child_process';
import { existsSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, isAbsolute } from 'node:path';

export const name = 'dsh-inimerse';
export const inject = ['tools'];

const PLUGIN = 'dsh-inimerse';

/** Where the engine checkout lives unless the config says otherwise. */
const DEFAULT_REPO_ROOT = '/home/sakiko/inimerse';
const DEFAULT_TIMEOUT_MS = 120_000;
const BUILD_TIMEOUT_MS = 900_000;
const TEST_TIMEOUT_MS = 600_000;
/** Cap on retained output lines per stream; the tail is what carries the error. */
const MAX_LINES = 400;

/* ------------------------------------------------------------------ */
/* config                                                              */
/* ------------------------------------------------------------------ */

/**
 * Resolve the row's config into a total record.
 *
 * The bundle declares no `Config` schema on purpose: a Host-only bundle that
 * imports nothing cannot validate a schema, and inventing a half-checked one
 * would be worse than reading defensively.  Every field therefore has a
 * default here, and a malformed field degrades to its default rather than
 * aborting activation.
 *
 * @param {unknown} config - the raw row config.
 * @returns {{repoRoot: string, buildDir: string, serverBin: string|null, jobs: number, timeoutMs: number, ctestLabels: boolean, env: Record<string,string>}} resolved settings.
 */
function resolveConfig(config) {
    const raw = config !== null && typeof config === 'object' ? config : {};
    const repoRoot = typeof raw.repoRoot === 'string' && raw.repoRoot.trim() !== ''
        ? resolve(raw.repoRoot)
        : DEFAULT_REPO_ROOT;
    const buildDir = typeof raw.buildDir === 'string' && raw.buildDir.trim() !== ''
        ? (isAbsolute(raw.buildDir) ? raw.buildDir : join(repoRoot, raw.buildDir))
        : join(repoRoot, 'build');
    const serverBin = typeof raw.serverBin === 'string' && raw.serverBin.trim() !== ''
        ? (isAbsolute(raw.serverBin) ? raw.serverBin : join(repoRoot, raw.serverBin))
        : join(buildDir, 'inim-server');
    const jobs = Number.isInteger(raw.jobs) && raw.jobs > 0 ? raw.jobs : 0;
    const timeoutMs = Number.isFinite(raw.timeoutMs) && raw.timeoutMs > 0 ? raw.timeoutMs : DEFAULT_TIMEOUT_MS;
    const env = raw.env !== null && typeof raw.env === 'object' ? { ...raw.env } : {};
    return { repoRoot, buildDir, serverBin, jobs, timeoutMs, ctestLabels: raw.ctestLabels !== false, env };
}

/** Effective job count: explicit config first, otherwise every core. */
function resolveJobs(configured, requested) {
    if (Number.isInteger(requested) && requested > 0) return requested;
    if (configured > 0) return configured;
    return 0; /* 0 tells the commands to use every core themselves */
}

/* ------------------------------------------------------------------ */
/* process plumbing                                                    */
/* ------------------------------------------------------------------ */

/** Keep the last `MAX_LINES` lines, reporting how many were dropped. */
function tailLines(text, max) {
    const lines = String(text ?? '').replace(/\r\n/g, '\n').split('\n');
    if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
    if (lines.length <= max) return { lines, dropped: 0 };
    return { lines: lines.slice(lines.length - max), dropped: lines.length - max };
}

/**
 * Run one child process to completion, honouring the tool call's abort signal.
 *
 * A cancelled tool call kills the child rather than leaking it: a build that
 * outlives its call would keep mutating the build directory while a later call
 * reads it.
 *
 * @param {string} command - executable.
 * @param {string[]} args - argv tail.
 * @param {{cwd: string, timeoutMs: number, signal?: AbortSignal, env?: Record<string,string>}} options - run settings.
 * @returns {Promise<{argv: string[], exitCode: number|null, signal: string|null, stdout: string, stderr: string, durationMs: number, timedOut: boolean, aborted: boolean}>} the finished run.
 */
function runCommand(command, args, options) {
    const { cwd, timeoutMs, signal, env } = options;
    const argv = [command, ...args];
    const startedAt = Date.now();

    if (signal !== undefined && signal.aborted) {
        return Promise.resolve({
            argv, exitCode: null, signal: null, stdout: '', stderr: '',
            durationMs: 0, timedOut: false, aborted: true,
        });
    }

    return new Promise((settle) => {
        let child;
        try {
            child = spawn(command, args, {
                cwd,
                env: { ...process.env, ...(env ?? {}) },
                stdio: ['ignore', 'pipe', 'pipe'],
                ...(signal === undefined ? {} : { signal }),
            });
        } catch (error) {
            settle({
                argv, exitCode: null, signal: null, stdout: '', stderr: String(error?.message ?? error),
                durationMs: Date.now() - startedAt, timedOut: false, aborted: false,
            });
            return;
        }

        let stdout = '';
        let stderr = '';
        let timedOut = false;
        const timer = setTimeout(() => {
            timedOut = true;
            child.kill('SIGKILL');
        }, timeoutMs);

        child.stdout.setEncoding('utf8');
        child.stderr.setEncoding('utf8');
        child.stdout.on('data', (chunk) => { stdout += chunk; });
        child.stderr.on('data', (chunk) => { stderr += chunk; });

        const finish = (exitCode, termSignal, aborted) => {
            clearTimeout(timer);
            settle({
                argv,
                exitCode,
                signal: termSignal,
                stdout,
                stderr,
                durationMs: Date.now() - startedAt,
                timedOut,
                aborted,
            });
        };

        child.on('error', (error) => {
            stderr += `${stderr === '' ? '' : '\n'}${String(error?.message ?? error)}`;
            finish(null, null, error?.name === 'AbortError');
        });
        child.on('close', (exitCode, termSignal) => finish(exitCode, termSignal, false));
    });
}

/**
 * Fold a finished run into the structured shape every tool returns.
 *
 * @param {Awaited<ReturnType<typeof runCommand>>} run - the finished run.
 * @param {string[]} [notes] - extra human-readable notes to carry through.
 * @returns {object} the public result.
 */
function runResult(run, notes = []) {
    const out = tailLines(run.stdout, MAX_LINES);
    const err = tailLines(run.stderr, MAX_LINES);
    return {
        argv: run.argv.join(' '),
        ok: run.exitCode === 0 && !run.timedOut && !run.aborted,
        exitCode: run.exitCode,
        signal: run.signal,
        timedOut: run.timedOut,
        aborted: run.aborted,
        durationMs: run.durationMs,
        stdout: out.lines.join('\n'),
        stderr: err.lines.join('\n'),
        stdoutLinesDropped: out.dropped,
        stderrLinesDropped: err.dropped,
        notes,
    };
}

/** Count compiler diagnostics in a build log. */
function countDiagnostics(text) {
    const warnings = (String(text).match(/warning:/g) ?? []).length;
    const errors = (String(text).match(/(?:error|Error):/g) ?? []).length;
    return { warnings, errors };
}

/* ------------------------------------------------------------------ */
/* tool plumbing                                                       */
/* ------------------------------------------------------------------ */

/** Text content block for a value; objects render as pretty JSON. */
function renderValue(value) {
    const text = typeof value === 'string' ? value : JSON.stringify(value, null, 2);
    return [{ type: 'text', text }];
}

/**
 * Build one registry-ready definition.
 *
 * The registry validates `output.schema` but takes `parameters` as written, so
 * both are declared here as plain JSON Schema — no `defineTool` import, and
 * therefore nothing for the bundle to depend on.
 *
 * @param {{name: string, description: string, parameters: object, output: object, execute: Function, isConcurrencySafe?: Function, presentCall?: Function}} spec - the tool.
 * @returns {object} the definition passed to `ctx.tools.register`.
 */
function tool(spec) {
    return {
        name: spec.name,
        description: spec.description,
        parameters: spec.parameters,
        output: { schema: spec.output, render: (_args, value) => renderValue(value) },
        execute: spec.execute,
        ...(spec.isConcurrencySafe === undefined ? {} : { isConcurrencySafe: spec.isConcurrencySafe }),
        ...(spec.presentCall === undefined ? {} : { presentCall: spec.presentCall }),
    };
}

/** Generic terminal affordance for the command-backed tools. */
function terminalCall(title, cwd) {
    return { card: 'terminal', title, cwd };
}

/** Generic affordance for the structured tools. */
function genericCall(title, kind = 'other') {
    return { card: 'generic', title, kind };
}

/* ------------------------------------------------------------------ */
/* the verse protocol client                                           */
/* ------------------------------------------------------------------ */

/**
 * Drive one `inim-server` child over its line protocol.
 *
 * The server speaks newline-delimited canonical JSON on stdin/stdout; this
 * client keeps the exact responses so the caller sees refusals verbatim rather
 * than a paraphrase.  `hello` is attempted first, but a refusal does not abort
 * the call: `drain` and `status` are deliberately reachable without a session
 * precisely so an operator can inspect and advance recovery, and the tool must
 * not take that away.
 *
 * @param {{serverBin: string, root: string, verseId: string, ops: object[], timeoutMs: number, signal?: AbortSignal, client: string}} request - the exchange.
 * @returns {Promise<object>} structured transcript.
 */
function verseExchange(request) {
    const { serverBin, root, verseId, ops, timeoutMs, signal, client } = request;

    if (!existsSync(serverBin)) {
        return Promise.resolve({
            ok: false,
            code: 'server_binary_missing',
            error: `inim-server not found at ${serverBin}; build the engine first (inim_build) or set config.serverBin`,
            requests: [],
        });
    }

    return new Promise((settle) => {
        const child = spawn(serverBin, [root, verseId], { stdio: ['pipe', 'pipe', 'pipe'] });
        const responses = [];
        const requests = [];
        const stderrChunks = [];
        let pending = [];
        let buffer = '';
        let settled = false;

        const timer = setTimeout(() => {
            child.kill('SIGKILL');
        }, timeoutMs);

        /** Resolve the oldest outstanding response with a parsed line. */
        const pushLine = (line) => {
            if (line.trim() === '') return;
            let parsed;
            try {
                parsed = JSON.parse(line);
            } catch {
                parsed = { ok: false, code: 'unparsable_response', raw: line };
            }
            responses.push(parsed);
            const waiter = pending.shift();
            if (waiter !== undefined) waiter(parsed);
        };

        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (chunk) => {
            buffer += chunk;
            for (;;) {
                const index = buffer.indexOf('\n');
                if (index < 0) break;
                const line = buffer.slice(0, index);
                buffer = buffer.slice(index + 1);
                pushLine(line);
            }
        });

        child.stderr.setEncoding('utf8');
        child.stderr.on('data', (chunk) => { stderrChunks.push(chunk); });

        /** Queue one request and wait for its line. */
        const send = (payload) => new Promise((next) => {
            pending.push(next);
            requests.push(payload);
            child.stdin.write(`${JSON.stringify(payload)}\n`);
        });

        const done = (extra) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            settle({
                ok: true,
                root,
                verseId,
                requests,
                responses,
                stderr: stderrChunks.join('').trim(),
                ...extra,
            });
        };

        child.on('error', (error) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            settle({
                ok: false,
                code: 'server_spawn_failed',
                error: String(error?.message ?? error),
                requests,
                responses,
            });
        });

        child.on('close', (exitCode) => done({
            exitCode,
            /* Exit 2 is the server's own signal that the closing anchor check
             * failed — surface it as a first-class flag, not a stray number. */
            anchorMismatch: exitCode === 2,
        }));

        if (signal !== undefined) {
            signal.addEventListener('abort', () => { child.kill('SIGKILL'); }, { once: true });
        }

        (async () => {
            await send({ op: 'hello', protocol: 1, client, caps: ['put', 'undo', 'drain', 'status'] });
            for (const op of ops) await send(op);
            await send({ op: 'bye' });
        })().catch((error) => {
            child.kill('SIGKILL');
            if (!settled) {
                settled = true;
                clearTimeout(timer);
                settle({ ok: false, code: 'exchange_failed', error: String(error?.message ?? error), requests, responses });
            }
        });
    });
}

/* ------------------------------------------------------------------ */
/* plugin                                                              */
/* ------------------------------------------------------------------ */

/**
 * Activate the bundle: register the five engine tools on this context.
 *
 * @param {object} ctx - the plugin context.
 * @param {unknown} config - the row's config.
 */
export function apply(ctx, config) {
    const cfg = resolveConfig(config);
    ctx.logger.info(`${PLUGIN}: repoRoot=${cfg.repoRoot} buildDir=${cfg.buildDir}`);

    /* ── 1. project status ─────────────────────────────────────────── */
    ctx.effect(() => ctx.tools.register(tool({
        name: 'inim_status',
        description:
            'Report the Inimerse checkout state in one structured shot: engine version, whether the build '
            + 'and the two P1 binaries exist, git branch/dirty/ahead-behind, and the CTest count declared in '
            + 'CMakeLists.txt. Read-only. Use it before building or testing to avoid acting on a stale tree.',
        parameters: {
            type: 'object',
            properties: {
                repo_root: { type: 'string', description: 'Override the configured checkout path.' },
            },
            additionalProperties: false,
        },
        output: {
            type: 'object',
            properties: {
                ok: { type: 'boolean' },
                repoRoot: { type: 'string' },
                engineVersion: { oneOf: [{ type: 'string' }, { type: 'null' }] },
                binaries: {
                    type: 'object',
                    properties: {
                        engine: { type: 'boolean' },
                        server: { type: 'boolean' },
                        client: { type: 'boolean' },
                    },
                },
                git: {
                    type: 'object',
                    properties: {
                        branch: { type: 'string' },
                        lastCommit: { type: 'string' },
                        dirty: { type: 'array', items: { type: 'string' } },
                        aheadBehind: { type: 'string' },
                    },
                },
                ctestDeclared: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                currentDocuments: { type: 'array', items: { type: 'string' } },
            },
            additionalProperties: true,
        },
        isConcurrencySafe: () => true,
        presentCall: () => genericCall('Inimerse project status', 'read'),
        async execute(args, exec) {
            const repoRoot = typeof args?.repo_root === 'string' && args.repo_root !== ''
                ? resolve(args.repo_root)
                : cfg.repoRoot;
            const result = { ok: false, repoRoot };

            if (!existsSync(repoRoot)) {
                return { ...result, code: 'repo_missing', error: `checkout not found at ${repoRoot}` };
            }

            const engine = join(repoRoot, 'inimerse');
            const built = join(cfg.buildDir, 'inimerse');
            const binaries = {
                engine: existsSync(engine) || existsSync(built),
                server: existsSync(join(cfg.buildDir, 'inim-server')),
                client: existsSync(join(cfg.buildDir, 'inim-client')),
            };

            const versionRun = existsSync(built)
                ? await runCommand(built, ['--version'], { cwd: repoRoot, timeoutMs: 20_000, signal: exec.signal })
                : null;
            const engineVersion = versionRun === null
                ? null
                : (versionRun.stdout.trim().split('\n').pop() || null);

            const gitRun = await runCommand('git', ['status', '--short', '--branch'], {
                cwd: repoRoot, timeoutMs: 20_000, signal: exec.signal,
            });
            const gitLines = gitRun.stdout.replace(/\r\n/g, '\n').split('\n').filter((l) => l !== '');
            const branchLine = gitLines.find((l) => l.startsWith('##')) ?? '';
            const logRun = await runCommand('git', ['log', '-1', '--format=%H %s'], {
                cwd: repoRoot, timeoutMs: 20_000, signal: exec.signal,
            });

            const cmake = join(repoRoot, 'CMakeLists.txt');
            let ctestDeclared = null;
            if (existsSync(cmake)) {
                const grepRun = await runCommand('grep', ['-c', 'add_test(', cmake], {
                    cwd: repoRoot, timeoutMs: 20_000, signal: exec.signal,
                });
                const parsed = Number.parseInt(grepRun.stdout.trim(), 10);
                ctestDeclared = Number.isNaN(parsed) ? null : parsed;
            }

            const docsDir = join(repoRoot, 'docs');
            const currentDocuments = existsSync(docsDir)
                ? (await runCommand('ls', [docsDir], { cwd: repoRoot, timeoutMs: 20_000, signal: exec.signal }))
                    .stdout.split('\n').filter((n) => n.endsWith('.md')).sort()
                : [];

            return {
                ok: true,
                repoRoot,
                engineVersion,
                binaries,
                git: {
                    branch: branchLine.replace(/^##\s*/, ''),
                    lastCommit: logRun.stdout.trim(),
                    dirty: gitLines.filter((l) => !l.startsWith('##')),
                    aheadBehind: /\[.*\]/.test(branchLine) ? branchLine : 'in sync',
                },
                ctestDeclared,
                currentDocuments,
            };
        },
    })));

    /* ── 2. build ──────────────────────────────────────────────────── */
    ctx.effect(() => ctx.tools.register(tool({
        name: 'inim_build',
        description:
            'Configure (unless skip_configure) and build the Inimerse engine with CMake, returning exit codes, '
            + 'warning/error counts and the tail of the log. Use clean to wipe the build directory first and '
            + 'reproduce a release gate from scratch. Not concurrency-safe: two builds in one directory fight.',
        parameters: {
            type: 'object',
            properties: {
                clean: { type: 'boolean', description: 'Remove the build directory before configuring.' },
                skip_configure: { type: 'boolean', description: 'Skip the cmake configure step.' },
                build_dir: { type: 'string', description: 'Override the configured build directory.' },
                jobs: { type: 'integer', description: 'Parallel job count; default = all cores.' },
                target: { type: 'string', description: 'Build only this CMake target (e.g. inim-server).' },
                build_type: { type: 'string', description: 'CMAKE_BUILD_TYPE; default Release.' },
            },
            additionalProperties: false,
        },
        output: {
            type: 'object',
            properties: {
                ok: { type: 'boolean' },
                code: { type: 'string' },
                error: { type: 'string' },
                configure: { oneOf: [{ type: 'object' }, { type: 'null' }] },
                build: { oneOf: [{ type: 'object' }, { type: 'null' }] },
                warnings: { type: 'integer' },
                errors: { type: 'integer' },
            },
            additionalProperties: true,
        },
        isConcurrencySafe: () => false,
        presentCall: (args) => terminalCall(args?.clean ? 'Clean rebuild of Inimerse' : 'Build Inimerse', cfg.repoRoot),
        async execute(args, exec) {
            if (!existsSync(join(cfg.repoRoot, 'CMakeLists.txt'))) {
                return { ok: false, code: 'repo_missing', error: `no CMakeLists.txt under ${cfg.repoRoot}` };
            }
            const buildDir = typeof args?.build_dir === 'string' && args.build_dir !== ''
                ? (isAbsolute(args.build_dir) ? args.build_dir : join(cfg.repoRoot, args.build_dir))
                : cfg.buildDir;
            const jobs = resolveJobs(cfg.jobs, args?.jobs);
            const buildType = typeof args?.build_type === 'string' && args.build_type !== '' ? args.build_type : 'Release';

            if (args?.clean === true) {
                await runCommand('rm', ['-rf', buildDir], { cwd: cfg.repoRoot, timeoutMs: 60_000, signal: exec.signal });
            }
            mkdirSync(buildDir, { recursive: true });

            let configure = null;
            if (args?.skip_configure !== true) {
                const configureRun = await runCommand('cmake', [
                    '-S', cfg.repoRoot, '-B', buildDir, `-DCMAKE_BUILD_TYPE=${buildType}`,
                ], { cwd: cfg.repoRoot, timeoutMs: cfg.timeoutMs, signal: exec.signal, env: cfg.env });
                configure = runResult(configureRun);
                if (!configure.ok) {
                    return {
                        ok: false,
                        code: configure.timedOut ? 'configure_timeout' : 'configure_failed',
                        error: 'CMake configure failed',
                        configure,
                        build: null,
                        warnings: countDiagnostics(`${configure.stdout}\n${configure.stderr}`).warnings,
                        errors: countDiagnostics(`${configure.stdout}\n${configure.stderr}`).errors,
                    };
                }
            }

            const buildArgs = ['--build', buildDir];
            if (jobs > 0) buildArgs.push('-j', String(jobs));
            else buildArgs.push('-j');
            if (typeof args?.target === 'string' && args.target !== '') buildArgs.push('--target', args.target);

            const buildRun = await runCommand('cmake', buildArgs, {
                cwd: cfg.repoRoot, timeoutMs: BUILD_TIMEOUT_MS, signal: exec.signal, env: cfg.env,
            });
            const build = runResult(buildRun);
            const diagnostics = countDiagnostics(`${build.stdout}\n${build.stderr}`);
            return {
                ok: build.ok,
                ...(build.ok ? {} : {
                    code: build.timedOut ? 'build_timeout' : 'build_failed',
                    error: 'the build did not succeed',
                }),
                buildDir,
                configure,
                build,
                warnings: diagnostics.warnings,
                errors: diagnostics.errors,
            };
        },
    })));

    /* ── 3. test ───────────────────────────────────────────────────── */
    ctx.effect(() => ctx.tools.register(tool({
        name: 'inim_test',
        description:
            'Run the CTest suite and return pass/fail totals plus the names of the failing tests. Supports a '
            + 'name regex and a label filter. Note that a test registered WILL_FAIL is expected to fail and is '
            + 'counted as passing by CTest — the plugin reports CTest\'s own verdict, it does not re-judge it.',
        parameters: {
            type: 'object',
            properties: {
                filter: { type: 'string', description: '--tests-regex value (e.g. verse_|ed25519).' },
                label: { type: 'string', description: '--label-regex value (e.g. known-defect).' },
                jobs: { type: 'integer', description: 'Parallel job count; default = all cores.' },
                build_dir: { type: 'string', description: 'Override the configured build directory.' },
                output_on_failure: { type: 'boolean', description: 'Attach failing-test output; default true.' },
            },
            additionalProperties: false,
        },
        output: {
            type: 'object',
            properties: {
                ok: { type: 'boolean' },
                code: { type: 'string' },
                error: { type: 'string' },
                total: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                failed: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                passed: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                failingTests: { type: 'array', items: { type: 'string' } },
                run: { oneOf: [{ type: 'object' }, { type: 'null' }] },
            },
            additionalProperties: true,
        },
        isConcurrencySafe: () => false,
        presentCall: () => terminalCall('Run Inimerse CTest suite', cfg.buildDir),
        async execute(args, exec) {
            const buildDir = typeof args?.build_dir === 'string' && args.build_dir !== ''
                ? (isAbsolute(args.build_dir) ? args.build_dir : join(cfg.repoRoot, args.build_dir))
                : cfg.buildDir;
            if (!existsSync(join(buildDir, 'CTestTestfile.cmake'))) {
                return {
                    ok: false,
                    code: 'not_configured',
                    error: `no CTest configuration in ${buildDir}; run inim_build first`,
                };
            }
            const jobs = resolveJobs(cfg.jobs, args?.jobs);
            const argv = ['--test-dir', buildDir, '-j', String(jobs > 0 ? jobs : 4)];
            if (typeof args?.filter === 'string' && args.filter !== '') argv.push('--tests-regex', args.filter);
            if (typeof args?.label === 'string' && args.label !== '') argv.push('--label-regex', args.label);
            if (args?.output_on_failure !== false) argv.push('--output-on-failure');

            const run = await runCommand('ctest', argv, {
                cwd: cfg.repoRoot, timeoutMs: TEST_TIMEOUT_MS, signal: exec.signal, env: cfg.env,
            });
            const log = `${run.stdout}\n${run.stderr}`;
            const summary = /tests passed,\s*(\d+) tests? failed out of\s*(\d+)/.exec(log);
            const failed = summary ? Number.parseInt(summary[1], 10) : null;
            const total = summary ? Number.parseInt(summary[2], 10) : null;
            const failingTests = [...log.matchAll(/^\s*\d+\/\d+\s+Test\s+#\d+:\s+(\S+)\s+.*\*\*\*Failed/gm)]
                .map((match) => match[1]);
            const result = runResult(run);

            if (summary === null) {
                return {
                    ok: false,
                    code: run.timedOut ? 'test_timeout' : 'test_run_failed',
                    error: 'ctest produced no summary line',
                    total: null, failed: null, passed: null,
                    failingTests,
                    run: result,
                };
            }
            const passed = total === null || failed === null ? null : total - failed;
            return {
                ok: failed === 0 && result.ok,
                ...(failed === 0 && result.ok ? {} : { error: `${failed} of ${total} tests failed` }),
                total, failed, passed,
                failingTests,
                run: result,
            };
        },
    })));

    /* ── 4. run a script ───────────────────────────────────────────── */
    ctx.effect(() => ctx.tools.register(tool({
        name: 'inim_run',
        description:
            'Run an Inimerse script (.im) through the engine interpreter and return stdout, stderr and the exit '
            + 'code. Pass script for a file in the checkout, or source for an inline snippet the plugin writes to '
            + 'a scratch file. The engine prints module-load notices on stdout before the program output.',
        parameters: {
            type: 'object',
            properties: {
                script: { type: 'string', description: 'Script path, absolute or relative to the checkout.' },
                source: { type: 'string', description: 'Inline Inimerse source; ignored when script is given.' },
                args: { type: 'array', items: { type: 'string' }, description: 'Extra argv after the script path.' },
                timeout_ms: { type: 'integer', description: 'Wall-clock limit; default 120000.' },
                extra_flags: {
                    type: 'array', items: { type: 'string' },
                    description: 'Engine flags inserted before the script path (e.g. ["--no-mods"]).',
                },
            },
            additionalProperties: false,
        },
        output: {
            type: 'object',
            properties: {
                ok: { type: 'boolean' },
                code: { type: 'string' },
                error: { type: 'string' },
                script: { type: 'string' },
                inline: { type: 'boolean' },
                exitCode: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                timedOut: { type: 'boolean' },
                stdout: { type: 'string' },
                stderr: { type: 'string' },
            },
            additionalProperties: true,
        },
        isConcurrencySafe: () => false,
        presentCall: (args) => terminalCall(
            args?.source !== undefined ? 'Run inline Inimerse source' : `Run ${args?.script ?? 'script'}`,
            cfg.repoRoot,
        ),
        async execute(args, exec) {
            if (typeof args?.script !== 'string' && typeof args?.source !== 'string') {
                return { ok: false, code: 'bad_request', error: 'pass either script or source' };
            }
            const engine = join(cfg.buildDir, 'inimerse');
            if (!existsSync(engine)) {
                return {
                    ok: false, code: 'engine_missing',
                    error: `engine not found at ${engine}; run inim_build first`,
                };
            }

            let scriptPath;
            let inline = false;
            if (typeof args?.script === 'string' && args.script !== '') {
                scriptPath = isAbsolute(args.script) ? args.script : join(cfg.repoRoot, args.script);
                if (!existsSync(scriptPath)) {
                    return { ok: false, code: 'script_missing', error: `no such script: ${scriptPath}`, script: scriptPath, inline: false };
                }
            } else {
                inline = true;
                scriptPath = join(cfg.buildDir, `dsh-inimerse-inline-${Date.now()}.im`);
                writeFileSync(scriptPath, String(args.source), 'utf8');
            }

            const flags = Array.isArray(args?.extra_flags) ? args.extra_flags.filter((f) => typeof f === 'string') : [];
            const extra = Array.isArray(args?.args) ? args.args.filter((a) => typeof a === 'string') : [];
            const timeoutMs = Number.isInteger(args?.timeout_ms) && args.timeout_ms > 0 ? args.timeout_ms : cfg.timeoutMs;

            const run = await runCommand(engine, [...flags, scriptPath, ...extra], {
                cwd: cfg.repoRoot, timeoutMs, signal: exec.signal, env: cfg.env,
            });
            const result = runResult(run);
            return {
                ...result,
                script: scriptPath,
                inline,
                ...(result.ok ? {} : { code: result.timedOut ? 'timeout' : 'nonzero_exit' }),
            };
        },
    })));

    /* ── 5. verse layer ────────────────────────────────────────────── */
    ctx.effect(() => ctx.tools.register(tool({
        name: 'inim_verse',
        description:
            'Drive a Verse layer through the real client/server boundary: the plugin starts inim-server on a '
            + 'root and verse id, performs a hello handshake, then applies the requested ops in order and closes '
            + 'with bye. Returns the verbatim protocol transcript, so refusals (no_session, capability_refused, '
            + 'client_authority, recovery_required) and server-assigned seq/rev are visible as the server stated '
            + 'them. The client must never supply seq, rev, head, balance, state_hash or committed — the server '
            + 'rejects such a request by design, so the tool never sends them.',
        parameters: {
            type: 'object',
            properties: {
                verse_id: { type: 'string', description: 'Verse id to open or create; default "default".' },
                root: { type: 'string', description: 'Layer root directory; default <buildDir>/dsh-verse.' },
                create: { type: 'boolean', description: 'Create the layer if missing (default true).' },
                ops: {
                    type: 'array',
                    description: 'Ops applied after hello, in order.',
                    items: {
                        type: 'object',
                        properties: {
                            op: { type: 'string', enum: ['put', 'undo', 'status', 'drain', 'bye'] },
                            key: { type: 'string', description: 'Idempotency key (put/undo).' },
                            cell: { type: 'string', description: 'Cell name (put).' },
                            value: { type: 'integer', description: 'Cell value (put).' },
                            target: { type: 'integer', description: 'Sequence number to undo back to (undo).' },
                        },
                        required: ['op'],
                        additionalProperties: false,
                    },
                },
                timeout_ms: { type: 'integer', description: 'Wall-clock limit for the whole exchange.' },
            },
            additionalProperties: false,
        },
        output: {
            type: 'object',
            properties: {
                ok: { type: 'boolean' },
                code: { type: 'string' },
                error: { type: 'string' },
                root: { type: 'string' },
                verseId: { type: 'string' },
                anchorMismatch: { type: 'boolean' },
                exitCode: { oneOf: [{ type: 'integer' }, { type: 'null' }] },
                stderr: { type: 'string' },
                hello: { oneOf: [{ type: 'object' }, { type: 'null' }] },
                responses: { type: 'array', items: { type: 'object', additionalProperties: true } },
            },
            additionalProperties: true,
        },
        isConcurrencySafe: () => false,
        presentCall: (args) => genericCall(`Verse ${args?.verse_id ?? 'default'}: ${Array.isArray(args?.ops) ? args.ops.map((o) => o?.op).join(' → ') : 'status'}`, 'other'),
        async execute(args, exec) {
            const verseId = typeof args?.verse_id === 'string' && args.verse_id !== '' ? args.verse_id : 'default';
            const root = typeof args?.root === 'string' && args.root !== ''
                ? (isAbsolute(args.root) ? args.root : join(cfg.repoRoot, args.root))
                : join(cfg.buildDir, 'dsh-verse');
            const timeoutMs = Number.isInteger(args?.timeout_ms) && args.timeout_ms > 0 ? args.timeout_ms : cfg.timeoutMs;
            const ops = Array.isArray(args?.ops)
                ? args.ops.filter((op) => op !== null && typeof op === 'object' && typeof op.op === 'string')
                : [{ op: 'status' }];

            mkdirSync(root, { recursive: true });
            if (args?.create === true) {
                const client = join(cfg.buildDir, 'inim-client');
                if (existsSync(client)) {
                    await runCommand(client, [root, verseId, 'create'], {
                        cwd: cfg.repoRoot, timeoutMs: 60_000, signal: exec.signal,
                    });
                }
            }

            const transcript = await verseExchange({
                serverBin: cfg.serverBin,
                root,
                verseId,
                ops,
                timeoutMs,
                signal: exec.signal,
                client: 'dsh-inimerse',
            });

            if (transcript.ok === false) return { ...transcript, root, verseId };

            const hello = transcript.responses.length > 0 ? transcript.responses[0] : null;
            const helloOk = hello !== null && hello.ok === true;
            return {
                ok: helloOk,
                ...(helloOk ? {} : {
                    code: hello?.code ?? 'hello_failed',
                    error: hello?.error ?? 'the server refused the session handshake',
                }),
                root,
                verseId,
                anchorMismatch: transcript.anchorMismatch === true,
                exitCode: transcript.exitCode ?? null,
                stderr: transcript.stderr,
                hello,
                ops,
                responses: transcript.responses,
            };
        },
    })));
}
