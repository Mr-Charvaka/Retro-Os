#ifndef CSS_TOKENIZER_H
#define CSS_TOKENIZER_H

#include "Std/Types.h"
#include "string.h"

namespace Browser {

enum class CSSTokenType {
  IDENT,
  FUNCTION,
  AT_KEYWORD,
  HASH,
  STRING,
  URL,
  DELIM,
  NUMBER,
  PERCENTAGE,
  DIMENSION,
  WHITESPACE,
  CDO, // <!--
  CDC, // -->
  COLON,
  SEMICOLON,
  COMMA,
  LEFT_SQUARE,
  RIGHT_SQUARE,
  LEFT_PAREN,
  RIGHT_PAREN,
  LEFT_CURLY,
  RIGHT_CURLY,
  COMMENT,
  EOF_TOKEN
};

struct CSSToken {
  CSSTokenType type;
  char value[256];
  float numeric_value;
  char unit[16];
  bool is_id; // For HASH tokens

  CSSToken(CSSTokenType t) : type(t), numeric_value(0), is_id(false) {
    value[0] = 0;
    unit[0] = 0;
  }
};

class CSSTokenizer {
public:
  CSSTokenizer(const char *input);
  CSSToken next_token();

private:
  const char *input;
  int position;

  char current_char() { return input[position]; }
  char peek(int n = 1) { return input[position + n]; }
  void advance(int n = 1) {
    for (int i = 0; i < n; i++)
      if (input[position])
        position++;
  }

  bool is_digit(char c) { return c >= '0' && c <= '9'; }
  bool is_ident_start(char c);
  bool is_ident(char c);
  bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
  }

  CSSToken consume_numeric();
  CSSToken consume_ident_like();
  CSSToken consume_string(char quote);
  void consume_comment();
};

} // namespace Browser

#endif
