import { FC } from "react";
import {
  Session,
  useGetApiSessionsQuery,
} from "../backend/wolfBackend.generated";
import { Section } from "../sections/section";

export const SessionsList: FC = () => {
  const { data: sessionsList, isLoading } = useGetApiSessionsQuery();

  return (
    <Section title="Sessions List">
      {sessionsList?.map((session: Session) => (
        <div key={session.id}>
          <p>Client: {session.clientName}</p>
          <p>App: {session.runningAppTitle}</p>
        </div>
      )) || "There are no active sessions at this time."}
    </Section>
  );
};
