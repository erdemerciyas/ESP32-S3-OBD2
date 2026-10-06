import { Img, interpolate, staticFile, useCurrentFrame } from "remotion";
import { C, clamp, ease, mono } from "../theme";

type Props = {
  shots: string[]; // file names in public/shots
  cuts: number[]; // frame where each shot (after the first) starts
  size: number;
  accent: string;
  tilt?: number; // degrees of Y rotation (negative = facing right)
  labels?: string[];
  ring?: [number, number]; // optional hold-progress ring: [startFrame, endFrame]
  style?: React.CSSProperties;
};

const FADE = 12;

// Waveshare 2.1" round display mock-up showing real firmware screenshots.
export const Device: React.FC<Props> = ({ shots, cuts, size, accent, tilt = 0, labels, ring, style }) => {
  const frame = useCurrentFrame();
  const starts = [0, ...cuts];
  const active = starts.filter((s) => frame >= s).length - 1;
  const bezel = size * 0.045;
  const inner = size - bezel * 2;
  const r = size / 2 + 26;
  const circ = 2 * Math.PI * r;
  const ringP = ring ? interpolate(frame, ring, [0, 1], clamp) : 0;

  return (
    <div style={{ position: "absolute", width: size, height: size, ...style }}>
      <div
        style={{
          width: size,
          height: size,
          transform: `perspective(1800px) rotateY(${tilt}deg) rotateX(${Math.sin(frame / 45) * 2}deg) translateY(${Math.sin(frame / 38) * 10}px)`,
        }}
      >
        {/* Glow */}
        <div
          style={{
            position: "absolute",
            inset: -size * 0.12,
            borderRadius: "50%",
            background: `radial-gradient(circle, ${accent}40 0%, transparent 65%)`,
            filter: "blur(20px)",
          }}
        />
        {/* Bezel */}
        <div
          style={{
            position: "absolute",
            inset: 0,
            borderRadius: "50%",
            background: "conic-gradient(from 210deg, #3b4250, #0b0d12 25%, #4a5160 45%, #0b0d12 70%, #3b4250)",
            boxShadow: `0 0 0 2px #1b1f27, 0 50px 90px rgba(0,0,0,0.7), 0 0 80px ${accent}55`,
          }}
        />
        {/* Screen */}
        <div
          style={{
            position: "absolute",
            left: bezel,
            top: bezel,
            width: inner,
            height: inner,
            borderRadius: "50%",
            overflow: "hidden",
            background: "#000",
          }}
        >
          {shots.map((s, i) => (
            <Img
              key={s}
              src={staticFile(`shots/${s}.png`)}
              style={{
                position: "absolute",
                width: "100%",
                height: "100%",
                opacity:
                  i === 0
                    ? 1
                    : interpolate(frame, [starts[i], starts[i] + FADE], [0, 1], clamp),
                scale: String(interpolate(frame, [starts[i], starts[i] + 240], [1.0, 1.04], clamp)),
              }}
            />
          ))}
          {/* Glass reflection */}
          <div
            style={{
              position: "absolute",
              inset: 0,
              borderRadius: "50%",
              background: "linear-gradient(135deg, rgba(255,255,255,0.13) 0%, rgba(255,255,255,0.02) 35%, transparent 50%)",
            }}
          />
        </div>
        {ring ? (
          <svg width={r * 2 + 20} height={r * 2 + 20} style={{ position: "absolute", left: size / 2 - r - 10, top: size / 2 - r - 10, rotate: "-90deg" }}>
            <circle cx={r + 10} cy={r + 10} r={r} fill="none" stroke={`${accent}22`} strokeWidth={8} />
            <circle
              cx={r + 10}
              cy={r + 10}
              r={r}
              fill="none"
              stroke={accent}
              strokeWidth={8}
              strokeLinecap="round"
              strokeDasharray={circ}
              strokeDashoffset={circ * (1 - ringP)}
              style={{ filter: `drop-shadow(0 0 12px ${accent})` }}
            />
          </svg>
        ) : null}
      </div>
      {labels ? (
        <div style={{ position: "absolute", top: size + 48, left: 0, right: 0, textAlign: "center", height: 40 }}>
          {labels.map((l, i) => (
            <div
              key={l}
              style={{
                position: "absolute",
                left: 0,
                right: 0,
                fontFamily: mono,
                fontSize: 26,
                letterSpacing: 4,
                textTransform: "uppercase",
                color: C.dim,
                opacity: i === active ? interpolate(frame, [starts[i], starts[i] + FADE], [0, 1], clamp) : 0,
                translate: `0 ${interpolate(frame, [starts[i], starts[i] + FADE * 2], [12, 0], { ...clamp, easing: ease })}px`,
              }}
            >
              <span style={{ color: accent }}>●</span> {l}
            </div>
          ))}
        </div>
      ) : null}
    </div>
  );
};
