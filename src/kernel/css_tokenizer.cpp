#include "../include/css_tokenizer.h"

namespace Browser {

bool CSSTokenizer::is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         (unsigned char)c >= 0x80;
}

bool CSSTokenizer::is_ident(char c) {
  return is_ident_start(c) || is_digit(c) || c == '-';
}

CSSTokenizer::CSSTokenizer(const char *in) : input(in), position(0) {}

void CSSTokenizer::consume_comment() {
  advance(2); // /*
  while (current_char()) {
    if (current_char() == '*' && peek() == '/') {
      advance(2);
      break;
    }
    advance();
  }
}

CSSToken CSSTokenizer::consume_string(char quote) {
  CSSToken token(CSSTokenType::STRING);
  advance(); // quote
  int i = 0;
  while (current_char() && current_char() != quote && i < 255) {
    if (current_char() == '\\') {
      advance();
      if (current_char()) {
        token.value[i++] = current_char();
        advance();
      }
    } else {
      token.value[i++] = current_char();
      advance();
    }
  }
  if (current_char() == quote)
    advance();
  token.value[i] = 0;
  return token;
}

CSSToken CSSTokenizer::consume_numeric() {
  char buf[64];
  int i = 0;
  bool has_dot = false;

  if (current_char() == '-' || current_char() == '+') {
    buf[i++] = current_char();
    advance();
  }

  while (is_digit(current_char()) && i < 63) {
    buf[i++] = current_char();
    advance();
  }

  if (current_char() == '.' && is_digit(peek()) && i < 63) {
    has_dot = true;
    buf[i++] = current_char();
    advance();
    while (is_digit(current_char()) && i < 63) {
      buf[i++] = current_char();
      advance();
    }
  }
  buf[i] = 0;

  float val = 0; // Simple float parsing
// ato-float implementation would be better but let's assume numeric_value is
// set later or use simple logic
token_val_parse:
  CSSToken token(CSSTokenType::NUMBER);
  // Simple integer part
  float res = 0;
  bool neg = (buf[0] == '-');
  int start = (buf[0] == '-' || buf[0] == '+') ? 1 : 0;
  int j = start;
  while (buf[j] >= '0' && buf[j] <= '9') {
    res = res * 10 + (buf[j] - '0');
    j++;
  }
  if (buf[j] == '.') {
    j++;
    float frac = 0.1f;
    while (buf[j] >= '0' && buf[j] <= '9') {
      res += (buf[j] - '0') * frac;
      frac /= 10.0f;
      j++;
    }
  }
  token.numeric_value = neg ? -res : res;

  if (current_char() == '%') {
    token.type = CSSTokenType::PERCENTAGE;
    advance();
  } else if (is_ident_start(current_char()) ||
             (current_char() == '-' && is_ident_start(peek()))) {
    token.type = CSSTokenType::DIMENSION;
    int k = 0;
    while (is_ident(current_char()) && k < 15) {
      token.unit[k++] = current_char();
      advance();
    }
    token.unit[k] = 0;
  }

  return token;
}

CSSToken CSSTokenizer::consume_ident_like() {
  char buf[256];
  int i = 0;
  while ((is_ident(current_char()) || current_char() == '-') && i < 255) {
    buf[i++] = current_char();
    advance();
  }
  buf[i] = 0;

  if (current_char() == '(') {
    advance();
    CSSToken token(CSSTokenType::FUNCTION);
    strcpy(token.value, buf);
    return token;
  }

  CSSToken token(CSSTokenType::IDENT);
  strcpy(token.value, buf);
  return token;
}

CSSToken CSSTokenizer::next_token() {
  while (current_char()) {
    char c = current_char();

    if (is_whitespace(c)) {
      advance();
      while (is_whitespace(current_char()))
        advance();
      return CSSToken(CSSTokenType::WHITESPACE);
    }

    if (c == '/' && peek() == '*') {
      consume_comment();
      continue;
    }

    if (c == '"' || c == '\'')
      return consume_string(c);

    if (c == '#') {
      advance();
      CSSToken token(CSSTokenType::HASH);
      int i = 0;
      while (is_ident(current_char()) && i < 255) {
        token.value[i++] = current_char();
        advance();
      }
      token.value[i] = 0;
      token.is_id = true; // Simplified
      return token;
    }

    if (c == '@') {
      advance();
      CSSToken token(CSSTokenType::AT_KEYWORD);
      int i = 0;
      while (is_ident(current_char()) && i < 255) {
        token.value[i++] = current_char();
        advance();
      }
      token.value[i] = 0;
      return token;
    }

    if (is_digit(c) || (c == '.' && is_digit(peek())) ||
        (c == '-' && (is_digit(peek()) ||
                      (peek() == '.' && is_digit(input[position + 2]))))) {
      return consume_numeric();
    }

    if (is_ident_start(c) || (c == '-' && is_ident_start(peek()))) {
      return consume_ident_like();
    }

    // Single characters
    CSSTokenType type = CSSTokenType::DELIM;
    switch (c) {
    case ':':
      type = CSSTokenType::COLON;
      break;
    case ';':
      type = CSSTokenType::SEMICOLON;
      break;
    case ',':
      type = CSSTokenType::COMMA;
      break;
    case '[':
      type = CSSTokenType::LEFT_SQUARE;
      break;
    case ']':
      type = CSSTokenType::RIGHT_SQUARE;
      break;
    case '(':
      type = CSSTokenType::LEFT_PAREN;
      break;
    case ')':
      type = CSSTokenType::RIGHT_PAREN;
      break;
    case '{':
      type = CSSTokenType::LEFT_CURLY;
      break;
    case '}':
      type = CSSTokenType::RIGHT_CURLY;
      break;
    }

    CSSToken token(type);
    token.value[0] = c;
    token.value[1] = 0;
    advance();
    return token;
  }

  return CSSToken(CSSTokenType::EOF_TOKEN);
}

} // namespace Browser
