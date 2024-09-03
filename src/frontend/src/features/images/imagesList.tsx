import { AspectRatio, Card, CardContent, Chip, Link } from "@mui/joy";
import { FC } from "react";
import { useGetApiContainerImagesQuery } from "../backend/wolfBackend.generated";
import { Section } from "../sections/section";

export const ImagesList: FC = () => {
  const { data: imagesList } = useGetApiContainerImagesQuery();

  return (
    <Section title="Images & Updates">
      <CardContent orientation="horizontal">
        {imagesList?.length === 0
          ? "There are no images on this server"
          : imagesList?.map((image) => (
              <Card
                orientation="vertical"
                variant="outlined"
                key={image.id}
                sx={{ alignItems: "center" }}
              >
                <Link href={"#" + image.id} overlay>
                  {image.state === "OutOfDate" ? (
                    <Chip variant="solid" color="warning" size="sm">
                      Update Required
                    </Chip>
                  ) : (
                    <Chip variant="outlined" size="sm">
                      Up to date
                    </Chip>
                  )}
                </Link>
                {image.art && (
                  <AspectRatio sx={{ width: "128px" }} ratio={2 / 3}>
                    <img src={image.art} alt={image.name + "Boxart"} />
                  </AspectRatio>
                )}
              </Card>
            ))}
      </CardContent>
    </Section>
  );
};
