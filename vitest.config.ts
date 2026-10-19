import { defineConfig } from 'vitest/config';

/**
 * Unit tests for the pure TypeScript modules. They live next to the code as
 * `*.test.ts` under `src/` — Playwright's `testDir` is `tests/` and would pick
 * a `.test.ts` up from there. Browser behaviour (AudioWorklet, cross-origin
 * isolation, the real engine) stays in the Playwright specs.
 */
export default defineConfig({
  test: {
    include: ['src/**/*.test.ts'],
    environment: 'node',
    restoreMocks: true,
    unstubGlobals: true,
  },
});
