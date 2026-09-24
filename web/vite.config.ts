import { defineConfig, Plugin } from 'vite';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function localSettings(): Map<string, string> {
  const settings = new Map<string, string>();
  const file = path.join(REPO_ROOT, '.dreams.local.env');
  if (!fs.existsSync(file)) return settings;
  for (const [index, raw] of fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '').split(/\r?\n/).entries()) {
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

function dreamsAssetPlugin(): Plugin {
  return {
    name: 'dreams-asset-server',
    configureServer(server) {
      const local = localSettings();
      const setting = (name: string) => process.env[name]?.trim() || local.get(name)?.trim();
      const configuredWork = setting('DREAMS_WORK_ROOT');
      if (!configuredWork) {
        throw new Error('DREAMS_WORK_ROOT is not configured; copy dev/paths.example.env to .dreams.local.env');
      }
      const DREAMS_WORK = path.resolve(configuredWork);
      const GLTF_DIR = path.join(DREAMS_WORK, 'gltf');
      const MODELS_DIR = path.join(DREAMS_WORK, 'models');
      const ANIMATIONS_DIR = path.join(DREAMS_WORK, 'animations');
      const BAKED_DIR = path.resolve(setting('DREAMS_BAKED') || path.join(DREAMS_WORK, 'baked'));
      server.middlewares.use((req, res, next) => {
        const url = req.url || '';

        // API: List available scenes
        if (url === '/api/scenes') {
          try {
            if (!fs.existsSync(GLTF_DIR)) {
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify([]));
              return;
            }
            const files = fs.readdirSync(GLTF_DIR);
            const scenes = files
              .filter(f => f.endsWith('.gltf'))
              .map(f => {
                const base = f.replace('.gltf', '');
                let hasTex = false;
                try {
                  const content = fs.readFileSync(path.join(GLTF_DIR, f), 'utf8');
                  hasTex = content.includes('"textures"');
                } catch {}
                return {
                  id: base,
                  filename: f,
                  hasTextures: hasTex,
                };
              })
              .sort((a, b) => (b.hasTextures ? 1 : 0) - (a.hasTextures ? 1 : 0) || a.id.localeCompare(b.id));

            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify(scenes));
            return;
          } catch (err) {
            res.statusCode = 500;
            res.end(JSON.stringify({ error: String(err) }));
            return;
          }
        }

        // API: List available 3D character/object models
        if (url === '/api/models') {
          try {
            if (!fs.existsSync(MODELS_DIR)) {
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify([]));
              return;
            }
            const files = fs.readdirSync(MODELS_DIR);
            const models = files
              .filter(f => f.endsWith('.gltf'))
              .map(f => {
                const base = f.replace('.gltf', '');
                let hasTex = false;
                try {
                  const content = fs.readFileSync(path.join(MODELS_DIR, f), 'utf8');
                  hasTex = content.includes('"textures"');
                } catch {}
                return {
                  id: base,
                  filename: f,
                  hasTextures: hasTex,
                };
              })
              .sort((a, b) => (b.hasTextures ? 1 : 0) - (a.hasTextures ? 1 : 0) || a.id.localeCompare(b.id));

            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify(models));
            return;
          } catch (err) {
            res.statusCode = 500;
            res.end(JSON.stringify({ error: String(err) }));
            return;
          }
        }

        // API: Get project metadata (NPCs, spawners, links, waypoints) by scene stem
        if (url.startsWith('/api/project/scene/')) {
          const sceneStem = url.replace('/api/project/scene/', '').split('?')[0].toLowerCase();
          try {
            const projectsFile = path.join(DREAMS_WORK, 'projects.json');
            if (fs.existsSync(projectsFile)) {
              const allProjects = JSON.parse(fs.readFileSync(projectsFile, 'utf8'));
              const match = Object.values(allProjects).find((p: any) => p.sceneStem === sceneStem);
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify(match || null));
              return;
            }
          } catch (err) {
            res.statusCode = 500;
            res.end(JSON.stringify({ error: String(err) }));
            return;
          }
        }

        // Shared animation library catalog. Static props may have no entry.
        if (url === '/api/animation-models') {
          const file = path.join(ANIMATIONS_DIR, 'catalog.json');
          res.setHeader('Content-Type', 'application/json');
          if (fs.existsSync(file)) fs.createReadStream(file).pipe(res);
          else { res.statusCode = 404; res.end(JSON.stringify({error:'Run dreams export-animations.'})); }
          return;
        }

        // API: List animations for a given model (e.g. /api/animations/xh_)
        if (url.startsWith('/api/animations/')) {
          const modelStem = url.replace('/api/animations/', '').split('?')[0].toLowerCase();
          try {
            const manifestFile = path.join(ANIMATIONS_DIR, 'manifest.json');
            if (fs.existsSync(manifestFile)) {
              const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
              const clips = manifest[modelStem] || [];
              res.setHeader('Content-Type', 'application/json');
              res.end(JSON.stringify(clips));
              return;
            }
            res.setHeader('Content-Type', 'application/json');
            res.end(JSON.stringify([]));
            return;
          } catch (err) {
            res.statusCode = 500;
            res.end(JSON.stringify({ error: String(err) }));
            return;
          }
        }

        // API: Get single animation clip data (e.g. /api/animation/xh_/xh_an000)
        if (url.startsWith('/api/animation/')) {
          const parts = url.replace('/api/animation/', '').split('?')[0].toLowerCase().split('/');
          if (parts.length >= 2) {
            const [modelStem, clipId] = parts;
            const clipFile = path.join(ANIMATIONS_DIR, modelStem, `${clipId.replace('.3da', '')}.json`);
            if (fs.existsSync(clipFile)) {
              res.setHeader('Content-Type', 'application/json');
              fs.createReadStream(clipFile).pipe(res);
              return;
            }
          }
          res.statusCode = 404;
          res.end(JSON.stringify({ error: 'Animation clip not found' }));
          return;
        }

        // API: Get model skinning bindings (vertex-to-node mapping)
        if (url.startsWith('/api/skin/')) {
          const modelName = url.replace('/api/skin/', '').split('?')[0].toLowerCase();
          const skinFile = path.join(ANIMATIONS_DIR, `${modelName}_skin.json`);
          if (fs.existsSync(skinFile)) {
            res.setHeader('Content-Type', 'application/json');
            fs.createReadStream(skinFile).pipe(res);
            return;
          }
          res.statusCode = 404;
          res.end(JSON.stringify({ error: `Skin not found for ${modelName}` }));
          return;
        }

        // Stream asset file: /api/assets/gltf/... or /api/assets/models/...
        let targetDir: string | null = null;
        let relPath = '';

        if (url.startsWith('/api/assets/scenes/') || url.startsWith('/api/assets/gltf/')) {
          targetDir = GLTF_DIR;
          relPath = decodeURIComponent(url.replace(/^\/api\/assets\/(scenes|gltf)\//, '').split('?')[0]);
        } else if (url.startsWith('/api/assets/models/')) {
          targetDir = MODELS_DIR;
          relPath = decodeURIComponent(url.replace('/api/assets/models/', '').split('?')[0]);
        } else if (url.startsWith('/api/assets/audio/')) {
          targetDir = path.join(BAKED_DIR, 'audio');
          relPath = decodeURIComponent(url.replace('/api/assets/audio/', '').split('?')[0]);
        } else if (url.startsWith('/api/assets/video/')) {
          targetDir = path.join(BAKED_DIR, 'video');
          relPath = decodeURIComponent(url.replace('/api/assets/video/', '').split('?')[0]);
        }

        if (targetDir && relPath) {
          const filePath = path.join(targetDir, relPath);
          if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
            const ext = path.extname(filePath).toLowerCase();
            const contentTypes: Record<string, string> = {
              '.gltf': 'model/gltf+json',
              '.bin': 'application/octet-stream',
              '.png': 'image/png',
              '.jpg': 'image/jpeg',
              '.jpeg': 'image/jpeg',
              '.opus': 'audio/ogg',
              '.ogg': 'audio/ogg',
              '.mp3': 'audio/mpeg',
              '.m4a': 'audio/mp4',
              '.mp4': 'video/mp4',
              '.webm': 'video/webm',
              '.flac': 'audio/flac',
              '.wav': 'audio/wav',
            };

            const stat = fs.statSync(filePath);
            const fileSize = stat.size;
            const range = req.headers.range;

            res.setHeader('Content-Type', contentTypes[ext] || 'application/octet-stream');
            res.setHeader('Access-Control-Allow-Origin', '*');
            res.setHeader('Accept-Ranges', 'bytes');

            if (range) {
              const parts = range.replace(/bytes=/, '').split('-');
              const start = parseInt(parts[0], 10);
              const end = parts[1] ? parseInt(parts[1], 10) : fileSize - 1;
              const chunkSize = end - start + 1;

              res.statusCode = 206;
              res.setHeader('Content-Range', `bytes ${start}-${end}/${fileSize}`);
              res.setHeader('Content-Length', chunkSize);
              const stream = fs.createReadStream(filePath, { start, end });
              stream.pipe(res);
              return;
            } else {
              res.setHeader('Content-Length', fileSize);
              const stream = fs.createReadStream(filePath);
              stream.pipe(res);
              return;
            }
          } else {
            res.statusCode = 404;
            const hint = targetDir.startsWith(BAKED_DIR)
              ? ' Please run "uv run dreams bake" to produce web-ready media assets.'
              : '';
            res.end(`File not found: ${relPath}.${hint}`);
            return;
          }
        }

        next();
      });
    },
  };
}

export default defineConfig({
  plugins: [dreamsAssetPlugin()],
  server: {
    port: 5173,
    open: false,
  },
});
