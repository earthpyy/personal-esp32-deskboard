import tailwindcss from '@tailwindcss/vite'
import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vite'

// Keep in step with the API's own default + PORT override (apps/api/src/index.ts).
const apiTarget = `http://localhost:${Number(process.env.PORT) || 3300}`

export default defineConfig({
  base: '/admin/',
  plugins: [vue(), tailwindcss()],
  server: {
    proxy: {
      '/api': apiTarget,
      '/schedule': apiTarget,
    },
  },
})
