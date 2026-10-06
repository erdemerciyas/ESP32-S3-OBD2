import { AbsoluteFill, random, useCurrentFrame } from "remotion";
import { C } from "../theme";

const STARS = Array.from({ length: 70 }, (_, i) => ({
  x: random(`x${i}`) * 100,
  y: random(`y${i}`) * 55,
  s: 1 + random(`s${i}`) * 2,
  p: random(`p${i}`) * Math.PI * 2,
}));

// Neon synthwave backdrop: drifting aurora glows, stars, moving perspective grid.
export const Backdrop: React.FC<{ accent?: string; grid?: number }> = ({ accent = C.cyan, grid = 1 }) => {
  const frame = useCurrentFrame();
  const t = frame / 30;

  return (
    <AbsoluteFill style={{ backgroundColor: C.bg, overflow: "hidden" }}>
      <AbsoluteFill
        style={{
          background: `radial-gradient(900px 600px at ${30 + Math.sin(t * 0.25) * 8}% ${30 + Math.cos(t * 0.2) * 6}%, ${accent}33, transparent 70%),
            radial-gradient(800px 560px at ${75 + Math.cos(t * 0.22) * 7}% ${40 + Math.sin(t * 0.3) * 6}%, ${C.magenta}26, transparent 70%),
            radial-gradient(1400px 500px at 50% 105%, ${C.magenta}22, transparent 70%)`,
        }}
      />
      {STARS.map((s, i) => (
        <div
          key={i}
          style={{
            position: "absolute",
            left: `${s.x}%`,
            top: `${s.y}%`,
            width: s.s,
            height: s.s,
            borderRadius: "50%",
            background: "#fff",
            opacity: 0.15 + 0.35 * (0.5 + 0.5 * Math.sin(t * 1.3 + s.p)),
          }}
        />
      ))}
      {/* Perspective grid floor */}
      <div
        style={{
          position: "absolute",
          left: "-50%",
          right: "-50%",
          bottom: -40,
          height: 620,
          opacity: 0.55 * grid,
          transform: "perspective(500px) rotateX(72deg)",
          transformOrigin: "50% 100%",
          backgroundImage: `linear-gradient(${C.magenta}aa 2px, transparent 2px), linear-gradient(90deg, ${C.magenta}88 2px, transparent 2px)`,
          backgroundSize: "120px 120px",
          backgroundPosition: `0px ${(frame * 3) % 120}px`,
          maskImage: "linear-gradient(to top, black 30%, transparent 95%)",
          WebkitMaskImage: "linear-gradient(to top, black 30%, transparent 95%)",
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          bottom: 300,
          height: 2,
          background: `linear-gradient(90deg, transparent, ${C.magenta}, ${accent}, ${C.magenta}, transparent)`,
          opacity: 0.35 * grid,
          filter: "blur(1px)",
        }}
      />
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at center, transparent 55%, rgba(0,0,0,0.65) 100%)" }} />
    </AbsoluteFill>
  );
};
