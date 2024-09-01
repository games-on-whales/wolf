import { SessionsList } from "./features/sessions/sessionsList";
import "./app.css";
import { PairingRequests } from "./features/pairing/pairingRequests";
import { Typography } from "@mui/joy";
import { SectionsList } from "./features/sections/list";
import { ImagesList } from "./features/images/imagesList";
import { UsersList } from "./features/users/usersList";

function App() {
  return (
    <main>
      <h1></h1>
      <Typography level="h1">Welcome, Fernie!</Typography>
      <SectionsList>
        <PairingRequests />
        <SessionsList />
        <UsersList />
        <ImagesList />
      </SectionsList>
    </main>
  );
}

export default App;
