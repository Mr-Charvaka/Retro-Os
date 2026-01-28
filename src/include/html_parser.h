#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include "html_dom.h"
#include "html_tokenizer.h"

namespace Browser {

enum class InsertionMode {
  INITIAL,
  BEFORE_HTML,
  BEFORE_HEAD,
  IN_HEAD,
  AFTER_HEAD,
  IN_BODY,
  TEXT,
  AFTER_BODY,
  AFTER_AFTER_BODY
};

class HTML5Parser {
public:
  HTML5Parser();
  ~HTML5Parser();
  DocumentNode *parse(const char *html);

private:
  void process_token(Token &t, Tokenizer &tokenizer);
  void insert_element(Token &t);
  void insert_character(Token &t);
  void insert_comment(Token &t);
  void insert_doctype(Token &t);

  DocumentNode *document;
  Node *open_elements[64];
  int open_elements_count;
  InsertionMode mode;

  Node *current_node() {
    return open_elements_count > 0 ? open_elements[open_elements_count - 1]
                                   : nullptr;
  }

  void push_element(Node *n) {
    if (open_elements_count < 64) {
      open_elements[open_elements_count++] = n;
    }
  }

  void pop_element() {
    if (open_elements_count > 0) {
      open_elements_count--;
    }
  }
};

} // namespace Browser

#endif
