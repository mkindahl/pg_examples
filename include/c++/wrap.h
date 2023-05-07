// -*- Mode: C++ -*-
namespace pg {
extern "C" {
#include <postgres.h>

#include <utils/elog.h>
}
}  // namespace pg

template <class Type>
class wrap {
 public:
  explicit wrap(Type* ptr) : ptr_(ptr) {
    elog(DEBUG2, "%s", __PRETTY_FUNCTION__);
  }

  wrap(const wrap& other) : ptr_(other.ptr_) {
    elog(DEBUG2, "%s", __PRETTY_FUNCTION__);
  }

  Type* operator->() { return ptr_; }

  operator Type*() { return ptr_; }

 private:
  Type* ptr_;
};
