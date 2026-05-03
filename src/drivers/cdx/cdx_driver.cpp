#include "drivers/cdx/cdx_driver.h"

#include <vector>

namespace openads::drivers::cdx {

util::Result<void> CdxDriver::open(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::ReadOnly);
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    // Read the 32-byte header.
    std::uint8_t hdr_buf[32]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();
    if (got.value() < 32) {
        return util::Error{5103, 0, "DBF header truncated", path};
    }

    auto hdr = parse_dbf_header(hdr_buf, sizeof(hdr_buf));
    if (!hdr) return hdr.error();
    rec_count_ = hdr.value().record_count;
    rec_len_   = hdr.value().record_length;
    hdr_len_   = hdr.value().header_length;

    // Read the field-descriptor block (header_length - 32 bytes).
    if (hdr_len_ < 33) {
        return util::Error{5103, 0, "DBF header length below 33 bytes", path};
    }
    std::size_t fd_size = hdr_len_ - 32;
    std::vector<std::uint8_t> fd_buf(fd_size, 0);
    auto fd_got = file_.read_at(32, fd_buf.data(), fd_buf.size());
    if (!fd_got) return fd_got.error();
    if (fd_got.value() < fd_buf.size()) {
        return util::Error{5103, 0, "field-descriptor block truncated", path};
    }
    auto fields = parse_dbf_fields(fd_buf.data(), fd_buf.size());
    if (!fields) return fields.error();
    fields_ = std::move(fields).value();
    return {};
}

util::Result<std::vector<std::uint8_t>>
CdxDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0 || recno > rec_count_) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    std::vector<std::uint8_t> buf(rec_len_, 0);
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto got = file_.read_at(offset, buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < buf.size()) {
        return util::Error{5000, 0, "short read on record body", ""};
    }
    return buf;
}

} // namespace openads::drivers::cdx
