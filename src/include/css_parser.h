#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "css_dom.h"
#include "css_tokenizer.h"


namespace Browser {

class CSSParser {
public:
  CSSParser();
  CSSStyleSheet *parse(const char *css);

private:
  CSSTokenizer *tokenizer;
  CSSToken current_token;

  void advance();
  bool match(CSSTokenType type);
  void skip_whitespace();

  CSSRule *parse_rule();
  CSSStyleRule *parse_style_rule();
  CSSSelector *parse_selector_list();
  CSSSelector *parse_complex_selector();
  CSSSelector *parse_compound_selector();

  CSSDeclaration *parse_declaration_list();
  CSSDeclaration *parse_declaration();
  CSSValue *parse_value();
};

} // namespace Browser

#endif
