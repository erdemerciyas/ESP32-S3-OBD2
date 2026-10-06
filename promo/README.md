# AURA promo video

The source for the AURA promo video, in English and Turkish, built with [Remotion](https://www.remotion.dev/) (React → MP4). The rendered videos, subtitles and thumbnails are attached to the [v1.2.1 release](https://github.com/erdemerciyas/ESP32-S3-OBD2/releases/tag/v1.2.1).

Every screen in the video is a real firmware frame from [`docs/screenshots`](../docs/screenshots).

## Build

Requires Node.js 18+ and `ffmpeg` / `ffprobe` on `PATH`.

```bash
npm i
npm run assets      # copy screenshots, generate EN + TR narration and the music bed into public/
npm run dev         # Remotion Studio preview
npm run render:en   # out/AURA-promo-en.mp4
npm run render:tr   # out/AURA-promo-tr.mp4
npm run thumbs      # out/AURA-thumbnail-{en,tr}.png (1280×720)
```

`npm run assets` also writes `out/AURA-promo-{en,tr}.srt` (YouTube subtitles) and `src/timeline-{en,tr}.json`. Scene lengths follow the narration length, so re-run it whenever you change the script.

## Narration

- Script: [`narration.json`](narration.json), with one entry per scene in `en` and `tr`.
- On-screen text: [`src/copy.ts`](src/copy.ts).
- Default voices are the free Microsoft Edge neural voices (`en-US-AndrewMultilingualNeural`, `tr-TR-AhmetNeural`).
- To use ElevenLabs (`eleven_multilingual_v2`) instead:

  ```bash
  ELEVENLABS_API_KEY=... node scripts/tts.mjs en elevenlabs
  ELEVENLABS_API_KEY=... node scripts/tts.mjs tr elevenlabs
  ```

The music bed is synthesized by [`scripts/music.mjs`](scripts/music.mjs) and is royalty-free. To use another track, replace `public/music.mp3`.

## Layout

```
src/
├── Root.tsx            # compositions: AuraPromoEN, AuraPromoTR, ThumbnailEN/TR
├── Promo.tsx           # scene timeline (TransitionSeries) + music
├── copy.ts             # on-screen text per language
├── theme.ts            # colours, fonts, easing
├── components/         # Backdrop (neon grid), Device (round display mock-up), TextBlock
└── scenes/             # Intro, Feature (OBD/NAV/ROLL/…), Tech, Outro
scripts/
├── tts.mjs             # narration → public/voice/<lang>/*.mp3, timeline JSON, SRT
└── music.mjs           # synthwave bed → public/music.mp3
```
