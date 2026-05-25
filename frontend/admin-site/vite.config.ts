import { defineConfig, loadEnv } from 'vite';
import tailwindcss from '@tailwindcss/vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const adminApiPort = env.SDK_ADMIN_HTTP_PORT || process.env.SDK_ADMIN_HTTP_PORT || '17080';
  const adminApiTarget =
    env.VITE_SDK_ADMIN_API_TARGET ||
    process.env.VITE_SDK_ADMIN_API_TARGET ||
    env.SDK_ADMIN_API_TARGET ||
    process.env.SDK_ADMIN_API_TARGET ||
    `http://127.0.0.1:${adminApiPort}`;

  return {
    plugins: [tailwindcss(), vue()],
    server: {
      host: '127.0.0.1',
      port: 17088,
      strictPort: true,
      proxy: {
        '/api': {
          target: adminApiTarget,
          changeOrigin: true,
        },
        '/healthz': {
          target: adminApiTarget,
          changeOrigin: true,
        },
      },
    },
  };
});
