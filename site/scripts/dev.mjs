// npm run dev: the client bundle in watch mode plus Astro's dev server.
import { spawn } from 'node:child_process';

const opts = { stdio: 'inherit', shell: true, windowsHide: true };
const client = spawn('node scripts/build-client.mjs --watch', opts);
const astro = spawn('npx astro dev', opts);
const stop = () => { client.kill(); astro.kill(); process.exit(); };
process.on('SIGINT', stop);
astro.on('exit', stop);
