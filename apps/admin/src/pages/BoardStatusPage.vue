<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { api } from '../api.js'

interface BoardStatus {
  lastPollAt: number | null
  lastPollIp: string | null
}

const status = ref<BoardStatus | null>(null)
const nowTick = ref(Date.now())
let timer: ReturnType<typeof setInterval> | undefined

async function load() {
  status.value = await api<BoardStatus>('/api/board-status')
  nowTick.value = Date.now()
}

onMounted(() => {
  load()
  timer = setInterval(load, 10_000)
})
onUnmounted(() => clearInterval(timer))

const ageMin = computed(() => {
  if (!status.value?.lastPollAt) return null
  return (nowTick.value - status.value.lastPollAt) / 60_000
})

function plural(n: number, unit: string) {
  return `${n} ${unit}${n === 1 ? '' : 's'} ago`
}

const ageLabel = computed(() => {
  if (!status.value?.lastPollAt) return null
  const seconds = Math.max(0, Math.round((nowTick.value - status.value.lastPollAt) / 1000))
  if (seconds < 60) return plural(seconds, 'second')
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return plural(minutes, 'minute')
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return plural(hours, 'hour')
  return plural(Math.floor(hours / 24), 'day')
})

const freshness = computed(() => {
  if (ageMin.value === null) return { cls: 'badge-neutral', label: 'never seen' }
  if (ageMin.value < 3) return { cls: 'badge-success', label: 'online' }
  if (ageMin.value < 10) return { cls: 'badge-warning', label: 'quiet' }
  return { cls: 'badge-error', label: 'offline?' }
})
</script>

<template>
  <div class="card bg-base-100 shadow-sm">
    <div class="card-body">
      <h2 class="card-title">
        Board status
        <span class="badge" :class="freshness.cls">{{ freshness.label }}</span>
      </h2>
      <div class="stats stats-vertical sm:stats-horizontal">
        <div class="stat">
          <div class="stat-title">Last poll</div>
          <div class="stat-value text-lg">
            {{ status?.lastPollAt ? new Date(status.lastPollAt).toLocaleTimeString() : '—' }}
          </div>
          <div class="stat-desc" v-if="ageLabel">{{ ageLabel }}</div>
        </div>
        <div class="stat">
          <div class="stat-title">Board IP</div>
          <div class="stat-value text-lg">{{ status?.lastPollIp ?? '—' }}</div>
        </div>
      </div>
    </div>
  </div>
</template>
