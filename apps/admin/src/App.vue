<script setup lang="ts">
import { ref } from 'vue'
import { api, type SyncResult } from './api.js'

const syncing = ref(false)
const syncResult = ref<SyncResult | null>(null)
const syncError = ref<string | null>(null)

async function syncNow() {
  syncing.value = true
  syncError.value = null
  try {
    syncResult.value = await api<SyncResult>('/api/sync', { method: 'POST' })
  } catch (err) {
    syncError.value = err instanceof Error ? err.message : String(err)
  } finally {
    syncing.value = false
  }
}
</script>

<template>
  <div class="min-h-screen bg-base-200">
    <div class="navbar bg-base-100 shadow-sm">
      <div class="navbar-start">
        <span class="text-xl font-bold px-2">Deskboard</span>
      </div>
      <div class="navbar-center">
        <div role="tablist" class="tabs tabs-box">
          <router-link to="/" class="tab" exact-active-class="tab-active">Accounts</router-link>
          <router-link to="/settings" class="tab" exact-active-class="tab-active">Settings</router-link>
          <router-link to="/board" class="tab" exact-active-class="tab-active">Board</router-link>
        </div>
      </div>
      <div class="navbar-end">
        <button class="btn btn-primary" :disabled="syncing" @click="syncNow">
          <span v-if="syncing" class="loading loading-spinner loading-sm"></span>
          Sync now
        </button>
      </div>
    </div>

    <div class="max-w-3xl mx-auto p-6 space-y-4">
      <div v-if="syncError" class="alert alert-error">
        <span>Sync failed: {{ syncError }}</span>
      </div>
      <div v-else-if="syncResult" class="alert alert-success">
        <span>
          Synced {{ new Date(syncResult.syncedAt).toLocaleTimeString() }} —
          <template v-for="a in syncResult.accounts" :key="a.email">
            <span class="mr-2">{{ a.email }}: {{ a.ok ? 'ok' : a.error }}</span>
          </template>
          <span v-if="!syncResult.accounts.length">no accounts connected</span>
        </span>
      </div>
      <router-view />
    </div>
  </div>
</template>
