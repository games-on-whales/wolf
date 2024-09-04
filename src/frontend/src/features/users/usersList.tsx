import { mdiAccountOutline, mdiAccountPlus } from "@mdi/js";
import Icon from "@mdi/react";
import { AspectRatio, Card, CardContent, Link } from "@mui/joy";
import { FC, ReactNode } from "react";
import { useGetApiUsersQuery } from "../backend/wolfBackend.generated";
import { OVERLAY_SX } from "../sections/overlay";
import { Section } from "../sections/section";

const IMAGE_SIZE = 128;

export const UsersList: FC = () => {
  const { data: usersList } = useGetApiUsersQuery();

  return (
    <Section title="Users">
      <CardContent orientation="horizontal">
        {usersList?.length === 0
          ? "There are no users on this server"
          : usersList?.map((user, index) => (
              <Card
                orientation="vertical"
                variant="outlined"
                key={index}
                sx={{ alignItems: "center" }}
              >
                <UserAspectRatio>
                  {user.profileImage != null ? (
                    <img src={user.profileImage} alt={user.name + "Avatar"} />
                  ) : (
                    <Icon path={mdiAccountOutline} size="64px" />
                  )}
                </UserAspectRatio>
                <Link href={"#"} overlay sx={OVERLAY_SX}>
                  {user.name}
                </Link>
              </Card>
            ))}
        <Card
          orientation="vertical"
          variant="outlined"
          sx={{ alignItems: "center" }}
        >
          <UserAspectRatio>
            <UserIcon path={mdiAccountPlus} />
          </UserAspectRatio>
          <Link href={"#"} overlay sx={OVERLAY_SX}>
            Add User
          </Link>
        </Card>
      </CardContent>
    </Section>
  );
};

const UserAspectRatio: FC<{ children: ReactNode }> = ({ children }) => {
  return (
    <AspectRatio ratio={1} sx={{ width: `${IMAGE_SIZE}px` }}>
      {children}
    </AspectRatio>
  );
};

const UserIcon: FC<{ path: string }> = ({ path }) => {
  const iconWidth = IMAGE_SIZE * 0.5;
  return (
    <Icon
      path={path}
      size={iconWidth + "px"}
      style={{ left: iconWidth / 2, top: iconWidth / 2, position: "absolute" }}
    />
  );
};
