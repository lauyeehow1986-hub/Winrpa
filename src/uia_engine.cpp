#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "mta_worker.h"
#include "winrpa_core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <queue>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32

namespace winrpa {

namespace {

struct VariantGuard {
  VARIANT v;
  VariantGuard() { VariantInit(&v); }
  VariantGuard(const VariantGuard&) = delete;
  VariantGuard& operator=(const VariantGuard&) = delete;
  ~VariantGuard() { VariantClear(&v); }
};

struct ControlTypeEntry {
  CONTROLTYPEID id;
  const char* name;
};

constexpr ControlTypeEntry kControlTypes[] = {
    {UIA_ButtonControlTypeId, "Button"},
    {UIA_CalendarControlTypeId, "Calendar"},
    {UIA_CheckBoxControlTypeId, "CheckBox"},
    {UIA_ComboBoxControlTypeId, "ComboBox"},
    {UIA_EditControlTypeId, "Edit"},
    {UIA_HyperlinkControlTypeId, "Hyperlink"},
    {UIA_ImageControlTypeId, "Image"},
    {UIA_ListItemControlTypeId, "ListItem"},
    {UIA_ListControlTypeId, "List"},
    {UIA_MenuControlTypeId, "Menu"},
    {UIA_MenuBarControlTypeId, "MenuBar"},
    {UIA_MenuItemControlTypeId, "MenuItem"},
    {UIA_ProgressBarControlTypeId, "ProgressBar"},
    {UIA_RadioButtonControlTypeId, "RadioButton"},
    {UIA_ScrollBarControlTypeId, "ScrollBar"},
    {UIA_SliderControlTypeId, "Slider"},
    {UIA_SpinnerControlTypeId, "Spinner"},
    {UIA_StatusBarControlTypeId, "StatusBar"},
    {UIA_TabControlTypeId, "Tab"},
    {UIA_TabItemControlTypeId, "TabItem"},
    {UIA_TextControlTypeId, "Text"},
    {UIA_ToolBarControlTypeId, "ToolBar"},
    {UIA_ToolTipControlTypeId, "ToolTip"},
    {UIA_TreeControlTypeId, "Tree"},
    {UIA_TreeItemControlTypeId, "TreeItem"},
    {UIA_CustomControlTypeId, "Custom"},
    {UIA_GroupControlTypeId, "Group"},
    {UIA_ThumbControlTypeId, "Thumb"},
    {UIA_DataGridControlTypeId, "DataGrid"},
    {UIA_DataItemControlTypeId, "DataItem"},
    {UIA_DocumentControlTypeId, "Document"},
    {UIA_SplitButtonControlTypeId, "SplitButton"},
    {UIA_WindowControlTypeId, "Window"},
    {UIA_PaneControlTypeId, "Pane"},
    {UIA_HeaderControlTypeId, "Header"},
    {UIA_HeaderItemControlTypeId, "HeaderItem"},
    {UIA_TableControlTypeId, "Table"},
    {UIA_TitleBarControlTypeId, "TitleBar"},
    {UIA_SeparatorControlTypeId, "Separator"}
};

std::string to_lower_ascii(std::string s) {
  for (char& ch : s) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return s;
}

std::string control_type_to_string(CONTROLTYPEID id) {
  for (const auto& e : kControlTypes) {
    if (e.id == id) {
      return e.name;
    }
  }
  return "Unknown";
}

bool string_to_control_type(const std::string& name, CONTROLTYPEID& out_id) {
  const std::string target = to_lower_ascii(name);
  for (const auto& e : kControlTypes) {
    if (to_lower_ascii(e.name) == target) {
      out_id = e.id;
      return true;
    }
  }
  return false;
}

std::string hwnd_to_hex(UIA_HWND hwnd) {
  if (hwnd == nullptr) {
    return "";
  }
  const std::uintptr_t val = reinterpret_cast<std::uintptr_t>(hwnd);
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string digits;
  std::uintptr_t cur = val;
  do {
    digits.push_back(kHex[cur & 0xFU]);
    cur >>= 4U;
  } while (cur != 0U);
  std::reverse(digits.begin(), digits.end());
  return "0x" + digits;
}

bool contains_icase_utf8(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  const std::string h = to_lower_ascii(haystack);
  const std::string n = to_lower_ascii(needle);
  return h.find(n) != std::string::npos;
}

bool match_field(
    const std::string& value,
    const std::string& pattern,
    bool use_regex,
    bool exact_when_plain,
    const std::wregex* compiled_re) {
  if (pattern.empty()) {
    return true;
  }
  if (use_regex && compiled_re != nullptr) {
    const std::wstring wval = utf8_to_wide(value);
    return std::regex_search(wval, *compiled_re);
  }
  if (exact_when_plain) {
    return to_lower_ascii(value) == to_lower_ascii(pattern);
  }
  return contains_icase_utf8(value, pattern);
}

ComPtr<IUIAutomationCacheRequest> make_standard_cache_request(
    IUIAutomation* uia, bool include_patterns) {
  ComPtr<IUIAutomationCacheRequest> req;
  if (FAILED(uia->CreateCacheRequest(req.put())) || !req) {
    throw std::runtime_error("Failed to create IUIAutomationCacheRequest.");
  }
  req->AddProperty(UIA_NamePropertyId);
  req->AddProperty(UIA_AutomationIdPropertyId);
  req->AddProperty(UIA_ControlTypePropertyId);
  req->AddProperty(UIA_ClassNamePropertyId);
  req->AddProperty(UIA_ProcessIdPropertyId);
  req->AddProperty(UIA_NativeWindowHandlePropertyId);
  req->AddProperty(UIA_BoundingRectanglePropertyId);
  req->AddProperty(UIA_IsEnabledPropertyId);
  if (include_patterns) {
    req->AddPattern(UIA_InvokePatternId);
    req->AddPattern(UIA_ValuePatternId);
    req->AddPattern(UIA_TogglePatternId);
    req->AddPattern(UIA_SelectionItemPatternId);
    req->AddPattern(UIA_ExpandCollapsePatternId);
    req->AddPattern(UIA_ScrollItemPatternId);
    req->AddPattern(UIA_GridPatternId);
    req->AddPattern(UIA_TablePatternId);
    req->AddPattern(UIA_WindowPatternId);
  }
  return req;
}

ComPtr<IUIAutomationCondition> make_bstr_prop_condition(
    IUIAutomation* uia,
    PROPERTYID prop_id,
    const std::string& utf8_val,
    bool ignore_case) {
  const std::wstring wide = utf8_to_wide(utf8_val);
  VariantGuard v;
  v.v.vt = VT_BSTR;
  v.v.bstrVal = SysAllocString(wide.c_str());
  if (v.v.bstrVal == nullptr) {
    throw std::runtime_error("SysAllocString allocation failed for UIA property condition.");
  }
  ComPtr<IUIAutomationCondition> cond;
  const PropertyConditionFlags flags =
      ignore_case ? PropertyConditionFlags_IgnoreCase : PropertyConditionFlags_None;
  if (FAILED(uia->CreatePropertyConditionEx(prop_id, v.v, flags, cond.put())) || !cond) {
    throw std::runtime_error("Failed to create UIA string property condition.");
  }
  return cond;
}

void and_condition(
    IUIAutomation* uia,
    ComPtr<IUIAutomationCondition>& acc,
    ComPtr<IUIAutomationCondition> next) {
  if (!next) {
    return;
  }
  if (!acc) {
    acc = std::move(next);
    return;
  }
  ComPtr<IUIAutomationCondition> combined;
  if (FAILED(uia->CreateAndCondition(acc.get(), next.get(), combined.put())) || !combined) {
    throw std::runtime_error("Failed to combine UIA conditions with AND.");
  }
  acc = std::move(combined);
}

struct CachedNodeData {
  std::string name;
  std::string automation_id;
  std::string control_type;
  std::string class_name;
  int pid = 0;
  std::string hwnd;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  bool enabled = false;
  std::vector<std::string> patterns;
};

CachedNodeData read_cached_node(IUIAutomationElement* el, bool read_patterns) {
  CachedNodeData d;
  if (el == nullptr) {
    return d;
  }
  BstrGuard b_name;
  if (SUCCEEDED(el->get_CachedName(b_name.put()))) {
    d.name = bstr_to_utf8(b_name.get());
  }
  BstrGuard b_aid;
  if (SUCCEEDED(el->get_CachedAutomationId(b_aid.put()))) {
    d.automation_id = bstr_to_utf8(b_aid.get());
  }
  CONTROLTYPEID ctype = 0;
  if (SUCCEEDED(el->get_CachedControlType(&ctype))) {
    d.control_type = control_type_to_string(ctype);
  }
  BstrGuard b_cls;
  if (SUCCEEDED(el->get_CachedClassName(b_cls.put()))) {
    d.class_name = bstr_to_utf8(b_cls.get());
  }
  int pid = 0;
  if (SUCCEEDED(el->get_CachedProcessId(&pid))) {
    d.pid = pid;
  }
  UIA_HWND hwnd = nullptr;
  if (SUCCEEDED(el->get_CachedNativeWindowHandle(&hwnd))) {
    d.hwnd = hwnd_to_hex(hwnd);
  }
  RECT rc{0, 0, 0, 0};
  if (SUCCEEDED(el->get_CachedBoundingRectangle(&rc))) {
    d.left = static_cast<int>(rc.left);
    d.top = static_cast<int>(rc.top);
    d.right = static_cast<int>(rc.right);
    d.bottom = static_cast<int>(rc.bottom);
  }
  BOOL en = FALSE;
  if (SUCCEEDED(el->get_CachedIsEnabled(&en))) {
    d.enabled = (en != FALSE);
  }
  if (read_patterns) {
    struct PatCheck {
      PATTERNID id;
      const char* name;
    };
    constexpr PatCheck kPats[] = {
        {UIA_InvokePatternId, "Invoke"},
        {UIA_ValuePatternId, "Value"},
        {UIA_TogglePatternId, "Toggle"},
        {UIA_SelectionItemPatternId, "SelectionItem"},
        {UIA_ExpandCollapsePatternId, "ExpandCollapse"},
        {UIA_ScrollItemPatternId, "ScrollItem"},
        {UIA_GridPatternId, "Grid"},
        {UIA_TablePatternId, "Table"},
        {UIA_WindowPatternId, "Window"}
    };
    for (const auto& p : kPats) {
      ComPtr<IUnknown> pat;
      if (SUCCEEDED(el->GetCachedPattern(p.id, pat.put())) && pat) {
        d.patterns.emplace_back(p.name);
      }
    }
  }
  return d;
}

IUIAutomationElement* unwrap_element_or_stop(SEXP elem_xptr) {
  if (elem_xptr == R_NilValue || TYPEOF(elem_xptr) != EXTPTRSXP) {
    cpp11::stop("Expected a valid winrpa_element external pointer.");
  }
  UiaElementXPtr xptr(elem_xptr);
  UiaElementHandle* handle = xptr.get();
  if (handle == nullptr || handle->elem == nullptr) {
    cpp11::stop("Element handle is null or already released.");
  }
  return handle->elem;
}

HWND get_element_hwnd(IUIAutomationElement* el) {
  if (el == nullptr) {
    return nullptr;
  }
  UIA_HWND raw_hwnd = nullptr;
  if (FAILED(el->get_CurrentNativeWindowHandle(&raw_hwnd)) || raw_hwnd == nullptr) {
    return nullptr;
  }
  HWND hwnd = reinterpret_cast<HWND>(raw_hwnd);
  return IsWindow(hwnd) ? hwnd : nullptr;
}

struct RawElementBag {
  std::vector<IUIAutomationElement*> ptrs;
  RawElementBag() = default;
  RawElementBag(const RawElementBag&) = delete;
  RawElementBag& operator=(const RawElementBag&) = delete;
  ~RawElementBag() {
    for (IUIAutomationElement* p : ptrs) {
      if (p != nullptr) {
        MtaComWorker::instance().post([p]() { p->Release(); });
      }
    }
  }
};

SEXP wrap_single_element(IUIAutomationElement* raw_ptr) {
  if (raw_ptr == nullptr) {
    return R_NilValue;
  }
  auto* handle = new UiaElementHandle{raw_ptr};
  UiaElementXPtr xptr(handle);
  return xptr;
}

}  // namespace

}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
cpp11::writable::data_frame cpp_uia_list_windows() {
#ifdef _WIN32
  struct WindowListRes {
    bool ok = false;
    std::string error;
    std::vector<winrpa::CachedNodeData> items;
  };

  auto res = winrpa::MtaComWorker::instance().submit([](IUIAutomation* uia) -> WindowListRes {
    WindowListRes out;
    try {
      winrpa::ComPtr<IUIAutomationElement> root;
      if (FAILED(uia->GetRootElement(root.put())) || !root) {
        out.error = "Failed to acquire UIA desktop root element.";
        return out;
      }
      auto cache_req = winrpa::make_standard_cache_request(uia, false);
      winrpa::ComPtr<IUIAutomationCondition> cond;
      if (FAILED(uia->CreateTrueCondition(cond.put())) || !cond) {
        out.error = "Failed to create UIA true condition.";
        return out;
      }
      winrpa::ComPtr<IUIAutomationElementArray> arr;
      if (FAILED(root->FindAllBuildCache(
              TreeScope_Children, cond.get(), cache_req.get(), arr.put())) ||
          !arr) {
        out.error = "Failed to enumerate top-level UIA windows.";
        return out;
      }
      int len = 0;
      arr->get_Length(&len);
      out.items.reserve(static_cast<std::size_t>(len > 0 ? len : 0));
      for (int i = 0; i < len; ++i) {
        winrpa::ComPtr<IUIAutomationElement> el;
        if (SUCCEEDED(arr->GetElement(i, el.put())) && el) {
          out.items.push_back(winrpa::read_cached_node(el.get(), false));
        }
      }
      out.ok = true;
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }

  const R_xlen_t n = static_cast<R_xlen_t>(res.items.size());
  cpp11::writable::strings names(n);
  cpp11::writable::strings class_names(n);
  cpp11::writable::strings automation_ids(n);
  cpp11::writable::strings control_types(n);
  cpp11::writable::integers pids(n);
  cpp11::writable::strings hwnds(n);
  cpp11::writable::integers lefts(n);
  cpp11::writable::integers tops(n);
  cpp11::writable::integers rights(n);
  cpp11::writable::integers bottoms(n);
  cpp11::writable::logicals enabled(n);

  for (R_xlen_t i = 0; i < n; ++i) {
    const auto& it = res.items[static_cast<std::size_t>(i)];
    names[i] = it.name;
    class_names[i] = it.class_name;
    automation_ids[i] = it.automation_id;
    control_types[i] = it.control_type;
    pids[i] = it.pid;
    hwnds[i] = it.hwnd;
    lefts[i] = it.left;
    tops[i] = it.top;
    rights[i] = it.right;
    bottoms[i] = it.bottom;
    enabled[i] = it.enabled;
  }

  using namespace cpp11::literals;
  return cpp11::writable::data_frame({
      "name"_nm = names,
      "class_name"_nm = class_names,
      "automation_id"_nm = automation_ids,
      "control_type"_nm = control_types,
      "pid"_nm = pids,
      "hwnd"_nm = hwnds,
      "left"_nm = lefts,
      "top"_nm = tops,
      "right"_nm = rights,
      "bottom"_nm = bottoms,
      "enabled"_nm = enabled
  });
#else
  winrpa::require_windows_or_stop("cpp_uia_list_windows()");
  return cpp11::writable::data_frame();
#endif
}

[[cpp11::register]]
SEXP cpp_uia_find_window(
    std::string title, std::string class_name, int pid, bool use_regex) {
#ifdef _WIN32
  std::wregex title_re;
  std::wregex cls_re;
  if (use_regex) {
    try {
      if (!title.empty()) {
        title_re.assign(winrpa::utf8_to_wide(title));
      }
      if (!class_name.empty()) {
        cls_re.assign(winrpa::utf8_to_wide(class_name));
      }
    } catch (const std::regex_error& e) {
      cpp11::stop("Invalid regular expression in window selector: %s", e.what());
    }
  }

  struct FindWinRes {
    bool ok = false;
    std::string error;
    IUIAutomationElement* found = nullptr;
  };

  auto res = winrpa::MtaComWorker::instance().submit([&](IUIAutomation* uia) -> FindWinRes {
    FindWinRes out;
    try {
      winrpa::ComPtr<IUIAutomationElement> root;
      if (FAILED(uia->GetRootElement(root.put())) || !root) {
        out.error = "Failed to acquire UIA desktop root element.";
        return out;
      }
      auto cache_req = winrpa::make_standard_cache_request(uia, false);
      winrpa::ComPtr<IUIAutomationCondition> cond;
      if (FAILED(uia->CreateTrueCondition(cond.put())) || !cond) {
        out.error = "Failed to create UIA true condition.";
        return out;
      }
      winrpa::ComPtr<IUIAutomationElementArray> arr;
      if (FAILED(root->FindAllBuildCache(
              TreeScope_Children, cond.get(), cache_req.get(), arr.put())) ||
          !arr) {
        out.error = "Failed to enumerate top-level windows.";
        return out;
      }
      int len = 0;
      arr->get_Length(&len);
      for (int i = 0; i < len; ++i) {
        winrpa::ComPtr<IUIAutomationElement> el;
        if (FAILED(arr->GetElement(i, el.put())) || !el) {
          continue;
        }
        const auto info = winrpa::read_cached_node(el.get(), false);
        if (pid > 0 && info.pid != pid) {
          continue;
        }
        if (!winrpa::match_field(
                info.name, title, use_regex, false,
                (!title.empty() && use_regex) ? &title_re : nullptr)) {
          continue;
        }
        if (!winrpa::match_field(
                info.class_name, class_name, use_regex, false,
                (!class_name.empty() && use_regex) ? &cls_re : nullptr)) {
          continue;
        }
        out.found = el.detach();
        break;
      }
      out.ok = true;
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }
  return winrpa::wrap_single_element(res.found);
#else
  (void)title;
  (void)class_name;
  (void)pid;
  (void)use_regex;
  winrpa::require_windows_or_stop("cpp_uia_find_window()");
  return R_NilValue;
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_uia_find_elements(
    SEXP parent_xptr,
    std::string name,
    std::string automation_id,
    std::string control_type,
    std::string class_name,
    bool use_regex,
    bool exact,
    int max_results,
    int max_depth) {
#ifdef _WIN32
  IUIAutomationElement* parent_raw = nullptr;
  if (parent_xptr != R_NilValue) {
    parent_raw = winrpa::unwrap_element_or_stop(parent_xptr);
  }

  CONTROLTYPEID ctype_id = 0;
  const bool has_ctype = !control_type.empty();
  if (has_ctype && !winrpa::string_to_control_type(control_type, ctype_id)) {
    cpp11::stop("Unknown UIA control_type: '%s'.", control_type.c_str());
  }
  const std::string ctype_lower = winrpa::to_lower_ascii(control_type);

  std::wregex name_re;
  std::wregex aid_re;
  std::wregex cls_re;
  if (use_regex) {
    try {
      if (!name.empty()) name_re.assign(winrpa::utf8_to_wide(name));
      if (!automation_id.empty()) aid_re.assign(winrpa::utf8_to_wide(automation_id));
      if (!class_name.empty()) cls_re.assign(winrpa::utf8_to_wide(class_name));
    } catch (const std::regex_error& e) {
      cpp11::stop("Invalid regular expression in element selector: %s", e.what());
    }
  }

  const int limit = max_results <= 0 ? 100 : (max_results > 1000 ? 1000 : max_results);
  const bool bounded_depth = max_depth >= 0;
  const int depth_cap = bounded_depth ? (max_depth > 32 ? 32 : max_depth) : 0;

  auto matches_node = [&](const winrpa::CachedNodeData& info, bool check_ctype) -> bool {
    if (check_ctype && has_ctype && winrpa::to_lower_ascii(info.control_type) != ctype_lower) {
      return false;
    }
    if (!winrpa::match_field(
            info.name, name, use_regex, exact,
            (!name.empty() && use_regex) ? &name_re : nullptr)) {
      return false;
    }
    if (!winrpa::match_field(
            info.automation_id, automation_id, use_regex, true,
            (!automation_id.empty() && use_regex) ? &aid_re : nullptr)) {
      return false;
    }
    if (!winrpa::match_field(
            info.class_name, class_name, use_regex, exact,
            (!class_name.empty() && use_regex) ? &cls_re : nullptr)) {
      return false;
    }
    return true;
  };

  struct FindElemsRes {
    bool ok = false;
    std::string error;
    std::vector<IUIAutomationElement*> elems;
  };

  auto res = winrpa::MtaComWorker::instance().submit([&](IUIAutomation* uia) -> FindElemsRes {
    FindElemsRes out;
    try {
      winrpa::ComPtr<IUIAutomationElement> scope_root;
      if (parent_raw != nullptr) {
        parent_raw->AddRef();
        scope_root.reset(parent_raw);
      } else {
        if (FAILED(uia->GetRootElement(scope_root.put())) || !scope_root) {
          out.error = "Failed to acquire UIA desktop root element.";
          return out;
        }
      }

      auto cache_req = winrpa::make_standard_cache_request(uia, false);

      if (bounded_depth) {
        winrpa::ComPtr<IUIAutomationTreeWalker> walker;
        if (FAILED(uia->get_ControlViewWalker(walker.put())) || !walker) {
          out.error = "Failed to acquire UIA ControlViewWalker.";
          return out;
        }
        winrpa::ComPtr<IUIAutomationElement> cached_root;
        if (FAILED(scope_root->BuildUpdatedCache(cache_req.get(), cached_root.put())) ||
            !cached_root) {
          out.error = "Failed to cache root element for UIA search.";
          return out;
        }
        std::queue<std::pair<winrpa::ComPtr<IUIAutomationElement>, int>> q;
        q.push({std::move(cached_root), 0});
        while (!q.empty() && static_cast<int>(out.elems.size()) < limit) {
          if (winrpa::g_abort_requested.load(std::memory_order_acquire)) {
            throw std::runtime_error("UI element search aborted by emergency kill-switch.");
          }
          auto item = std::move(q.front());
          q.pop();
          winrpa::ComPtr<IUIAutomationElement> node = std::move(item.first);
          const int depth = item.second;
          const auto info = winrpa::read_cached_node(node.get(), false);
          if (matches_node(info, true)) {
            node->AddRef();
            out.elems.push_back(node.get());
            if (static_cast<int>(out.elems.size()) >= limit) {
              break;
            }
          }
          if (depth < depth_cap) {
            winrpa::ComPtr<IUIAutomationElement> child;
            if (SUCCEEDED(walker->GetFirstChildElementBuildCache(
                    node.get(), cache_req.get(), child.put()))) {
              while (child) {
                if (winrpa::g_abort_requested.load(std::memory_order_acquire)) {
                  throw std::runtime_error("UI element search aborted by emergency kill-switch.");
                }
                winrpa::ComPtr<IUIAutomationElement> next;
                walker->GetNextSiblingElementBuildCache(
                    child.get(), cache_req.get(), next.put());
                q.push({std::move(child), depth + 1});
                child = std::move(next);
              }
            }
          }
        }
        out.ok = true;
        return out;
      }

      // Unbounded subtree search: push exact / ControlType predicates server-side.
      winrpa::ComPtr<IUIAutomationCondition> cond;
      if (has_ctype) {
        winrpa::VariantGuard v;
        v.v.vt = VT_I4;
        v.v.lVal = ctype_id;
        winrpa::ComPtr<IUIAutomationCondition> ctype_cond;
        if (FAILED(uia->CreatePropertyCondition(
                UIA_ControlTypePropertyId, v.v, ctype_cond.put())) ||
            !ctype_cond) {
          out.error = "Failed to create UIA ControlType condition.";
          return out;
        }
        winrpa::and_condition(uia, cond, std::move(ctype_cond));
      }
      if (!use_regex && !automation_id.empty()) {
        winrpa::and_condition(
            uia, cond,
            winrpa::make_bstr_prop_condition(
                uia, UIA_AutomationIdPropertyId, automation_id, true));
      }
      if (!use_regex && exact && !name.empty()) {
        winrpa::and_condition(
            uia, cond,
            winrpa::make_bstr_prop_condition(uia, UIA_NamePropertyId, name, true));
      }
      if (!use_regex && exact && !class_name.empty()) {
        winrpa::and_condition(
            uia, cond,
            winrpa::make_bstr_prop_condition(uia, UIA_ClassNamePropertyId, class_name, true));
      }
      if (!cond) {
        if (FAILED(uia->CreateTrueCondition(cond.put())) || !cond) {
          out.error = "Failed to create UIA true condition.";
          return out;
        }
      }

      const bool all_server_side =
          !use_regex && (name.empty() || exact) && (class_name.empty() || exact);

      if (limit == 1 && all_server_side) {
        winrpa::ComPtr<IUIAutomationElement> found;
        const HRESULT hr = scope_root->FindFirstBuildCache(
            TreeScope_SubTree, cond.get(), cache_req.get(), found.put());
        if (FAILED(hr)) {
          out.error = "UIA subtree query failed.";
          return out;
        }
        if (found) {
          out.elems.push_back(found.detach());
        }
        out.ok = true;
        return out;
      }

      winrpa::ComPtr<IUIAutomationElementArray> arr;
      if (FAILED(scope_root->FindAllBuildCache(
              TreeScope_SubTree, cond.get(), cache_req.get(), arr.put())) ||
          !arr) {
        out.error = "UIA subtree query failed.";
        return out;
      }

      int len = 0;
      arr->get_Length(&len);
      for (int i = 0; i < len; ++i) {
        if (winrpa::g_abort_requested.load(std::memory_order_acquire)) {
          throw std::runtime_error("UI element search aborted by emergency kill-switch.");
        }
        if (static_cast<int>(out.elems.size()) >= limit) {
          break;
        }
        winrpa::ComPtr<IUIAutomationElement> el;
        if (FAILED(arr->GetElement(i, el.put())) || !el) {
          continue;
        }
        const auto info = winrpa::read_cached_node(el.get(), false);
        if (!matches_node(info, false)) {
          continue;
        }
        out.elems.push_back(el.detach());
      }
      out.ok = true;
    } catch (const std::exception& e) {
      for (IUIAutomationElement* p : out.elems) {
        if (p != nullptr) {
          p->Release();
        }
      }
      out.elems.clear();
      out.error = e.what();
    }
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }

  winrpa::RawElementBag bag;
  bag.ptrs = std::move(res.elems);

  cpp11::writable::list r_list(static_cast<R_xlen_t>(bag.ptrs.size()));
  for (std::size_t i = 0; i < bag.ptrs.size(); ++i) {
    IUIAutomationElement* raw = bag.ptrs[i];
    bag.ptrs[i] = nullptr;
    r_list[static_cast<R_xlen_t>(i)] = winrpa::wrap_single_element(raw);
  }
  return r_list;
#else
  (void)parent_xptr;
  (void)name;
  (void)automation_id;
  (void)control_type;
  (void)class_name;
  (void)use_regex;
  (void)exact;
  (void)max_results;
  (void)max_depth;
  winrpa::require_windows_or_stop("cpp_uia_find_elements()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_uia_element_info(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);

  struct InfoRes {
    bool ok = false;
    std::string error;
    winrpa::CachedNodeData data;
  };

  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation* uia) -> InfoRes {
    InfoRes out;
    try {
      auto cache_req = winrpa::make_standard_cache_request(uia, true);
      winrpa::ComPtr<IUIAutomationElement> updated;
      if (FAILED(el_raw->BuildUpdatedCache(cache_req.get(), updated.put())) || !updated) {
        out.error = "Element is no longer valid or detached from UI tree.";
        return out;
      }
      out.data = winrpa::read_cached_node(updated.get(), true);
      out.ok = true;
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }

  cpp11::writable::strings pats(static_cast<R_xlen_t>(res.data.patterns.size()));
  for (std::size_t i = 0; i < res.data.patterns.size(); ++i) {
    pats[static_cast<R_xlen_t>(i)] = res.data.patterns[i];
  }

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "name"_nm = res.data.name,
      "automation_id"_nm = res.data.automation_id,
      "control_type"_nm = res.data.control_type,
      "class_name"_nm = res.data.class_name,
      "pid"_nm = res.data.pid,
      "hwnd"_nm = res.data.hwnd,
      "left"_nm = res.data.left,
      "top"_nm = res.data.top,
      "right"_nm = res.data.right,
      "bottom"_nm = res.data.bottom,
      "enabled"_nm = res.data.enabled,
      "patterns"_nm = pats
  });
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_element_info()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
cpp11::writable::data_frame cpp_uia_snapshot_tree(
    SEXP elem_xptr, int max_depth, int max_nodes) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = nullptr;
  if (elem_xptr != R_NilValue) {
    el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  }
  const int depth_cap = max_depth < 0 ? 0 : (max_depth > 32 ? 32 : max_depth);
  const int nodes_cap = max_nodes <= 0 ? 200 : (max_nodes > 5000 ? 5000 : max_nodes);

  struct TreeRow {
    int node_id;
    int parent_id;
    int depth;
    winrpa::CachedNodeData data;
  };
  struct TreeRes {
    bool ok = false;
    std::string error;
    std::vector<TreeRow> rows;
  };

  auto res = winrpa::MtaComWorker::instance().submit([&](IUIAutomation* uia) -> TreeRes {
    TreeRes out;
    try {
      winrpa::ComPtr<IUIAutomationElement> root;
      if (el_raw != nullptr) {
        el_raw->AddRef();
        root.reset(el_raw);
      } else {
        if (FAILED(uia->GetRootElement(root.put())) || !root) {
          out.error = "Failed to get UIA root element.";
          return out;
        }
      }
      auto cache_req = winrpa::make_standard_cache_request(uia, false);
      winrpa::ComPtr<IUIAutomationTreeWalker> walker;
      if (FAILED(uia->get_ControlViewWalker(walker.put())) || !walker) {
        out.error = "Failed to acquire UIA ControlViewWalker.";
        return out;
      }
      winrpa::ComPtr<IUIAutomationElement> cached_root;
      if (FAILED(root->BuildUpdatedCache(cache_req.get(), cached_root.put())) ||
          !cached_root) {
        out.error = "Failed to cache root element for UI tree snapshot.";
        return out;
      }

      auto walk = [&](auto& self, IUIAutomationElement* node, int parent_id, int depth) -> void {
        if (static_cast<int>(out.rows.size()) >= nodes_cap) {
          return;
        }
        const int my_id = static_cast<int>(out.rows.size()) + 1;
        out.rows.push_back({my_id, parent_id, depth, winrpa::read_cached_node(node, false)});
        if (depth >= depth_cap || static_cast<int>(out.rows.size()) >= nodes_cap) {
          return;
        }
        winrpa::ComPtr<IUIAutomationElement> child;
        if (FAILED(walker->GetFirstChildElementBuildCache(
                node, cache_req.get(), child.put()))) {
          return;
        }
        while (child && static_cast<int>(out.rows.size()) < nodes_cap) {
          self(self, child.get(), my_id, depth + 1);
          winrpa::ComPtr<IUIAutomationElement> next;
          if (FAILED(walker->GetNextSiblingElementBuildCache(
                  child.get(), cache_req.get(), next.put()))) {
            break;
          }
          child = std::move(next);
        }
      };

      walk(walk, cached_root.get(), 0, 0);
      out.ok = true;
    } catch (const std::exception& e) {
      out.error = e.what();
    }
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }

  const R_xlen_t n = static_cast<R_xlen_t>(res.rows.size());
  cpp11::writable::integers node_id(n);
  cpp11::writable::integers parent_id(n);
  cpp11::writable::integers depth(n);
  cpp11::writable::strings name(n);
  cpp11::writable::strings automation_id(n);
  cpp11::writable::strings control_type(n);
  cpp11::writable::strings class_name(n);
  cpp11::writable::logicals enabled(n);
  cpp11::writable::integers left(n);
  cpp11::writable::integers top(n);
  cpp11::writable::integers right(n);
  cpp11::writable::integers bottom(n);

  for (R_xlen_t i = 0; i < n; ++i) {
    const auto& r = res.rows[static_cast<std::size_t>(i)];
    node_id[i] = r.node_id;
    parent_id[i] = r.parent_id;
    depth[i] = r.depth;
    name[i] = r.data.name;
    automation_id[i] = r.data.automation_id;
    control_type[i] = r.data.control_type;
    class_name[i] = r.data.class_name;
    enabled[i] = r.data.enabled;
    left[i] = r.data.left;
    top[i] = r.data.top;
    right[i] = r.data.right;
    bottom[i] = r.data.bottom;
  }

  using namespace cpp11::literals;
  return cpp11::writable::data_frame({
      "node_id"_nm = node_id,
      "parent_id"_nm = parent_id,
      "depth"_nm = depth,
      "name"_nm = name,
      "automation_id"_nm = automation_id,
      "control_type"_nm = control_type,
      "class_name"_nm = class_name,
      "enabled"_nm = enabled,
      "left"_nm = left,
      "top"_nm = top,
      "right"_nm = right,
      "bottom"_nm = bottom
  });
#else
  (void)elem_xptr;
  (void)max_depth;
  (void)max_nodes;
  winrpa::require_windows_or_stop("cpp_uia_snapshot_tree()");
  return cpp11::writable::data_frame();
#endif
}

[[cpp11::register]]
void cpp_uia_set_focus(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> OpRes {
    HRESULT hr = el_raw->SetFocus();
    if (FAILED(hr)) {
      return {false, "SetFocus failed on UIA element."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_set_focus()");
#endif
}

[[cpp11::register]]
void cpp_uia_window_focus(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationWindowPattern> pat;
    if (SUCCEEDED(el_raw->GetCurrentPatternAs(
            UIA_WindowPatternId, IID_IUIAutomationWindowPattern,
            reinterpret_cast<void**>(pat.put()))) &&
        pat) {
      WindowVisualState vis = WindowVisualState_Normal;
      if (SUCCEEDED(pat->get_CurrentWindowVisualState(&vis)) &&
          vis == WindowVisualState_Minimized) {
        pat->SetWindowVisualState(WindowVisualState_Normal);
      }
    }

    bool hwnd_ok = false;
    HWND hwnd = winrpa::get_element_hwnd(el_raw);
    if (hwnd != nullptr) {
      if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
      }
      BringWindowToTop(hwnd);
      if (SetForegroundWindow(hwnd) != FALSE) {
        hwnd_ok = true;
      }
      HWND fg = GetForegroundWindow();
      if (fg != hwnd) {
        const DWORD my_tid = GetCurrentThreadId();
        const DWORD fg_tid = (fg != nullptr) ? GetWindowThreadProcessId(fg, nullptr) : 0UL;
        const bool attached = (fg_tid != 0UL && fg_tid != my_tid &&
                               AttachThreadInput(my_tid, fg_tid, TRUE) != FALSE);
        BringWindowToTop(hwnd);
        if (SetForegroundWindow(hwnd) != FALSE || GetForegroundWindow() == hwnd) {
          hwnd_ok = true;
        }
        if (attached) {
          AttachThreadInput(my_tid, fg_tid, FALSE);
        }
      } else {
        hwnd_ok = true;
      }
    }

    const HRESULT hr_focus = el_raw->SetFocus();
    if (!hwnd_ok && FAILED(hr_focus)) {
      return {false, "Failed to focus or bring target window/element to the foreground."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_window_focus()");
#endif
}

[[cpp11::register]]
void cpp_uia_window_state(SEXP elem_xptr, std::string action) {
  if (action != "maximize" && action != "minimize" &&
      action != "restore" && action != "close") {
    cpp11::stop("Unknown window state action: '%s'.", action.c_str());
  }
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw, &action](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationWindowPattern> pat;
    if (SUCCEEDED(el_raw->GetCurrentPatternAs(
            UIA_WindowPatternId, IID_IUIAutomationWindowPattern,
            reinterpret_cast<void**>(pat.put()))) &&
        pat) {
      HRESULT hr = E_FAIL;
      if (action == "maximize") {
        hr = pat->SetWindowVisualState(WindowVisualState_Maximized);
      } else if (action == "minimize") {
        hr = pat->SetWindowVisualState(WindowVisualState_Minimized);
      } else if (action == "restore") {
        hr = pat->SetWindowVisualState(WindowVisualState_Normal);
      } else if (action == "close") {
        hr = pat->Close();
      }
      if (SUCCEEDED(hr)) {
        return {true, ""};
      }
    }

    HWND hwnd = winrpa::get_element_hwnd(el_raw);
    if (hwnd != nullptr) {
      if (action == "maximize") {
        ShowWindow(hwnd, SW_MAXIMIZE);
        return {true, ""};
      }
      if (action == "minimize") {
        ShowWindow(hwnd, SW_MINIMIZE);
        return {true, ""};
      }
      if (action == "restore") {
        ShowWindow(hwnd, SW_RESTORE);
        return {true, ""};
      }
      if (action == "close") {
        if (PostMessageW(hwnd, WM_CLOSE, static_cast<WPARAM>(0), static_cast<LPARAM>(0)) != FALSE) {
          return {true, ""};
        }
      }
    }

    return {
        false,
        "Target element does not support UIA WindowPattern and exposes no active HWND."
    };
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  (void)action;
  winrpa::require_windows_or_stop("cpp_uia_window_state()");
#endif
}

[[cpp11::register]]
void cpp_uia_invoke(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationInvokePattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA InvokePattern."};
    }
    hr = pat->Invoke();
    if (FAILED(hr)) {
      return {false, "InvokePattern::Invoke() failed."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_invoke()");
#endif
}

[[cpp11::register]]
std::string cpp_uia_get_value(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct ValRes { bool ok = false; std::string error; std::string value; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> ValRes {
    winrpa::ComPtr<IUIAutomationValuePattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_ValuePatternId, IID_IUIAutomationValuePattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA ValuePattern.", ""};
    }
    winrpa::BstrGuard bstr;
    hr = pat->get_CurrentValue(bstr.put());
    if (FAILED(hr)) {
      return {false, "ValuePattern::get_CurrentValue() failed.", ""};
    }
    return {true, "", winrpa::bstr_to_utf8(bstr.get())};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
  return res.value;
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_get_value()");
  return "";
#endif
}

[[cpp11::register]]
void cpp_uia_set_value(SEXP elem_xptr, std::string value) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  const std::wstring wide = winrpa::utf8_to_wide(value);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw, &wide](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationValuePattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_ValuePatternId, IID_IUIAutomationValuePattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA ValuePattern."};
    }
    BSTR b = SysAllocString(wide.c_str());
    if (b == nullptr) {
      return {false, "SysAllocString allocation failed."};
    }
    hr = pat->SetValue(b);
    SysFreeString(b);
    if (FAILED(hr)) {
      return {false, "ValuePattern::SetValue() failed."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  (void)value;
  winrpa::require_windows_or_stop("cpp_uia_set_value()");
#endif
}

[[cpp11::register]]
std::string cpp_uia_toggle(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct TogRes { bool ok = false; std::string error; std::string state; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> TogRes {
    winrpa::ComPtr<IUIAutomationTogglePattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_TogglePatternId, IID_IUIAutomationTogglePattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA TogglePattern.", ""};
    }
    hr = pat->Toggle();
    if (FAILED(hr)) {
      return {false, "TogglePattern::Toggle() failed.", ""};
    }
    ToggleState st = ToggleState_Indeterminate;
    pat->get_CurrentToggleState(&st);
    std::string s = "indeterminate";
    if (st == ToggleState_Off) s = "off";
    else if (st == ToggleState_On) s = "on";
    return {true, "", s};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
  return res.state;
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_toggle()");
  return "";
#endif
}

[[cpp11::register]]
void cpp_uia_select(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationSelectionItemPattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA SelectionItemPattern."};
    }
    hr = pat->Select();
    if (FAILED(hr)) {
      return {false, "SelectionItemPattern::Select() failed."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_select()");
#endif
}

[[cpp11::register]]
void cpp_uia_expand_collapse(SEXP elem_xptr, bool expand) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw, expand](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationExpandCollapsePattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA ExpandCollapsePattern."};
    }
    hr = expand ? pat->Expand() : pat->Collapse();
    if (FAILED(hr)) {
      return {false, expand ? "Expand() failed." : "Collapse() failed."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  (void)expand;
  winrpa::require_windows_or_stop("cpp_uia_expand_collapse()");
#endif
}

[[cpp11::register]]
void cpp_uia_scroll_into_view(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);
  struct OpRes { bool ok = false; std::string error; };
  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> OpRes {
    winrpa::ComPtr<IUIAutomationScrollItemPattern> pat;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern,
        reinterpret_cast<void**>(pat.put()));
    if (FAILED(hr) || !pat) {
      return {false, "Element does not support UIA ScrollItemPattern."};
    }
    hr = pat->ScrollIntoView();
    if (FAILED(hr)) {
      return {false, "ScrollItemPattern::ScrollIntoView() failed."};
    }
    return {true, ""};
  });
  if (!res.ok) cpp11::stop("%s", res.error.c_str());
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_scroll_into_view()");
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_uia_read_table(SEXP elem_xptr) {
#ifdef _WIN32
  IUIAutomationElement* el_raw = winrpa::unwrap_element_or_stop(elem_xptr);

  struct TableRes {
    bool ok = false;
    std::string error;
    int nrow = 0;
    int ncol = 0;
    std::vector<std::string> headers;
    std::vector<std::string> cells;
  };

  auto res = winrpa::MtaComWorker::instance().submit([el_raw](IUIAutomation*) -> TableRes {
    TableRes out;
    winrpa::ComPtr<IUIAutomationGridPattern> grid;
    HRESULT hr = el_raw->GetCurrentPatternAs(
        UIA_GridPatternId, IID_IUIAutomationGridPattern,
        reinterpret_cast<void**>(grid.put()));
    if (FAILED(hr) || !grid) {
      out.error = "Element does not support UIA GridPattern.";
      return out;
    }

    int rows = 0;
    int cols = 0;
    grid->get_CurrentRowCount(&rows);
    grid->get_CurrentColumnCount(&cols);
    out.nrow = rows;
    out.ncol = cols;

    winrpa::ComPtr<IUIAutomationTablePattern> tbl;
    if (SUCCEEDED(el_raw->GetCurrentPatternAs(
            UIA_TablePatternId, IID_IUIAutomationTablePattern,
            reinterpret_cast<void**>(tbl.put()))) &&
        tbl) {
      winrpa::ComPtr<IUIAutomationElementArray> hdr_arr;
      if (SUCCEEDED(tbl->GetCurrentColumnHeaders(hdr_arr.put())) && hdr_arr) {
        int hlen = 0;
        hdr_arr->get_Length(&hlen);
        out.headers.reserve(static_cast<std::size_t>(hlen > 0 ? hlen : 0));
        for (int c = 0; c < hlen; ++c) {
          winrpa::ComPtr<IUIAutomationElement> hel;
          if (SUCCEEDED(hdr_arr->GetElement(c, hel.put())) && hel) {
            winrpa::BstrGuard b;
            hel->get_CurrentName(b.put());
            out.headers.push_back(winrpa::bstr_to_utf8(b.get()));
          } else {
            out.headers.emplace_back("");
          }
        }
      }
    }

    if (rows > 0 && cols > 0) {
      out.cells.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
      for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
          winrpa::ComPtr<IUIAutomationElement> cell;
          std::string text;
          if (SUCCEEDED(grid->GetItem(r, c, cell.put())) && cell) {
            winrpa::ComPtr<IUIAutomationValuePattern> vp;
            if (SUCCEEDED(cell->GetCurrentPatternAs(
                    UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                    reinterpret_cast<void**>(vp.put()))) &&
                vp) {
              winrpa::BstrGuard b;
              if (SUCCEEDED(vp->get_CurrentValue(b.put()))) {
                text = winrpa::bstr_to_utf8(b.get());
              }
            }
            if (text.empty()) {
              winrpa::BstrGuard b;
              if (SUCCEEDED(cell->get_CurrentName(b.put()))) {
                text = winrpa::bstr_to_utf8(b.get());
              }
            }
          }
          out.cells.push_back(std::move(text));
        }
      }
    }
    out.ok = true;
    return out;
  });

  if (!res.ok) {
    cpp11::stop("%s", res.error.c_str());
  }

  cpp11::writable::strings r_headers(static_cast<R_xlen_t>(res.headers.size()));
  for (std::size_t i = 0; i < res.headers.size(); ++i) {
    r_headers[static_cast<R_xlen_t>(i)] = res.headers[i];
  }
  cpp11::writable::strings r_cells(static_cast<R_xlen_t>(res.cells.size()));
  for (std::size_t i = 0; i < res.cells.size(); ++i) {
    r_cells[static_cast<R_xlen_t>(i)] = res.cells[i];
  }

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "nrow"_nm = res.nrow,
      "ncol"_nm = res.ncol,
      "headers"_nm = r_headers,
      "cells"_nm = r_cells
  });
#else
  (void)elem_xptr;
  winrpa::require_windows_or_stop("cpp_uia_read_table()");
  return cpp11::writable::list();
#endif
}
