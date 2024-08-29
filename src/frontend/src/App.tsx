import { Flex, Space, Typography } from "antd";
import { SessionsList } from "./features/sessions/sessionsList";
import "./app.css";
import { PairingRequests } from "./features/pairing/pairingRequests";

const { Title } = Typography;

function App() {
  return (
    <main>
      <Space direction="vertical" size="large" style={{ display: "flex" }}>
        <Title level={2}>Wolf Admin Dashboard</Title>
        <PairingRequests />
        <SessionsList />
      </Space>
    </main>
  );
}

export default App;
