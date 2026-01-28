#ifndef CSS_ENGINE_H
#define CSS_ENGINE_H

#include "css_dom.h"
#include "html_dom.h"

namespace Browser {

class CSSEngine {
public:
  static bool matches(CSSSelector *selector, Node *node);
  static void apply_styles(CSSStyleSheet *sheet, Node *node);

private:
  static bool matches_simple(CSSSelector *selector, Node *node);
  static bool has_class(ElementNode *el, const char *cls);
};

} // namespace Browser

#endif
