// Synthesizes a royalty-free synthwave bed (pad + arp + soft kick) -> public/music.mp3
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";

const SR = 44100;
const SECONDS = 135;
const BPM = 100;
const beat = 60 / BPM;
const bar = beat * 4;
const N = SR * SECONDS;
const L = new Float32Array(N);
const R = new Float32Array(N);

const midi = (m) => 440 * 2 ** ((m - 69) / 12);
// Am - F - C - G, one chord per bar
const CHORDS = [
  [45, 57, 60, 64, 69],
  [41, 53, 57, 60, 65],
  [48, 55, 60, 64, 67],
  [43, 55, 59, 62, 67],
];

const saw = (ph) => {
  let s = 0;
  for (let k = 1; k <= 6; k++) s += Math.sin(ph * k) / k;
  return s;
};

for (let i = 0; i < N; i++) {
  const t = i / SR;
  const b = Math.floor(t / bar);
  const chord = CHORDS[b % 4];
  const tb = t - b * bar;
  // Pad: soft attack/release inside each bar, two detuned voices
  const env = Math.min(1, tb / 0.8) * Math.min(1, (bar - tb) / 0.5);
  let pad = 0;
  for (const m of chord) {
    const f = midi(m);
    pad += saw(2 * Math.PI * f * t) + saw(2 * Math.PI * f * 1.006 * t + 1.3);
  }
  pad *= 0.022 * env;

  // Arp: 16th-note plucks over chord tones, one octave up
  const step = beat / 4;
  const si = Math.floor(t / step);
  const ts = t - si * step;
  const note = chord[1 + (si % 4)] + 12;
  const arp = Math.sin(2 * Math.PI * midi(note) * t) * Math.exp(-ts * 14) * 0.06;

  // Kick on every beat, entering after 2 bars; bass on 8ths
  const intro = Math.min(1, Math.max(0, (t - bar * 2) / bar));
  const tk = t % beat;
  const kick = Math.sin(2 * Math.PI * (45 + 90 * Math.exp(-tk * 30)) * tk) * Math.exp(-tk * 9) * 0.32 * intro;
  const te = t % (beat / 2);
  const bass = Math.sin(2 * Math.PI * midi(chord[0] - 12 + 12) * t) * Math.exp(-te * 5) * 0.12 * intro;

  // Global fade in / out
  const g = Math.min(1, t / 3) * Math.min(1, (SECONDS - t) / 4);
  const pan = 0.5 + 0.3 * Math.sin(t * 0.7);
  L[i] = (pad * (1.2 - pan) + arp * pan + kick + bass) * g;
  R[i] = (pad * (0.2 + pan) + arp * (1 - pan) + kick + bass) * g;
}

const buf = Buffer.alloc(44 + N * 4);
buf.write("RIFF", 0); buf.writeUInt32LE(36 + N * 4, 4); buf.write("WAVE", 8);
buf.write("fmt ", 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
buf.writeUInt32LE(SR, 24); buf.writeUInt32LE(SR * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34);
buf.write("data", 36); buf.writeUInt32LE(N * 4, 40);
for (let i = 0; i < N; i++) {
  buf.writeInt16LE(Math.round(Math.max(-1, Math.min(1, L[i])) * 32767), 44 + i * 4);
  buf.writeInt16LE(Math.round(Math.max(-1, Math.min(1, R[i])) * 32767), 46 + i * 4);
}
const root = path.resolve(import.meta.dirname, "..");
const wav = path.join(root, "out", "music.wav");
fs.mkdirSync(path.dirname(wav), { recursive: true });
fs.writeFileSync(wav, buf);
execFileSync("ffmpeg", [
  "-y", "-v", "error", "-i", wav,
  "-af", "aecho=0.8:0.6:180|370:0.35|0.22,lowpass=f=6000,loudnorm=I=-16:TP=-1.5",
  "-ar", "44100", "-b:a", "192k", path.join(root, "public", "music.mp3"),
]);
console.log("public/music.mp3");
