import { createRouter, createWebHashHistory } from 'vue-router'
import AccountsPage from './pages/AccountsPage.vue'
import BoardStatusPage from './pages/BoardStatusPage.vue'
import SettingsPage from './pages/SettingsPage.vue'

export const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', component: AccountsPage },
    { path: '/settings', component: SettingsPage },
    { path: '/board', component: BoardStatusPage },
  ],
})
