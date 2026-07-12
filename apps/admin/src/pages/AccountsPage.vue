<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api.js'

interface AccountRow {
  email: string
  status: 'ok' | 'needs-reconnect'
  calendars: number | null
  error: string | null
}

const accounts = ref<AccountRow[]>([])
const loading = ref(true)

async function load() {
  loading.value = true
  try {
    accounts.value = await api<AccountRow[]>('/api/accounts')
  } finally {
    loading.value = false
  }
}

async function disconnect(email: string) {
  if (!confirm(`Disconnect ${email}?`)) return
  await api(`/api/accounts/${encodeURIComponent(email)}`, { method: 'DELETE' })
  await load()
}

onMounted(load)
</script>

<template>
  <div class="card bg-base-100 shadow-sm">
    <div class="card-body">
      <div class="flex items-center justify-between">
        <h2 class="card-title">Google accounts</h2>
        <a href="/api/oauth/start" class="btn btn-primary btn-sm">Connect account</a>
      </div>

      <div v-if="loading" class="py-8 text-center">
        <span class="loading loading-spinner"></span>
      </div>
      <p v-else-if="!accounts.length" class="text-base-content/60 py-4">
        No accounts connected yet. Click "Connect account" to add one.
      </p>
      <table v-else class="table">
        <thead>
          <tr><th>Email</th><th>Status</th><th>Calendars</th><th></th></tr>
        </thead>
        <tbody>
          <tr v-for="a in accounts" :key="a.email">
            <td>{{ a.email }}</td>
            <td>
              <span v-if="a.status === 'ok' && !a.error" class="badge badge-success">ok</span>
              <span v-else-if="a.status === 'needs-reconnect'" class="badge badge-error">needs reconnect</span>
              <span v-else class="badge badge-warning" :title="a.error ?? ''">error</span>
            </td>
            <td>{{ a.calendars ?? '—' }}</td>
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
</template>
