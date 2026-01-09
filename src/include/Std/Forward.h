#pragma once

#include <Std/Types.h>

extern "C" void *kmalloc(size_t);
extern "C" void kfree(void *);

void *operator new(size_t size);
void *operator new[](size_t size);
inline void *operator new(size_t, void *ptr) { return ptr; }
inline void *operator new[](size_t, void *ptr) { return ptr; }
void operator delete(void *ptr) noexcept;
void operator delete[](void *ptr) noexcept;
void operator delete(void *ptr, size_t) noexcept;
void operator delete[](void *ptr, size_t) noexcept;

namespace Std {

template <typename T> struct RemoveReference {
  using Type = T;
};

template <typename T> struct RemoveReference<T &> {
  using Type = T;
};

template <typename T> struct RemoveReference<T &&> {
  using Type = T;
};

template <typename T>
constexpr typename RemoveReference<T>::Type &&move(T &&arg) {
  return static_cast<typename RemoveReference<T>::Type &&>(arg);
}

// Forward Decls
class String;

template <typename T> class Vector;

template <typename T> class RefPtr;

template <typename T> class OwnPtr;

} // namespace Std

using Std::move;
