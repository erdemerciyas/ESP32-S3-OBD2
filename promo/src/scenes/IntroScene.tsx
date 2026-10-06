import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, interpolate, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { Backdrop } from "../components/Backdrop";
import { Device } from "../components/Device";
import { C, clamp, display, ease, mono, type Lang } from "../theme";
import type { SceneTiming } from "./FeatureScene";

export const Wordmark: React.FC<{ size: number; progress: number }> = ({ size, progress }) => (
  <div
    style={{
      fontFamily: display,
      fontWeight: 800,
      fontSize: size,
      letterSpacing: interpolate(progress, [0, 1], [size * 0.6, size * 0.28]),
      paddingLeft: size * 0.28,
      background: `linear-gradient(100deg, ${C.cyan}, #ffffff 45%, ${C.magenta})`,
      WebkitBackgroundClip: "text",
      backgroundClip: "text",
      color: "transparent",
      filter: `drop-shadow(0 0 ${size * 0.15}px ${C.cyan}66)`,
      opacity: progress,
    }}
  >
    AURA
  </div>
);

export const IntroScene: React.FC<SceneTiming & { lang: Lang; tagline: string }> = ({ lang, tagline, voiceFrom }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const appear = interpolate(frame, [0, 40], [0, 1], { ...clamp, easing: ease });
  const move = interpolate(frame, [55, 95], [0, 1], { ...clamp, easing: ease });
  const word = interpolate(frame, [75, 120], [0, 1], { ...clamp, easing: ease });

  return (
    <AbsoluteFill>
      <Backdrop accent={C.cyan} grid={appear} />
      <Device
        shots={["splash"]}
        cuts={[]}
        size={600}
        accent={C.cyan}
        tilt={interpolate(move, [0, 1], [0, 12])}
        style={{
          left: interpolate(move, [0, 1], [660, 190]),
          top: 220,
          opacity: appear,
          scale: String(interpolate(appear, [0, 1], [0.6, 1])),
          filter: `blur(${interpolate(appear, [0, 1], [18, 0])}px)`,
        }}
      />
      <div style={{ position: "absolute", left: 900, top: 330, width: 900, textAlign: "center" }}>
        <Wordmark size={190} progress={word} />
        <div
          style={{
            fontFamily: mono,
            fontSize: 36,
            letterSpacing: 6,
            color: C.dim,
            marginTop: 30,
            opacity: interpolate(frame, [100, 125], [0, 1], clamp),
            translate: `0 ${interpolate(frame, [100, 130], [20, 0], { ...clamp, easing: ease })}px`,
          }}
        >
          {tagline.toLocaleUpperCase(lang)}
        </div>
      </div>
      <Sequence from={voiceFrom} premountFor={fps} name="Voice">
        <Audio src={staticFile(`voice/${lang}/intro.mp3`)} />
      </Sequence>
    </AbsoluteFill>
  );
};
