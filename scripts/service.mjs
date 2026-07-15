#!/usr/bin/env node
// Manage the deskboard API as a per-user macOS LaunchAgent.
// Subcommands: install | uninstall | restart | status
// Invoked via the root `pnpm service:*` scripts.

import { execFileSync, execSync } from 'node:child_process'
import { mkdirSync, rmSync, writeFileSync, existsSync } from 'node:fs'
import { homedir } from 'node:os'
import path from 'node:path'

const LABEL = 'com.earthpyy.esp32-deskboard'

const nodeBin = process.execPath
const repoRoot = path.resolve(import.meta.dirname, '..')
const apiDir = path.join(repoRoot, 'apps', 'api')
const dataDir = path.join(apiDir, 'data')
const plistPath = path.join(homedir(), 'Library', 'LaunchAgents', `${LABEL}.plist`)
const uid = process.getuid()
const domainTarget = `gui/${uid}/${LABEL}`

function fail(msg) {
  console.error(`✗ ${msg}`)
  process.exit(1)
}

// Run a command, streaming output; throws on non-zero exit.
function run(cmd, args, opts = {}) {
  execFileSync(cmd, args, { stdio: 'inherit', ...opts })
}

// Run launchctl, swallowing benign "already loaded / not loaded" errors.
function launchctlSoft(args) {
  try {
    execFileSync('launchctl', args, { stdio: 'pipe' })
  } catch {
    /* idempotent: ignore */
  }
}

function build() {
  console.log('• Building (pnpm build)…')
  run('pnpm', ['build'], { cwd: repoRoot })
}

function xmlEscape(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function plistContents() {
  const pathEnv = `${path.dirname(nodeBin)}:/usr/local/bin:/usr/bin:/bin`
  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key>
	<string>${LABEL}</string>
	<key>ProgramArguments</key>
	<array>
		<string>${xmlEscape(nodeBin)}</string>
		<string>${xmlEscape(path.join(apiDir, 'dist', 'index.js'))}</string>
	</array>
	<key>WorkingDirectory</key>
	<string>${xmlEscape(apiDir)}</string>
	<key>EnvironmentVariables</key>
	<dict>
		<key>PATH</key>
		<string>${xmlEscape(pathEnv)}</string>
		<key>NODE_ENV</key>
		<string>production</string>
	</dict>
	<key>RunAtLoad</key>
	<true/>
	<key>KeepAlive</key>
	<true/>
	<key>StandardOutPath</key>
	<string>${xmlEscape(path.join(dataDir, 'service.log'))}</string>
	<key>StandardErrorPath</key>
	<string>${xmlEscape(path.join(dataDir, 'service.err.log'))}</string>
</dict>
</plist>
`
}

function install() {
  build()
  mkdirSync(dataDir, { recursive: true })
  mkdirSync(path.dirname(plistPath), { recursive: true })
  writeFileSync(plistPath, plistContents())
  console.log(`• Wrote ${plistPath}`)
  // Reload cleanly in case it was already installed.
  launchctlSoft(['unload', plistPath])
  run('launchctl', ['load', '-w', plistPath])
  console.log(`✓ Installed and started ${LABEL}`)
  console.log('  Check health:  curl localhost:3300/health')
  status()
}

function uninstall() {
  if (!existsSync(plistPath)) {
    console.log('• Not installed (no plist found); nothing to do.')
    return
  }
  launchctlSoft(['unload', plistPath])
  rmSync(plistPath, { force: true })
  console.log(`✓ Uninstalled ${LABEL}`)
}

function restart() {
  if (!existsSync(plistPath)) {
    fail(`Not installed. Run: pnpm service:install`)
  }
  build()
  run('launchctl', ['kickstart', '-k', domainTarget])
  console.log(`✓ Restarted ${LABEL}`)
  status()
}

function status() {
  if (!existsSync(plistPath)) {
    console.log(`• ${LABEL}: not installed`)
    return
  }
  let out
  try {
    out = execSync(`launchctl list ${LABEL}`, { encoding: 'utf8' })
  } catch {
    console.log(`• ${LABEL}: plist present but not loaded (run pnpm service:install)`)
    return
  }
  const pid = out.match(/"PID"\s*=\s*(\d+)/)?.[1]
  const exit = out.match(/"LastExitStatus"\s*=\s*(-?\d+)/)?.[1]
  if (pid) {
    console.log(`• ${LABEL}: running (PID ${pid})`)
  } else {
    console.log(`• ${LABEL}: loaded but not running (last exit status ${exit ?? '?'})`)
  }
}

const cmd = process.argv[2]
switch (cmd) {
  case 'install':
    install()
    break
  case 'uninstall':
    uninstall()
    break
  case 'restart':
    restart()
    break
  case 'status':
    status()
    break
  default:
    fail(`Unknown command "${cmd ?? ''}". Use: install | uninstall | restart | status`)
}
