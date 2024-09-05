import { mdiAccountOutline, mdiAccountPlus } from "@mdi/js";
import Icon from "@mdi/react";
import { AspectRatio, Card, CardContent, Link } from "@mui/joy";
import type { FC } from "react";
import { useGetApiUsersQuery } from "../backend/wolfBackend.generated";
import { OVERLAY_SX } from "../sections/overlay";
import { Section } from "../sections/section";

export const UsersList: FC = () => {
  const { data: usersList } = useGetApiUsersQuery();

  return (
    <Section title="Users">
      <CardContent orientation="horizontal">
        {usersList?.length === 0
          ? "There are no users on this server"
          : usersList?.map((user, index) => (
              <UserView
                key={index}
                profileImage={user.profileImage}
                fallbackIcon={mdiAccountOutline}
                name={user.name ?? "Unknown"}
              />
            ))}
        <UserView
          profileImage={null}
          fallbackIcon={mdiAccountPlus}
          name="Add User"
        />
      </CardContent>
    </Section>
  );
};

interface IUserViewProps {
  profileImage?: string | null;
  fallbackIcon: string;
  name: string;
}
const UserView: FC<IUserViewProps> = ({ profileImage, fallbackIcon, name }) => {
  const imageSize = 128;
  const iconWidth = imageSize * 0.5;
  return (
    <Card
      orientation="vertical"
      variant="outlined"
      sx={{ alignItems: "center" }}
    >
      <AspectRatio ratio={1} sx={{ width: `${imageSize}px` }}>
        {profileImage != null ? (
          <img src={profileImage} alt={name + "Avatar"} />
        ) : (
          <Icon
            path={fallbackIcon}
            size={iconWidth + "px"}
            style={{
              left: iconWidth / 2,
              top: iconWidth / 2,
              position: "absolute",
            }}
          />
        )}
      </AspectRatio>
      <Link href={"#"} overlay sx={OVERLAY_SX}>
        {name}
      </Link>
    </Card>
  );
};
