export const OVERLAY_SX = {
  ":focus-visible": {
    ":after": {
      border: "5px solid",
      borderImage:
        "conic-gradient(from var(--angle), var(--c2), var(--c1) 0.1turn, var(--c1) 0.15turn, var(--c2) 0.25turn) 30",
      animation: "borderRotate var(--d) linear infinite forwards",
      outline: "none",
      outlineOffset: "none",
    },
  },
};
