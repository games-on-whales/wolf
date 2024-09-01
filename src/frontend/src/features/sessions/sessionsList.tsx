import { FC } from "react";
import {
  Session,
  useGetApiSessionsQuery,
} from "../backend/wolfBackend.generated";
import { Section } from "../sections/section";

export const SessionsList: FC = () => {
  const { data: sessionsList, isLoading } = useGetApiSessionsQuery();

  console.log({ sessionsList, isLoading });

  return (
    <Section title="Sessions List">
      {sessionsList?.length !== 0
        ? sessionsList?.map((session: Session) => (
            <div key={session.id}>
              <p>Client: {session.clientName}</p>
              {/* <p>App: {session.}</p> */}
            </div>
          ))
        : "There are no active sessions at this time."}
    </Section>
  );
};
