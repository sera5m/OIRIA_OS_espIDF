#include "rshell_filter.hpp"

#include <algorithm>

void PipeFilter::clear() {
    steps_.clear();
    material_.clear();
}

void PipeFilter::create(size_t at, const uint8_t* bytes, size_t n) {
    Step s{CrudOp::Create, at, n, material_.size()};
    if (bytes && n) material_.insert(material_.end(), bytes, bytes + n);
    steps_.push_back(s);
}

void PipeFilter::read(size_t at, size_t n) {
    steps_.push_back(Step{CrudOp::Read, at, n, 0});
}

void PipeFilter::update(size_t at, const uint8_t* bytes, size_t n) {
    Step s{CrudOp::Update, at, n, material_.size()};
    if (bytes && n) material_.insert(material_.end(), bytes, bytes + n);
    steps_.push_back(s);
}

void PipeFilter::erase(size_t at, size_t n) {
    steps_.push_back(Step{CrudOp::Delete, at, n, 0});
}

bool PipeFilter::apply(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) const {
    std::vector<uint8_t> buf;
    if (in && in_len) buf.assign(in, in + in_len);

    for (const Step& s : steps_) {
        if (s.at > buf.size()) {
            out.clear();
            return false;
        }
        switch (s.op) {
        case CrudOp::Create: {
            if (s.n && s.mat + s.n > material_.size()) {
                out.clear();
                return false;
            }
            buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                       material_.begin() + static_cast<std::ptrdiff_t>(s.mat),
                       material_.begin() + static_cast<std::ptrdiff_t>(s.mat + s.n));
            break;
        }
        case CrudOp::Read: {
            if (s.at + s.n > buf.size()) {
                out.clear();
                return false;
            }
            buf.assign(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                       buf.begin() + static_cast<std::ptrdiff_t>(s.at + s.n));
            break;
        }
        case CrudOp::Update: {
            if (s.n && s.mat + s.n > material_.size()) {
                out.clear();
                return false;
            }
            if (s.at + s.n > buf.size()) buf.resize(s.at + s.n);
            std::copy(material_.begin() + static_cast<std::ptrdiff_t>(s.mat),
                      material_.begin() + static_cast<std::ptrdiff_t>(s.mat + s.n),
                      buf.begin() + static_cast<std::ptrdiff_t>(s.at));
            break;
        }
        case CrudOp::Delete: {
            if (s.at + s.n > buf.size()) {
                out.clear();
                return false;
            }
            buf.erase(buf.begin() + static_cast<std::ptrdiff_t>(s.at),
                      buf.begin() + static_cast<std::ptrdiff_t>(s.at + s.n));
            break;
        }
        }
    }

    out.swap(buf);
    return true;
}
