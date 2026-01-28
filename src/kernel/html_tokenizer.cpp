#include "../include/html_tokenizer.h"

namespace Browser {

Tokenizer::Tokenizer(const char *in)
    : input(in), position(0), state(TokenizerState::DATA),
      return_state(TokenizerState::DATA), current_token(nullptr),
      temp_buf_idx(0) {
  temporary_buffer[0] = 0;
}

void Tokenizer::flush_temp_buffer() {
  // This will be called when we need to emit the temporary buffer as characters
  // For now, let's keep it simple.
}

Token Tokenizer::next_token() {
  while (true) {
    char c = current_char();

    // Check for EOF
    if (c == 0 && state != TokenizerState::DATA &&
        state != TokenizerState::RCDATA && state != TokenizerState::RAWTEXT) {
      // EOF in tag/comment/etc usually emits the current token and then EOF
      if (current_token) {
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      }
      return Token(TokenType::EOF_TOKEN);
    }
    if (c == 0 &&
        (state == TokenizerState::DATA || state == TokenizerState::RCDATA ||
         state == TokenizerState::RAWTEXT)) {
      return Token(TokenType::EOF_TOKEN);
    }

    switch (state) {
    case TokenizerState::DATA:
      if (c == '<') {
        state = TokenizerState::TAG_OPEN;
        advance();
      } else if (c == '&') {
        return_state = TokenizerState::DATA;
        state = TokenizerState::CHARACTER_REFERENCE;
        advance();
      } else {
        Token t(TokenType::CHARACTER);
        t.data[0] = c;
        t.data[1] = 0;
        advance();
        return t;
      }
      break;

    case TokenizerState::RCDATA:
      if (c == '<') {
        state =
            TokenizerState::TAG_OPEN; // Simplified, should be RCDATA_LESS_THAN
        advance();
      } else if (c == '&') {
        return_state = TokenizerState::RCDATA;
        state = TokenizerState::CHARACTER_REFERENCE;
        advance();
      } else {
        Token t(TokenType::CHARACTER);
        t.data[0] = c;
        t.data[1] = 0;
        advance();
        return t;
      }
      break;

    case TokenizerState::RAWTEXT:
      if (c == '<') {
        state = TokenizerState::TAG_OPEN; // Simplified
        advance();
      } else {
        Token t(TokenType::CHARACTER);
        t.data[0] = c;
        t.data[1] = 0;
        advance();
        return t;
      }
      break;

    case TokenizerState::TAG_OPEN:
      if (c == '!') {
        state = TokenizerState::COMMENT_START;
        advance();
      } else if (c == '/') {
        state = TokenizerState::END_TAG_OPEN;
        advance();
      } else if (is_ascii_alpha(c)) {
        current_token = new Token(TokenType::START_TAG);
        state = TokenizerState::TAG_NAME;
        // Reconsume
      } else {
        state = TokenizerState::DATA;
        Token t(TokenType::CHARACTER);
        t.data[0] = '<';
        t.data[1] = 0;
        return t;
      }
      break;

    case TokenizerState::END_TAG_OPEN:
      if (is_ascii_alpha(c)) {
        current_token = new Token(TokenType::END_TAG);
        state = TokenizerState::TAG_NAME;
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
      } else {
        // Bogus comment or error
        state = TokenizerState::DATA;
      }
      break;

    case TokenizerState::TAG_NAME:
      if (is_space(c)) {
        state = TokenizerState::BEFORE_ATTRIBUTE_NAME;
        advance();
      } else if (c == '/') {
        state = TokenizerState::SELF_CLOSING_START_TAG;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        int len = strlen(current_token->name);
        if (len < 63) {
          current_token->name[len] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
          current_token->name[len + 1] = 0;
        }
        advance();
      }
      break;

    case TokenizerState::BEFORE_ATTRIBUTE_NAME:
      if (is_space(c)) {
        advance();
      } else if (c == '/' || c == '>' || c == 0) {
        state = TokenizerState::AFTER_ATTRIBUTE_NAME;
      } else {
        if (current_token->attr_count < 16) {
          current_token->attr_count++;
          current_token->attrs[current_token->attr_count - 1].name[0] = 0;
          current_token->attrs[current_token->attr_count - 1].value[0] = 0;
        }
        state = TokenizerState::ATTRIBUTE_NAME;
      }
      break;

    case TokenizerState::ATTRIBUTE_NAME:
      if (is_space(c) || c == '/' || c == '>' || c == 0) {
        state = TokenizerState::AFTER_ATTRIBUTE_NAME;
      } else if (c == '=') {
        state = TokenizerState::BEFORE_ATTRIBUTE_VALUE;
        advance();
      } else {
        int idx = current_token->attr_count - 1;
        if (idx >= 0) {
          int len = strlen(current_token->attrs[idx].name);
          if (len < 63) {
            current_token->attrs[idx].name[len] =
                (c >= 'A' && c <= 'Z') ? (c + 32) : c;
            current_token->attrs[idx].name[len + 1] = 0;
          }
        }
        advance();
      }
      break;

    case TokenizerState::AFTER_ATTRIBUTE_NAME:
      if (is_space(c)) {
        advance();
      } else if (c == '/') {
        state = TokenizerState::SELF_CLOSING_START_TAG;
        advance();
      } else if (c == '=') {
        state = TokenizerState::BEFORE_ATTRIBUTE_VALUE;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        if (current_token->attr_count < 16) {
          current_token->attr_count++;
          current_token->attrs[current_token->attr_count - 1].name[0] = 0;
          current_token->attrs[current_token->attr_count - 1].value[0] = 0;
        }
        state = TokenizerState::ATTRIBUTE_NAME;
      }
      break;

    case TokenizerState::BEFORE_ATTRIBUTE_VALUE:
      if (is_space(c)) {
        advance();
      } else if (c == '"') {
        state = TokenizerState::ATTRIBUTE_VALUE_DOUBLE_QUOTED;
        advance();
      } else if (c == '\'') {
        state = TokenizerState::ATTRIBUTE_VALUE_SINGLE_QUOTED;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        state = TokenizerState::ATTRIBUTE_VALUE_UNQUOTED;
      }
      break;

    case TokenizerState::ATTRIBUTE_VALUE_DOUBLE_QUOTED:
      if (c == '"') {
        state = TokenizerState::AFTER_ATTRIBUTE_VALUE_QUOTED;
        advance();
      } else if (c == '&') {
        return_state = TokenizerState::ATTRIBUTE_VALUE_DOUBLE_QUOTED;
        state = TokenizerState::CHARACTER_REFERENCE;
        advance();
      } else {
        int idx = current_token->attr_count - 1;
        if (idx >= 0) {
          int len = strlen(current_token->attrs[idx].value);
          if (len < 255) {
            current_token->attrs[idx].value[len] = c;
            current_token->attrs[idx].value[len + 1] = 0;
          }
        }
        advance();
      }
      break;

    case TokenizerState::ATTRIBUTE_VALUE_SINGLE_QUOTED:
      if (c == '\'') {
        state = TokenizerState::AFTER_ATTRIBUTE_VALUE_QUOTED;
        advance();
      } else if (c == '&') {
        return_state = TokenizerState::ATTRIBUTE_VALUE_SINGLE_QUOTED;
        state = TokenizerState::CHARACTER_REFERENCE;
        advance();
      } else {
        int idx = current_token->attr_count - 1;
        if (idx >= 0) {
          int len = strlen(current_token->attrs[idx].value);
          if (len < 255) {
            current_token->attrs[idx].value[len] = c;
            current_token->attrs[idx].value[len + 1] = 0;
          }
        }
        advance();
      }
      break;

    case TokenizerState::ATTRIBUTE_VALUE_UNQUOTED:
      if (is_space(c)) {
        state = TokenizerState::BEFORE_ATTRIBUTE_NAME;
        advance();
      } else if (c == '&') {
        return_state = TokenizerState::ATTRIBUTE_VALUE_UNQUOTED;
        state = TokenizerState::CHARACTER_REFERENCE;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        int idx = current_token->attr_count - 1;
        if (idx >= 0) {
          int len = strlen(current_token->attrs[idx].value);
          if (len < 255) {
            current_token->attrs[idx].value[len] = c;
            current_token->attrs[idx].value[len + 1] = 0;
          }
        }
        advance();
      }
      break;

    case TokenizerState::AFTER_ATTRIBUTE_VALUE_QUOTED:
      if (is_space(c)) {
        state = TokenizerState::BEFORE_ATTRIBUTE_NAME;
        advance();
      } else if (c == '/') {
        state = TokenizerState::SELF_CLOSING_START_TAG;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        state = TokenizerState::BEFORE_ATTRIBUTE_NAME;
      }
      break;

    case TokenizerState::SELF_CLOSING_START_TAG:
      if (c == '>') {
        current_token->self_closing = true;
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        state = TokenizerState::BEFORE_ATTRIBUTE_NAME;
      }
      break;

    case TokenizerState::COMMENT_START:
      if (c == '-') {
        state = TokenizerState::COMMENT_START_DASH;
        advance();
      } else if (strncmp(&input[position], "DOCTYPE", 7) == 0) {
        state = TokenizerState::DOCTYPE;
        for (int i = 0; i < 7; i++)
          advance();
      } else {
        // Bogus comment
        current_token = new Token(TokenType::COMMENT);
        state = TokenizerState::COMMENT;
        // Reconsume
      }
      break;

    case TokenizerState::COMMENT_START_DASH:
      if (c == '-') {
        state = TokenizerState::COMMENT;
        current_token = new Token(TokenType::COMMENT);
        advance();
      } else {
        state = TokenizerState::DATA; // Simplified
        advance();
      }
      break;

    case TokenizerState::COMMENT:
      if (c == '-') {
        state = TokenizerState::COMMENT_END_DASH;
        advance();
      } else if (c == 0) {
        state = TokenizerState::DATA;
      } else {
        int len = strlen(current_token->data);
        if (len < 255) {
          current_token->data[len] = c;
          current_token->data[len + 1] = 0;
        }
        advance();
      }
      break;

    case TokenizerState::COMMENT_END_DASH:
      if (c == '-') {
        state = TokenizerState::COMMENT_END;
        advance();
      } else {
        int len = strlen(current_token->data);
        if (len < 254) {
          current_token->data[len] = '-';
          current_token->data[len + 1] = c;
          current_token->data[len + 2] = 0;
        }
        state = TokenizerState::COMMENT;
        advance();
      }
      break;

    case TokenizerState::COMMENT_END:
      if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        state = TokenizerState::COMMENT; // Simplified
        advance();
      }
      break;

    case TokenizerState::CHARACTER_REFERENCE:
      // Very simplified character reference handling
      if (is_ascii_alpha(c)) {
        state = TokenizerState::NAMED_CHARACTER_REFERENCE;
      } else if (c == '#') {
        state = TokenizerState::NUMERIC_CHARACTER_REFERENCE;
        advance();
      } else {
        // Not a char ref, emit '&'
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '&';
        t.data[1] = 0;
        return t;
      }
      break;

    case TokenizerState::NAMED_CHARACTER_REFERENCE:
      // Extremely simplified: just handle common ones or bail
      if (strncmp(&input[position], "lt;", 3) == 0) {
        for (int i = 0; i < 3; i++)
          advance();
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '<';
        t.data[1] = 0;
        return t;
      } else if (strncmp(&input[position], "gt;", 3) == 0) {
        for (int i = 0; i < 3; i++)
          advance();
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '>';
        t.data[1] = 0;
        return t;
      } else if (strncmp(&input[position], "amp;", 4) == 0) {
        for (int i = 0; i < 4; i++)
          advance();
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '&';
        t.data[1] = 0;
        return t;
      } else if (strncmp(&input[position], "quot;", 5) == 0) {
        for (int i = 0; i < 5; i++)
          advance();
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '"';
        t.data[1] = 0;
        return t;
      } else {
        state = return_state;
        Token t(TokenType::CHARACTER);
        t.data[0] = '&';
        t.data[1] = 0;
        return t;
      }
      break;

    case TokenizerState::DOCTYPE:
      // Simplified DOCTYPE
      if (is_space(c)) {
        advance();
      } else {
        current_token = new Token(TokenType::DOCTYPE);
        state = TokenizerState::DOCTYPE_NAME;
        // Reconsume
      }
      break;

    case TokenizerState::DOCTYPE_NAME:
      if (is_space(c)) {
        state = TokenizerState::AFTER_DOCTYPE_NAME;
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        int len = strlen(current_token->name);
        if (len < 63) {
          current_token->name[len] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
          current_token->name[len + 1] = 0;
        }
        advance();
      }
      break;

    case TokenizerState::AFTER_DOCTYPE_NAME:
      if (is_space(c)) {
        advance();
      } else if (c == '>') {
        state = TokenizerState::DATA;
        advance();
        Token t = *current_token;
        delete current_token;
        current_token = nullptr;
        return t;
      } else {
        advance(); // Skip everything else for now
      }
      break;

    default:
      advance();
      break;
    }
  }
}

} // namespace Browser
