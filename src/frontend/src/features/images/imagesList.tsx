import { FC } from "react";
import { Section } from "../sections/section";
import { useGetApiContainerImagesQuery } from "../backend/wolfBackend.generated";
import { AspectRatio, Card, CardContent, Chip, Typography } from "@mui/joy";

export const ImagesList: FC = () => {
  const { data: imagesList, isLoading } = useGetApiContainerImagesQuery();

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
                {image.state === "OutOfDate" ? (
                  <Chip variant="solid" color="warning" size="sm">
                    Update Required
                  </Chip>
                ) : (
                  <Chip variant="outlined" size="sm">
                    Up to date
                  </Chip>
                )}
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
