namespace pg {
extern "C" {
#include <postgres.h>

#include <access/relscan.h>
#include <access/tableam.h>
#include <executor/tuptable.h>
#include <utils/snapmgr.h>
}
}  // namespace pg

#include <cassert>
#include <iterator>

#include "pgexxt/wrap.h"

namespace pgexxt {
struct ScannerBase {
 public:
  class iterator
      : public std::iterator<std::input_iterator_tag, TupleTableSlot, long,
                             wrap<TupleTableSlot>, TupleTableSlot&> {
    friend struct ScannerBase;

   public:
    // Scanner iterators can be moved, but not copied since the state
    // will be lost.
    iterator(const iterator&) = delete;
    iterator(iterator&&) = default;
    iterator& operator=(const iterator&) = delete;

    iterator(Relation rel, ScanDirection dir, TupleTableSlot* slot)
        : scandesc_(table_beginscan(rel, GetActiveSnapshot(), 0, NULL)),
          dir_(dir),
          slot_(slot),
          more_(table_scan_getnextslot(scandesc_, dir, slot_)) {}

    bool operator==(const iterator& other) { return (more_ == other.more_); }
    bool operator!=(const iterator& other) { return !(*this == other); }

    wrap<TupleTableSlot> operator*() { return wrap<TupleTableSlot>(slot_); }

    iterator& operator++() {
      Advance();
      return *this;
    }

    ~iterator() {
      if (scandesc_)
        table_endscan(scandesc_);
    }

   private:
    iterator()
        : scandesc_(nullptr),
          dir_(NoMovementScanDirection),
          slot_(nullptr),
          more_(false) {}

    void Advance() {
      assert(more_);
      more_ = table_scan_getnextslot(scandesc_, dir_, slot_);
    }

    TableScanDesc scandesc_;
    ScanDirection dir_;
    TupleTableSlot* slot_;
    bool more_;
  };

  ScannerBase(Relation rel)
      : relation_(rel),
        slot_(
            MakeSingleTupleTableSlot(rel->rd_att, table_slot_callbacks(rel))) {}

  ~ScannerBase() { ExecDropSingleTupleTableSlot(slot_); }

  iterator begin() { return iterator(relation_, ForwardScanDirection, slot_); }

  iterator end() { return iterator(); }

 private:
  Relation relation_;
  TupleTableSlot* slot_;
};

class ForwardScanner : public ScannerBase {
 public:
  ForwardScanner(Relation rel) : ScannerBase(rel) {}
};

}  // namespace pgexxt
