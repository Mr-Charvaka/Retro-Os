#ifndef HTML_TOKENIZER_H
#define HTML_TOKENIZER_H

#include "Std/Types.h"
#include "string.h"

namespace Browser {

enum class TokenType {
  DOCTYPE,
  START_TAG,
  END_TAG,
  COMMENT,
  CHARACTER,
  EOF_TOKEN
};

struct Token {
  TokenType type;
  char name[64];
  char data[256]; // For text or comment data
  bool self_closing;

  struct Attr {
    char name[64];
    char value[256];
  };
  Attr attrs[16];
  int attr_count;

  Token(TokenType t) : type(t), self_closing(false), attr_count(0) {
    name[0] = 0;
    data[0] = 0;
  }
};

enum class TokenizerState {
  DATA,
  TAG_OPEN,
  END_TAG_OPEN,
  TAG_NAME,
  BEFORE_ATTRIBUTE_NAME,
  ATTRIBUTE_NAME,
  AFTER_ATTRIBUTE_NAME,
  BEFORE_ATTRIBUTE_VALUE,
  ATTRIBUTE_VALUE_DOUBLE_QUOTED,
  ATTRIBUTE_VALUE_SINGLE_QUOTED,
  ATTRIBUTE_VALUE_UNQUOTED,
  AFTER_ATTRIBUTE_VALUE_QUOTED,
  SELF_CLOSING_START_TAG,
  BOGUS_COMMENT,
  COMMENT_START,
  COMMENT_START_DASH,
  COMMENT,
  COMMENT_END_DASH,
  COMMENT_END,
  COMMENT_END_BANG,
  DOCTYPE,
  BEFORE_DOCTYPE_NAME,
  DOCTYPE_NAME,
  AFTER_DOCTYPE_NAME,
  RCDATA,
  RAWTEXT,
  SCRIPT_DATA,
  PLAINTEXT,
  CHARACTER_REFERENCE,
  NAMED_CHARACTER_REFERENCE,
  NUMERIC_CHARACTER_REFERENCE,
  HEXADECIMAL_CHARACTER_REFERENCE_START,
  DECIMAL_CHARACTER_REFERENCE_START,
  HEXADECIMAL_CHARACTER_REFERENCE,
  DECIMAL_CHARACTER_REFERENCE,
  NUMERIC_CHARACTER_REFERENCE_END
};

class Tokenizer {
public:
  Tokenizer(const char *input);
  Token next_token();
  void set_state(TokenizerState s) { state = s; }

private:
  const char *input;
  int position;
  TokenizerState state;
  TokenizerState return_state;
  Token *current_token;
  char temporary_buffer[64];
  int temp_buf_idx;

  char current_char() { return input[position]; }
  void advance() {
    if (input[position])
      position++;
  }
  bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  }
  bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
  }
  void reconsume(TokenizerState s) { state = s; } // Position not advanced
  void flush_temp_buffer();
};

} // namespace Browser

#endif
