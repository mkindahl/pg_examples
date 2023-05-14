namespace pg {
extern "C" {
#include <postgres.h>

#include <access/relscan.h>
#include <access/tableam.h>
#include <executor/tuptable.h>
#include <utils/snapmgr.h>
}
}  // namespace pg

namespace pgexxt {

class Attribute {
 public:
  Attribute(Form_pg_attribute attr) : attr_(attr) {}
  bool is_dropped() { return attr_->attisdropped; }

 private:
  Form_pg_attribute attr_;
};

class Table {
 public:
  class Handle {
   public:
    Handle(Oid reloid, LOCKMODE mode)
        : relation_(table_open(reloid, mode)),
          tupdesc_(RelationGetDescr(relation_)) {}
    ~Handle() { table_close(relation_, NoLock); }

    TupleDesc GetTupleDesc() const { return tupdesc_; }
    ForwardScanner ForwardScan() const { return ForwardScanner(relation_); }
    Attribute attribute(int i) const { return TupleDescAttr(tupdesc_, i); }

   private:
    Relation relation_;
    TupleDesc tupdesc_;
  };
};
}  // namespace pgexxt
