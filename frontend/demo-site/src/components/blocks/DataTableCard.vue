<template>
  <div class="overflow-hidden rounded-[28px] border border-white/70 bg-white/90 shadow-[var(--demo-shadow)]">
    <div class="overflow-x-auto">
      <table class="min-w-full border-collapse">
        <thead class="bg-slate-50/90">
          <tr>
            <th
              v-for="column in columns"
              :key="column.key"
              class="px-4 py-3 text-left text-xs font-semibold uppercase tracking-[0.18em] text-slate-500"
              :class="column.align === 'right' ? 'text-right' : 'text-left'"
            >
              {{ column.label }}
            </th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="row in rows"
            :key="row.id"
            class="border-t border-slate-100 text-sm text-slate-700 transition-colors hover:bg-cyan-50/50"
          >
            <td
              v-for="column in columns"
              :key="column.key"
              class="px-4 py-3"
              :class="[
                column.align === 'right' ? 'text-right' : 'text-left',
                column.monospace ? 'font-mono text-[13px]' : '',
              ]"
            >
              {{ row.cells[column.key] }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { TableColumn, TableRow } from '../../types/demo';

defineProps<{
  columns: TableColumn[];
  rows: TableRow[];
}>();
</script>
