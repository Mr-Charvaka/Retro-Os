#ifndef HTML_DOM_H
#define HTML_DOM_H

#include "Std/Types.h"
#include "string.h"

namespace Browser {

enum class NodeType {
  ELEMENT_NODE = 1,
  TEXT_NODE = 3,
  COMMENT_NODE = 8,
  DOCUMENT_NODE = 9,
  DOCUMENT_TYPE_NODE = 10
};

struct Node {
  NodeType type;
  Node *parent;
  Node *prev_sibling;
  Node *next_sibling;
  Node *first_child;
  Node *last_child;

  Node(NodeType t)
      : type(t), parent(nullptr), prev_sibling(nullptr), next_sibling(nullptr),
        first_child(nullptr), last_child(nullptr) {}
  virtual ~Node() {
    Node *child = first_child;
    while (child) {
      Node *next = child->next_sibling;
      delete child;
      child = next;
    }
  }

  void append_child(Node *child) {
    child->parent = this;
    if (!first_child) {
      first_child = child;
      last_child = child;
    } else {
      last_child->next_sibling = child;
      child->prev_sibling = last_child;
      last_child = child;
    }
  }
};

struct TextNode : public Node {
  char *text;
  TextNode(const char *t) : Node(NodeType::TEXT_NODE) {
    int len = strlen(t);
    text = new char[len + 1];
    strcpy(text, t);
  }
  ~TextNode() { delete[] text; }
};

struct Attribute {
  char name[64];
  char value[256];
};

struct ElementNode : public Node {
  char tag_name[64];
  Attribute *attributes;
  int attr_count;
  int attr_capacity;

  ElementNode(const char *tag)
      : Node(NodeType::ELEMENT_NODE), attr_count(0), attr_capacity(8) {
    strncpy(tag_name, tag, 63);
    tag_name[63] = 0;
    attributes = new Attribute[attr_capacity];
  }

  ~ElementNode() { delete[] attributes; }

  void set_attribute(const char *name, const char *value) {
    for (int i = 0; i < attr_count; i++) {
      if (strcmp(attributes[i].name, name) == 0) {
        strncpy(attributes[i].value, value, 255);
        attributes[i].value[255] = 0;
        return;
      }
    }
    if (attr_count >= attr_capacity) {
      attr_capacity *= 2;
      Attribute *next = new Attribute[attr_capacity];
      memcpy(next, attributes, sizeof(Attribute) * attr_count);
      delete[] attributes;
      attributes = next;
    }
    strncpy(attributes[attr_count].name, name, 63);
    attributes[attr_count].name[63] = 0;
    strncpy(attributes[attr_count].value, value, 255);
    attributes[attr_count].value[255] = 0;
    attr_count++;
  }

  const char *get_attribute(const char *name) {
    for (int i = 0; i < attr_count; i++) {
      if (strcmp(attributes[i].name, name) == 0) {
        return attributes[i].value;
      }
    }
    return nullptr;
  }
};

struct DocumentNode : public Node {
  char mode[32]; // "no-quirks", etc.
  DocumentNode() : Node(NodeType::DOCUMENT_NODE) { strcpy(mode, "no-quirks"); }
};

struct CommentNode : public Node {
  char *data;
  CommentNode(const char *d) : Node(NodeType::COMMENT_NODE) {
    int len = strlen(d);
    data = new char[len + 1];
    strcpy(data, d);
  }
  ~CommentNode() { delete[] data; }
};

struct DoctypeNode : public Node {
  char name[64];
  char public_id[128];
  char system_id[128];

  DoctypeNode(const char *n, const char *pub, const char *sys)
      : Node(NodeType::DOCUMENT_TYPE_NODE) {
    strncpy(name, n ? n : "", 63);
    strncpy(public_id, pub ? pub : "", 127);
    strncpy(system_id, sys ? sys : "", 127);
  }
};

} // namespace Browser

#endif
