//
//  packed_structs.cpp
//

#include "bdsg/internal/packed_structs.hpp"


namespace bdsg {
    

const double PackedDeque::factor = 1.25;

PackedDeque::PackedDeque() {
    
}
    
PackedDeque::PackedDeque(istream& in) {
    deserialize(in);
}

PackedDeque::~PackedDeque() {
    
}
    
void PackedDeque::deserialize(istream& in) {
    sdsl::read_member(begin_idx, in);
    sdsl::read_member(filled, in);
    vec.deserialize(in);
}

void PackedDeque::serialize(ostream& out) const  {
    sdsl::write_member(begin_idx, out);
    sdsl::write_member(filled, out);
    vec.serialize(out);
}

size_t PackedDeque::memory_usage() const {
    return sizeof(begin_idx) + sizeof(filled) + vec.memory_usage();
}

PackedSet::PackedSet() : gen(random_device()()) {
    table.resize(bdsg_packed_set_size_schedule[0]);
}

PackedSet::iterator PackedSet::begin() const {
    return iterator(this);
}

PackedSet::iterator PackedSet::end() const {
    return iterator(this, table.size());
}

PackedSet::iterator::iterator(const PackedSet* iteratee) : iteratee(iteratee) {
//    cerr << "making iterator to table: " << endl;
//    for (size_t j = 0; j < iteratee->table.size(); j++) {
//        cerr << iteratee->table.get(j) << " ";
//    }
//    cerr << endl;
    // make sure i is always pointing at a non-null element
    while (i < iteratee->table.size() && iteratee->table.get(i) == 0) {
//        cerr << "advancing from " << i << " to find initial position" << endl;
        ++i;
    }
//    cerr << "starting at " << i << endl;
}

PackedSet::iterator::iterator(const PackedSet* iteratee, size_t i) : iteratee(iteratee), i(i) {
    // nothing to do
}

PackedSet::iterator& PackedSet::iterator::operator++() {
    // advance to the next non-null element
    do {
//        cerr << "advancing from " << i << " to find next position" << endl;
        ++i;
    } while (i < iteratee->table.size() && iteratee->table.get(i) == 0);
//    cerr << "at " << i << endl;
    return *this;
}

uint64_t PackedSet::iterator::operator*() const {
    return iteratee->from_diff(iteratee->table.get(i), iteratee->anchor);
}

bool PackedSet::iterator::operator==(const PackedSet::iterator& other) const {
//    cerr << "checking == for " << iteratee << " " << i << " and " << other.iteratee << " " << other.i << endl;
    return iteratee == other.iteratee && i == other.i;
}

bool PackedSet::iterator::operator!=(const PackedSet::iterator& other) const {
    return !(*this == other);
}


}

