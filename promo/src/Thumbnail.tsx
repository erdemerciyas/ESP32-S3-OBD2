import { AbsoluteFill } from "remotion";
import { Backdrop } from "./components/Backdrop";
import { Device } from "./components/Device";
import { COPY } from "./copy";
import { Wordmark } from "./scenes/IntroScene";
import { C, display, type Lang } from "./theme";

// YouTube thumbnail, 1280x720.
export const Thumbnail: React.FC<{ lang: Lang }> = ({ lang }) => (
  <AbsoluteFill>
    <Backdrop accent={C.cyan} />
    <Device shots={["nav_guide"]} cuts={[]} size={330} accent={C.cyan} tilt={12} style={{ left: 760, top: 60 }} />
    <Device shots={["roll"]} cuts={[]} size={290} accent={C.orange} tilt={-10} style={{ left: 930, top: 370 }} />
    <div style={{ position: "absolute", left: 70, top: 150 }}>
      <div style={{ marginLeft: -40 }}>
        <Wordmark size={150} progress={1} />
      </div>
      <div style={{ fontFamily: display, fontWeight: 800, fontSize: 64, lineHeight: 1.05, color: C.text, whiteSpace: "pre-line", marginTop: 20, textShadow: "0 4px 30px #000" }}>
        {COPY[lang].thumb}
      </div>
      <div style={{ display: "flex", gap: 14, marginTop: 34 }}>
        {["OBD", "NAV", "0-100"].map((x, i) => (
          <div
            key={x}
            style={{
              fontFamily: display,
              fontWeight: 800,
              fontSize: 36,
              color: "#000",
              background: [C.cyan, C.green, C.orange][i],
              padding: "6px 22px",
              borderRadius: 12,
            }}
          >
            {x}
          </div>
        ))}
      </div>
    </div>
  </AbsoluteFill>
);
