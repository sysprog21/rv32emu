// Pthread builds require SharedArrayBuffer. On GitHub Pages, the COI service
// worker reloads the first visit with isolation headers; do not start the
// runtime in the original, unisolated document.
if (window.crossOriginIsolated) {
  const script = document.createElement("script");
  script.src = "rv32emu.js";
  script.onerror = () =>
    ProgressiveLoader.setProgress(0, "Error", "Failed to load rv32emu.js");
  document.body.appendChild(script);
} else if (!window.isSecureContext || !navigator.serviceWorker) {
  ProgressiveLoader.setProgress(
    0,
    "Error",
    "Use HTTPS or localhost with service workers enabled",
  );
} else {
  ProgressiveLoader.setProgress(
    0,
    "Enabling cross-origin isolation...",
    "Waiting for the service worker to reload the page",
  );
  // A working service worker reloads the page, discarding this timer. If it
  // fails to register or gives up (e.g. in private browsing), say so rather
  // than waiting forever.
  setTimeout(
    () =>
      ProgressiveLoader.setProgress(
        0,
        "Error",
        "Cross-origin isolation failed; service workers may be blocked",
      ),
    10000,
  );
}
