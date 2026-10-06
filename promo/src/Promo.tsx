import { Audio } from "@remotion/media";
import { TransitionSeries, linearTiming } from "@remotion/transitions";
import { fade } from "@remotion/transitions/fade";
import { slide } from "@remotion/transitions/slide";
import { AbsoluteFill, interpolate, staticFile, useVideoConfig } from "remotion";
import { COPY } from "./copy";
import { FeatureScene } from "./scenes/FeatureScene";
import { IntroScene } from "./scenes/IntroScene";
import { OutroScene } from "./scenes/OutroScene";
import { TechScene } from "./scenes/TechScene";
import { C, TRANSITION, type Lang } from "./theme";
import en from "./timeline-en.json";
import tr from "./timeline-tr.json";

export const TIMELINES = { en, tr };

export const totalFrames = (lang: Lang) =>
  TIMELINES[lang].reduce((a, s) => a + s.durationInFrames, 0) - (TIMELINES[lang].length - 1) * TRANSITION;

export type PromoProps = { lang: Lang };

export const Promo: React.FC<PromoProps> = ({ lang }) => {
  const { fps, durationInFrames } = useVideoConfig();
  const t = Object.fromEntries(TIMELINES[lang].map((s) => [s.id, s]));
  const c = COPY[lang];
  const fadeT = () => <TransitionSeries.Transition presentation={fade()} timing={linearTiming({ durationInFrames: TRANSITION })} />;
  const slideT = () => (
    <TransitionSeries.Transition presentation={slide({ direction: "from-right" })} timing={linearTiming({ durationInFrames: TRANSITION })} />
  );

  return (
    <AbsoluteFill lang={lang}>
      <TransitionSeries>
        <TransitionSeries.Sequence name="Intro" durationInFrames={t.intro.durationInFrames} premountFor={fps}>
          <IntroScene lang={lang} tagline={c.tagline} {...t.intro} />
        </TransitionSeries.Sequence>
        {fadeT()}
        <TransitionSeries.Sequence name="Modes" durationInFrames={t.modes.durationInFrames} premountFor={fps}>
          <FeatureScene lang={lang} accent={C.orange} side="left" shots={["home"]} {...c.modes} {...t.modes} />
        </TransitionSeries.Sequence>
        {slideT()}
        <TransitionSeries.Sequence name="OBD" durationInFrames={t.obd.durationInFrames} premountFor={fps}>
          <FeatureScene lang={lang} accent={C.cyan} side="right" shots={["obd_dash", "obd_settings"]} {...c.obd} {...t.obd} />
        </TransitionSeries.Sequence>
        {fadeT()}
        <TransitionSeries.Sequence name="Diagnostics" durationInFrames={t.diag.durationInFrames} premountFor={fps}>
          <FeatureScene lang={lang} accent={C.green} side="left" shots={["obd_grid", "obd_dtc", "obd_gyro"]} {...c.diag} {...t.diag} />
        </TransitionSeries.Sequence>
        {slideT()}
        <TransitionSeries.Sequence name="NAV" durationInFrames={t.nav.durationInFrames} premountFor={fps}>
          <FeatureScene lang={lang} accent={C.cyan} side="right" shots={["nav_guide", "nav_turn", "nav_idle"]} {...c.nav} {...t.nav} />
        </TransitionSeries.Sequence>
        {slideT()}
        <TransitionSeries.Sequence name="ROLL" durationInFrames={t.roll.durationInFrames} premountFor={fps}>
          <FeatureScene lang={lang} accent={C.orange} side="right" shots={["roll"]} {...c.roll} {...t.roll} />
        </TransitionSeries.Sequence>
        {fadeT()}
        <TransitionSeries.Sequence name="Clock" durationInFrames={t.clock.durationInFrames} premountFor={fps}>
          <FeatureScene
            lang={lang}
            accent={C.magenta}
            side="left"
            shots={["home", "clock"]}
            cuts={[Math.round(3.4 * fps)]}
            ring={[Math.round(0.8 * fps), Math.round(3.4 * fps)]}
            {...c.clock}
            {...t.clock}
          />
        </TransitionSeries.Sequence>
        {fadeT()}
        <TransitionSeries.Sequence name="Tech" durationInFrames={t.tech.durationInFrames} premountFor={fps}>
          <TechScene lang={lang} {...c.tech} {...t.tech} />
        </TransitionSeries.Sequence>
        {fadeT()}
        <TransitionSeries.Sequence name="Outro" durationInFrames={t.outro.durationInFrames} premountFor={fps}>
          <OutroScene lang={lang} {...c.outro} {...t.outro} />
        </TransitionSeries.Sequence>
      </TransitionSeries>
      <Audio
        name="Music"
        src={staticFile("music.mp3")}
        volume={(f) =>
          interpolate(f, [0, 2 * fps, durationInFrames - 4 * fps, durationInFrames], [0, 0.14, 0.14, 0], {
            extrapolateLeft: "clamp",
            extrapolateRight: "clamp",
          })
        }
      />
    </AbsoluteFill>
  );
};
