<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api, type SyncResult } from '../api.js'

interface AccountRow {
  email: string
  alias: string | null
  status: 'ok' | 'needs-reconnect'
  calendars: number | null
  calendarList: { name: string; color: string }[] | null
  error: string | null
}

const accounts = ref<AccountRow[]>([])
const loading = ref(true)

const syncing = ref(false)
const lastSync = ref<SyncResult | null>(null)
const syncError = ref<string | null>(null)

const calendarModal = ref<HTMLDialogElement | null>(null)
const modalAccount = ref<AccountRow | null>(null)

async function load() {
  loading.value = true
  try {
    accounts.value = await api<AccountRow[]>('/api/accounts')
  } finally {
    loading.value = false
  }
}

async function syncNow() {
  syncing.value = true
  syncError.value = null
  try {
    lastSync.value = await api<SyncResult>('/api/sync', { method: 'POST' })
    await load()
  } catch (err) {
    syncError.value = err instanceof Error ? err.message : String(err)
  } finally {
    syncing.value = false
  }
}

async function saveAlias(a: AccountRow) {
  const alias = (a.alias ?? '').trim()
  await api(`/api/accounts/${encodeURIComponent(a.email)}`, {
    method: 'PATCH',
    body: JSON.stringify({ alias }),
  })
  a.alias = alias || null
}

async function disconnect(email: string) {
  if (!confirm(`Disconnect ${email}?`)) return
  await api(`/api/accounts/${encodeURIComponent(email)}`, { method: 'DELETE' })
  await load()
}

function showCalendars(a: AccountRow) {
  modalAccount.value = a
  calendarModal.value?.showModal()
}

onMounted(async () => {
  await load()
  // null/empty when the server has never synced yet — don't let that break the page
  try {
    lastSync.value = await api<SyncResult | null>('/api/sync')
  } catch {
    lastSync.value = null
  }
})
</script>

<template>
  <div class="space-y-4">
    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <div class="flex items-center justify-between">
          <h2 class="card-title">Google accounts</h2>
          <div class="flex gap-2">
            <button class="btn btn-sm" :disabled="syncing" @click="syncNow">
              <span v-if="syncing" class="loading loading-spinner loading-sm"></span>
              Sync now
            </button>
            <a href="/api/oauth/start" class="btn btn-primary btn-sm">Connect account</a>
          </div>
        </div>

        <div v-if="loading" class="py-8 text-center">
          <span class="loading loading-spinner"></span>
        </div>
        <p v-else-if="!accounts.length" class="text-base-content/60 py-4">
          No accounts connected yet. Click "Connect account" to add one.
        </p>
        <table v-else class="table">
          <thead>
            <tr><th>Email</th><th>Alias</th><th>Status</th><th>Calendars</th><th></th></tr>
          </thead>
          <tbody>
            <tr v-for="a in accounts" :key="a.email">
              <td>{{ a.email }}</td>
              <td>
                <input
                  v-model="a.alias"
                  class="input input-sm w-40"
                  :placeholder="a.email"
                  @blur="saveAlias(a)"
                  @keyup.enter="saveAlias(a)"
                />
              </td>
              <td>
                <span v-if="a.status === 'ok' && !a.error" class="badge badge-success">ok</span>
                <span v-else-if="a.status === 'needs-reconnect'" class="badge badge-error">needs reconnect</span>
                <span v-else class="badge badge-warning" :title="a.error ?? ''">error</span>
              </td>
              <td>
                <button
                  v-if="a.calendars"
                  class="link link-primary"
                  @click="showCalendars(a)"
                >
                  {{ a.calendars }}
                </button>
                <span v-else>—</span>
              </td>
              <td class="text-right">
                <button class="btn btn-ghost btn-xs text-error" @click="disconnect(a.email)">
                  Disconnect
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <h2 class="card-title">Google sync</h2>
        <p class="text-base-content/60 text-sm">How the API server pulls events from Google.</p>
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
          <li v-for="acc in lastSync.accounts" :key="acc.email">
            {{ acc.email }}:
            <span :class="acc.ok ? 'text-success' : 'text-error'">
              {{ acc.ok ? 'ok' : acc.error }}
            </span>
          </li>
        </ul>
      </div>
    </div>

    <dialog ref="calendarModal" class="modal">
      <div class="modal-box">
        <h3 class="text-lg font-bold">Synced calendars</h3>
        <p class="text-base-content/60 text-sm">{{ modalAccount?.email }}</p>
        <ul v-if="modalAccount?.calendarList?.length" class="mt-3 space-y-1">
          <li v-for="c in modalAccount.calendarList" :key="c.name" class="flex items-center gap-2">
            <span
              class="inline-block h-3 w-3 shrink-0 rounded-full"
              :style="{ backgroundColor: c.color }"
            ></span>{{ c.name }}
          </li>
        </ul>
        <p v-else class="text-base-content/60 mt-3">No calendar details available yet — try Sync now.</p>
        <div class="modal-action">
          <form method="dialog"><button class="btn">Close</button></form>
        </div>
      </div>
      <form method="dialog" class="modal-backdrop"><button>close</button></form>
    </dialog>
  </div>
</template>
