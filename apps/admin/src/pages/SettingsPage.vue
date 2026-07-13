<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { api } from '../api.js'

const timezone = ref('')
const cacheTtlMinutes = ref(5)
const claudeCacheTtlMinutes = ref(5)
const lunchEnabled = ref(true)
const lunchStart = ref('12:00')
const lunchEnd = ref('13:00')
const saved = ref(false)
const error = ref<string | null>(null)

onMounted(async () => {
  const s = await api<{
    timezone: string
    cacheTtlMinutes: number
    claudeCacheTtlMinutes: number
    lunchEnabled: boolean
    lunchStart: string
    lunchEnd: string
  }>('/api/settings')
  timezone.value = s.timezone
  cacheTtlMinutes.value = s.cacheTtlMinutes
  claudeCacheTtlMinutes.value = s.claudeCacheTtlMinutes
  lunchEnabled.value = s.lunchEnabled
  lunchStart.value = s.lunchStart
  lunchEnd.value = s.lunchEnd
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
        lunchEnabled: lunchEnabled.value,
        lunchStart: lunchStart.value,
        lunchEnd: lunchEnd.value,
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
        <label class="label mt-2" for="lunch-enabled">
          <input id="lunch-enabled" v-model="lunchEnabled" type="checkbox" class="checkbox checkbox-sm" />
          Show lunch break divider (when free)
        </label>
        <div class="flex gap-3">
          <div class="flex-1">
            <label class="label" for="lunch-start">Lunch start</label>
            <input id="lunch-start" v-model="lunchStart" type="time" :disabled="!lunchEnabled" class="input w-full" />
          </div>
          <div class="flex-1">
            <label class="label" for="lunch-end">Lunch end</label>
            <input id="lunch-end" v-model="lunchEnd" type="time" :disabled="!lunchEnabled" class="input w-full" />
          </div>
        </div>
      </fieldset>
      <div class="card-actions items-center">
        <button class="btn btn-primary" @click="save">Save</button>
        <span v-if="saved" class="text-success">Saved — applies immediately.</span>
        <span v-if="error" class="text-error">{{ error }}</span>
      </div>
    </div>
  </div>
</template>
