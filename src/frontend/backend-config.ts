import type { ConfigFile } from "@rtk-query/codegen-openapi";

export const backendConfig: ConfigFile = {
  schemaFile: "../api-server/api-server/bin/swagger.json",
  apiFile: "./src/features/backend/emptyApi.ts",
  apiImport: "emptyApi",
  outputFile: "./src/features/backend/wolfBackend.generated.ts",
  exportName: "wolfBackend",
  hooks: true,
};

export default backendConfig;
