import { Card, Divider, Typography } from "@mui/joy";
import { FC, ReactNode } from "react";

interface ISessionProps {
  title?: string;
  children: ReactNode;
}

export const Section: FC<ISessionProps> = ({ title, children }) => {
  return (
    <Card sx={{ marginY: "2em" }}>
      {title && <Typography level="h3">{title}</Typography>}
      <Divider orientation="horizontal" />
      {children}
    </Card>
  );
};
