import { defineConfig } from 'vite';
import tailwindcss from '@tailwindcss/vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [tailwindcss(), vue()],
  server: {
    host: '127.0.0.1',
    port: 17081,
    strictPort: true,
  },
});
