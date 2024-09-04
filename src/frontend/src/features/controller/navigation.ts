const DIRECTION_KEYS = [
  "ArrowUp",
  "ArrowDown",
  "ArrowLeft",
  "ArrowRight",
  "Escape",
];

export const FOCUS_SECTION_CLASS = "focus-section";

const keyDownListener = (event: KeyboardEvent) => {
  if (!DIRECTION_KEYS.includes(event.key)) {
    return;
  }

  event.preventDefault();

  const focusedElement = document.activeElement as HTMLElement;
  const focusedSection = focusedElement.closest(`.${FOCUS_SECTION_CLASS}`);

  const isSectionActive =
    focusedSection instanceof HTMLElement &&
    focusedSection.dataset.sectionId === window.location.hash.slice(1);

  let nextElement: Element | null;
  if (isSectionActive) {
    if (event.key === "Escape") {
      window.location.hash = "";
      nextElement = focusedSection.querySelector(`a[data-section-overlay]`);
    } else {
      const elements = focusedSection.querySelectorAll(
        "a[href]:not([data-section-overlay])"
      );
      nextElement = getNextFocusableElement(
        event.key,
        focusedElement,
        Array.from(elements)
      );
    }
  } else {
    // Clear the hash if the focus has somehow escaped the section
    if (window.location.hash) {
      window.location.hash = "";
    }

    const elements = document.querySelectorAll(
      `.${FOCUS_SECTION_CLASS} a[data-section-overlay]`
    );

    nextElement =
      focusedSection === null
        ? event.key === "ArrowUp"
          ? elements[elements.length - 1]
          : elements[0]
        : getNextFocusableElement(event.key, focusedElement, elements);
  }
  if (nextElement instanceof HTMLElement) {
    nextElement.focus();
  }
};

function getNextFocusableElement(
  direction: string,
  currentFocus: Element,
  elements: Iterable<Element>
): Element | null {
  // Get the bounding rectangle of the current focused element
  const focusRect = currentFocus.getBoundingClientRect();

  // Traverse the DOM in the specified direction
  let closestElement = null;
  let closestDistance = Infinity;

  for (const element of elements) {
    if (element === currentFocus) {
      continue;
    }

    const candidateRect = element.getBoundingClientRect();
    let distance = -1;

    if (direction === "ArrowDown") {
      distance = candidateRect.bottom - focusRect.top;
    } else if (direction === "ArrowUp") {
      distance = focusRect.bottom - candidateRect.top;
    } else if (direction === "ArrowLeft") {
      distance = focusRect.left - candidateRect.right;
    } else if (direction === "ArrowRight") {
      distance = candidateRect.left - focusRect.right;
    }

    // Check if the element is in the correct direction and closer than the previous closest element
    if (distance < closestDistance && distance > 1) {
      closestElement = element;
      closestDistance = distance;
    }
  }

  return closestElement;
}

const hashChangeHandler = () => {
  const sectionId = window.location.hash.slice(1);
  const section = document.querySelector(
    `[data-section-id="${sectionId}"] a[href]:not([data-section-overlay])`
  );
  if (section instanceof HTMLElement) {
    section.focus();
  }
};

window.addEventListener("hashchange", hashChangeHandler);
window.addEventListener("keydown", keyDownListener);

// Hot Module Replacement so the whole page doesn't reload
if (module.hot) {
  module.hot.dispose(() => {
    window.removeEventListener("keydown", keyDownListener);
    window.removeEventListener("hashchange", hashChangeHandler);
  });

  module.hot.accept();
}
