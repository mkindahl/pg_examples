
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

#include "wrap.h"

#define F(X) ((X) ? "true" : "false")

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
          more_(table_scan_getnextslot(scandesc_, dir, slot_)) {
      elog(DEBUG2, "%s: ran table_beginscan()", __PRETTY_FUNCTION__);
      elog(DEBUG2, "%s: ran table_scan_getnextslot()", __PRETTY_FUNCTION__);
    }

    bool operator==(const iterator& other) {
      elog(DEBUG2, "%s: lhs.more: %s, rhs.more: %s", __PRETTY_FUNCTION__,
           F(more_), F(other.more_));
      return (more_ == other.more_);
    }

    bool operator!=(const iterator& other) { return !(*this == other); }

    wrap<TupleTableSlot> operator*() {
      elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
      return wrap<TupleTableSlot>(slot_);
    }

    iterator& operator++() {
      elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
      Advance();
      return *this;
    }

    ~iterator() {
      elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
      elog(DEBUG2, "%s", __PRETTY_FUNCTION__);
      if (scandesc_)
        table_endscan(scandesc_);
    }

   private:
    iterator()
        : scandesc_(nullptr),
          dir_(NoMovementScanDirection),
          slot_(nullptr),
          more_(false) {
      elog(DEBUG2, "%s", __PRETTY_FUNCTION__);
    }

    void Advance() {
      assert(more_);
      more_ = table_scan_getnextslot(scandesc_, dir_, slot_);
      elog(DEBUG2, "%s: ran table_scan_getnextslot() ", __PRETTY_FUNCTION__);
    }

    TableScanDesc scandesc_;
    ScanDirection dir_;
    TupleTableSlot* slot_;
    bool more_;
  };

  ScannerBase(Relation rel)
      : relation_(rel),
        slot_(
            MakeSingleTupleTableSlot(rel->rd_att, table_slot_callbacks(rel))) {
    elog(DEBUG2, "%s: ran MakeSingleTupleTableSlot()", __PRETTY_FUNCTION__);
    elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
  }

  ~ScannerBase() {
    elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
    ExecDropSingleTupleTableSlot(slot_);
    elog(DEBUG2, "%s: ran ExecDropSingleTupleTableSlot()", __PRETTY_FUNCTION__);
  }

  iterator begin() {
    elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
    return iterator(relation_, ForwardScanDirection, slot_);
  }

  iterator end() {
    elog(DEBUG2, "%s: slot: %p", __PRETTY_FUNCTION__, slot_);
    return iterator();
  }

 private:
  Relation relation_;
  TupleTableSlot* slot_;
};

class ForwardScanner : public ScannerBase {
 public:
  ForwardScanner(Relation rel) : ScannerBase(rel) {
    elog(DEBUG2, "%s", __PRETTY_FUNCTION__);
  }
};
