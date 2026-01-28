#include "../include/css_parser.h"

namespace Browser {

CSSParser::CSSParser()
    : tokenizer(nullptr), current_token(CSSTokenType::EOF_TOKEN) {}

void CSSParser::advance() {
  if (tokenizer) {
    current_token = tokenizer->next_token();
  }
}

bool CSSParser::match(CSSTokenType type) {
  if (current_token.type == type) {
    advance();
    return true;
  }
  return false;
}

void CSSParser::skip_whitespace() {
  while (current_token.type == CSSTokenType::WHITESPACE)
    advance();
}

CSSStyleSheet *CSSParser::parse(const char *css) {
  CSSTokenizer tok(css);
  tokenizer = &tok;
  advance();

  CSSStyleSheet *sheet = new CSSStyleSheet();
  CSSRule **last_link = &sheet->rules;

  while (current_token.type != CSSTokenType::EOF_TOKEN) {
    skip_whitespace();
    if (current_token.type == CSSTokenType::EOF_TOKEN)
      break;

    CSSRule *rule = parse_rule();
    if (rule) {
      *last_link = rule;
      last_link = &rule->next;
    } else {
      // Error recovery: skip until next rule start or EOF
      while (current_token.type != CSSTokenType::EOF_TOKEN &&
             current_token.type != CSSTokenType::LEFT_CURLY &&
             current_token.type != CSSTokenType::RIGHT_CURLY)
        advance();
      if (current_token.type == CSSTokenType::RIGHT_CURLY)
        advance();
    }
  }

  return sheet;
}

CSSRule *CSSParser::parse_rule() {
  if (current_token.type == CSSTokenType::AT_KEYWORD) {
    // Handle @rules (media, import, etc.) - Simplified to skip for now
    while (current_token.type != CSSTokenType::SEMICOLON &&
           current_token.type != CSSTokenType::LEFT_CURLY &&
           current_token.type != CSSTokenType::EOF_TOKEN)
      advance();
    if (current_token.type == CSSTokenType::LEFT_CURLY) {
      int level = 1;
      advance();
      while (level > 0 && current_token.type != CSSTokenType::EOF_TOKEN) {
        if (current_token.type == CSSTokenType::LEFT_CURLY)
          level++;
        else if (current_token.type == CSSTokenType::RIGHT_CURLY)
          level--;
        advance();
      }
    } else if (current_token.type == CSSTokenType::SEMICOLON) {
      advance();
    }
    return nullptr;
  }

  return parse_style_rule();
}

CSSStyleRule *CSSParser::parse_style_rule() {
  CSSStyleRule *rule = new CSSStyleRule();
  rule->selectors = parse_selector_list();

  skip_whitespace();
  if (current_token.type == CSSTokenType::LEFT_CURLY) {
    advance();
    rule->declarations = parse_declaration_list();
    if (current_token.type == CSSTokenType::RIGHT_CURLY)
      advance();
  } else {
    delete rule;
    return nullptr;
  }

  return rule;
}

CSSSelector *CSSParser::parse_selector_list() {
  CSSSelector *head = parse_complex_selector();
  CSSSelector *current = head;

  while (current_token.type == CSSTokenType::COMMA) {
    advance();
    skip_whitespace();
    CSSSelector *next = parse_complex_selector();
    if (current)
      current->next_alternative = next;
    current = next;
  }

  return head;
}

CSSSelector *CSSParser::parse_complex_selector() {
  CSSSelector *head = parse_compound_selector();
  CSSSelector *current = head;

  while (true) {
    skip_whitespace();
    char combinator = 0;
    if (current_token.type == CSSTokenType::DELIM) {
      char c = current_token.value[0];
      if (c == '>' || c == '+' || c == '~') {
        combinator = c;
        advance();
        skip_whitespace();
      }
    } else if (current_token.type == CSSTokenType::IDENT ||
               current_token.type == CSSTokenType::HASH ||
               current_token.type == CSSTokenType::DELIM) {
      // Check if it's a descendant combinator (implied by space)
      // But we already skipped whitespace. We need to know if there WAS
      // whitespace. Simplified: just assume space for now if another selector
      // follows.
      combinator = ' ';
    }

    if (combinator || current_token.type == CSSTokenType::IDENT ||
        current_token.type == CSSTokenType::HASH ||
        (current_token.type == CSSTokenType::DELIM &&
         (current_token.value[0] == '.' || current_token.value[0] == '*'))) {
      if (!combinator)
        combinator = ' ';
      CSSSelector *next = parse_compound_selector();
      if (current) {
        current->combinator = combinator;
        current->next_in_complex = next;
      }
      current = next;
      if (!current)
        break;
    } else {
      break;
    }
  }

  return head;
}

CSSSelector *CSSParser::parse_compound_selector() {
  CSSSelector *head = nullptr;
  CSSSelector *last = nullptr;

  auto add_selector = [&](CSSSelector *s) {
    if (!head)
      head = s;
    else
      last->next_in_compound = s;
    last = s;
  };

  while (current_token.type != CSSTokenType::WHITESPACE &&
         current_token.type != CSSTokenType::COMMA &&
         current_token.type != CSSTokenType::LEFT_CURLY &&
         current_token.type != CSSTokenType::EOF_TOKEN) {

    if (current_token.type == CSSTokenType::IDENT) {
      add_selector(new CSSSelector(SelectorType::TYPE));
      strcpy(last->value, current_token.value);
      advance();
    } else if (current_token.type == CSSTokenType::HASH) {
      add_selector(new CSSSelector(SelectorType::ID));
      strcpy(last->value, current_token.value);
      advance();
    } else if (current_token.type == CSSTokenType::DELIM) {
      if (current_token.value[0] == '.') {
        advance();
        if (current_token.type == CSSTokenType::IDENT) {
          add_selector(new CSSSelector(SelectorType::CLASS));
          strcpy(last->value, current_token.value);
          advance();
        }
      } else if (current_token.value[0] == '*') {
        add_selector(new CSSSelector(SelectorType::UNIVERSAL));
        advance();
      } else {
        break; // Not a selector start
      }
    } else if (current_token.type == CSSTokenType::COLON) {
      advance();
      if (current_token.type == CSSTokenType::IDENT) {
        add_selector(new CSSSelector(SelectorType::PSEUDO_CLASS));
        strcpy(last->value, current_token.value);
        advance();
      }
    } else {
      break;
    }
  }

  return head;
}

CSSDeclaration *CSSParser::parse_declaration_list() {
  CSSDeclaration *head = nullptr;
  CSSDeclaration *last = nullptr;

  while (current_token.type != CSSTokenType::RIGHT_CURLY &&
         current_token.type != CSSTokenType::EOF_TOKEN) {
    skip_whitespace();
    if (current_token.type == CSSTokenType::RIGHT_CURLY)
      break;

    CSSDeclaration *decl = parse_declaration();
    if (decl) {
      if (!head)
        head = decl;
      else
        last->next = decl;
      last = decl;
    }

    skip_whitespace();
    if (current_token.type == CSSTokenType::SEMICOLON)
      advance();
    else
      break; // Missing semicolon or unexpected token
  }

  return head;
}

CSSDeclaration *CSSParser::parse_declaration() {
  if (current_token.type != CSSTokenType::IDENT)
    return nullptr;

  CSSDeclaration *decl = new CSSDeclaration();
  strcpy(decl->property, current_token.value);
  advance();

  skip_whitespace();
  if (current_token.type != CSSTokenType::COLON) {
    delete decl;
    return nullptr;
  }
  advance();

  skip_whitespace();
  decl->value = parse_value();

  // Check for !important
  skip_whitespace();
  if (current_token.type == CSSTokenType::DELIM &&
      current_token.value[0] == '!') {
    advance();
    if (current_token.type == CSSTokenType::IDENT &&
        strcmp(current_token.value, "important") == 0) {
      decl->important = true;
      advance();
    }
  }

  return decl;
}

CSSValue *CSSParser::parse_value() {
  CSSValue *head = nullptr;
  CSSValue *last = nullptr;

  while (current_token.type != CSSTokenType::SEMICOLON &&
         current_token.type != CSSTokenType::RIGHT_CURLY &&
         current_token.type != CSSTokenType::EOF_TOKEN) {

    skip_whitespace();
    if (current_token.type == CSSTokenType::SEMICOLON ||
        current_token.type == CSSTokenType::RIGHT_CURLY ||
        current_token.type == CSSTokenType::EOF_TOKEN)
      break;

    // Handle !important (it's not part of the value)
    if (current_token.type == CSSTokenType::DELIM &&
        current_token.value[0] == '!')
      break;

    CSSValue *val = nullptr;

    if (current_token.type == CSSTokenType::HASH) {
      val = new CSSValue(CSSValueType::COLOR);
      strcpy(val->data, current_token.value);
      advance();
    } else if (current_token.type == CSSTokenType::DIMENSION) {
      val = new CSSValue(CSSValueType::DIMENSION);
      val->numeric_value = current_token.numeric_value;
      strcpy(val->unit, current_token.unit);
      advance();
    } else if (current_token.type == CSSTokenType::NUMBER) {
      val = new CSSValue(CSSValueType::NUMBER);
      val->numeric_value = current_token.numeric_value;
      advance();
    } else if (current_token.type == CSSTokenType::PERCENTAGE) {
      val = new CSSValue(CSSValueType::PERCENTAGE);
      val->numeric_value = current_token.numeric_value;
      advance();
    } else if (current_token.type == CSSTokenType::IDENT) {
      val = new CSSValue(CSSValueType::KEYWORD);
      strcpy(val->data, current_token.value);
      advance();
    } else if (current_token.type == CSSTokenType::STRING) {
      val = new CSSValue(CSSValueType::STRING);
      strcpy(val->data, current_token.value);
      advance();
    } else if (current_token.type == CSSTokenType::COMMA) {
      // Comma is also a value separator
      advance();
      continue;
    } else {
      advance();
      continue;
    }

    if (val) {
      if (!head)
        head = val;
      else
        last->next = val;
      last = val;
    }
  }

  return head;
}

} // namespace Browser
