import { FC, ReactElement } from "react";
import { Section } from "./section";

interface ISectionsListProps {
  children: ReactElement<typeof Section>[];
}

export const SectionsList: FC<ISectionsListProps> = ({ children }) => {
  return <>{children}</>;
};
