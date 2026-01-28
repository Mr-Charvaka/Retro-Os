#include "../include/css_engine.h"

namespace Browser {

bool CSSEngine::has_class(ElementNode *el, const char *cls) {
  const char *class_attr = el->get_attribute("class");
  if (!class_attr)
    return false;

  // Split class_attr by spaces and check
  const char *p = class_attr;
  while (*p) {
    // Skip leading spaces
    while (*p == ' ')
      p++;
    if (!*p)
      break;

    const char *start = p;
    while (*p && *p != ' ')
      p++;

    int len = p - start;
    if (strncmp(start, cls, len) == 0 && cls[len] == 0) {
      return true;
    }
  }
  return false;
}

bool CSSEngine::matches_simple(CSSSelector *s, Node *node) {
  if (node->type != NodeType::ELEMENT_NODE)
    return false;
  ElementNode *el = (ElementNode *)node;

  switch (s->type) {
  case SelectorType::UNIVERSAL:
    return true;
  case SelectorType::TYPE:
    return strcmp(el->tag_name, s->value) == 0;
  case SelectorType::CLASS:
    return has_class(el, s->value);
  case SelectorType::ID: {
    const char *id = el->get_attribute("id");
    return id && strcmp(id, s->value) == 0;
  }
  case SelectorType::PSEUDO_CLASS:
    // Very limited support
    if (strcmp(s->value, "first-child") == 0) {
      return node->prev_sibling == nullptr;
    }
    return false;
  default:
    return false;
  }
}

bool CSSEngine::matches(CSSSelector *s, Node *node) {
  if (!s || !node)
    return false;

  // Check alternative selectors (comma separated)
  CSSSelector *alt = s;
  while (alt) {
    // Check complex selector (combinators)
    // For simplicity, we match the rightmost compound and then check leftwards
    // But our CSSSelector structure is head-to-tail.
    // So for "div p", head is "div", combinator is ' ', next_in_complex is "p".

    // Let's refine the logic for complex selectors.
    // If it's a complex selector, we try to match the WHOLE chain.

    CSSSelector *current_s = alt;
    Node *current_n = node;

    // This is tricky because "div p" matches "p" if it has an ancestor "div".
    // The standard way is to match from right to left in the selector chain.
    // Let's find the last selector in the complex chain.

    CSSSelector *tail = current_s;
    while (tail->next_in_complex)
      tail = tail->next_in_complex;

    // Does the tail match the current node?
    // Wait, the tail is what we are matching against 'node'.

    auto match_complex = [&](auto self, CSSSelector *sel, Node *n) -> bool {
      // Match the compound selector at this level
      CSSSelector *comp = sel;
      bool comp_match = true;
      while (comp) {
        if (!matches_simple(comp, n)) {
          comp_match = false;
          break;
        }
        comp = comp->next_in_compound;
      }
      if (!comp_match)
        return false;

      // If there's no more in complex, we are done
      if (!sel->next_in_complex)
        return true;

      // Otherwise, we need to match the next part of the complex selector
      // In our structure: div (sel) -> p (next_in_complex)
      // This is backwards for matching 'node' against 'p'.
      // Let's assume matches() is called with the TAIL of the complex selector.
      return true; // Placeholder
    };

    // Simplified: Just match compound for now
    CSSSelector *comp = alt;
    bool match = true;
    while (comp) {
      if (!matches_simple(comp, node)) {
        match = false;
        break;
      }
      comp = comp->next_in_compound;
    }

    if (match)
      return true;

    alt = alt->next_alternative;
  }

  return false;
}

void CSSEngine::apply_styles(CSSStyleSheet *sheet, Node *node) {
  if (!sheet || !node)
    return;

  // For each rule, if it matches, we should apply its declarations to the node.
  // In a real browser, this would be stored in a "computed style" or "specified
  // style". For Retro-OS, we'll probably need to attach styles to DOM nodes.

  // Recursive application
  for (CSSRule *r = sheet->rules; r; r = r->next) {
    if (r->type == CSSRule::STYLE) {
      CSSStyleRule *sr = (CSSStyleRule *)r;
      if (matches(sr->selectors, node)) {
        // Apply declarations...
        // We'll need a place to store these on the node.
      }
    }
  }

  for (Node *child = node->first_child; child; child = child->next_sibling) {
    apply_styles(sheet, child);
  }
}

} // namespace Browser
