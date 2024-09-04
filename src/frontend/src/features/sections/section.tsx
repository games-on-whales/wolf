import { Card, Divider, Link, Typography } from "@mui/joy";
import { FC, ReactNode } from "react";
import "./section.css";
import { FOCUS_SECTION_CLASS } from "../controller/navigation";
import { OVERLAY_SX } from "./overlay";

interface ISessionProps {
  title?: string;
  children: ReactNode;
}

export const Section: FC<ISessionProps> = ({ title, children }) => {
  const sectionSlug = title?.toLowerCase().replace(/\s/g, "-");
  return (
    <Card
      className={FOCUS_SECTION_CLASS}
      data-section-id={sectionSlug}
      variant="plain"
      sx={{
        marginY: "2em",
        ":focus-within": {
          boxShadow: "0 0 10px 5px var(--c1)",
        },
      }}
    >
      {title && <Typography level="h3">{title}</Typography>}
      <Divider orientation="horizontal" />
      {children}
      <Link
        sx={OVERLAY_SX}
        href={"#" + sectionSlug}
        overlay
        data-section-overlay
      />
    </Card>
  );
};
