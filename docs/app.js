(function () {
  "use strict";

  /* ========================================================================
     Theme Persistence (Dark-First with Light Toggle)
     ======================================================================== */
  var root = document.documentElement;
  var themeBtn = document.getElementById("theme-toggle");

  function applyTheme(theme) {
    root.setAttribute("data-theme", theme);
    if (themeBtn) {
      themeBtn.textContent = theme === "light" ? "Dark Mode" : "Light Mode";
      themeBtn.setAttribute("aria-pressed", theme === "light" ? "true" : "false");
    }
    try {
      localStorage.setItem("winrpa-theme", theme);
    } catch (_) {
      /* ignore storage access issues */
    }
  }

  var savedTheme = null;
  try {
    savedTheme = localStorage.getItem("winrpa-theme");
  } catch (_) {
    savedTheme = null;
  }
  applyTheme(savedTheme === "light" ? "light" : "dark");

  if (themeBtn) {
    themeBtn.addEventListener("click", function () {
      var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
      applyTheme(next);
    });
  }

  /* ========================================================================
     Mobile Navigation Menu
     ======================================================================== */
  var navToggle = document.getElementById("nav-toggle");
  var navLinks = document.getElementById("nav-links");
  if (navToggle && navLinks) {
    navToggle.addEventListener("click", function () {
      var open = navLinks.classList.toggle("open");
      navToggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
    navLinks.querySelectorAll("a").forEach(function (link) {
      link.addEventListener("click", function () {
        navLinks.classList.remove("open");
        navToggle.setAttribute("aria-expanded", "false");
      });
    });
  }

  /* ========================================================================
     Single-Pass R Syntax Highlighter & Copy Helper
     ======================================================================== */
  function escapeHtml(str) {
    return str
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  var R_TOKEN_RE = /(#[^\n]*)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(\|>)|(\brpa_[a-z0-9_]+\b)|\b(library|function|if|else|for|repeat|return|in|tryCatch|try|stopifnot|on\.exit|TRUE|FALSE|NULL|NA)\b|(\b\d+(?:\.\d+)?L?\b)/g;

  function highlightR(code) {
    var out = [];
    var lastIndex = 0;
    R_TOKEN_RE.lastIndex = 0;
    var match;
    while ((match = R_TOKEN_RE.exec(code)) !== null) {
      if (match.index > lastIndex) {
        out.push(escapeHtml(code.slice(lastIndex, match.index)));
      }
      var token = match[0];
      var escaped = escapeHtml(token);
      if (match[1]) {
        out.push('<span class="tok-comment">' + escaped + "</span>");
      } else if (match[2]) {
        out.push('<span class="tok-string">' + escaped + "</span>");
      } else if (match[3]) {
        out.push('<span class="tok-pipe">' + escaped + "</span>");
      } else if (match[4]) {
        out.push('<span class="tok-rpa">' + escaped + "</span>");
      } else if (match[5]) {
        out.push('<span class="tok-kw">' + escaped + "</span>");
      } else if (match[6]) {
        out.push('<span class="tok-num">' + escaped + "</span>");
      }
      lastIndex = R_TOKEN_RE.lastIndex;
    }
    if (lastIndex < code.length) {
      out.push(escapeHtml(code.slice(lastIndex)));
    }
    return out.join("");
  }

  function bindCopyButton(btn, getTextFn) {
    btn.addEventListener("click", function () {
      var text = getTextFn();
      if (!navigator.clipboard) {
        return;
      }
      navigator.clipboard.writeText(text).then(function () {
        var orig = btn.textContent;
        btn.textContent = "Copied!";
        btn.classList.add("copied");
        setTimeout(function () {
          btn.textContent = orig;
          btn.classList.remove("copied");
        }, 1600);
      });
    });
  }

  document.querySelectorAll("[data-copy-target]").forEach(function (btn) {
    var targetId = btn.getAttribute("data-copy-target");
    var el = document.getElementById(targetId);
    if (el) {
      bindCopyButton(btn, function () {
        return el.textContent || "";
      });
    }
  });

  document.querySelectorAll("pre code.lang-r").forEach(function (codeEl) {
    codeEl.innerHTML = highlightR(codeEl.textContent || "");
  });

  /* ========================================================================
     Interactive Workflow Recipes Tabs
     ======================================================================== */
  var WORKFLOWS = [
    {
      id: "notepad",
      label: "1. Live Notepad Typing",
      title: "Launch, Locate Document & Multi-Mode Typing",
      summary: "Uses rpa_launch() (visible window by default), discovers the Windows 11 Notepad RichEditDocument control, types across Unicode/scancode/paste modes, and snaps a 24-bit BMP.",
      bullets: [
        "rpa_launch() avoids base::system2()'s Windows default invisible = TRUE flag.",
        "rpa_type() defaults to delay_ms = 5L with 50ms focus settle and grouped UTF-16 surrogate pairs.",
        "rpa_screenshot() uses PrintWindow(PW_RENDERFULLCONTENT) with automatic BitBlt fallback."
      ],
      filename: "workflow_notepad.R",
      code:
        'library(winrpa)\n\n' +
        '# 1. Launch Notepad cleanly (visible window)\n' +
        'rpa_launch("notepad.exe")\n' +
        'win <- rpa_wait_for(\n' +
        '  function() tryCatch(rpa_window(class_name = "Notepad"), error = function(e) NULL),\n' +
        '  timeout = 8, interval = 0.05, label = "Notepad window"\n' +
        ')\n\n' +
        '# 2. Locate Document (Win11) or classic Edit control\n' +
        'editor <- tryCatch(\n' +
        '  rpa_find(win, control_type = "Document"),\n' +
        '  error = function(e) rpa_find(win, control_type = "Edit")\n' +
        ')\n\n' +
        '# 3. Pipe across Unicode, Hardware Scancode, and Paste modes\n' +
        'editor |>\n' +
        '  rpa_type("Hello from winrpa! (Unicode: caf\\u00e9 \\u65e5\\u672c\\u8a9e)\\n", mode = "unicode") |>\n' +
        '  rpa_type("Hardware scancode test 123\\n", mode = "scancode", delay_ms = 5L) |>\n' +
        '  rpa_type("Transactional paste OK\\n", mode = "paste")\n\n' +
        '# 4. Capture 24-bit BMP window screenshot\n' +
        'bmp <- tempfile(pattern = "notepad_", fileext = ".bmp")\n' +
        'rpa_screenshot(win, path = bmp)'
    },
    {
      id: "clipboard",
      label: "2. Clipboard Transaction",
      title: "Non-Destructive Clipboard Borrowing",
      summary: "Borrow the Windows clipboard to transfer bulk or Unicode text without wiping out what the user previously copied — guaranteed restoration even if an error is thrown.",
      bullets: [
        "ClipboardGuard retries OpenClipboard() up to 5 times (15ms backoff) to survive Excel/RDP locks.",
        "rpa_with_clipboard() registers on.exit() immediately after snapshotting CF_UNICODETEXT.",
        "rpa_wait_for_clipboard() polls the monotonic OS sequence counter with <50ms abort responsiveness."
      ],
      filename: "workflow_clipboard.R",
      code:
        'library(winrpa)\n\n' +
        'rpa_clipboard_set("my original clipboard text")\n\n' +
        'inside <- rpa_with_clipboard({\n' +
        '  # Clipboard safely holds the temporary payload inside this scope\n' +
        '  rpa_clipboard_get()\n' +
        '}, text = "temporary RPA payload")\n\n' +
        'stopifnot(\n' +
        '  identical(inside, "temporary RPA payload"),\n' +
        '  identical(rpa_clipboard_get(), "my original clipboard text")\n' +
        ')'
    },
    {
      id: "killswitch",
      label: "3. Kill-Switch & Hook",
      title: "Emergency OS Hotkey + Lock-Free Key Telemetry",
      summary: "Register a system-wide Ctrl+Alt+F12 abort hotkey on a dedicated message-pump thread and optionally stream hardware keystrokes through a lock-free ring buffer.",
      bullets: [
        "Pressing Ctrl+Alt+F12 flips std::atomic<bool> g_abort_requested immediately.",
        "All rpa_wait_for() polls and SendInput sleeps unwind safely within <=50ms.",
        "WH_KEYBOARD_LL captures vk_code, scan_code, key_up, and timestamp_ms into an R data.frame."
      ],
      filename: "workflow_safety.R",
      code:
        'library(winrpa)\n\n' +
        'rpa_reset_abort()\n' +
        'rpa_enable_killswitch(modifiers = c("ctrl", "alt"), key = "F12")\n\n' +
        '# Press Ctrl+Alt+F12 during any wait to abort cleanly\n' +
        'tryCatch(\n' +
        '  rpa_wait_for(function() FALSE, timeout = 10, label = "killswitch test"),\n' +
        '  error = function(e) message("Caught abort: ", conditionMessage(e))\n' +
        ')\n' +
        'rpa_abort_triggered()   # TRUE\n' +
        'rpa_reset_abort()\n' +
        'rpa_disable_killswitch()\n\n' +
        '# Drain hardware keyboard events from WH_KEYBOARD_LL ring buffer\n' +
        'rpa_hook_start()\n' +
        'Sys.sleep(3)\n' +
        'events <- rpa_hook_drain()\n' +
        'rpa_hook_stop()\n' +
        'print(events)'
    },
    {
      id: "tables",
      label: "4. UIA Tree & Tables",
      title: "Inspect Hierarchy & Extract Grids to data.frame",
      summary: "Traverse UI Automation trees with bounded depth/node caps and pull entire desktop DataGrids directly into R data.frames via GridPattern and TablePattern.",
      bullets: [
        "rpa_inspect_tree() avoids runaway tree scans on complex Chromium/Electron windows.",
        "rpa_read_table() extracts column headers cleanly and normalizes unique column names.",
        "rpa_selector() supports exact or regex matches over Name, AutomationId, ControlType, and ClassName."
      ],
      filename: "workflow_tree_tables.R",
      code:
        'library(winrpa)\n\n' +
        'wins <- rpa_windows()\n' +
        'win  <- rpa_window(pid = wins$pid[1L])\n\n' +
        '# Snapshot bounded Control View hierarchy\n' +
        'tree <- rpa_inspect_tree(win, max_depth = 3L, max_nodes = 50L)\n' +
        'head(tree[, c("node_id", "depth", "control_type", "name", "automation_id")], 15)\n\n' +
        '# Compose reusable selectors & extract DataGrids directly\n' +
        'grid_sel <- rpa_selector(control_type = "DataGrid")\n' +
        'grid_el  <- tryCatch(rpa_find(win, selector = grid_sel), error = function(e) NULL)\n' +
        'if (!is.null(grid_el)) {\n' +
        '  df <- rpa_read_table(grid_el)\n' +
        '  str(df)\n' +
        '}'
    },
    {
      id: "dpi",
      label: "5. Multi-Monitor DPI",
      title: "VirtualDesk Negative Coordinates & Per-Monitor DPI",
      summary: "Query monitor geometry across mixed-DPI scaling setups and normalize mouse targets spanning negative coordinates on secondary left/top monitors.",
      bullets: [
        "rpa_try_enable_dpi_awareness() safely handles E_ACCESSDENIED inside DPI-locked IDEs like RStudio.",
        "rpa_display_metrics() reports GetDpiForMonitor() per screen plus total virtual desktop bounds.",
        "rpa_mouse_move() / rpa_mouse_click() normalize coordinates onto 0..65535 MOUSEEVENTF_VIRTUALDESK."
      ],
      filename: "workflow_dpi.R",
      code:
        'library(winrpa)\n\n' +
        'rpa_try_enable_dpi_awareness()\n' +
        'metrics <- rpa_display_metrics()\n' +
        'metrics$dpi_awareness\n' +
        'metrics$monitors\n\n' +
        '# Pure math normalization works identically on Windows, Linux, and macOS\n' +
        'rpa_normalize_virtual_coords(\n' +
        '  -960L, 540L,\n' +
        '  vx = -1920L, vy = 0L, vw = 3840L, vh = 1080L\n' +
        ')\n' +
        '# norm_x = 16388, norm_y = 32798'
    },
    {
      id: "diagnostics",
      label: "6. Debug & Lifecycle",
      title: "Forensic Timeout Dumps & Zero-Hang Reload",
      summary: "Capture diagnostic .log files (and optional pruned UIA tree + window .bmp) automatically whenever a wait times out, and unload/reload the native DLL cleanly.",
      bullets: [
        "Set options(winrpa.debug_dir = ...) to persist forensic artifacts on failure.",
        "reg.finalizer(..., onexit = TRUE) tears down MTA COM threads prior to Windows ExitProcess().",
        "MtaComWorker restarts lazily on the next call after pkgload::unload() or unloadNamespace()."
      ],
      filename: "workflow_diagnostics.R",
      code:
        'library(winrpa)\n\n' +
        'dbg <- tempfile("winrpa_dbg_")\n' +
        'old_opt <- options(winrpa.debug_dir = dbg)\n\n' +
        'try(rpa_wait_for(\n' +
        '  function() FALSE,\n' +
        '  timeout = 0.2, interval = 0.05,\n' +
        '  label = "demo timeout"\n' +
        '))\n' +
        'list.files(dbg)  # "winrpa_timeout_YYYYMMDD_HHMMSS.log"\n\n' +
        'options(old_opt)\n' +
        'unlink(dbg, recursive = TRUE)\n\n' +
        '# Clean DLL unload + reload restarts the MTA COM worker immediately\n' +
        'unloadNamespace("winrpa")\n' +
        'library(winrpa)\n' +
        'nrow(rpa_windows())'
    },
    {
      id: "webcdp",
      label: "7. Headless Web CDP",
      title: "CSS/XPath, Network-Idle Waits, Upload & PNG (Headless)",
      summary: "Automate Edge or Chrome directly via native C++17 WinHTTP WebSockets without hijacking the mouse or requiring an unlocked screen, and bridge to UIA when OS dialogs appear.",
      bullets: [
        "CSS & XPath selectors with trusted Input.dispatchMouseEvent and Input.insertText.",
        "Tracks Page.domContentEventFired, Page.loadEventFired, and in-flight XHR/fetch requests.",
        "Populates <input type='file'> headlessly and writes lossless PNGs via pure C++ Base64 decode."
      ],
      filename: "workflow_web_cdp.R",
      code:
        'library(winrpa)\n\n' +
        '# Runs cleanly in background headless mode with zero extra R deps\n' +
        'sess <- rpa_web_launch(browser = "auto", headless = TRUE)\n' +
        'on.exit(rpa_web_close(sess), add = TRUE)\n\n' +
        'rpa_web_goto(sess, "https://example.org", wait = "load")\n' +
        'rpa_web_wait_network_idle(sess, idle_ms = 500L, timeout = 15)\n\n' +
        'rpa_web_type(sess, "winrpa CDP", css = "#search-input", clear = TRUE)\n' +
        'rpa_web_click(sess, xpath = "//button[@type=\'submit\']")\n\n' +
        'heading <- rpa_web_text(sess, css = "h1")\n' +
        'rpa_web_screenshot(sess, path = "page.png", full_page = TRUE)'
    },
    {
      id: "recorder",
      label: "8. Script Recorder",
      title: "Record Desktop UIA & Browser DOM Flows Into Clean R Code",
      summary: "Capture human desktop interactions via lock-free WH_MOUSE_LL + WH_KEYBOARD_LL -> MTA ElementFromPoint resolution, or record browser flows via CDP Runtime.addBinding with automatic password redaction and selector ranking.",
      bullets: [
        "Desktop clicks resolve on WM_LBUTTONDOWN before popups close; RStudio/parent PIDs filter out cleanly.",
        "Typo-aware key coalescing folds in-burst Backspace and emits Sys.getenv('WINRPA_SECRET') for password fields.",
        "rpa_web_record_start() ranks CSS selectors (#id > data-testid > name > aria-label > path) across page navigations."
      ],
      filename: "workflow_recorder.R",
      code:
        'library(winrpa)\n\n' +
        '# A. Desktop UIA + SendInput Recorder\n' +
        'rpa_record_start(ignore_self = TRUE, redact_passwords = TRUE)\n' +
        '# Click & type in any Windows app; press Ctrl+Shift+F9 or call stop:\n' +
        'desktop_rec <- rpa_record_stop(file = "notepad_recorded.R")\n' +
        'print(desktop_rec)\n\n' +
        '# B. Web Browser CDP DOM Recorder\n' +
        'sess <- rpa_web_launch(headless = FALSE, url = "https://example.org")\n' +
        'rpa_web_record_start(sess, redact_passwords = TRUE)\n' +
        '# Interact with the page, then stop and emit clean rpa_web_*() R code:\n' +
        'web_rec <- rpa_web_record_stop(sess, file = "web_recorded.R")\n' +
        'rpa_web_close(sess)'
    }
  ];

  var tabsBar = document.getElementById("workflow-tabs");
  var wfTitle = document.getElementById("wf-title");
  var wfSummary = document.getElementById("wf-summary");
  var wfBullets = document.getElementById("wf-bullets");
  var wfFilename = document.getElementById("wf-filename");
  var wfCode = document.getElementById("wf-code");
  var wfCopyBtn = document.getElementById("wf-copy-btn");
  var currentWorkflowCode = "";

  function renderWorkflow(idx) {
    var wf = WORKFLOWS[idx];
    if (!wf) return;
    currentWorkflowCode = wf.code;
    wfTitle.textContent = wf.title;
    wfSummary.textContent = wf.summary;
    wfFilename.textContent = wf.filename;
    wfCode.innerHTML = highlightR(wf.code);
    wfBullets.innerHTML = wf.bullets.map(function (b) {
      return "<li>" + escapeHtml(b) + "</li>";
    }).join("");
    tabsBar.querySelectorAll(".tab-btn").forEach(function (b, i) {
      b.classList.toggle("active", i === idx);
      b.setAttribute("aria-selected", i === idx ? "true" : "false");
    });
  }

  if (tabsBar && wfCode) {
    WORKFLOWS.forEach(function (wf, idx) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "tab-btn" + (idx === 0 ? " active" : "");
      btn.textContent = wf.label;
      btn.setAttribute("role", "tab");
      btn.addEventListener("click", function () {
        renderWorkflow(idx);
      });
      tabsBar.appendChild(btn);
    });
    bindCopyButton(wfCopyBtn, function () {
      return currentWorkflowCode;
    });
    renderWorkflow(0);
  }

  /* ========================================================================
     Searchable 87-Entry API Explorer
     ======================================================================== */
  var apiList = Array.isArray(window.WINRPA_API) ? window.WINRPA_API : [];
  var apiSearch = document.getElementById("api-search");
  var apiCount = document.getElementById("api-count");
  var apiPills = document.getElementById("api-cat-pills");
  var apiGrid = document.getElementById("api-grid");
  var activeCategory = "All";

  function getCategories() {
    var seen = { All: apiList.length };
    var order = ["All"];
    apiList.forEach(function (item) {
      if (!seen[item.category]) {
        seen[item.category] = 0;
        order.push(item.category);
      }
      seen[item.category] += 1;
    });
    return { order: order, counts: seen };
  }

  function renderCategoryPills() {
    if (!apiPills) return;
    var cats = getCategories();
    apiPills.innerHTML = "";
    cats.order.forEach(function (cat) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "cat-pill" + (cat === activeCategory ? " active" : "");
      btn.textContent = cat + " (" + cats.counts[cat] + ")";
      btn.addEventListener("click", function () {
        activeCategory = cat;
        renderCategoryPills();
        renderApiCards();
      });
      apiPills.appendChild(btn);
    });
  }

  function renderApiCards() {
    if (!apiGrid) return;
    var query = (apiSearch ? apiSearch.value : "").trim().toLowerCase();
    var filtered = apiList.filter(function (item) {
      var catOk = activeCategory === "All" || item.category === activeCategory;
      if (!catOk) return false;
      if (!query) return true;
      var haystack = [
        item.name,
        item.category,
        item.signature,
        item.summary,
        item.returns,
        (item.tags || []).join(" ")
      ].join(" ").toLowerCase();
      return haystack.indexOf(query) !== -1;
    });

    if (apiCount) {
      apiCount.textContent = "Showing " + filtered.length + " of " + apiList.length + " documented symbols";
    }

    if (filtered.length === 0) {
      apiGrid.innerHTML = '<div class="api-card"><p class="api-desc">No matching functions found. Try clearing the search filter.</p></div>';
      return;
    }

    apiGrid.innerHTML = "";
    filtered.forEach(function (item, i) {
      var card = document.createElement("article");
      card.className = "api-card";
      card.id = "api-" + item.name;

      var tagsHtml = (item.tags || []).map(function (t) {
        return '<span class="tag">' + escapeHtml(t) + "</span>";
      }).join("");

      var exampleId = "api-ex-" + i;
      card.innerHTML =
        '<div class="api-head">' +
          '<a class="api-name mono" href="#api-' + escapeHtml(item.name) + '">' + escapeHtml(item.name) + "</a>" +
          '<span class="api-cat">' + escapeHtml(item.category) + "</span>" +
        "</div>" +
        '<div class="api-sig mono">' + escapeHtml(item.signature) + "</div>" +
        '<p class="api-desc">' + escapeHtml(item.summary) + "</p>" +
        '<p class="api-returns"><strong>Returns:</strong> ' + escapeHtml(item.returns) + "</p>" +
        '<div class="api-footer">' +
          '<div class="tag-list">' + tagsHtml + "</div>" +
          '<button type="button" class="btn-example" aria-expanded="false">Show example</button>' +
        "</div>" +
        '<div class="api-example-drawer" hidden>' +
          '<div class="code-window">' +
            '<div class="code-window-bar">' +
              '<span class="code-title">Example — ' + escapeHtml(item.name) + "</span>" +
              '<button type="button" class="btn-copy" data-ex-id="' + exampleId + '">Copy</button>' +
            "</div>" +
            "<pre><code id=\"" + exampleId + "\">" + highlightR(item.example) + "</code></pre>" +
          "</div>" +
        "</div>";

      var exBtn = card.querySelector(".btn-example");
      var drawer = card.querySelector(".api-example-drawer");
      exBtn.addEventListener("click", function () {
        var isHidden = drawer.hasAttribute("hidden");
        if (isHidden) {
          drawer.removeAttribute("hidden");
          exBtn.textContent = "Hide example";
          exBtn.setAttribute("aria-expanded", "true");
        } else {
          drawer.setAttribute("hidden", "");
          exBtn.textContent = "Show example";
          exBtn.setAttribute("aria-expanded", "false");
        }
      });

      var copyBtn = card.querySelector(".btn-copy");
      bindCopyButton(copyBtn, function () {
        return item.example;
      });

      apiGrid.appendChild(card);
    });
  }

  if (apiList.length > 0) {
    renderCategoryPills();
    renderApiCards();
    if (apiSearch) {
      apiSearch.addEventListener("input", renderApiCards);
    }
  }

  /* ========================================================================
     Screenshot Lightbox Modal
     ======================================================================== */
  var lightbox = document.getElementById("lightbox");
  var lbImg = document.getElementById("lb-img");
  var lbTitle = document.getElementById("lb-title");
  var lbCaption = document.getElementById("lb-caption");
  var lbClose = document.getElementById("lb-close");
  var lbPrev = document.getElementById("lb-prev");
  var lbNext = document.getElementById("lb-next");
  var galleryItems = Array.prototype.slice.call(document.querySelectorAll("[data-lightbox]"));
  var lbIndex = 0;

  function showSlide(idx) {
    if (galleryItems.length === 0) return;
    lbIndex = (idx + galleryItems.length) % galleryItems.length;
    var el = galleryItems[lbIndex];
    var img = el.querySelector("img");
    lbImg.src = img ? img.getAttribute("src") : "";
    lbImg.alt = img ? (img.getAttribute("alt") || "") : "";
    lbTitle.textContent = el.getAttribute("data-title") || "";
    lbCaption.textContent = el.getAttribute("data-caption") || "";
  }

  function openLightbox(idx) {
    if (!lightbox) return;
    showSlide(idx);
    lightbox.classList.add("open");
    lightbox.setAttribute("aria-hidden", "false");
    document.body.style.overflow = "hidden";
  }

  function closeLightbox() {
    if (!lightbox) return;
    lightbox.classList.remove("open");
    lightbox.setAttribute("aria-hidden", "true");
    document.body.style.overflow = "";
  }

  galleryItems.forEach(function (el, idx) {
    el.addEventListener("click", function () {
      openLightbox(idx);
    });
    el.addEventListener("keydown", function (e) {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        openLightbox(idx);
      }
    });
  });

  if (lbClose) lbClose.addEventListener("click", closeLightbox);
  if (lbPrev) lbPrev.addEventListener("click", function () { showSlide(lbIndex - 1); });
  if (lbNext) lbNext.addEventListener("click", function () { showSlide(lbIndex + 1); });
  if (lightbox) {
    lightbox.addEventListener("click", function (e) {
      if (e.target === lightbox) closeLightbox();
    });
  }
  document.addEventListener("keydown", function (e) {
    if (!lightbox || !lightbox.classList.contains("open")) return;
    if (e.key === "Escape") closeLightbox();
    if (e.key === "ArrowLeft") showSlide(lbIndex - 1);
    if (e.key === "ArrowRight") showSlide(lbIndex + 1);
  });
})();
