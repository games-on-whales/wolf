import React from "react";
import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { store } from "./app/store";
import App from "./App";
import reportWebVitals from "./reportWebVitals";
import "@fontsource/inter";
import { CssBaseline, CssVarsProvider, extendTheme } from "@mui/joy";

import "./features/controller/navigation";

const container = document.getElementById("root")!;
const root = createRoot(container);

const theme = extendTheme({
  radius: {
    xl: "0",
    lg: "0",
    md: "0",
    sm: "0",
    xs: "0",
  },
});

root.render(
  <React.StrictMode>
    <Provider store={store}>
      <CssVarsProvider theme={theme} defaultMode="dark" disableNestedContext>
        <CssBaseline />
        <App />
      </CssVarsProvider>
    </Provider>
  </React.StrictMode>
);

// If you want to start measuring performance in your app, pass a function
// to log results (for example: reportWebVitals(console.log))
// or send to an analytics endpoint. Learn more: https://bit.ly/CRA-vitals
reportWebVitals();
