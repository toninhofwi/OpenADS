#pragma once

#include "openads/ace.h"  // ADSHANDLE, UNSIGNED32, SIGNED32, UNSIGNED16, UNSIGNED8

namespace openads::abi {

// One backend's table-level ABI operations. A SQL backend fills this struct;
// the native (local DBF) and remote (tcp://) paths do NOT — they remain the
// fall-through in each ABI function. Any pointer may be left null if the
// backend does not implement that op.
struct BackendTableOps {
    UNSIGNED32 (*close_table)      (ADSHANDLE);
    UNSIGNED32 (*goto_top)         (ADSHANDLE);
    UNSIGNED32 (*goto_bottom)      (ADSHANDLE);
    UNSIGNED32 (*skip)             (ADSHANDLE, SIGNED32);
    UNSIGNED32 (*at_eof)           (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*at_bof)           (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*num_fields)       (ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*field_name)       (ADSHANDLE, UNSIGNED16, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*field_type)       (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*field_length)     (ADSHANDLE, UNSIGNED8*, UNSIGNED32*);
    UNSIGNED32 (*field_decimals)   (ADSHANDLE, UNSIGNED8*, UNSIGNED16*);
    UNSIGNED32 (*record_num)       (ADSHANDLE, UNSIGNED32*);
    UNSIGNED32 (*record_count)     (ADSHANDLE, UNSIGNED32*, UNSIGNED16);
    UNSIGNED32 (*get_field)        (ADSHANDLE, UNSIGNED8*, UNSIGNED8*, UNSIGNED32*, UNSIGNED16);
    UNSIGNED32 (*is_record_deleted)(ADSHANDLE, UNSIGNED16*);
    UNSIGNED32 (*open_index)       (ADSHANDLE, UNSIGNED8*, ADSHANDLE*, UNSIGNED16*);
    UNSIGNED32 (*is_found)         (ADSHANDLE, UNSIGNED16*);
    // Tier-2 push-down: install (non-null) or clear (null) a SQL WHERE
    // fragment so the backend filters rows server-side; navigation then walks
    // only matching rows. Null when the backend can't push filters down.
    UNSIGNED32 (*set_filter)       (ADSHANDLE, UNSIGNED8* /*where, null=clear*/);
};

}  // namespace openads::abi
