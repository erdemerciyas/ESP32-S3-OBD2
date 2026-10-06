import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, interpolate, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { Backdrop } from "../components/Backdrop";
import { REPO } from "../copy";
import { C, clamp, display, ease, mono, type Lang } from "../theme";
import type { SceneTiming } from "./FeatureScene";
import { Wordmark } from "./IntroScene";

type Props = SceneTiming & { lang: Lang; line: string; chips: string[]; footer: string };

export const OutroScene: React.FC<Props> = ({ lang, line, chips, footer, voiceFrom, durationInFrames }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const inAt = (f: number) => ({
    opacity: interpolate(frame, [f, f + 18], [0, 1], clamp),
    translate: `0 ${interpolate(frame, [f, f + 26], [30, 0], { ...clamp, easing: ease })}px`,
  });
  const out = interpolate(frame, [durationInFrames - 30, durationInFrames], [1, 0], clamp);

  return (
    <AbsoluteFill style={{ opacity: out }}>
      <Backdrop accent={C.cyan} />
      <AbsoluteFill style={{ alignItems: "center", justifyContent: "center", flexDirection: "column", gap: 34 }}>
        <Wordmark size={210} progress={interpolate(frame, [0, 35], [0, 1], { ...clamp, easing: ease })} />
        <div style={{ fontFamily: display, fontWeight: 600, fontSize: 54, color: C.text, ...inAt(20) }}>{line}</div>
        <div
          style={{
            fontFamily: mono,
            fontWeight: 700,
            fontSize: 44,
            color: C.cyan,
            padding: "20px 40px",
            borderRadius: 20,
            border: `2px solid ${C.cyan}88`,
            background: `${C.cyan}14`,
            boxShadow: `0 0 50px ${C.cyan}33`,
            ...inAt(40),
          }}
        >
          {REPO}
        </div>
        <div style={{ display: "flex", gap: 22, ...inAt(60) }}>
          {chips.map((c) => (
            <div key={c} style={{ fontFamily: display, fontWeight: 600, fontSize: 32, color: C.dim, padding: "10px 26px", borderRadius: 999, border: "2px solid #2a3140" }}>
              {c}
            </div>
          ))}
        </div>
        <div style={{ fontFamily: mono, fontSize: 34, letterSpacing: 6, color: C.magenta, marginTop: 20, ...inAt(Math.round(voiceFrom + 4.5 * fps)) }}>
          {footer.toLocaleUpperCase(lang)}
        </div>
      </AbsoluteFill>
      <Sequence from={voiceFrom} premountFor={fps} name="Voice">
        <Audio src={staticFile(`voice/${lang}/outro.mp3`)} />
      </Sequence>
    </AbsoluteFill>
  );
};
