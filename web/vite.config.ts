import { defineConfig, Plugin } from 'vite';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function localSettings(): Map<string, string> {
  const settings = new Map<string, string>();
  const file = path.join(REPO_ROOT, '.dreams.local.env');
  if (!fs.existsSync(file)) return settings;
  for (const [index, raw] of fs.readFileSync(file, 'utf8').replace(/^﻿/, '').split(/\r?\n/).entries()) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const equals = line.indexOf('=');
    if (equals < 1) throw new Error(file + ':' + (index + 1) + ': expected NAME=VALUE');
    const name = line.slice(0, equals).trim();
    let value = line.slice(equals + 1).trim();
    if (!name) throw new Error(file + ':' + (index + 1) + ': environment name is empty');
    if (value.length >= 2 && ((value.startsWith('"') && value.endsWith('"')) ||
        (value.startsWith("'") && value.endsWith("'")))) {
      value = value.slice(1, -1);
    }
    settings.set(name, value);
  }
  return settings;
}

const CONTENT_TYPES: Record<string, string> = {
  '.json': 'application/json',
  '.gltf': 'model/gltf+json',
  '.bin': 'application/octet-stream',
  '.png': 'image/png',
  '.opus': 'audio/ogg',
  '.mp3': 'audio/mpeg',
  '.m4a': 'audio/mp4',
  '.mp4': 'video/mp4',
};

/**
 * Serves the baked data root at /data, as plain files - the way a static host
 * will. No routes, no queries: if something works here, it works deployed.
 * See docs/pipeline.md.
 */
function dataRootPlugin(): Plugin {
  return {
    name: 'dreams-data-root',
    configureServer(server) {
      const local = localSettings();
      const setting = (name: string) => process.env[name]?.trim() || local.get(name)?.trim();
      const work = setting('DREAMS_WORK_ROOT');
      const baked = setting('DREAMS_BAKED') || (work && path.join(work, 'baked'));
      if (!baked) {
        throw new Error('DREAMS_BAKED / DREAMS_WORK_ROOT is not configured; copy dev/paths.example.env to .dreams.local.env');
      }
      const root = path.resolve(baked);
      server.middlewares.use('/data', (req, res) => {
        const rel = decodeURIComponent((req.url || '/').split('?')[0]);
        const file = path.resolve(root, '.' + rel);
        if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
          res.statusCode = 404;
          res.end(`Not in the data root: ${rel} (run: uv run dreams bake)`);
          return;
        }
        const size = fs.statSync(file).size;
        res.setHeader('Content-Type', CONTENT_TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream');
        res.setHeader('Accept-Ranges', 'bytes');
        res.setHeader('Cache-Control', 'no-cache');
        const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || '');
        if (range) {
          const start = range[1] ? Number(range[1]) : Math.max(0, size - Number(range[2]));
          const end = range[1] && range[2] ? Math.min(Number(range[2]), size - 1) : size - 1;
          res.statusCode = 206;
          res.setHeader('Content-Range', `bytes ${start}-${end}/${size}`);
          res.setHeader('Content-Length', end - start + 1);
          fs.createReadStream(file, { start, end }).pipe(res);
          return;
        }
        res.setHeader('Content-Length', size);
        fs.createReadStream(file).pipe(res);
      });
    },
  };
}

export default defineConfig({
  // Relative asset URLs: a release works from any host or subfolder.
  base: './',
  // Game data never enters the app build; it comes from the data root.
  publicDir: false,
  plugins: [dataRootPlugin()],
  server: {
    port: 5173,
    open: false,
  },
});
