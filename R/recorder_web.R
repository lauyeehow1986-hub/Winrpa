web_recorder_session_key <- function(session) {
  paste0(
    as.character(session$host %||% "127.0.0.1"),
    ":",
    as.character(session$port %||% 0L),
    "|",
    as.character(session$ws_url %||% "")
  )
}

build_web_recorder_js <- function(redact_passwords = TRUE) {
  redact_lit <- if (isTRUE(redact_passwords)) "true" else "false"
  paste0(
    "(function(){",
    "window.__winrpaRecConfig={active:true,redact:", redact_lit, "};",
    "if(window.__winrpaRecInstalled){",
    "  if(typeof window.__winrpaRecScan==='function'){window.__winrpaRecScan(document);}",
    "  return true;",
    "}",
    "window.__winrpaRecInstalled=true;",
    "var lastVals=new WeakMap();",
    "var attachedWins=new WeakSet();",
    "function escCss(s){",
    "  if(window.CSS&&CSS.escape){return CSS.escape(s);}",
    "  return String(s).replace(/([^a-zA-Z0-9_-])/g,'\\\\$1');",
    "}",
    "function escAttr(s){",
    "  return String(s).replace(/\\\\/g,'\\\\\\\\').replace(/\"/g,'\\\\\"');",
    "}",
    "function isUnstableId(id){",
    "  if(!id){return true;}",
    "  if(/^[0-9]+$/.test(id)){return true;}",
    "  if(/^[0-9a-f]{8}-[0-9a-f]{4}-/i.test(id)){return true;}",
    "  return false;",
    "}",
    "function isUniqueIn(sel,root){",
    "  try{return root.querySelectorAll(sel).length===1;}catch(e){return false;}",
    "}",
    "function pickLocalSelector(el,root){",
    "  if(!el||!el.tagName){return '';}",
    "  var qRoot=(root&&typeof root.querySelectorAll==='function')?root:document;",
    "  var id=el.getAttribute('id');",
    "  if(id&&!isUnstableId(id)){",
    "    var idSel='#'+escCss(id);",
    "    if(isUniqueIn(idSel,qRoot)){return idSel;}",
    "  }",
    "  var testAttrs=['data-testid','data-test','data-cy'];",
    "  for(var i=0;i<testAttrs.length;i++){",
    "    var av=el.getAttribute(testAttrs[i]);",
    "    if(av){",
    "      var ts='['+testAttrs[i]+'=\"'+escAttr(av)+'\"]';",
    "      if(isUniqueIn(ts,qRoot)){return ts;}",
    "    }",
    "  }",
    "  var tag=el.tagName.toLowerCase();",
    "  var nm=el.getAttribute('name');",
    "  if(nm){",
    "    var ns=tag+'[name=\"'+escAttr(nm)+'\"]';",
    "    if(isUniqueIn(ns,qRoot)){return ns;}",
    "    var val=el.getAttribute('value');",
    "    if(val){",
    "      var nvs=tag+'[name=\"'+escAttr(nm)+'\"][value=\"'+escAttr(val)+'\"]';",
    "      if(isUniqueIn(nvs,qRoot)){return nvs;}",
    "    }",
    "  }",
    "  var aria=el.getAttribute('aria-label');",
    "  if(aria&&aria.length<=80){",
    "    var ars=tag+'[aria-label=\"'+escAttr(aria)+'\"]';",
    "    if(isUniqueIn(ars,qRoot)){return ars;}",
    "  }",
    "  if(tag==='a'){",
    "    var href=el.getAttribute('href');",
    "    if(href&&href.length<=120){",
    "      var hs='a[href=\"'+escAttr(href)+'\"]';",
    "      if(isUniqueIn(hs,qRoot)){return hs;}",
    "    }",
    "  }",
    "  var parts=[];",
    "  var cur=el;",
    "  var doc=el.ownerDocument||document;",
    "  while(cur&&cur.nodeType===1&&cur!==doc.documentElement&&cur!==qRoot){",
    "    var ct=cur.tagName.toLowerCase();",
    "    var cid=cur.getAttribute('id');",
    "    if(cid&&!isUnstableId(cid)&&isUniqueIn('#'+escCss(cid),qRoot)){",
    "      parts.unshift('#'+escCss(cid));",
    "      break;",
    "    }",
    "    var idx=1;",
    "    var sib=cur;",
    "    while((sib=sib.previousElementSibling)){",
    "      if(sib.tagName===cur.tagName){idx++;}",
    "    }",
    "    parts.unshift(ct+':nth-of-type('+idx+')');",
    "    var cand=parts.join(' > ');",
    "    if(isUniqueIn(cand,qRoot)){return cand;}",
    "    cur=cur.parentElement;",
    "  }",
    "  return parts.join(' > ')||tag;",
    "}",
    "function pickDeepSelector(el){",
    "  var segments=[];",
    "  var cur=el;",
    "  var depth=0;",
    "  while(cur&&cur.nodeType===1&&depth<16){",
    "    depth++;",
    "    var root=(cur.getRootNode?cur.getRootNode():cur.ownerDocument)||document;",
    "    segments.unshift(pickLocalSelector(cur,root));",
    "    if(root&&root.host){cur=root.host;continue;}",
    "    var doc=cur.ownerDocument||document;",
    "    var win=doc?doc.defaultView:null;",
    "    var frameEl=null;",
    "    try{frameEl=(win&&win!==window&&win.frameElement)?win.frameElement:null;}catch(ex){frameEl=null;}",
    "    if(frameEl){cur=frameEl;continue;}",
    "    break;",
    "  }",
    "  return segments.join(' >>> ');",
    "}",
    "function deepTarget(e){",
    "  if(!e){return null;}",
    "  if(typeof e.composedPath==='function'){",
    "    var p=e.composedPath();",
    "    if(p&&p.length>0){",
    "      for(var i=0;i<p.length;i++){",
    "        if(p[i]&&p[i].nodeType===1){return p[i];}",
    "      }",
    "    }",
    "  }",
    "  return (e.target&&e.target.nodeType===1)?e.target:null;",
    "}",
    "function pickTarget(el){",
    "  if(!el||!el.tagName){return null;}",
    "  var c=el.closest('a,button,input,select,textarea,summary,[role=\"button\"],[role=\"link\"],[role=\"textbox\"],label');",
    "  return c||el;",
    "}",
    "function isPwd(el){",
    "  if(!el){return false;}",
    "  var tp=(el.type||'').toLowerCase();",
    "  var ac=(el.getAttribute&&el.getAttribute('autocomplete')||'').toLowerCase();",
    "  return tp==='password'||ac.indexOf('password')!==-1;",
    "}",
    "function isEditable(el){",
    "  if(!el||!el.tagName||el.disabled){return false;}",
    "  var tag=el.tagName.toLowerCase();",
    "  if(tag==='textarea'){return true;}",
    "  if(tag==='input'){",
    "    var it=(el.type||'text').toLowerCase();",
    "    return it!=='checkbox'&&it!=='radio'&&it!=='file'&&it!=='button'&&it!=='submit'&&it!=='reset'&&it!=='image'&&it!=='hidden';",
    "  }",
    "  if(el.isContentEditable){return true;}",
    "  var role=(el.getAttribute&&el.getAttribute('role')||'').toLowerCase();",
    "  return role==='textbox'||role==='combobox'||role==='searchbox';",
    "}",
    "function readRawVal(el){",
    "  if(el.value!==undefined){return String(el.value);}",
    "  return String(el.innerText!==undefined?el.innerText:(el.textContent||''));",
    "}",
    "function emit(obj){",
    "  if(!window.__winrpaRecConfig||!window.__winrpaRecConfig.active){return;}",
    "  var fn=null;",
    "  if(typeof window.__winrpa_record==='function'){fn=window.__winrpa_record;}",
    "  else{try{if(window.top&&typeof window.top.__winrpa_record==='function'){fn=window.top.__winrpa_record;}}catch(ex){}}",
    "  if(fn){fn(JSON.stringify(obj));}",
    "}",
    "function emitEditable(el,force){",
    "  if(!isEditable(el)){return;}",
    "  var raw=readRawVal(el);",
    "  if(!force&&lastVals.get(el)===raw){return;}",
    "  lastVals.set(el,raw);",
    "  var pwd=isPwd(el);",
    "  var redact=Boolean(window.__winrpaRecConfig&&window.__winrpaRecConfig.redact);",
    "  var val=(pwd&&redact)?'':raw;",
    "  var doc=el.ownerDocument||document;",
    "  var urlStr=location.href;",
    "  try{if(doc&&doc.location){urlStr=doc.location.href;}}catch(ex){}",
    "  emit({kind:'input',selector:pickDeepSelector(el),tag:el.tagName.toLowerCase(),inputType:(el.type||'').toLowerCase(),value:val,is_password:pwd,url:urlStr,ts:Date.now()});",
    "}",
    "function flushNode(root){",
    "  if(!root||typeof root.querySelectorAll!=='function'){return;}",
    "  var ed=root.querySelectorAll('input,textarea,[contenteditable],[role=\"textbox\"],[role=\"combobox\"],[role=\"searchbox\"]');",
    "  for(var i=0;i<ed.length;i++){",
    "    var el=ed[i];",
    "    if(!isEditable(el)){continue;}",
    "    var raw=readRawVal(el);",
    "    var defv=String(el.defaultValue||'');",
    "    if(raw!==''&&raw!==defv&&lastVals.get(el)!==raw){emitEditable(el,false);}",
    "  }",
    "  var all=root.querySelectorAll('*');",
    "  for(var j=0;j<all.length;j++){",
    "    var n=all[j];",
    "    if(n.shadowRoot){flushNode(n.shadowRoot);}",
    "    var nt=n.tagName?n.tagName.toLowerCase():'';",
    "    if(nt==='iframe'||nt==='frame'){",
    "      try{if(n.contentDocument){flushNode(n.contentDocument);}}catch(ex){}",
    "    }",
    "  }",
    "}",
    "function attachIframe(fr){",
    "  if(!fr||fr.__winrpaFrHooked){return;}",
    "  fr.__winrpaFrHooked=true;",
    "  try{if(fr.contentWindow&&fr.contentWindow.document&&fr.contentWindow.document.readyState!=='uninitialized'){attachWindow(fr.contentWindow);}}catch(ex){}",
    "  fr.addEventListener('load',function(){",
    "    try{if(fr.contentWindow){attachWindow(fr.contentWindow);}}catch(ex){}",
    "  });",
    "}",
    "function scanFrames(root){",
    "  if(!root||typeof root.querySelectorAll!=='function'){return;}",
    "  var frs=root.querySelectorAll('iframe,frame');",
    "  for(var i=0;i<frs.length;i++){attachIframe(frs[i]);}",
    "  var all=root.querySelectorAll('*');",
    "  for(var j=0;j<all.length;j++){",
    "    if(all[j].shadowRoot){scanFrames(all[j].shadowRoot);}",
    "  }",
    "}",
    "function attachWindow(win){",
    "  if(!win||attachedWins.has(win)){return;}",
    "  var doc=null;",
    "  try{doc=win.document;}catch(ex){return;}",
    "  if(!doc){return;}",
    "  attachedWins.add(win);",
    "  doc.addEventListener('click',function(e){",
    "    var rt=deepTarget(e);",
    "    var t=pickTarget(rt);",
    "    if(!t||!t.tagName){return;}",
    "    var tag=t.tagName.toLowerCase();",
    "    if(tag==='html'||tag==='body'){return;}",
    "    var it=(t.type||'').toLowerCase();",
    "    if(tag==='button'||it==='submit'||(t.getAttribute&&t.getAttribute('role')==='button')){",
    "      try{flushNode(t.form||doc);}catch(ex){}",
    "    }",
    "    emit({kind:'click',selector:pickDeepSelector(t),tag:tag,inputType:it,button:e.button||0,clicks:1,url:location.href,ts:Date.now(),is_password:isPwd(t)});",
    "  },true);",
    "  doc.addEventListener('dblclick',function(e){",
    "    var t=pickTarget(deepTarget(e));",
    "    if(!t||!t.tagName){return;}",
    "    emit({kind:'dblclick',selector:pickDeepSelector(t),tag:t.tagName.toLowerCase(),inputType:(t.type||'').toLowerCase(),button:e.button||0,clicks:2,url:location.href,ts:Date.now(),is_password:isPwd(t)});",
    "  },true);",
    "  doc.addEventListener('input',function(e){",
    "    var t=deepTarget(e);",
    "    if(isEditable(t)){emitEditable(t,true);}",
    "  },true);",
    "  doc.addEventListener('change',function(e){",
    "    var t=deepTarget(e);",
    "    if(!t||!t.tagName){return;}",
    "    var tag=t.tagName.toLowerCase();",
    "    if(tag==='select'){",
    "      emit({kind:'select',selector:pickDeepSelector(t),tag:tag,inputType:'select',value:String(t.value||''),url:location.href,ts:Date.now(),is_password:false});",
    "    }else if(tag==='input'&&(t.type||'').toLowerCase()==='file'){",
    "      emit({kind:'file',selector:pickDeepSelector(t),tag:tag,inputType:'file',value:'',url:location.href,ts:Date.now(),is_password:false});",
    "    }else if(isEditable(t)){",
    "      emitEditable(t,false);",
    "    }",
    "  },true);",
    "  doc.addEventListener('focusout',function(e){",
    "    var t=deepTarget(e);",
    "    if(isEditable(t)){",
    "      var raw=readRawVal(t);",
    "      if(raw!==''&&lastVals.get(t)!==raw){emitEditable(t,false);}",
    "    }",
    "  },true);",
    "  doc.addEventListener('submit',function(e){",
    "    var form=e.target;",
    "    flushNode((form&&form.tagName==='FORM')?form:doc);",
    "  },true);",
    "  win.addEventListener('pagehide',function(){try{flushNode(doc);}catch(ex){}},true);",
    "  doc.addEventListener('keydown',function(e){",
    "    if(e.key!=='Enter'&&e.key!=='Tab'&&e.key!=='Escape'){return;}",
    "    var t=deepTarget(e);",
    "    if(e.key==='Enter'&&isEditable(t)){emitEditable(t,false);}",
    "    var tag=(t&&t.tagName)?t.tagName.toLowerCase():'';",
    "    emit({kind:'key',key:e.key,selector:(t&&t.tagName)?pickDeepSelector(t):'',tag:tag,inputType:(t&&t.type||'').toLowerCase(),url:location.href,ts:Date.now(),is_password:isPwd(t)});",
    "  },true);",
    "  var obs=new MutationObserver(function(muts){",
    "    for(var m=0;m<muts.length;m++){",
    "      var add=muts[m].addedNodes;",
    "      for(var k=0;k<add.length;k++){",
    "        var nd=add[k];",
    "        if(nd&&nd.nodeType===1){",
    "          var nt=nd.tagName?nd.tagName.toLowerCase():'';",
    "          if(nt==='iframe'||nt==='frame'){attachIframe(nd);}",
    "          else{scanFrames(nd);}",
    "        }",
    "      }",
    "    }",
    "  });",
    "  var startObs=function(){if(doc.documentElement){obs.observe(doc.documentElement,{childList:true,subtree:true});scanFrames(doc);}};",
    "  if(doc.documentElement){startObs();}else{doc.addEventListener('DOMContentLoaded',startObs);}",
    "}",
    "window.__winrpaRecFlush=function(){flushNode(document);return true;};",
    "window.__winrpaRecScan=function(d){scanFrames(d||document);return true;};",
    "attachWindow(window);",
    "return true;",
    "})()"
  )
}

empty_web_events_df <- function() {
  data.frame(
    kind = character(),
    timestamp_ms = numeric(),
    url = character(),
    selector = character(),
    tag = character(),
    input_type = character(),
    value = character(),
    button = integer(),
    clicks = integer(),
    key = character(),
    is_password = logical(),
    stringsAsFactors = FALSE
  )
}

parse_web_payloads_df <- function(payloads, start_url = "") {
  rows <- list()
  if (is.character(start_url) && length(start_url) == 1L &&
      nzchar(start_url) && !identical(start_url, "about:blank")) {
    rows[[1L]] <- list(
      kind = "navigate",
      timestamp_ms = 0,
      url = start_url,
      selector = "",
      tag = "",
      input_type = "",
      value = "",
      button = 0L,
      clicks = 0L,
      key = "",
      is_password = FALSE
    )
  }

  for (raw in payloads) {
    obj <- tryCatch(cpp_cdp_parse_json(raw), error = function(e) NULL)
    if (!is.list(obj) || is.null(obj$kind)) {
      next
    }
    rows[[length(rows) + 1L]] <- list(
      kind = as.character(obj$kind %||% ""),
      timestamp_ms = as.numeric(obj$ts %||% 0),
      url = as.character(obj$url %||% ""),
      selector = as.character(obj$selector %||% ""),
      tag = as.character(obj$tag %||% ""),
      input_type = as.character(obj$inputType %||% ""),
      value = as.character(obj$value %||% ""),
      button = as.integer(obj$button %||% 0L),
      clicks = as.integer(obj$clicks %||% 1L),
      key = as.character(obj$key %||% ""),
      is_password = isTRUE(obj$is_password)
    )
  }

  if (length(rows) == 0L) {
    return(empty_web_events_df())
  }
  # Drop duplicate initial navigation if CDP immediately recorded the same URL
  if (length(rows) >= 2L && identical(rows[[1L]]$kind, "navigate") &&
      identical(rows[[2L]]$kind, "navigate") &&
      identical(rows[[1L]]$url, rows[[2L]]$url)) {
    rows <- rows[-1L]
  }

  data.frame(
    kind = vapply(rows, `[[`, character(1L), "kind"),
    timestamp_ms = vapply(rows, `[[`, numeric(1L), "timestamp_ms"),
    url = vapply(rows, `[[`, character(1L), "url"),
    selector = vapply(rows, `[[`, character(1L), "selector"),
    tag = vapply(rows, `[[`, character(1L), "tag"),
    input_type = vapply(rows, `[[`, character(1L), "input_type"),
    value = vapply(rows, `[[`, character(1L), "value"),
    button = vapply(rows, `[[`, integer(1L), "button"),
    clicks = vapply(rows, `[[`, integer(1L), "clicks"),
    key = vapply(rows, `[[`, character(1L), "key"),
    is_password = vapply(rows, `[[`, logical(1L), "is_password"),
    stringsAsFactors = FALSE
  )
}

#' Record Browser DOM Interactions Into a Replayable `rpa_web_*()` Script
#'
#' Attaches a DOM event listener over Chrome DevTools Protocol
#' (`Runtime.addBinding` + `Page.addScriptToEvaluateOnNewDocument`) to an
#' active `"winrpa_web_session"`, capturing clicks, form inputs, `<select>`
#' changes, file inputs, key submits, and page navigations with ranked CSS
#' selectors (`#id`, `[data-testid]`, `name`, `aria-label`, `href`, or unique
#' DOM path).
#'
#' @param session A `"winrpa_web_session"` created by [rpa_web_launch()] or
#'   [rpa_web_connect()].
#' @param redact_passwords Logical scalar; when `TRUE`, masks values typed into
#'   `<input type="password">` controls inside browser JavaScript and emits
#'   `rpa_web_type(..., Sys.getenv("WINRPA_SECRET"), secret = TRUE)`.
#' @param file Optional output `.R` script path.
#' @param coalesce_typing Logical scalar; collapse consecutive `input` events on
#'   the same CSS selector into a single `rpa_web_type(..., clear = TRUE)`.
#' @param wait_threshold_ms Numeric pause threshold in milliseconds (reserved for
#'   future timing waits and parity with [rpa_record_stop()]).
#' @param recording A `"winrpa_recording"` object or raw web events
#'   `data.frame`.
#'
#' @return
#'   * `rpa_web_record_start()` invisibly returns `TRUE`.
#'   * `rpa_web_record_stop()` and `rpa_web_record_emit()` return an object of
#'     class `"winrpa_recording"` (`mode = "web"`).
#' @export
#' @examplesIf interactive() && .Platform$OS.type == "windows"
#' s <- rpa_web_launch(headless = TRUE)
#' on.exit(rpa_web_close(s), add = TRUE)
#' rpa_web_record_start(s)
#' rec <- rpa_web_record_stop(s)
rpa_web_record_start <- function(session, redact_passwords = TRUE) {
  assert_web_session(session)
  if (!is.logical(redact_passwords) || length(redact_passwords) != 1L || is.na(redact_passwords)) {
    rlang::abort("'redact_passwords' must be a single non-NA logical value.")
  }
  rpa_session_check(require_interactive = FALSE)

  cdp_call(session, "Runtime.addBinding", '{"name":"__winrpa_record"}')
  js_src <- build_web_recorder_js(redact_passwords = redact_passwords)
  cpp_cdp_record_set_active(session$ptr, TRUE, TRUE, js_src)

  try(
    cdp_call(
      session,
      "Target.setAutoAttach",
      '{"autoAttach":true,"waitForDebuggerOnStart":false,"flatten":true}',
      timeout_ms = 3000L
    ),
    silent = TRUE
  )

  add_res <- cdp_call(
    session,
    "Page.addScriptToEvaluateOnNewDocument",
    sprintf('{"source":%s}', cdp_json_str(js_src))
  )
  cdp_eval_raw(session, js_src)
  start_url <- tryCatch(rpa_web_eval(session, "window.location.href"), error = function(e) "")

  if (is.null(.winrpa_env$web_recorders)) {
    .winrpa_env$web_recorders <- list()
  }
  skey <- web_recorder_session_key(session)
  .winrpa_env$web_recorders[[skey]] <- list(
    script_id = add_res$identifier %||% "",
    redact_passwords = isTRUE(redact_passwords),
    start_url = as.character(start_url %||% ""),
    start_ts = proc.time()[["elapsed"]],
    browser = as.character(session$browser %||% "auto"),
    headless = isTRUE(session$headless)
  )
  invisible(TRUE)
}

#' @rdname rpa_web_record_start
#' @export
rpa_web_record_stop <- function(session,
                                file = NULL,
                                coalesce_typing = TRUE,
                                wait_threshold_ms = 800,
                                redact_passwords = NULL) {
  assert_web_session(session)
  if (!is.null(file) && (!is.character(file) || length(file) != 1L || is.na(file) || !nzchar(file))) {
    rlang::abort("'file' must be NULL or a single non-empty file path.")
  }
  rpa_session_check(require_interactive = FALSE)

  skey <- web_recorder_session_key(session)
  meta <- if (!is.null(.winrpa_env$web_recorders)) .winrpa_env$web_recorders[[skey]] else NULL
  use_redact <- if (is.null(redact_passwords)) {
    if (!is.null(meta)) isTRUE(meta$redact_passwords) else TRUE
  } else {
    isTRUE(redact_passwords)
  }

  try(
    cdp_eval_raw(session, "if(typeof window.__winrpaRecFlush==='function'){window.__winrpaRecFlush();}", timeout_ms = 2000L),
    silent = TRUE
  )
  Sys.sleep(0.05)

  try(
    cdp_eval_raw(session, "if(window.__winrpaRecConfig){window.__winrpaRecConfig.active=false;}", timeout_ms = 3000L),
    silent = TRUE
  )
  try(
    cdp_call(
      session,
      "Target.setAutoAttach",
      '{"autoAttach":false,"waitForDebuggerOnStart":false,"flatten":true}',
      timeout_ms = 2000L
    ),
    silent = TRUE
  )
  if (!is.null(meta) && is.character(meta$script_id) && nzchar(meta$script_id)) {
    try(
      cdp_call(
        session,
        "Page.removeScriptToEvaluateOnNewDocument",
        sprintf('{"identifier":%s}', cdp_json_str(meta$script_id)),
        timeout_ms = 3000L
      ),
      silent = TRUE
    )
  }
  if (!is.null(.winrpa_env$web_recorders)) {
    .winrpa_env$web_recorders[[skey]] <- NULL
  }

  cpp_cdp_record_set_active(session$ptr, FALSE, FALSE, "")
  drained <- cpp_cdp_record_drain(session$ptr)
  df <- parse_web_payloads_df(drained$payloads, start_url = if (!is.null(meta)) meta$start_url else "")

  rec <- rpa_web_record_emit(
    recording = df,
    coalesce_typing = coalesce_typing,
    wait_threshold_ms = wait_threshold_ms,
    redact_passwords = use_redact
  )
  rec$dropped <- as.integer(drained$dropped %||% 0L)
  rec$elapsed_sec <- if (!is.null(meta) && is.numeric(meta$start_ts)) {
    max(0, proc.time()[["elapsed"]] - meta$start_ts)
  } else {
    0
  }
  if (rec$dropped > 0L) {
    rlang::warn(sprintf("%d web recorder event(s) dropped due to queue overflow.", rec$dropped))
  }
  if (!is.null(file)) {
    rpa_record_save(rec, file)
  }
  rec
}

is_text_input_type <- function(tag, input_type) {
  t <- tolower(as.character(tag %||% ""))
  it <- tolower(as.character(input_type %||% ""))
  if (t %in% c("textarea", "div", "span", "p")) {
    return(TRUE)
  }
  if (!identical(t, "input")) {
    return(FALSE)
  }
  it %in% c("", "text", "password", "email", "search", "url", "tel", "number",
            "date", "datetime-local", "month", "time", "week")
}

web_button_name <- function(btn_code) {
  switch(as.character(as.integer(btn_code %||% 0L)), "1" = "middle", "2" = "right", "left")
}

#' @rdname rpa_web_record_start
#' @export
rpa_web_record_emit <- function(recording,
                                coalesce_typing = TRUE,
                                wait_threshold_ms = 800,
                                redact_passwords = TRUE) {
  if (!is.logical(coalesce_typing) || length(coalesce_typing) != 1L || is.na(coalesce_typing)) {
    rlang::abort("'coalesce_typing' must be a single non-NA logical value.")
  }
  if (!is.numeric(wait_threshold_ms) || length(wait_threshold_ms) != 1L ||
      is.na(wait_threshold_ms) || wait_threshold_ms < 0) {
    rlang::abort("'wait_threshold_ms' must be a non-negative number.")
  }
  if (!is.logical(redact_passwords) || length(redact_passwords) != 1L || is.na(redact_passwords)) {
    rlang::abort("'redact_passwords' must be a single non-NA logical value.")
  }

  df <- if (inherits(recording, "winrpa_recording")) {
    recording$events
  } else if (is.data.frame(recording)) {
    recording
  } else {
    rlang::abort("'recording' must be a 'winrpa_recording' object or web events data.frame.")
  }

  lines <- c(
    "library(winrpa)",
    "sess <- rpa_web_launch(browser = \"auto\", headless = FALSE)",
    "on.exit(try(rpa_web_close(sess), silent = TRUE), add = TRUE)"
  )

  if (nrow(df) == 0L) {
    return(structure(
      list(
        script = lines,
        events = df,
        mode = "web",
        dropped = 0L,
        elapsed_sec = 0,
        stopped_by_hotkey = FALSE,
        created_at = Sys.time()
      ),
      class = "winrpa_recording"
    ))
  }

  kind <- as.character(ensure_df_col(df, "kind", ""))
  ts <- as.numeric(ensure_df_col(df, "timestamp_ms", 0))
  url <- as.character(ensure_df_col(df, "url", ""))
  sel <- as.character(ensure_df_col(df, "selector", ""))
  tag <- as.character(ensure_df_col(df, "tag", ""))
  itype <- as.character(ensure_df_col(df, "input_type", ""))
  val <- as.character(ensure_df_col(df, "value", ""))
  btn <- as.integer(ensure_df_col(df, "button", 0L))
  clicks <- as.integer(ensure_df_col(df, "clicks", 1L))
  key <- as.character(ensure_df_col(df, "key", ""))
  is_pwd <- as.logical(ensure_df_col(df, "is_password", FALSE))

  steps <- list()
  for (i in seq_len(nrow(df))) {
    k <- kind[i]
    cur <- list(
      kind = k,
      timestamp_ms = ts[i],
      url = url[i],
      selector = sel[i],
      tag = tag[i],
      input_type = itype[i],
      value = val[i],
      button = btn[i],
      clicks = max(1L, clicks[i]),
      key = key[i],
      is_password = isTRUE(is_pwd[i])
    )
    m_cnt <- length(steps)

    if (identical(k, "navigate")) {
      if (m_cnt > 0L && identical(steps[[m_cnt]]$kind, "navigate") &&
          identical(steps[[m_cnt]]$url, cur$url)) {
        next
      }
      steps[[m_cnt + 1L]] <- cur
      next
    }

    if (identical(k, "input")) {
      if (m_cnt > 0L && identical(steps[[m_cnt]]$kind, "click") &&
          identical(steps[[m_cnt]]$selector, cur$selector) &&
          is_text_input_type(cur$tag, cur$input_type)) {
        steps <- steps[seq_len(m_cnt - 1L)]
        m_cnt <- length(steps)
      }
      if ((isTRUE(coalesce_typing) || (isTRUE(redact_passwords) && isTRUE(cur$is_password))) &&
          m_cnt > 0L && identical(steps[[m_cnt]]$kind, "input") &&
          identical(steps[[m_cnt]]$selector, cur$selector)) {
        steps[[m_cnt]]$value <- cur$value
        steps[[m_cnt]]$timestamp_ms <- cur$timestamp_ms
        steps[[m_cnt]]$is_password <- isTRUE(steps[[m_cnt]]$is_password) || isTRUE(cur$is_password)
        next
      }
      steps[[m_cnt + 1L]] <- cur
      next
    }

    if (identical(k, "dblclick")) {
      while (length(steps) > 0L &&
             identical(steps[[length(steps)]]$kind, "click") &&
             identical(steps[[length(steps)]]$selector, cur$selector) &&
             abs(cur$timestamp_ms - steps[[length(steps)]]$timestamp_ms) <= 800) {
        steps <- steps[seq_len(length(steps) - 1L)]
      }
      cur$kind <- "click"
      cur$clicks <- 2L
      steps[[length(steps) + 1L]] <- cur
      next
    }

    steps[[m_cnt + 1L]] <- cur
  }

  for (st in steps) {
    if (identical(st$kind, "navigate") && nzchar(st$url)) {
      lines <- c(
        lines,
        sprintf("rpa_web_goto(sess, %s)", r_quote_str(st$url)),
        "rpa_web_wait_network_idle(sess)"
      )
    } else if (identical(st$kind, "click") && nzchar(st$selector)) {
      bname <- web_button_name(st$button)
      extra <- character()
      if (!identical(bname, "left")) {
        extra <- c(extra, sprintf("button = %s", r_quote_str(bname)))
      }
      if (as.integer(st$clicks) > 1L) {
        extra <- c(extra, sprintf("clicks = %dL", as.integer(st$clicks)))
      }
      suffix <- if (length(extra) > 0L) paste0(", ", paste(extra, collapse = ", ")) else ""
      lines <- c(
        lines,
        sprintf("rpa_web_wait_selector(sess, css = %s)", r_quote_str(st$selector)),
        sprintf("rpa_web_click(sess, css = %s%s)", r_quote_str(st$selector), suffix)
      )
    } else if (identical(st$kind, "input") && nzchar(st$selector)) {
      lines <- c(
        lines,
        sprintf("rpa_web_wait_selector(sess, css = %s)", r_quote_str(st$selector))
      )
      if (isTRUE(redact_passwords) && isTRUE(st$is_password)) {
        lines <- c(
          lines,
          sprintf(
            "rpa_web_type(sess, Sys.getenv(\"WINRPA_SECRET\"), css = %s, clear = TRUE, secret = TRUE)",
            r_quote_str(st$selector)
          )
        )
      } else {
        lines <- c(
          lines,
          sprintf(
            "rpa_web_type(sess, %s, css = %s, clear = TRUE)",
            r_quote_str(st$value), r_quote_str(st$selector)
          )
        )
      }
    } else if (identical(st$kind, "select") && nzchar(st$selector)) {
      js_sel <- sprintf(
        "(function(){var el=document.querySelector(%s);if(el){el.value=%s;el.dispatchEvent(new Event('change',{bubbles:true}));}})()",
        cdp_json_str(st$selector),
        cdp_json_str(st$value)
      )
      lines <- c(
        lines,
        sprintf("rpa_web_wait_selector(sess, css = %s)", r_quote_str(st$selector)),
        sprintf("rpa_web_eval(sess, %s)", r_quote_str(js_sel))
      )
    } else if (identical(st$kind, "file") && nzchar(st$selector)) {
      lines <- c(
        lines,
        sprintf("rpa_web_wait_selector(sess, css = %s)", r_quote_str(st$selector)),
        sprintf("rpa_web_set_files(sess, files = \"path/to/file\", css = %s)", r_quote_str(st$selector))
      )
    } else if (identical(st$kind, "key") && identical(st$key, "Enter") && nzchar(st$selector)) {
      js_enter <- sprintf(
        "(function(){var el=document.querySelector(%s);if(!el)return;el.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));if(el.form&&typeof el.form.requestSubmit==='function'){el.form.requestSubmit();}else if(el.form){el.form.submit();}})()",
        cdp_json_str(st$selector)
      )
      lines <- c(lines, sprintf("rpa_web_eval(sess, %s)", r_quote_str(js_enter)))
    }
  }

  structure(
    list(
      script = lines,
      events = df,
      mode = "web",
      dropped = if (inherits(recording, "winrpa_recording")) (recording$dropped %||% 0L) else 0L,
      elapsed_sec = if (inherits(recording, "winrpa_recording")) (recording$elapsed_sec %||% 0) else 0,
      stopped_by_hotkey = FALSE,
      created_at = Sys.time()
    ),
    class = "winrpa_recording"
  )
}
