import { createRouter, createWebHashHistory } from 'vue-router'
import AccountsPage from './pages/AccountsPage.vue'
import BoardStatusPage from './pages/BoardStatusPage.vue'
import ClaudePage from './pages/ClaudePage.vue'
import SettingsPage from './pages/SettingsPage.vue'
import TodoistPage from './pages/TodoistPage.vue'

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', component: AccountsPage },
    { path: '/claude', component: ClaudePage },
    { path: '/todoist', component: TodoistPage },
    { path: '/settings', component: SettingsPage },
    { path: '/board', component: BoardStatusPage },
  ],
})
