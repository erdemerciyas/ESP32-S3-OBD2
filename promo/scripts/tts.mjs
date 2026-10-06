// Generates narration audio + timeline + YouTube SRT for one language.
//   node scripts/tts.mjs en            (free Microsoft Edge neural voices)
//   node scripts/tts.mjs tr elevenlabs (needs ELEVENLABS_API_KEY in env)
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { MsEdgeTTS, OUTPUT_FORMAT } from "msedge-tts";

const lang = process.argv[2] ?? "en";
const engine = process.argv[3] ?? "edge";
const FPS = 30;
const TRANSITION = 15; // frames of overlap between scenes (keep in sync with src/Promo.tsx)
const LEAD = 0.35; // seconds before the voice starts in each scene
const TAIL = 0.75; // seconds after the voice ends
const EXTRA = { intro: { lead: 2.2, tail: 0.6 }, outro: { lead: 0.3, tail: 4.5 } };

const VOICES = {
  edge: { en: "en-US-AndrewMultilingualNeural", tr: "tr-TR-AhmetNeural" },
  elevenlabs: { en: "nPczCjzI2devNBz1zQrb", tr: "dLAxUPEUCZ9cz4KHisnL" }, // Brian / Sercan Esen
};

const root = path.resolve(import.meta.dirname, "..");
const scenes = JSON.parse(fs.readFileSync(path.join(root, "narration.json"), "utf8"));
const outDir = path.join(root, "public", "voice", lang);
fs.mkdirSync(outDir, { recursive: true });

const duration = (file) =>
  Number(execFileSync("ffprobe", ["-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", file]).toString().trim());

async function edge(text, file) {
  const tts = new MsEdgeTTS();
  await tts.setMetadata(VOICES.edge[lang], OUTPUT_FORMAT.AUDIO_24KHZ_96KBITRATE_MONO_MP3);
  const tmp = fs.mkdtempSync(path.join(outDir, "tmp-"));
  const { audioFilePath } = await tts.toFile(tmp, text, { rate: lang === "tr" ? "+4%" : "+2%" });
  fs.copyFileSync(audioFilePath, file);
  fs.rmSync(tmp, { recursive: true, force: true });
  tts.close();
}

async function elevenlabs(text, file) {
  const res = await fetch(`https://api.elevenlabs.io/v1/text-to-speech/${VOICES.elevenlabs[lang]}?output_format=mp3_44100_128`, {
    method: "POST",
    headers: { "xi-api-key": process.env.ELEVENLABS_API_KEY, "content-type": "application/json" },
    body: JSON.stringify({
      text,
      model_id: "eleven_multilingual_v2",
      language_code: lang,
      voice_settings: { stability: 0.45, similarity_boost: 0.8, style: 0.2 },
    }),
  });
  if (!res.ok) throw new Error(`ElevenLabs ${res.status}: ${await res.text()}`);
  fs.writeFileSync(file, Buffer.from(await res.arrayBuffer()));
}

const srtTime = (s) => {
  const ms = Math.round(s * 1000);
  const p = (n, w = 2) => String(n).padStart(w, "0");
  return `${p(Math.floor(ms / 3600000))}:${p(Math.floor(ms / 60000) % 60)}:${p(Math.floor(ms / 1000) % 60)},${p(ms % 1000, 3)}`;
};

const timeline = [];
const srt = [];
let start = 0; // frame where the current scene starts in the full video
for (const scene of scenes) {
  const text = scene[lang];
  const file = path.join(outDir, `${scene.id}.mp3`);
  console.log(`[${lang}/${engine}] ${scene.id}`);
  await (engine === "elevenlabs" ? elevenlabs : edge)(text, file);
  const voice = duration(file);
  const lead = EXTRA[scene.id]?.lead ?? LEAD;
  const tail = EXTRA[scene.id]?.tail ?? TAIL;
  const voiceFrom = Math.round(lead * FPS);
  const durationInFrames = Math.ceil((lead + voice + tail) * FPS) + TRANSITION;
  timeline.push({ id: scene.id, durationInFrames, voiceFrom, voiceSeconds: voice });

  // One cue per sentence, timed proportionally to character count.
  const sentences = text.match(/[^.!?;:]+[.!?;:]?/g).map((s) => s.trim()).filter(Boolean);
  const total = sentences.reduce((a, s) => a + s.length, 0);
  let t = (start + voiceFrom) / FPS;
  for (const s of sentences) {
    const len = (voice * s.length) / total;
    srt.push(`${srt.length + 1}\n${srtTime(t)} --> ${srtTime(t + len)}\n${s}\n`);
    t += len;
  }
  start += durationInFrames - TRANSITION;
}

fs.writeFileSync(path.join(root, "src", `timeline-${lang}.json`), JSON.stringify(timeline, null, 2));
fs.mkdirSync(path.join(root, "out"), { recursive: true });
fs.writeFileSync(path.join(root, "out", `AURA-promo-${lang}.srt`), srt.join("\n"));
console.log(`total ${((start + TRANSITION) / FPS).toFixed(1)} s`);
