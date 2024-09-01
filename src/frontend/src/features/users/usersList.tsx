import { FC } from "react";
import { Section } from "../sections/section";
import { useGetApiUsersQuery } from "../backend/wolfBackend.generated";
import { AspectRatio, Card, CardContent, Typography } from "@mui/joy";

export const UsersList: FC = () => {
  const { data: usersList, isLoading } = useGetApiUsersQuery();

  return (
    <Section title="Users">
      <CardContent orientation="horizontal">
        {usersList?.length === 0
          ? "There are no users on this server"
          : usersList?.map((user, index) => (
              <Card orientation="vertical" variant="outlined" key={index}>
                <AspectRatio ratio={1} sx={{ width: "128px" }}>
                  <img src={user.profileImage!} alt={user.name + "Avatar"} />
                </AspectRatio>
              </Card>
            ))}
        <Card orientation="vertical" variant="outlined">
          <AspectRatio ratio={1} sx={{ width: "128px" }}>
            <Typography level="h3" textAlign={"center"}>
              Add <br /> User
            </Typography>
          </AspectRatio>
        </Card>
      </CardContent>
    </Section>
  );
};
