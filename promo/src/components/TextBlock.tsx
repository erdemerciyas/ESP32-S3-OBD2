import { interpolate, useCurrentFrame, useVideoConfig } from "remotion";
import { C, clamp, display, ease, mono } from "../theme";

type Props = {
  kicker: string;
  title: string;
  chips?: string[];
  accent: string;
  delay?: number;
  style?: React.CSSProperties;
};

// Kicker + headline + staggered feature chips.
export const TextBlock: React.FC<Props> = ({ kicker, title, chips = [], accent, delay = 0, style }) => {
  const frame = useCurrentFrame() - delay;
  const { fps } = useVideoConfig();
  const longest = Math.max(...title.split("\n").map((l) => l.length));
  const titleSize = longest <= 15 ? 92 : longest <= 18 ? 80 : 70;
  const inAt = (f: number) => ({
    opacity: interpolate(frame, [f, f + 18], [0, 1], clamp),
    translate: `0 ${interpolate(frame, [f, f + 24], [40, 0], { ...clamp, easing: ease })}px`,
  });

  return (
    <div style={{ position: "absolute", width: 900, ...style }}>
      <div style={{ display: "flex", alignItems: "center", gap: 18, ...inAt(0) }}>
        <div style={{ width: interpolate(frame, [0, 20], [0, 56], { ...clamp, easing: ease }), height: 3, background: accent, boxShadow: `0 0 12px ${accent}` }} />
        <div style={{ fontFamily: mono, fontWeight: 700, fontSize: 28, letterSpacing: 6, color: accent }}>{kicker}</div>
      </div>
      <div
        style={{
          fontFamily: display,
          fontWeight: 800,
          fontSize: titleSize,
          lineHeight: 1.04,
          letterSpacing: -2,
          color: C.text,
          marginTop: 26,
          whiteSpace: "pre-line",
          textShadow: "0 6px 40px rgba(0,0,0,0.6)",
          ...inAt(6),
        }}
      >
        {title}
      </div>
      <div style={{ display: "flex", flexDirection: "column", gap: 16, marginTop: 46 }}>
        {chips.map((c, i) => (
          <div
            key={c}
            style={{
              alignSelf: "flex-start",
              fontFamily: display,
              fontWeight: 600,
              fontSize: 34,
              color: C.text,
              padding: "14px 26px",
              borderRadius: 999,
              border: `2px solid ${accent}66`,
              background: `linear-gradient(90deg, ${accent}22, rgba(255,255,255,0.03))`,
              display: "flex",
              alignItems: "center",
              gap: 16,
              ...inAt(Math.round(0.9 * fps) + i * Math.round(0.45 * fps)),
            }}
          >
            <div style={{ width: 12, height: 12, borderRadius: "50%", background: accent, boxShadow: `0 0 10px ${accent}` }} />
            {c}
          </div>
        ))}
      </div>
    </div>
  );
};
