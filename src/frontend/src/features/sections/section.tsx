import { Card, Divider, Link, Typography } from "@mui/joy";
import { FC, ReactNode } from "react";
import "./section.css";

interface ISessionProps {
  title?: string;
  children: ReactNode;
}

export const Section: FC<ISessionProps> = ({ title, children }) => {
  return (
    <Card
      variant="plain"
      sx={{
        marginY: "2em",
        border: "5px solid transparent",
        ":focus-within": {
          border: "5px solid",
          borderImage:
            "conic-gradient(from var(--angle), var(--c2), var(--c1) 0.1turn, var(--c1) 0.15turn, var(--c2) 0.25turn) 30",
          animation: "borderRotate var(--d) linear infinite forwards",
          boxShadow: "0 0 10px 5px var(--c1)",
        },
      }}
    >
      {title && <Typography level="h3">{title}</Typography>}
      <Divider orientation="horizontal" />
      {children}
      <Link
        sx={{
          ":focus-visible": {
            ":after": {
              border: "none",
              outline: "none",
              outlineOffset: "none",
            },
          },
        }}
        href="#title"
        overlay
      />
    </Card>
  );
};
