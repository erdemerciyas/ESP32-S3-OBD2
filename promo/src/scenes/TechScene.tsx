import { Audio } from "@remotion/media";
import { AbsoluteFill, Img, Sequence, interpolate, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { Backdrop } from "../components/Backdrop";
import { C, clamp, display, ease, mono, type Lang } from "../theme";
import type { SceneTiming } from "./FeatureScene";

const SHOTS = ["splash", "home", "obd_dash", "obd_grid", "obd_dtc", "obd_gyro", "nav_guide", "nav_turn", "nav_idle", "roll", "clock", "obd_settings"];

type Props = SceneTiming & { lang: Lang; kicker: string; title: string; chips: string[] };

// Mosaic of every real firmware screen.
export const TechScene: React.FC<Props> = ({ lang, kicker, title, chips, voiceFrom }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const d = 230;

  return (
    <AbsoluteFill>
      <Backdrop accent={C.magenta} grid={0.6} />
      <div style={{ position: "absolute", top: 90, left: 0, right: 0, textAlign: "center" }}>
        <div style={{ fontFamily: mono, fontWeight: 700, fontSize: 28, letterSpacing: 6, color: C.magenta, opacity: interpolate(frame, [0, 15], [0, 1], clamp) }}>{kicker}</div>
        <div
          style={{
            fontFamily: display,
            fontWeight: 800,
            fontSize: 84,
            lineHeight: 1.05,
            color: C.text,
            whiteSpace: "pre-line",
            marginTop: 18,
            opacity: interpolate(frame, [6, 24], [0, 1], clamp),
            translate: `0 ${interpolate(frame, [6, 30], [30, 0], { ...clamp, easing: ease })}px`,
          }}
        >
          {title}
        </div>
      </div>
      {SHOTS.map((s, i) => {
        const col = i % 6;
        const row = Math.floor(i / 6);
        const at = 12 + i * 4;
        const p = interpolate(frame, [at, at + 26], [0, 1], { ...clamp, easing: ease });
        return (
          <Img
            key={s}
            src={staticFile(`shots/${s}.png`)}
            style={{
              position: "absolute",
              width: d,
              height: d,
              left: 960 - 3 * (d + 40) + 20 + col * (d + 40),
              top: 400 + row * (d + 30) + Math.sin(frame / 30 + i) * 6,
              borderRadius: "50%",
              opacity: p,
              scale: String(interpolate(p, [0, 1], [0.4, 1])),
              boxShadow: `0 0 0 6px #151922, 0 0 40px ${i % 2 ? C.cyan : C.magenta}44`,
            }}
          />
        );
      })}
      <div style={{ position: "absolute", bottom: 50, left: 0, right: 0, display: "flex", justifyContent: "center", gap: 22 }}>
        {chips.map((c, i) => (
          <div
            key={c}
            style={{
              fontFamily: mono,
              fontWeight: 700,
              fontSize: 28,
              color: C.text,
              padding: "10px 24px",
              borderRadius: 999,
              border: `2px solid ${C.magenta}66`,
              background: `${C.magenta}1a`,
              opacity: interpolate(frame, [60 + i * 10, 75 + i * 10], [0, 1], clamp),
            }}
          >
            {c}
          </div>
        ))}
      </div>
      <Sequence from={voiceFrom} premountFor={fps} name="Voice">
        <Audio src={staticFile(`voice/${lang}/tech.mp3`)} />
      </Sequence>
    </AbsoluteFill>
  );
};
