#pragma once

#include <Std/Assertions.h>
#include <Std/Forward.h>
#include <Std/Types.h>

extern "C" {
size_t strlen(const char *);
char *strcpy(char *, const char *);
char *strcat(char *, const char *);
int strcmp(const char *, const char *);
void *kmalloc(size_t);
void kfree(void *);
}

namespace Std {

class String {
public:
  String() : m_data(nullptr), m_length(0) {}

  String(const char *cstr) {
    if (!cstr) {
      m_data = nullptr;
      m_length = 0;
      return;
    }
    m_length = strlen(cstr);
    m_data = (char *)kmalloc(m_length + 1);
    strcpy(m_data, cstr);
  }

  String(const String &other) {
    if (other.m_data) {
      m_length = other.m_length;
      m_data = (char *)kmalloc(m_length + 1);
      strcpy(m_data, other.m_data);
    } else {
      m_data = nullptr;
      m_length = 0;
    }
  }

  String(String &&other) {
    m_data = other.m_data;
    m_length = other.m_length;
    other.m_data = nullptr;
    other.m_length = 0;
  }

  ~String() {
    if (m_data) {
      // kfree(m_data); // TODO: Enable when kfree is ready
    }
  }

  String &operator=(const String &other) {
    if (this == &other)
      return *this;
    // if (m_data) kfree(m_data);
    if (other.m_data) {
      m_length = other.m_length;
      m_data = (char *)kmalloc(m_length + 1);
      strcpy(m_data, other.m_data);
    } else {
      m_data = nullptr;
      m_length = 0;
    }
    return *this;
  }

  String operator+(const char *other) const {
    if (!other)
      return *this;
    size_t other_len = strlen(other);
    char *new_data = (char *)kmalloc(m_length + other_len + 1);
    if (m_data)
      strcpy(new_data, m_data);
    else
      new_data[0] = 0;
    strcat(new_data, other);
    String res;
    res.m_data = new_data;
    res.m_length = m_length + other_len;
    return res;
  }

  String operator+(const String &other) const { return *this + other.c_str(); }

  String &operator+=(const char *other) {
    if (!other)
      return *this;
    size_t other_len = strlen(other);
    char *new_data = (char *)kmalloc(m_length + other_len + 1);
    if (m_data)
      strcpy(new_data, m_data);
    else
      new_data[0] = 0;
    strcat(new_data, other);
    m_data = new_data;
    m_length += other_len;
    return *this;
  }

  String &operator+=(const String &other) { return *this += other.c_str(); }

  bool operator==(const char *other) const {
    if (!m_data || !other)
      return m_data == other;
    return strcmp(m_data, other) == 0;
  }

  bool operator==(const String &other) const { return *this == other.c_str(); }
  bool operator!=(const char *other) const { return !(*this == other); }
  bool operator!=(const String &other) const { return !(*this == other); }

  bool operator<(const String &other) const {
    if (!m_data || !other.m_data)
      return m_data < other.m_data;
    return strcmp(m_data, other.m_data) < 0;
  }
  bool operator>(const String &other) const {
    if (!m_data || !other.m_data)
      return m_data > other.m_data;
    return strcmp(m_data, other.m_data) > 0;
  }
  bool operator<=(const String &other) const { return !(*this > other); }
  bool operator>=(const String &other) const { return !(*this < other); }

  size_t find_last_of(char c) const {
    if (!m_data)
      return (size_t)-1;
    for (int i = (int)m_length - 1; i >= 0; i--) {
      if (m_data[i] == c)
        return (size_t)i;
    }
    return (size_t)-1;
  }

  String substr(size_t pos, size_t len = (size_t)-1) const {
    if (pos >= m_length)
      return String("");
    if (len == (size_t)-1 || pos + len > m_length)
      len = m_length - pos;
    char *buf = (char *)kmalloc(len + 1);
    for (size_t i = 0; i < len; i++)
      buf[i] = m_data[pos + i];
    buf[len] = 0;
    String res;
    res.m_data = buf;
    res.m_length = len;
    return res;
  }

  const char *c_str() const { return m_data ? m_data : ""; }
  size_t length() const { return m_length; }

  static String format(const char *fmt, ...); // To verify later

private:
  char *m_data;
  size_t m_length;
};

inline String operator+(const char *lhs, const String &rhs) {
  return String(lhs) + rhs;
}

inline String to_string(int val) {
  char buf[32];
  int i = 0;
  if (val == 0) {
    buf[i++] = '0';
  } else {
    bool neg = false;
    if (val < 0) {
      neg = true;
      val = -val;
    }
    while (val > 0) {
      buf[i++] = '0' + (val % 10);
      val /= 10;
    }
    if (neg)
      buf[i++] = '-';
  }
  buf[i] = 0;
  // Reverse
  for (int j = 0; j < i / 2; j++) {
    char t = buf[j];
    buf[j] = buf[i - j - 1];
    buf[i - j - 1] = t;
  }
  return String(buf);
}

} // namespace Std

using Std::String;
using Std::to_string;
