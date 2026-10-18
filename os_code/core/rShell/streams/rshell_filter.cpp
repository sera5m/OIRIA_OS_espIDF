#include "rshell_filter.hpp"

#include <algorithm>
#include <cstring>

namespace {

bool fail(std::vector<uint8_t>& out) {
    out.clear();
    return false;
}

// First match at or after `from`. npos if none.
size_t search_one(const std::vector<uint8_t>& buf, size_t from,
                  const uint8_t* pat, size_t n) {
    if (!pat || n == 0 || from > buf.size() || n > buf.size() - from) return static_cast<size_t>(-1);
    for (size_t i = from; i + n <= buf.size(); ++i) {
        if (std::memcmp(buf.data() + i, pat, n) == 0) return i;
    }
    return static_cast<size_t>(-1);
}

}  // namespace

size_t PipeFilter::push_mat(const uint8_t* bytes, size_t n) {
    size_t at = material_.size();
    if (bytes && n) material_.insert(material_.end(), bytes, bytes + n);
    return at;
}

void PipeFilter::clear() {
    steps_.clear();
    material_.clear();
}

void PipeFilter::create(size_t at, const uint8_t* bytes, size_t n) {
    Step s{};
    s.op = XformOp::Create;
    s.at = at;
    s.n = n;
    s.mat = push_mat(bytes, n);
    steps_.push_back(s);
}

void PipeFilter::read(size_t at, size_t n) {
    Step s{};
    s.op = XformOp::Read;
    s.at = at;
    s.n = n;
    steps_.push_back(s);
}

void PipeFilter::update(size_t at, const uint8_t* bytes, size_t n) {
    Step s{};
    s.op = XformOp::Update;
    s.at = at;
    s.n = n;
    s.mat = push_mat(bytes, n);
    steps_.push_back(s);
}

void PipeFilter::erase(size_t at, size_t n) {
    Step s{};
    s.op = XformOp::Delete;
    s.at = at;
    s.n = n;
    steps_.push_back(s);
}

void PipeFilter::find(const uint8_t* pattern, size_t n, size_t nth) {
    Step s{};
    s.op = XformOp::Find;
    s.n = n;
    s.mat = push_mat(pattern, n);
    s.count = nth;
    steps_.push_back(s);
}

void PipeFilter::swap_span(size_t at_a, size_t at_b, size_t n) {
    Step s{};
    s.op = XformOp::Swap;
    s.at = at_a;
    s.at2 = at_b;
    s.n = n;
    steps_.push_back(s);
}

void PipeFilter::replace(const uint8_t* old_b, size_t old_n,
                         const uint8_t* new_b, size_t new_n,
                         size_t count) {
    Step s{};
    s.op = XformOp::Replace;
    s.n = old_n;
    s.mat = push_mat(old_b, old_n);
    s.n2 = new_n;
    s.at2 = push_mat(new_b, new_n);
    s.count = count;
    steps_.push_back(s);
}

void PipeFilter::remove(const uint8_t* pattern, size_t n, size_t count) {
    Step s{};
    s.op = XformOp::Remove;
    s.n = n;
    s.mat = push_mat(pattern, n);
    s.count = count;
    steps_.push_back(s);
}

bool PipeFilter::apply(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) const {
    std::vector<uint8_t> buf;
    if (in && in_len) buf.assign(in, in + in_len);

    for (const Step& s : steps_) {
        const bool positional = s.op == XformOp::Create || s.op == XformOp::Read ||
                                s.op == XformOp::Update || s.op == XformOp::Delete ||
                                s.op == XformOp::Swap;
        if (positional && s.at > buf.size()) return fail(out);

        switch (s.op) {
        case XformOp::Create: {
            if (s.n && s.mat + s.n > material_.size()) return fail(out);
            buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                       material_.begin() + static_cast<std::ptrdiff_t>(s.mat),
                       material_.begin() + static_cast<std::ptrdiff_t>(s.mat + s.n));
            break;
        }
        case XformOp::Read: {
            if (s.at + s.n > buf.size()) return fail(out);
            buf.assign(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                       buf.begin() + static_cast<std::ptrdiff_t>(s.at + s.n));
            break;
        }
        case XformOp::Update: {
            if (s.n && s.mat + s.n > material_.size()) return fail(out);
            if (s.at + s.n > buf.size()) buf.resize(s.at + s.n);
            std::copy(material_.begin() + static_cast<std::ptrdiff_t>(s.mat),
                      material_.begin() + static_cast<std::ptrdiff_t>(s.mat + s.n),
                      buf.begin() + static_cast<std::ptrdiff_t>(s.at));
            break;
        }
        case XformOp::Delete: {
            if (s.at + s.n > buf.size()) return fail(out);
            buf.erase(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                      buf.begin() + static_cast<std::ptrdiff_t>(s.at + s.n));
            break;
        }
        case XformOp::Find: {
            if (s.n == 0 || s.mat + s.n > material_.size()) return fail(out);
            const uint8_t* pat = material_.data() + s.mat;
            size_t from = 0;
            size_t seen = 0;
            size_t hit = static_cast<size_t>(-1);
            while (seen <= s.count) {
                hit = search_one(buf, from, pat, s.n);
                if (hit == static_cast<size_t>(-1)) return fail(out);
                if (seen == s.count) break;
                ++seen;
                from = hit + s.n;
            }
            buf.assign(buf.begin() + static_cast<std::ptrdiff_t>(hit),
                       buf.begin() + static_cast<std::ptrdiff_t>(hit + s.n));
            break;
        }
        case XformOp::Swap: {
            if (s.n == 0) break;
            if (s.at2 > buf.size() || s.at + s.n > buf.size() || s.at2 + s.n > buf.size()) return fail(out);
            const bool overlap = s.at < s.at2 + s.n && s.at2 < s.at + s.n;
            if (overlap) return fail(out);
            std::swap_ranges(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                             buf.begin() + static_cast<std::ptrdiff_t>(s.at + s.n),
                             buf.begin() + static_cast<std::ptrdiff_t>(s.at2));
            break;
        }
        case XformOp::Replace: {
            if (s.n == 0 || s.mat + s.n > material_.size()) return fail(out);
            if (s.n2 && s.at2 + s.n2 > material_.size()) return fail(out);
            const uint8_t* oldp = material_.data() + s.mat;
            const uint8_t* newp = s.n2 ? material_.data() + s.at2 : nullptr;
            std::vector<uint8_t> built;
            built.reserve(buf.size());
            size_t i = 0;
            size_t done = 0;
            const bool all = s.count == 0;
            while (i < buf.size()) {
                bool match = (all || done < s.count) && i + s.n <= buf.size() &&
                             std::memcmp(buf.data() + i, oldp, s.n) == 0;
                if (match) {
                    if (newp && s.n2) built.insert(built.end(), newp, newp + s.n2);
                    i += s.n;
                    ++done;
                } else {
                    built.push_back(buf[i]);
                    ++i;
                }
            }
            buf.swap(built);
            break;
        }
        case XformOp::Remove: {
            if (s.n == 0 || s.mat + s.n > material_.size()) return fail(out);
            const uint8_t* pat = material_.data() + s.mat;
            std::vector<uint8_t> built;
            built.reserve(buf.size());
            size_t i = 0;
            size_t done = 0;
            const bool all = s.count == 0;
            while (i < buf.size()) {
                bool match = (all || done < s.count) && i + s.n <= buf.size() &&
                             std::memcmp(buf.data() + i, pat, s.n) == 0;
                if (match) {
                    i += s.n;
                    ++done;
                } else {
                    built.push_back(buf[i]);
                    ++i;
                }
            }
            buf.swap(built);
            break;
        }
        }
    }

    out.swap(buf);
    return true;
}
