import { FC, ReactElement } from "react";
import { Section } from "./section";

interface ISectionsListProps {
  children: ReactElement<typeof Section>[];
}

export const SectionsList: FC<ISectionsListProps> = ({ children }) => {
  //   return <Sheet sx={{ marginY: "1em" }}>{children}</Sheet>;
  return <>{children}</>;
};
