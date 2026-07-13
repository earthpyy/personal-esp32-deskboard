<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { api } from '../api.js'

interface ClaudeAccountConfig {
  id: string
  alias: string
  configDir: string
}

interface ClaudeLimit {
  percent: number
  severity: 'normal' | 'warning' | 'critical'
  resets_label: string
}

interface ClaudeUsageAccount {
  label: string
  available: boolean
  error?: string
  five_hour?: ClaudeLimit
  weekly?: ClaudeLimit
}

interface ClaudeUsage {
  now_label: string
  accounts: ClaudeUsageAccount[]
}

interface ClaudeSyncResult {
  syncedAt: number
  accounts: { label: string; ok: boolean; error?: string }[]
}

const accounts = ref<ClaudeAccountConfig[]>([])
const usage = ref<ClaudeUsage | null>(null)
const lastSync = ref<ClaudeSyncResult | null>(null)
const syncing = ref(false)
const syncError = ref<string | null>(null)
let timer: ReturnType<typeof setInterval> | undefined

const severityClass: Record<ClaudeLimit['severity'], string> = {
  normal: 'progress-success',
  warning: 'progress-warning',
  critical: 'progress-error',
}

// add/edit modal state
const modal = ref<HTMLDialogElement | null>(null)
const editingId = ref<string | null>(null)
const formAlias = ref('')
const formConfigDir = ref('')
const formError = ref<string | null>(null)
const saving = ref(false)

async function loadAccounts() {
  accounts.value = await api<ClaudeAccountConfig[]>('/api/claude/accounts')
}

async function loadUsage() {
  usage.value = await api<ClaudeUsage>('/api/claude').catch(() => null)
  lastSync.value = await api<ClaudeSyncResult | null>('/api/claude/sync').catch(() => null)
}

async function syncNow() {
  syncing.value = true
  syncError.value = null
  try {
    lastSync.value = await api<ClaudeSyncResult>('/api/claude/sync', { method: 'POST' })
    usage.value = await api<ClaudeUsage>('/api/claude')
  } catch (err) {
    syncError.value = err instanceof Error ? err.message : String(err)
  } finally {
    syncing.value = false
  }
}

function openAdd() {
  editingId.value = null
  formAlias.value = ''
  formConfigDir.value = ''
  formError.value = null
  modal.value?.showModal()
}

function openEdit(acc: ClaudeAccountConfig) {
  editingId.value = acc.id
  formAlias.value = acc.alias
  formConfigDir.value = acc.configDir
  formError.value = null
  modal.value?.showModal()
}

async function save() {
  saving.value = true
  formError.value = null
  const body = JSON.stringify({ alias: formAlias.value, configDir: formConfigDir.value })
  try {
    if (editingId.value)
      await api(`/api/claude/accounts/${editingId.value}`, { method: 'PATCH', body })
    else await api('/api/claude/accounts', { method: 'POST', body })
    modal.value?.close()
    await Promise.all([loadAccounts(), loadUsage()])
  } catch (err) {
    formError.value = err instanceof Error ? err.message : String(err)
  } finally {
    saving.value = false
  }
}

async function remove(acc: ClaudeAccountConfig) {
  if (!confirm(`Remove ${acc.alias}?`)) return
  await api(`/api/claude/accounts/${acc.id}`, { method: 'DELETE' })
  await Promise.all([loadAccounts(), loadUsage()])
}

onMounted(() => {
  loadAccounts()
  loadUsage()
  timer = setInterval(loadUsage, 30_000)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <div class="space-y-4">
    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <div class="flex items-center justify-between">
          <h2 class="card-title">Claude accounts</h2>
          <button class="btn btn-primary btn-sm" @click="openAdd">Add account</button>
        </div>
        <p v-if="!accounts.length" class="text-base-content/60 py-4">
          No accounts yet. Click "Add account" to add one.
        </p>
        <table v-else class="table">
          <thead>
            <tr><th>Alias</th><th>Config dir</th><th></th></tr>
          </thead>
          <tbody>
            <tr v-for="a in accounts" :key="a.id">
              <td class="font-medium">{{ a.alias }}</td>
              <td><code class="text-xs">{{ a.configDir || '~/.claude (default)' }}</code></td>
              <td class="text-right space-x-1">
                <button class="btn btn-ghost btn-xs" @click="openEdit(a)">Edit</button>
                <button class="btn btn-ghost btn-xs text-error" @click="remove(a)">Delete</button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <div class="flex items-center justify-between">
          <h2 class="card-title">
            Claude usage
            <span v-if="usage" class="text-sm font-normal opacity-60">as of {{ usage.now_label }}</span>
          </h2>
          <button class="btn btn-sm" :disabled="syncing" @click="syncNow">
            <span v-if="syncing" class="loading loading-spinner loading-sm"></span>
            Sync now
          </button>
        </div>

        <p v-if="!usage || !usage.accounts.length" class="text-base-content/60 py-4">
          No usage to show.
        </p>
        <div v-else class="space-y-4">
          <div v-for="acc in usage.accounts" :key="acc.label">
            <div class="flex items-center gap-2 mb-1">
              <span class="font-semibold">{{ acc.label }}</span>
              <span v-if="acc.available" class="badge badge-success badge-sm">ok</span>
              <span v-else class="badge badge-warning badge-sm">{{ acc.error ?? 'unavailable' }}</span>
            </div>
            <div v-if="acc.available" class="space-y-2">
              <div
                v-for="row in [
                  { name: '5h', limit: acc.five_hour },
                  { name: 'Week', limit: acc.weekly },
                ]"
                :key="row.name"
                class="flex items-center gap-3 text-sm"
              >
                <span class="w-10 opacity-70">{{ row.name }}</span>
                <progress
                  class="progress flex-1"
                  :class="row.limit ? severityClass[row.limit.severity] : ''"
                  :value="row.limit?.percent ?? 0"
                  max="100"
                />
                <span class="w-10 text-right tabular-nums">{{ row.limit?.percent ?? '--' }}%</span>
                <span class="w-24 text-right opacity-60">{{ row.limit?.resets_label ?? '' }}</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <h2 class="card-title">Claude sync</h2>
        <p class="text-base-content/60 text-sm">
          How the API server pulls usage limits from Claude (read from the macOS Keychain, cached per the
          Claude TTL in Settings).
        </p>
        <div v-if="syncError" class="alert alert-error">
          <span>Sync failed: {{ syncError }}</span>
        </div>
        <div class="text-sm">
          Last synced:
          <span class="font-medium">
            {{ lastSync?.syncedAt ? new Date(lastSync.syncedAt).toLocaleString() : '—' }}
          </span>
        </div>
        <ul v-if="lastSync?.accounts.length" class="text-sm space-y-1">
          <li v-for="acc in lastSync.accounts" :key="acc.label">
            {{ acc.label }}:
            <span :class="acc.ok ? 'text-success' : 'text-error'">
              {{ acc.ok ? 'ok' : acc.error }}
            </span>
          </li>
        </ul>
      </div>
    </div>

    <dialog ref="modal" class="modal">
      <div class="modal-box">
        <h3 class="text-lg font-bold">{{ editingId ? 'Edit account' : 'Add account' }}</h3>
        <fieldset class="fieldset mt-3">
          <label class="label" for="alias">Alias</label>
          <input id="alias" v-model="formAlias" class="input w-full" placeholder="Personal" />
          <label class="label" for="configDir">Config dir (CLAUDE_CONFIG_DIR)</label>
          <input id="configDir" v-model="formConfigDir" class="input w-full" placeholder="~/.claude" />
          <p class="label text-xs">Leave empty for the default (~/.claude) profile.</p>
        </fieldset>
        <div v-if="formError" class="alert alert-error mt-2">
          <span>{{ formError }}</span>
        </div>
        <div class="modal-action">
          <button class="btn" :disabled="saving" @click="modal?.close()">Cancel</button>
          <button class="btn btn-primary" :disabled="saving || !formAlias.trim()" @click="save">
            <span v-if="saving" class="loading loading-spinner loading-sm"></span>
            Save
          </button>
        </div>
      </div>
      <form method="dialog" class="modal-backdrop"><button>close</button></form>
    </dialog>
  </div>
</template>
