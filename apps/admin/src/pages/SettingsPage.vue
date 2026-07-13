<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api.js'

const timezone = ref('')
const cacheTtlMinutes = ref(5)
const claudeCacheTtlMinutes = ref(5)
const saved = ref(false)
const error = ref<string | null>(null)

onMounted(async () => {
  const s = await api<{
    timezone: string
    cacheTtlMinutes: number
    claudeCacheTtlMinutes: number
  }>('/api/settings')
  timezone.value = s.timezone
  cacheTtlMinutes.value = s.cacheTtlMinutes
  claudeCacheTtlMinutes.value = s.claudeCacheTtlMinutes
})

async function save() {
  saved.value = false
  error.value = null
  try {
    await api('/api/settings', {
      method: 'PUT',
      body: JSON.stringify({
        timezone: timezone.value,
        cacheTtlMinutes: cacheTtlMinutes.value,
        claudeCacheTtlMinutes: claudeCacheTtlMinutes.value,
      }),
    })
    saved.value = true
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  }
}
</script>

<template>
  <div class="card bg-base-100 shadow-sm">
    <div class="card-body">
      <h2 class="card-title">Settings</h2>
      <fieldset class="fieldset">
        <label class="label" for="tz">Timezone (IANA)</label>
        <input id="tz" v-model="timezone" class="input w-full" placeholder="Asia/Bangkok" />
        <label class="label" for="ttl">Google cache TTL (minutes)</label>
        <input id="ttl" v-model.number="cacheTtlMinutes" type="number" min="1" class="input w-full" />
        <label class="label" for="claude-ttl">Claude usage cache TTL (minutes)</label>
        <input id="claude-ttl" v-model.number="claudeCacheTtlMinutes" type="number" min="1" class="input w-full" />
      </fieldset>
      <div class="card-actions items-center">
        <button class="btn btn-primary" @click="save">Save</button>
        <span v-if="saved" class="text-success">Saved — applies immediately.</span>
        <span v-if="error" class="text-error">{{ error }}</span>
      </div>
    </div>
  </div>
</template>
