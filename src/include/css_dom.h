#ifndef CSS_DOM_H
#define CSS_DOM_H

#include "Std/Types.h"
#include "string.h"

namespace Browser {

enum class CSSValueType {
  KEYWORD,
  NUMBER,
  PERCENTAGE,
  DIMENSION,
  COLOR,
  STRING,
  URL,
  FUNCTION,
  LIST
};

struct CSSValue {
  CSSValueType type;
  char data[128];
  float numeric_value;
  char unit[16];
  CSSValue *next;

  CSSValue(CSSValueType t) : type(t), numeric_value(0), next(nullptr) {
    data[0] = 0;
    unit[0] = 0;
  }
  ~CSSValue() {
    if (next)
      delete next;
  }
};

struct CSSDeclaration {
  char property[64];
  CSSValue *value;
  bool important;
  CSSDeclaration *next;

  CSSDeclaration() : value(nullptr), important(false), next(nullptr) {
    property[0] = 0;
  }
  ~CSSDeclaration() {
    if (value)
      delete value;
    if (next)
      delete next;
  }
};

enum class SelectorType {
  UNIVERSAL,
  TYPE,
  CLASS,
  ID,
  ATTRIBUTE,
  PSEUDO_CLASS,
  PSEUDO_ELEMENT
};

struct CSSSelector {
  SelectorType type;
  char value[64];
  char attr_name[64];
  char attr_op[4];
  CSSSelector *next_in_compound; // .class#id
  char combinator;               // ' ', '>', '+', '~'
  CSSSelector *next_in_complex;  // div > p
  CSSSelector *next_alternative; // div, p

  CSSSelector(SelectorType t)
      : type(t), next_in_compound(nullptr), combinator(0),
        next_in_complex(nullptr), next_alternative(nullptr) {
    value[0] = 0;
    attr_name[0] = 0;
    attr_op[0] = 0;
  }

  ~CSSSelector() {
    if (next_in_compound)
      delete next_in_compound;
    if (next_in_complex)
      delete next_in_complex;
    if (next_alternative)
      delete next_alternative;
  }
};

struct CSSRule {
  enum Type { STYLE, MEDIA, IMPORT, KEYFRAMES };
  Type type;
  CSSRule *next;

  CSSRule(Type t) : type(t), next(nullptr) {}
  virtual ~CSSRule() {}
};

struct CSSStyleRule : public CSSRule {
  CSSSelector *selectors;
  CSSDeclaration *declarations;

  CSSStyleRule() : CSSRule(STYLE), selectors(nullptr), declarations(nullptr) {}
  ~CSSStyleRule() {
    if (selectors)
      delete selectors;
    if (declarations)
      delete declarations;
  }
};

struct CSSStyleSheet {
  CSSRule *rules;
  CSSStyleSheet() : rules(nullptr) {}

  ~CSSStyleSheet() {
    CSSRule *r = rules;
    while (r) {
      CSSRule *next = r->next;
      delete r;
      r = next;
    }
  }
};

} // namespace Browser

#endif
