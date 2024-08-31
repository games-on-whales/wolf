import { SessionsList } from "./features/sessions/sessionsList";
import "./app.css";
import { PairingRequests } from "./features/pairing/pairingRequests";
import { Typography } from "@mui/joy";
import { SectionsList } from "./features/sections/list";

function App() {
  return (
    <main>
      <Typography level="h1">Wolf Admin Dashboard</Typography>
      <SectionsList>
        <PairingRequests />
        <SessionsList />
      </SectionsList>
    </main>
  );
}

export default App;
