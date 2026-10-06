import { Composition, Folder, Still } from "remotion";
import { Promo, totalFrames, type PromoProps } from "./Promo";
import { Thumbnail } from "./Thumbnail";

export const RemotionRoot: React.FC = () => {
  return (
    <>
      <Composition
        id="AuraPromoEN"
        component={Promo}
        durationInFrames={totalFrames("en")}
        fps={30}
        width={1920}
        height={1080}
        defaultProps={{ lang: "en" } as PromoProps}
      />
      <Composition
        id="AuraPromoTR"
        component={Promo}
        durationInFrames={totalFrames("tr")}
        fps={30}
        width={1920}
        height={1080}
        defaultProps={{ lang: "tr" } as PromoProps}
      />
      <Folder name="Thumbnails">
        <Still id="ThumbnailEN" component={Thumbnail} width={1280} height={720} defaultProps={{ lang: "en" as const }} />
        <Still id="ThumbnailTR" component={Thumbnail} width={1280} height={720} defaultProps={{ lang: "tr" as const }} />
      </Folder>
    </>
  );
};
