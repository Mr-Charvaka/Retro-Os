#include "../include/html_parser.h"

namespace Browser {

HTML5Parser::HTML5Parser()
    : document(nullptr), open_elements_count(0), mode(InsertionMode::INITIAL) {}

HTML5Parser::~HTML5Parser() {}

DocumentNode *HTML5Parser::parse(const char *html) {
  document = new DocumentNode();
  open_elements_count = 0;
  push_element(document);
  mode = InsertionMode::INITIAL;

  Tokenizer tokenizer(html);
  while (true) {
    Token t = tokenizer.next_token();
    if (t.type == TokenType::EOF_TOKEN)
      break;
    process_token(t, tokenizer);
  }

  return document;
}

void HTML5Parser::process_token(Token &t, Tokenizer &tokenizer) {
  switch (mode) {
  case InsertionMode::INITIAL:
    if (t.type == TokenType::CHARACTER &&
        (t.data[0] == ' ' || t.data[0] == '\n'))
      break;
    if (t.type == TokenType::DOCTYPE) {
      insert_doctype(t);
      mode = InsertionMode::BEFORE_HTML;
    } else {
      mode = InsertionMode::BEFORE_HTML;
      process_token(t, tokenizer);
    }
    break;

  case InsertionMode::BEFORE_HTML:
    if (t.type == TokenType::CHARACTER &&
        (t.data[0] == ' ' || t.data[0] == '\n'))
      break;
    if (t.type == TokenType::START_TAG && strcmp(t.name, "html") == 0) {
      insert_element(t);
      mode = InsertionMode::BEFORE_HEAD;
    } else {
      Token html_token(TokenType::START_TAG);
      strcpy(html_token.name, "html");
      insert_element(html_token);
      mode = InsertionMode::BEFORE_HEAD;
      process_token(t, tokenizer);
    }
    break;

  case InsertionMode::BEFORE_HEAD:
    if (t.type == TokenType::CHARACTER &&
        (t.data[0] == ' ' || t.data[0] == '\n'))
      break;
    if (t.type == TokenType::START_TAG && strcmp(t.name, "head") == 0) {
      insert_element(t);
      mode = InsertionMode::IN_HEAD;
    } else {
      Token head_token(TokenType::START_TAG);
      strcpy(head_token.name, "head");
      insert_element(head_token);
      mode = InsertionMode::IN_HEAD;
      process_token(t, tokenizer);
    }
    break;

  case InsertionMode::IN_HEAD:
    if (t.type == TokenType::CHARACTER &&
        (t.data[0] == ' ' || t.data[0] == '\n')) {
      insert_character(t);
      break;
    }
    if (t.type == TokenType::END_TAG && strcmp(t.name, "head") == 0) {
      pop_element();
      mode = InsertionMode::AFTER_HEAD;
    } else if (t.type == TokenType::START_TAG && strcmp(t.name, "title") == 0) {
      insert_element(t);
      tokenizer.set_state(TokenizerState::RCDATA);
      mode = InsertionMode::TEXT;
    } else if (t.type == TokenType::START_TAG &&
               (strcmp(t.name, "style") == 0 ||
                strcmp(t.name, "script") == 0)) {
      insert_element(t);
      tokenizer.set_state(TokenizerState::RAWTEXT);
      mode = InsertionMode::TEXT;
    } else if (t.type == TokenType::START_TAG && strcmp(t.name, "meta") == 0) {
      insert_element(t);
      pop_element();
    } else {
      pop_element();
      mode = InsertionMode::AFTER_HEAD;
      process_token(t, tokenizer);
    }
    break;

  case InsertionMode::AFTER_HEAD:
    if (t.type == TokenType::CHARACTER &&
        (t.data[0] == ' ' || t.data[0] == '\n')) {
      insert_character(t);
      break;
    }
    if (t.type == TokenType::START_TAG && strcmp(t.name, "body") == 0) {
      insert_element(t);
      mode = InsertionMode::IN_BODY;
    } else {
      Token body_token(TokenType::START_TAG);
      strcpy(body_token.name, "body");
      insert_element(body_token);
      mode = InsertionMode::IN_BODY;
      process_token(t, tokenizer);
    }
    break;

  case InsertionMode::IN_BODY:
    if (t.type == TokenType::CHARACTER) {
      insert_character(t);
    } else if (t.type == TokenType::START_TAG) {
      insert_element(t);
      const char *void_elements[] = {"br",   "img",  "input",
                                     "link", "meta", "hr"};
      for (auto v : void_elements) {
        if (strcmp(t.name, v) == 0) {
          pop_element();
          break;
        }
      }
    } else if (t.type == TokenType::END_TAG) {
      if (strcmp(t.name, "body") == 0) {
        mode = InsertionMode::AFTER_BODY;
      } else {
        while (open_elements_count > 1) {
          Node *n = current_node();
          if (n->type == NodeType::ELEMENT_NODE) {
            ElementNode *en = (ElementNode *)n;
            if (strcmp(en->tag_name, t.name) == 0) {
              pop_element();
              break;
            }
          }
          pop_element();
        }
      }
    }
    break;

  case InsertionMode::TEXT:
    if (t.type == TokenType::CHARACTER) {
      insert_character(t);
    } else if (t.type == TokenType::END_TAG) {
      pop_element();
      mode = InsertionMode::IN_HEAD; // Simplified
      tokenizer.set_state(TokenizerState::DATA);
    }
    break;

  case InsertionMode::AFTER_BODY:
    if (t.type == TokenType::END_TAG && strcmp(t.name, "html") == 0) {
      mode = InsertionMode::AFTER_AFTER_BODY;
    }
    break;

  default:
    break;
  }
}

void HTML5Parser::insert_element(Token &t) {
  ElementNode *el = new ElementNode(t.name);
  for (int i = 0; i < t.attr_count; i++) {
    el->set_attribute(t.attrs[i].name, t.attrs[i].value);
  }
  current_node()->append_child(el);
  push_element(el);
}

void HTML5Parser::insert_character(Token &t) {
  Node *parent = current_node();
  if (parent->last_child && parent->last_child->type == NodeType::TEXT_NODE) {
    TextNode *tn = (TextNode *)parent->last_child;
    int old_len = strlen(tn->text);
    char *next = new char[old_len + 2];
    strcpy(next, tn->text);
    next[old_len] = t.data[0];
    next[old_len + 1] = 0;
    delete[] tn->text;
    tn->text = next;
  } else {
    TextNode *tn = new TextNode(t.data);
    parent->append_child(tn);
  }
}

void HTML5Parser::insert_doctype(Token &t) {
  DoctypeNode *d = new DoctypeNode(t.name, nullptr, nullptr);
  document->append_child(d);
}

} // namespace Browser
