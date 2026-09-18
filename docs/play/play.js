// The page around pIRCIS in the browser: sizes the screen, starts the
// program, keeps what it saves, and passes pasted programs in.
(function () {
  var note = document.getElementById("play-note");
  var holder = document.getElementById("screen");
  var canvas = document.getElementById("canvas");
  function say(t) { note.textContent = t; note.hidden = !t; }

  // With ?debug in the address the page shows what pIRCIS prints and what
  // keys the browser reports, for finding out what a browser does differently.
  var debug = /[?&]debug/.test(location.search), logBox = null;
  var lines = [];
  function showLog() {
    if (logBox) return;
    logBox = document.createElement("textarea"); logBox.rows = 14; logBox.readOnly = true; logBox.id = "play-log"; logBox.style.marginTop = "1rem";
    holder.parentNode.insertBefore(logBox, document.getElementById("play-note"));
    logBox.value = lines.join("\n") + "\n";
  }
  function log(t) {
    var line = (performance.now() / 1000).toFixed(2) + "  " + t;
    if (lines.length < 400) lines.push(line);
    if (logBox) { logBox.value += line + "\n"; logBox.scrollTop = logBox.scrollHeight; }
  }
  if (debug) showLog();
  // A start that is taking far too long shows what it has got through, so
  // there is something to report.
  setTimeout(function () { if (!started) { say("pIRCIS is taking a long time to start. What it has done so far is below."); showLog(); } }, 8000);
  if (debug) ["keydown", "keypress", "keyup"].forEach(function (n) {
    window.addEventListener(n, function (e) {
      log("JS " + n + " key=" + e.key + " code=" + e.code + " keyCode=" + e.keyCode + " charCode=" + e.charCode +
          " target=" + (e.target && (e.target.id || e.target.tagName)) + " prevented=" + e.defaultPrevented);
    }, false);
  });

  if (!window.WebAssembly) { say("This browser cannot run pIRCIS. A current Safari, Chrome, Firefox or Edge can."); return; }

  // Threads need an isolated page; sw.js arranges that, and takes effect on
  // the load after the one that installs it.
  if (!window.crossOriginIsolated) {
    if (!("serviceWorker" in navigator)) { say("This browser cannot run pIRCIS here. Private windows in some browsers cannot either."); return; }
    navigator.serviceWorker.register("play/sw.js", { scope: "play/" }).then(function (reg) {
      if (sessionStorage.getItem("pircis-reloaded")) { say("This browser would not start pIRCIS. A private window can cause that."); return; }
      var go = function () { sessionStorage.setItem("pircis-reloaded", "1"); location.reload(); };
      if (reg.active && !navigator.serviceWorker.controller) return go();
      var w = reg.installing || reg.waiting;
      if (w) w.addEventListener("statechange", function () { if (w.state === "activated") go(); });
      else if (reg.active) go();
    }, function () { say("This browser would not start pIRCIS."); });
    return;
  }
  sessionStorage.removeItem("pircis-reloaded");

  // The screen's size, in CSS pixels. Room for twice the board's 480x320
  // gets exactly that; anything narrower gets the width there is and the
  // height left under the page's header, which on a phone is the phone's
  // own shape, as the app has it.
  function want() {
    var avail = Math.floor(holder.clientWidth);
    if (document.fullscreenElement === holder) return [window.innerWidth, window.innerHeight];
    var top = document.querySelector(".top").offsetHeight;
    var room = window.innerHeight - top;
    if (avail >= 600) {
      // The board's own shape, as large as fits under the header with the
      // whole of it in view, and no larger than twice the board.
      // In steps the screen can draw sharply: a whole number of its own
      // pixels to each of the board's, so the picture is the board's exactly.
      var dpr = Math.max(1, window.devicePixelRatio || 1);
      var k = Math.floor(Math.min(avail / 480, (room - 24) / 320, 2) * dpr) / dpr;
      if (k < 1) k = 1;
      return [Math.round(480 * k), Math.round(320 * k)];
    }
    return [Math.max(320, avail), Math.max(240, Math.min(room - 64, Math.floor(avail * 1.7)))];
  }
  // The picture is shown at exactly the size asked for, whatever else has a
  // go at the canvas's style.
  var rule = document.createElement("style");
  document.head.appendChild(rule);
  function show(s) {
    rule.textContent = "#canvas{width:" + s[0] + "px !important;height:" + s[1] + "px !important}";
    if (document.fullscreenElement !== holder) holder.style.minHeight = s[1] + "px";
  }
  var size = want();
  show(size);
  var started = false;

  window.pircisOpen = function (url) {
    var w = window.open(url, "_blank", "noopener");
    if (w) return;
    var bar = document.getElementById("play-link");
    bar.innerHTML = "";
    var a = document.createElement("a");
    a.href = url; a.target = "_blank"; a.rel = "noopener"; a.textContent = url;
    bar.appendChild(document.createTextNode("pIRCIS wants to open ")); bar.appendChild(a);
    bar.hidden = false;
  };

  function paste(text) {
    if (!started || !text) return;
    Module.ccall("pircis_paste", null, ["string"], [text.replace(/\r/g, "")]);
    canvas.focus();
  }

  // The browser's storage is opened only for the moment of reading or
  // writing. Left open, a page Safari keeps for the Back button kept the
  // next page waiting half a minute to open it.
  function closeStore() {
    var idb = FS.filesystems && FS.filesystems.IDBFS;
    if (!idb || !idb.dbs) return;
    for (var k in idb.dbs) { try { idb.dbs[k].close(); } catch (e) {} delete idb.dbs[k]; }
  }
  function sync(populate, then) { FS.syncfs(populate, function (err) { closeStore(); if (then) then(err); }); }

  window.Module = {
    canvas: canvas,
    arguments: ["--display", size[0] + "x" + size[1], "--data", "/data"],
    locateFile: function (f) { return "play/" + f + "?v=" + (window.pircisBuild || ""); },
    monitorRunDependencies: function (n) { log("still to load: " + n); },
    setStatus: function (t) { if (t) log("status: " + t); },
    print: function (t) { log(t); },
    printErr: function (t) { log(t); },
    preRun: [function () {
      // What pIRCIS keeps -- settings, saved programs -- lives in this
      // browser's own storage, and is read back before it starts.
      // A program handed over in the address, by a RUN button elsewhere on
      // the site, is left where pIRCIS looks for one as it starts.
      var m = /[#&]p=([A-Za-z0-9_-]+)/.exec(location.hash), nm = /[#&]n=([A-Za-z0-9 _-]{1,40})/.exec(location.hash);
      if (m) try {
        var b = atob(m[1].replace(/-/g, "+").replace(/_/g, "/"));
        FS.writeFile("/start.txt", decodeURIComponent(escape(b)).replace(/\r/g, ""));
        if (nm) FS.writeFile("/start-name.txt", nm[1]);
        history.replaceState(null, "", location.pathname);
      } catch (e) {}
      log("program fetched and compiled; reading saved data");
      FS.mkdir("/data"); FS.mount(IDBFS, {}, "/data");
      addRunDependency("pircis-data");
      sync(true, function () { log("saved data read"); removeRunDependency("pircis-data"); });
    }],
    onRuntimeInitialized: function () {
      started = true; say(""); log("threads ready; starting");
      setInterval(function () { sync(false); }, 4000);
      // Leaving the page: what was saved is written out, and the threads are
      // stopped there and then. Left running behind a page the browser is
      // keeping in case of Back, they held up the next page's start for half
      // a minute in Safari.
      addEventListener("pagehide", function () {
        sync(false);
        try { Module.PThread.terminateAllThreads(); } catch (e) {}
        started = false;
      });
      // Back to a page whose threads were stopped: start it afresh.
      addEventListener("pageshow", function (e) { if (e.persisted) location.reload(); });
    }
  };

  // The same hand-over once pIRCIS is already up on this page.
  addEventListener("hashchange", function () {
    var m = /[#&]p=([A-Za-z0-9_-]+)/.exec(location.hash);
    if (!m) return;
    try { paste(decodeURIComponent(escape(atob(m[1].replace(/-/g, "+").replace(/_/g, "/"))))); } catch (e) {}
    history.replaceState(null, "", location.pathname);
  });

  var t = 0;
  function resized() {
    clearTimeout(t);
    t = setTimeout(function () {
      if (!started) return;
      var s = want();
      show(s);
      Module.ccall("pircis_resize", null, ["number", "number"], [s[0], s[1]]);
    }, 150);
  }
  addEventListener("resize", resized);
  document.addEventListener("fullscreenchange", resized);

  document.addEventListener("paste", function (e) {
    if (e.target && e.target.id === "play-box") return;
    var text = (e.clipboardData || window.clipboardData).getData("text");
    if (text) { e.preventDefault(); paste(text); }
  });
  document.getElementById("play-paste").addEventListener("click", function () {
    var box = document.getElementById("play-manual");
    if (navigator.clipboard && navigator.clipboard.readText)
      navigator.clipboard.readText().then(function (text) { if (text) paste(text); else box.hidden = false; },
                                          function () { box.hidden = false; });
    else box.hidden = false;
  });
  document.getElementById("play-box-go").addEventListener("click", function () {
    paste(document.getElementById("play-box").value);
    document.getElementById("play-manual").hidden = true;
  });
  var full = document.getElementById("play-full");
  if (holder.requestFullscreen) full.addEventListener("click", function () { holder.requestFullscreen(); });
  else full.hidden = true;

  // Keys go to pIRCIS while the pointer has last been on it, and to the
  // page otherwise, so the page still scrolls with the arrow keys.
  // A press is placed by the last movement before it, so one that arrives
  // without any is given its own.
  canvas.addEventListener("mousedown", function (e) {
    canvas.focus();
    if (e.isTrusted !== false && !e.pircis)
      canvas.dispatchEvent(new MouseEvent("mousemove", { clientX: e.clientX, clientY: e.clientY, screenX: e.screenX, screenY: e.screenY, bubbles: true }));
  }, true);

  say("Loading pIRCIS…");
  var s = document.createElement("script");
  s.src = "play/pircis.js?v=" + (window.pircisBuild || "");
  log("build " + (window.pircisBuild || "unstamped"));
  s.onerror = function () { say("pIRCIS did not load. Try again in a moment."); };
  document.body.appendChild(s);
})();
