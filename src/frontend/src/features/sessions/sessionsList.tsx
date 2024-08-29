import { Card } from "antd";
import { FC } from "react";
import {
  Session,
  useGetApiSessionsQuery,
} from "../backend/wolfBackend.generated";

export const SessionsList: FC = () => {
  const { data: sessionsList, isLoading } = useGetApiSessionsQuery();

  return (
    <Card title="Sessions List" loading={isLoading} hoverable>
      {sessionsList?.map((session: Session) => (
        <div key={session.id}>
          <p>Client: {session.clientName}</p>
          <p>App: {session.runningAppTitle}</p>
        </div>
      )) || "There are no active sessions at this time."}
    </Card>
  );
};
