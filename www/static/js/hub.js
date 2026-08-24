// Served as application/javascript by the `types` block in the config.
// Its only job is to prove the script was fetched and executed.
document.addEventListener("DOMContentLoaded", function () {
  var slot = document.getElementById("js-probe");
  if (!slot) {
    return;
  }
  slot.textContent =
    "/static/js/hub.js loaded and ran at " + new Date().toLocaleTimeString();
  slot.classList.add("ok");
});
