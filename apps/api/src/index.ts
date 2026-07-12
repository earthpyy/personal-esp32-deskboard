import { node } from '@elysiajs/node'
import { Elysia } from 'elysia'

new Elysia({ adapter: node() })
  .get('/health', () => ({ status: 'ok' }))
  .listen(3000, ({ hostname, port }) => {
    console.log(`API running at http://${hostname}:${port}`)
  })
