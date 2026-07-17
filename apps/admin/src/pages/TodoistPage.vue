<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { api } from '../api.js'

interface TodoistConfig {
  hasToken: boolean
  activeFilterId: string
}

interface TodoistFilter {
  id: string
  name: string
  query: string
}

interface BoardTask {
  id: string
  content: string
  priority: number
  project: string
  labels: string
  due_label: string
  overdue: boolean
}

interface TodoistBoard {
  now_label: string
  configured: boolean
  filter_name: string
  error: string
  tasks: BoardTask[]
}

interface TodoistSyncResult {
  syncedAt: number
  ok: boolean
  error?: string
  filterName?: string
  taskCount?: number
}

const config = ref<TodoistConfig>({ hasToken: false, activeFilterId: '' })
const filters = ref<TodoistFilter[]>([])
const board = ref<TodoistBoard | null>(null)
const lastSync = ref<TodoistSyncResult | null>(null)

const savingFilter = ref(false)
const loadingFilters = ref(false)
const syncing = ref(false)
const error = ref<string | null>(null)
let timer: ReturnType<typeof setInterval> | undefined

// Todoist priority is 1..4 (4 = p1); map to a daisyUI badge colour.
const priorityClass: Record<number, string> = {
  4: 'badge-error',
  3: 'badge-warning',
  2: 'badge-info',
  1: 'badge-ghost',
}

const noFilters = computed(() => config.value.hasToken && filters.value.length === 0)

async function loadConfig() {
  config.value = await api<TodoistConfig>('/api/todoist/config')
}

async function loadFilters() {
  if (!config.value.hasToken) {
    filters.value = []
    return
  }
  loadingFilters.value = true
  try {
    filters.value = await api<TodoistFilter[]>('/api/todoist/filters')
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    loadingFilters.value = false
  }
}

async function loadBoard() {
  board.value = await api<TodoistBoard>('/api/todoist').catch(() => null)
  lastSync.value = await api<TodoistSyncResult | null>('/api/todoist/sync').catch(() => null)
}

async function selectFilter(id: string) {
  savingFilter.value = true
  error.value = null
  try {
    await api('/api/todoist/config', {
      method: 'PUT',
      body: JSON.stringify({ activeFilterId: id }),
    })
    config.value.activeFilterId = id
    await loadBoard()
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    savingFilter.value = false
  }
}

async function syncNow() {
  syncing.value = true
  error.value = null
  try {
    lastSync.value = await api<TodoistSyncResult>('/api/todoist/sync', { method: 'POST' })
    await loadBoard()
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  } finally {
    syncing.value = false
  }
}

onMounted(async () => {
  await loadConfig()
  await Promise.all([loadFilters(), loadBoard()])
  timer = setInterval(loadBoard, 30_000)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <div class="space-y-4">
    <div v-if="error" class="alert alert-error">
      <span>{{ error }}</span>
    </div>

    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <div class="flex items-center justify-between">
          <h2 class="card-title">Filter</h2>
          <button class="btn btn-ghost btn-sm" :disabled="!config.hasToken || loadingFilters" @click="loadFilters">
            <span v-if="loadingFilters" class="loading loading-spinner loading-sm"></span>
            Refresh
          </button>
        </div>
        <p class="text-base-content/60 text-sm">
          The board shows one saved filter's tasks. Renaming or editing the filter in Todoist is
          picked up automatically.
        </p>
        <div v-if="!config.hasToken" class="alert alert-warning">
          <span>Set <code>TODOIST_TOKEN</code> in <code>apps/api/.env</code> and restart the API to enable this.</span>
        </div>
        <p v-else-if="noFilters" class="text-base-content/60 py-2">
          No saved filters found on this account.
        </p>
        <select
          v-else
          class="select w-full"
          :value="config.activeFilterId"
          :disabled="savingFilter"
          @change="selectFilter(($event.target as HTMLSelectElement).value)"
        >
          <option value="" disabled>Pick a filter…</option>
          <option v-for="f in filters" :key="f.id" :value="f.id">
            {{ f.name }} — {{ f.query }}
          </option>
        </select>
      </div>
    </div>

    <div class="card bg-base-100 shadow-sm">
      <div class="card-body">
        <div class="flex items-center justify-between">
          <h2 class="card-title">
            Tasks
            <span v-if="board?.filter_name" class="text-sm font-normal opacity-60">
              {{ board.filter_name }}
            </span>
          </h2>
          <button class="btn btn-sm" :disabled="syncing || !config.hasToken" @click="syncNow">
            <span v-if="syncing" class="loading loading-spinner loading-sm"></span>
            Sync now
          </button>
        </div>

        <div v-if="lastSync && !lastSync.ok" class="alert alert-warning">
          <span>Last sync failed: {{ lastSync.error }}</span>
        </div>
        <div class="text-sm opacity-60">
          Last synced:
          <span class="font-medium">
            {{ lastSync?.syncedAt ? new Date(lastSync.syncedAt).toLocaleString() : '—' }}
          </span>
        </div>

        <p v-if="!board || !board.configured" class="text-base-content/60 py-2">
          Not configured yet.
        </p>
        <p v-else-if="!board.tasks.length" class="text-base-content/60 py-2">
          No tasks in this filter.
        </p>
        <ul v-else class="divide-y divide-base-200">
          <li v-for="task in board.tasks" :key="task.id" class="flex items-center gap-3 py-2">
            <span class="badge badge-sm" :class="priorityClass[task.priority] ?? 'badge-ghost'">
              p{{ 5 - task.priority }}
            </span>
            <span class="flex-1">{{ task.content }}</span>
            <span v-if="task.project || task.labels" class="text-xs opacity-50">
              {{ [task.project, task.labels].filter(Boolean).join(' ') }}
            </span>
            <span
              v-if="task.due_label"
              class="text-xs tabular-nums"
              :class="task.overdue ? 'text-error' : 'opacity-60'"
            >
              {{ task.due_label }}
            </span>
          </li>
        </ul>
      </div>
    </div>
  </div>
</template>
