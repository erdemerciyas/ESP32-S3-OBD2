import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, interpolate, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { Backdrop } from "../components/Backdrop";
import { Device } from "../components/Device";
import { TextBlock } from "../components/TextBlock";
import { C, clamp, ease, mono, type Lang } from "../theme";

export type SceneTiming = { durationInFrames: number; voiceFrom: number };

type Props = SceneTiming & {
  lang: Lang;
  id: string;
  accent: string;
  side: "left" | "right"; // where the device sits
  shots: string[];
  cuts?: number[];
  labels?: string[];
  ring?: [number, number];
  kicker: string;
  title: string;
  chips: string[];
  stopwatch?: string;
};

export const FeatureScene: React.FC<Props> = (p) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const cuts = p.cuts ?? p.shots.slice(1).map((_, i) => Math.round(((i + 1) * p.durationInFrames) / p.shots.length));
  const left = p.side === "left";
  const enter = interpolate(frame, [0, 30], [0, 1], { ...clamp, easing: ease });

  return (
    <AbsoluteFill>
      <Backdrop accent={p.accent} />
      <Device
        shots={p.shots}
        cuts={cuts}
        labels={p.stopwatch ? undefined : p.labels}
        ring={p.ring}
        size={620}
        accent={p.accent}
        tilt={left ? 14 - 4 * enter : -14 + 4 * enter}
        style={{
          left: left ? 170 : 1130,
          top: 170,
          opacity: enter,
          scale: String(interpolate(enter, [0, 1], [0.85, 1])),
        }}
      />
      <TextBlock
        kicker={p.kicker}
        title={p.title}
        chips={p.chips}
        accent={p.accent}
        delay={6}
        style={{ left: left ? 940 : 130, top: 170 }}
      />
      {p.stopwatch ? <Stopwatch label={p.stopwatch} accent={p.accent} from={Math.round(1.2 * fps)} /> : null}
      <Sequence from={p.voiceFrom} premountFor={fps} name="Voice">
        <Audio src={staticFile(`voice/${p.lang}/${p.id}.mp3`)} />
      </Sequence>
    </AbsoluteFill>
  );
};

// Animated 0-100 readout for the ROLL scene.
const Stopwatch: React.FC<{ label: string; accent: string; from: number }> = ({ label, accent, from }) => {
  const frame = useCurrentFrame();
  const t = interpolate(frame, [from, from + 110], [0, 1], clamp);
  const secs = 7.42 * Math.pow(t, 0.9);
  const speed = Math.round(100 * Math.sqrt(t));
  const done = t >= 1;

  return (
    <div
      style={{
        position: "absolute",
        left: 1130,
        width: 620,
        top: 830,
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        gap: 4,
        fontFamily: mono,
        opacity: interpolate(frame, [from - 15, from], [0, 1], clamp),
      }}
    >
      <div style={{ fontSize: 30, letterSpacing: 4, color: C.dim }}>{label}</div>
      <div style={{ fontSize: 88, fontWeight: 700, color: done ? C.green : C.text, textShadow: done ? `0 0 30px ${C.green}` : `0 0 20px ${accent}66` }}>
        {secs.toFixed(2)}
        <span style={{ fontSize: 40, color: C.dim }}> s</span>
      </div>
      <div style={{ fontSize: 34, color: accent }}>{speed} km/h</div>
    </div>
  );
};
