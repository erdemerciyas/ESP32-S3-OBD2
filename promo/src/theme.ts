import { loadFont as loadSora } from "@remotion/google-fonts/Sora";
import { loadFont as loadMono } from "@remotion/google-fonts/JetBrainsMono";
import { Easing } from "remotion";

export const display = loadSora("normal", { weights: ["400", "600", "800"], subsets: ["latin", "latin-ext"] }).fontFamily;
export const mono = loadMono("normal", { weights: ["500", "700"], subsets: ["latin", "latin-ext"] }).fontFamily;

export const C = {
  bg: "#04060c",
  cyan: "#22E5FF",
  orange: "#FF9F1C",
  magenta: "#FF3EA5",
  green: "#3DFF9A",
  text: "#F4F7FB",
  dim: "#93A0B4",
};

export const ease = Easing.bezier(0.16, 1, 0.3, 1);
export const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
export const TRANSITION = 15; // keep in sync with scripts/tts.mjs

export type Lang = "en" | "tr";
