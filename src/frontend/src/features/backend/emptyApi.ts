import { createApi, fetchBaseQuery } from "@reduxjs/toolkit/query/react";

export const emptyApi = createApi({
  endpoints: () => ({}),
  baseQuery: fetchBaseQuery({
    baseUrl:
      process.env.NODE_ENV === "development" ? "http://localhost:5091" : "/api",
  }),
});
