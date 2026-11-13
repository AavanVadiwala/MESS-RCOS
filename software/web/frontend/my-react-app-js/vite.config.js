import react, { reactCompilerPreset } from '@vitejs/plugin-react'
import babel from '@rolldown/plugin-babel'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    babel({ presets: [reactCompilerPreset()] })
  ],
  // In `npm run dev`, forward /api calls to the Express server (server.js)
  server: {
    proxy: {
      '/api': 'http://localhost:3000',
    },
  },
})
