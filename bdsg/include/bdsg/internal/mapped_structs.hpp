//
//  mapped_structs.hpp
//  
// Contains implementations of classic data structures for use in a memory-mapped area.
//

#ifndef BDSG_MAPPED_STRUCTS_HPP_INCLUDED
#define BDSG_MAPPED_STRUCTS_HPP_INCLUDED

#include <cstdio>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <functional>
#include <limits>

#include <map>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

#include <endian.h>

namespace bdsg {
    
using namespace std;

/**
 * Wrapper that stores a number in a defined endian-ness.
 */
template<typename T>
class big_endian {
public:
    big_endian();
    big_endian(const T& value);
    operator T () const;
    big_endian<T>& operator=(const T& x);
protected:
    char storage[sizeof(T)];
};

/**
 * You Only Map Once: mapped memory with safe allocation by objects in the mapped memory.
 *
 * YOMO provides an interconnected system for file-backed memory mapping and
 * allocation, so that objects whose *this is in a mapped memory segment can
 * safely allocate more memory backed by the file the object exists in.
 * Allocating more memory will not unmap memory already allocated.
 */
namespace yomo {

/**
 * Pointer to an object of type T, which lives at an address mapped by a
 * yomo::Allocator (or otherwise in a yomo::Manager's chain), and is itself
 * stored at an address mapped by a yomo::Allocator (or otherwise in the same
 * yomo::Manager chain).
 *
 * We interpret constness as applying to the value of the pointer and not the
 * pointed-to object. If you want a pointer to a const object, you need a
 * Pointer<const T>.
 */
template<typename T>
class Pointer {
public:
    /// Be constructable.
    /// Constructs as a pointer that equals nullptr.
    Pointer() = default;
    
    Pointer(T* destination);
    
    // Be a good pointer
    operator bool () const;
    T& operator*() const;
    T* operator->() const;
    operator T* () const;
    Pointer<T>& operator=(T* addr);
    T* operator+(size_t items) const;
    
    // Expose the address of the pointed-to object not through manual operator
    // calling.
    T* get() const;
    
protected:
    /// Stores the destination position in the chain, or max size_t for null.
    big_endian<size_t> position = std::numeric_limits<size_t>::max();
};

/**
 * Global manager of mapped memory segments. Talked to by pointers in memory
 * segments to figure out where they actually point to.
 *
 * The manager manages one or more "chains", each potentially corresponding to
 * a file. The chains are made up of mapped memory "segments", and each segment
 * can be mapped at a different base address.
 *
 * When a file is initially mapped, it is mapped as a single segment.
 * Additional segments may be mapped later to fulfill allocations from
 * yomo::Allocator<T> instances stored in the chain.
 */
class Manager {

public:

    using chainid_t = intptr_t;
    static const chainid_t NO_CHAIN = 0;

    /**
     * Create a chain not backed by any file. The given prefix data will occur
     * before the chain allocator data structures.
     */
    static chainid_t create_chain(const std::string& prefix = "");
    
    /**
     * Create a chain by mapping all of the given open file.
     *
     * Modifications to the chain will affect the file, and it will grow as
     * necessary.
     *
     * The Manager will not take ownership of the file descriptor.
     *
     * If the file is nonempty, data after the length of the passed prefix must
     * contain the chain allocator data structures. If it is empty, the prefix
     * and the chain allocator data structures will be written to it.
     */
    static chainid_t create_chain(int fd, const std::string& prefix = "");
    
    /**
     * Return a chain which has the same stored data as the given chain, but
     * for which modification of the chain will not modify any backing file on
     * disk. The chain returned may be the same chain as the given chain.
     *
     * Not thread safe with concurrent modificatons to the source chain.
     */
    static chainid_t get_dissociated_chain(chainid_t chain);
    
    /**
     * Return a chain which has the same stored data as the given chain, but
     * for which modification of the chain will modify the open file with the
     * given file descriptor. The chain returned may be the same chain as the
     * given chain.
     *
     * The Manager will not take ownership of the file descriptor.
     *
     * Not thread safe with concurrent modificatons to the source chain.
     */
    static chainid_t get_associated_chain(chainid_t chain, int fd);
    
    /**
     * Destroy the given chain and unmap all of its memory, and close any
     * associated file.
     */
    static void destroy_chain(chainid_t chain);
    
    /**
     * Get the chain that contains the given address, or NO_CHAIN if the
     * address is outside all current chains.
     */
    static chainid_t get_chain(const void* address);
    
    /**
     * Get the address of the given byte from the start of the chain.
     * If a length is provided, throws if the given length of bytes from
     * position are not contiguous in memory.
     */
    static void* get_address_in_chain(chainid_t chain, size_t position, size_t length = 0);
    
    /**
     * Get the position of the given byte in the chain it is in, along with the
     * identifier for that chain.
     * If a length is provided, throws if the given length of bytes from
     * position are not contiguous in memory.
     */
    static std::pair<chainid_t, size_t> get_chain_and_position(const void* address, size_t length = 0);
    
    /**
     * Find the address of the given position in the chain that the given address exists in.
     */
    static void* get_address_in_same_chain(const void* here, size_t position);
    
    /**
     * Find the position of the given address in the chain that here exists in.
     */
    static size_t get_position_in_same_chain(const void* here, const void* address);
    
    /**
     * Allocate the given number of bytes from the given chain.
     */
    static void* allocate_from(chainid_t chain, size_t bytes);
    
    /**
     * Allocate the given number of bytes from the chain containing the given
     * address.
     */
    static void* allocate_from_same_chain(void* here, size_t bytes);
    
    /**
     * Free the given allocated block in the chain to which it belongs.
     */
    static void deallocate(void* address);
    
    /**
     * Find the mapped address of the first thing allocated in the chain, given
     * that it was allocated with the given size.
     *
     * That first allocated thing must exist and not be deallocated.
     */
    static void* find_first_allocation(chainid_t chain, size_t bytes);
    
    /**
     * Dump information about free and allocated memory.
     * Not thread safe!
     */
    static void dump(chainid_t chain);  
    
protected:
    
    
    struct LinkRecord;
    
    /**
     * This occurs inside the chains and represents the header of some free or
     * allocated memory.
     */
    struct AllocatorBlock {
        /// Previous block. Only used when block is free. Null if allocated.
        Pointer<AllocatorBlock> prev;
        /// Next block. Only used when block is free. Null if allocated.
        Pointer<AllocatorBlock> next;
        /// Size fo the block in bytes, not counting this header. Used for free
        /// and allocated blocks.
        big_endian<size_t> size;
        
        /// Get the address of the first byte of memory we manage.
        void* get_user_data() const;
        /// Get the address of the block managing the data starting at the given byte.
        static AllocatorBlock* get_from_data(void* user_data);
        
        /// Split the block, keeping first_bytes bytes and giving the rest to a new
        /// subsequent block, which is wired up and returned. Assumes the block is
        /// free.
        AllocatorBlock* split(size_t first_bytes);
        
        /// Remove this block from the free list. Returns the blocks before and
        /// after it, which is has wired together. If this was the first or last
        /// block (or both), the appropriate return value will be null.
        std::pair<AllocatorBlock*, AllocatorBlock*> detach();
        
        /// Attach this block to the free list, between the given blocks, which may
        /// be null.
        void attach(AllocatorBlock* left, AllocatorBlock* right);
        
        /// Defragment and coalesce adjacent free blocks in the contiguous run this
        /// block is part of, if any. Returns the first and last blocks in the run;
        /// the last block's header will be in the free space of the first block,
        /// unless the last block is the first block.
        std::pair<AllocatorBlock*, AllocatorBlock*> coalesce();
        
    protected:
        
        /// Return true if this block comes immediately before the other one, with
        /// no space between them.
        bool immediately_before(const AllocatorBlock* other) const;
    };

    /**
     * This occurs at the start of a chain, after any prefix, and lets us find the free list.
     */
    struct AllocatorHeader {
        /// Where is the first free block of memory?
        Pointer<AllocatorBlock> first_free;
        /// Where is the last free block of memory?
        Pointer<AllocatorBlock> last_free;
    };
    
    
    /**
     * For each chain, stores each mapping's start address by chain offset position.
     * Each entry is useful for bound queries.
     */
    static unordered_map<chainid_t, map<size_t, intptr_t>> chain_space_index;
    
    /**
     * For each memory start address, what mapping does it start?
     * Useful for bound queries.
     */
    static map<intptr_t, LinkRecord> address_space_index;
    
    /**
     * Mutex for a readers-writer lock on the indexes, for allocating.
     */
    static shared_timed_mutex mutex;
    
    /**
     * How big should a link be to start with.
     */
    static constexpr size_t BASE_SIZE = 1024;
    
    /**
     * Of that, how many bytes should we allow the prefix to possibly use?
     * The allocator has to be able to keep its header within the rest of the first link.
     */
    static constexpr size_t MAX_PREFIX_SIZE = 16;
    
    /**
     * How much should we expand each new link by?
     */
    static constexpr size_t SCALE_FACTOR = 2;
   
    /**
     * Create a chain with one link and no allocator setup.
     * Block will either be the entire size of an existing file, or the given starting size.
     * Returns the chain ID and a flag for if there was data in an open file to read.
     */
    static std::pair<chainid_t, bool> open_chain(int fd = 0, size_t start_size = BASE_SIZE);
    
    /**
     * Extend the given chain to the given new total size.
     */
    static void extend_chain_to(chainid_t chain, size_t new_total_size);
    
    /**
     * Add a link into a chain. The caller must hold a write lock on the manager data structures.
     * The number of bytes must be nonzero.
     */
    static LinkRecord& add_link(LinkRecord& head, size_t new_bytes);
    
    /**
     * Create a new chain, using the given file if set, and copy data from the
     * given existing chain.
     */
    static chainid_t copy_chain(chainid_t chain, int fd = 0);
   
    /**
     * Set up the allocator data structures in the first link, assuming they
     * aren't present. Put them at the given offset, and carve them out of the
     * given amoutn of remaining space in the link. 
     */
    static void set_up_allocator_at(chainid_t chain, size_t offset, size_t space);
    
    /**
     * Connect to the allocator data structures in the first link, assuming they are present.
     */
    static void connect_allocator_at(chainid_t chain, size_t offset);
    
    /**
     * Find the allocator header in a chain that has had its allocator connected.
     */
    static AllocatorHeader* find_allocator_header(chainid_t chain);
    
    /**
     * Run the given callback with the allocator header for the given chain.
     * The allocator will be locked; only one function will be run on a given
     * chain's allocator at a time.
     */
    static void with_allocator_header(chainid_t chain,
                                      const std::function<void(AllocatorHeader*)>& callback);

};

/**
 * Allocator that allocates via the yomo::Manager from the chain in which it
 * itself occurs.
 * Still deals in normal pointers, which are safe to use when not storing them
 * in the allocated memory. Pointers in the allocated memory should be
 * yomo::Pointer<T>.
 */
template<typename T>
class Allocator {

public:
    // TODO: If we want to template these like this, we need to make them able
    // to work from the stack and be the types that allocate and deallocate
    // use, if we really want to be standard-compliant.
    using pointer = Pointer<T>;
    using const_pointer = Pointer<const T>;
    using size_type = size_t;
    using value_type = T;
    
    
    Allocator() = default;
    Allocator(const Allocator& other) = default;
    Allocator(Allocator&& other) = default;
    Allocator& operator=(const Allocator& other) = default;
    Allocator& operator=(Allocator&& other) = default;
    
    /**
     * Copy an allocator, but change the allocated type.
     */
    template<typename U>
    Allocator(const Allocator<U>& alloc);
    
    /**
     * Allocate the given number of items. Ought to be near the hint.
     */
    T* allocate(size_type n, const T* hint = nullptr);
    
    /**
     * Deallocate the given number of items. Must be the same number as were allocated.
     */
    void deallocate(T* p, size_type n);

};

/**
 * Interface between normally allocated objects and chain-allocated objects.
 * Pointer to an object that is allocated at the beginning of a chain, and
 * which should allocate, if it allocates, from the chain.
 * Itself lives outside the chain. Can be null.
 *
 * T must use yomo::Pointer as its pointer type and yomo::Allocator as its
 * allocator type.
 */
template<typename T>
class UniqueMappedPointer {
public:
    UniqueMappedPointer() = default;
    UniqueMappedPointer(const UniqueMappedPointer& other) = delete;
    UniqueMappedPointer(UniqueMappedPointer&& other) = default;
    UniqueMappedPointer& operator=(const UniqueMappedPointer& other) = delete;
    UniqueMappedPointer& operator=(UniqueMappedPointer&& other) = default;
    
    operator bool () const;
    T& operator*();
    const T& operator*() const;
    T* operator->();
    const T* operator->() const;
    operator T*();
    operator const T*() const;
    
    /**
     * Get the memory address of the referenced object, or nullptr.
     */
    T* get();
    const T* get() const;

    /**
     * Make a new default-constructed T in memory, preceeded by the given
     * prefix. Forward other arguments to the constructor.
     */
    template <typename... Args>
    void construct(const std::string& prefix = "", Args&&... constructor_args);
    
    /**
     * Point to the already-constructed T saved to the file at fd by a previous
     * save() call.
     */
    void load(int fd, const std::string& prefix = "");
    
    /**
     * Break any write-back association with a backing file and move the object
     * to non-file-backed memory.
     *
     * TODO: Allow private COW mappings by adding features to MIO, to avoid a copy.
     */
    void dissociate();
    
    /**
     * Move the stored item and all associated memory into memory mapped in the
     * given file. The pointer must not be null. No move constructors are
     * called.
     */
    void save(int fd);
    
    /**
     * Free any associated memory and become empty.
     */
    void reset();
private:
    Manager::chainid_t chain = Manager::NO_CHAIN;
};

/**
 * Default-construct a T in the given file, or connect to one previously so constructed.
 */
template<typename T>
UniqueMappedPointer<T> make_mapped(const std::string& prefix, int fd);

};

/**
 * Offset pointer to a primitive type, in the memory mapping.
 * Exists in the memory mapping.
 */
template<typename T>
class offset_ptr {
protected:
    /// Offset from our own storage location to what we point to, or max
    /// possible value if null.
    big_endian<size_t> offset;
public:
    /// Be constructable.
    /// Constructs as a pointer that equals nullptr.
    offset_ptr();
    
    // Be a good pointer
    operator bool () const; // TODO: why doesn't this work as an implicit conversion?
    T& operator*();
    const T& operator*() const;
    T* operator->();
    const T* operator->() const;
    offset_ptr<T>& operator=(const T* addr);
    // TODO: how do we make the const version of us a pointer to a const thing
    // that we can set to a const pointer? FOr now assignment vanishes away
    // constness.
    T* operator+(size_t items);
    const T* operator+(size_t items) const;
    
    // Make sure to account for offset changes when copying or moving
    offset_ptr<T>& operator=(const offset_ptr<T>& other);
    offset_ptr<T>& operator=(offset_ptr<T>&& other);
    offset_ptr(const offset_ptr<T>& other);
    offset_ptr(offset_ptr<T>&& other);
};

// TODO: If we allocate as part of a method on an object in memory that's part
// of the mapping we might need to move to allocate, then when we unmap the
// memory, the object won't exist anymore and its method won't be able to
// finish running.

/**
 * Context in which memory mapping happens. Needs to be passed down through all
 * the reference object constructors so that more reference objects can be
 * made.
 */
struct MappingContext {
    char* base_address;
    size_t size;
    /// When we're trying to allocate and we can't, resize. Might move the whole thing.
    /// Must throw if allocation did not happen. Does not update the context's size.
    std::function<char*(size_t)> resize;
    // TODO: stick a mutex in here?
    // If so we'd also need to protect any math involving base_address, or even
    // the addresses of any things in the arena, or even accesses to any things
    // in the arena...
};

// We work with "reference" types which have a ::body_t that exists in the
// memory mapping at a certain context-relative position, and possibly other
// fields that exist outside it.
// The reference type can have members that e.g. allocate, while the body
// cannot. The reference can be null.
// These are meant to be passed around as values, and should expose some
// accessor methods that operate on the body.

template<typename Derived>
class base_ref_t {
public:
    /// The MappingContext that the referenced object lives in, or nullptr if we are null. 
    MappingContext* context;
    /// The position in the context that the referenced object exists at.
    size_t position;
    
    // Derived must provide a body_t.
    // Also we can't inherit multiple levels.
    
    // Pointer templates expect implementations fo these constructors:
    
    /// Get a reference object to the existing body at the given position.
    base_ref_t(MappingContext* context, size_t position);
    
    /// Allocate and construct a new body at some available position and get a
    /// reference object to it.
    base_ref_t(MappingContext* context);
    
    /// Get a null reference.
    base_ref_t();
    
    // Copy and move
    base_ref_t(const base_ref_t<Derived>& other) = default;
    base_ref_t(base_ref_t<Derived>&& other) = default;
    base_ref_t<Derived>& operator=(const base_ref_t<Derived>& other) = default;
    base_ref_t<Derived>& operator=(base_ref_t<Derived>&& other) = default;
    
    /// Be truthy if not null, and false otherwise
    operator bool () const;
    
    // TODO: we can't have helpers that return Dervied::body_t, even as a
    // pointer, because Derived inherits us and C++ thinks it isn't ready to be
    // used when we're doing our types. So we use a void pointer
    void* get_body();
    const void* get_body() const;
};

/**
 * Offset pointer to the body of a reference type, in the memory mapping.
 * Exists in the memory mapping.
 * Makes it easy to wrap the body back up in a reference type.
 */
template<typename ref_t>
class offset_to : public offset_ptr<typename ref_t::body_t> {
public:
    /// Get a ref_t to the body pointed to. If the pointer is null, the ref is also null.
    ref_t get(MappingContext* context);
    
    /// Get a ref_t to the body pointed to. If the pointer is null, the ref is also null.
    const ref_t get(MappingContext* context) const;
    
    /// Get a ref_t to the body pointed to, plus a given offset in bodies of the same size.
    /// If the pointer is null, the ref is also null.
    ref_t get_at(MappingContext* context, size_t index);
    
    /// Get a ref_t to the body pointed to, plus a given offset in bodies of the same size.
    /// If the pointer is null, the ref is also null.
    const ref_t get_at(MappingContext* context, size_t index) const;
    
    /// Set this pointer to point to the body of the given ref_t
    offset_to<ref_t>& operator=(const ref_t& other);
    
    /// Determine if this pointer points to the body of the given ref.
    bool operator==(const ref_t& other) const;
    /// Determine if this pointer does not point to the body of the given ref.
    bool operator!=(const ref_t& other) const;
};

/**
 * Entry in the list of free blocks in the arena allocator. Also sits before
 * allocated blocks, to remember the blocks' sizes.
 */
class ArenaAllocatorBlockRef : public base_ref_t<ArenaAllocatorBlockRef> {
public:
    struct body_t {
        /// Next block. Only used when block is free.
        offset_to<ArenaAllocatorBlockRef> prev;
        /// Previous block. Only used when block is free.
        offset_to<ArenaAllocatorBlockRef> next;
        /// Size fo the block in bytes, not counting this header. Used for free
        /// and allocated blocks.
        big_endian<size_t> size;
    };
    
    using base_ref_t<ArenaAllocatorBlockRef>::base_ref_t;
    
    // Body member accessors
    
    // I tried to give out references to the backing fields, but getting const
    // correctness on the fields was difficult, and providing the context to
    // use the fields was awkward. So we just pass around refs by value for
    // anything that's a pointer.
    
    /// Get the previous entry in the free list.
    ArenaAllocatorBlockRef get_prev() const;
    
    /// Set the previous entry in the free list.
    void set_prev(const ArenaAllocatorBlockRef& prev);
    
    /// Get the next entry in the free list.
    ArenaAllocatorBlockRef get_next() const;
    
    /// Set the next entry in the free list.
    void set_next(const ArenaAllocatorBlockRef& next);
    
    /// Get the size of the block, after the header.
    const big_endian<size_t>& size() const;
    
    /// Get or set the size of the block, after the header. You can assign to the result.
    big_endian<size_t>& size();
    
    /// Get the position in the context of the first byte of memory we manage.
    size_t get_user_data() const;
    /// Get a ref to the block managing the data starting at the given byte.
    static ArenaAllocatorBlockRef get_from_data(MappingContext* context, size_t user_data);
    
    /// Split the block, keeping first_bytes bytes and giving the rest to a new
    /// subsequent block, which is wired up and returned. Assumes the block is
    /// free.
    ArenaAllocatorBlockRef split(size_t first_bytes);
    
    /// Remove this block from the free list. Returns the blocks before and
    /// after it, which is has wired together. If this was the first or last
    /// block (or both), the appropriate return value will be a null ref.
    pair<ArenaAllocatorBlockRef, ArenaAllocatorBlockRef> detach();
    
    /// Attach this block to the free list, between the given blocks, which may
    /// be null.
    void attach(ArenaAllocatorBlockRef& left, ArenaAllocatorBlockRef& right);
    
    /// Defragment and coalesce adjacent free blocks in the contiguous run this
    /// block is part of, if any. Returns the first and last blocks in the run;
    /// the last block's header will be in the free space of the first block,
    /// unless the last block is the first block.
    pair<ArenaAllocatorBlockRef, ArenaAllocatorBlockRef> coalesce();
    
protected:
    
    /// Return true if this block comes immediately before the other one, with
    /// no space between them.
    bool immediately_before(const ArenaAllocatorBlockRef& other) const;
    
};

/**
 * Allocator on top of a byte array.
 * Maintains a free list and deals in offsets in the array as an interface.
 */
template<typename T>
class ArenaAllocatorRef : public base_ref_t<ArenaAllocatorRef<T>> {
public:
    struct body_t {
        offset_to<ArenaAllocatorBlockRef> first_free;
        offset_to<ArenaAllocatorBlockRef> last_free;
    };

    using pointer = size_t;
    using const_pointer = size_t;
    using size_type = size_t;
    
    /**
     * Make an allocator in the given MappingContext, or connect to the existing one.
     * Resizes the context to be big enough to hold the allocator, if it isn't already.
     * We assume allocators are only allowed at 0, one per context.
     */
    ArenaAllocatorRef(MappingContext* context);
    
    /**
     * Copy an allocator.
     */
    ArenaAllocatorRef(const ArenaAllocatorRef& other) = default;
    
    /**
     * Copy an allocator, but change the allocated type.
     */
    template<typename U>
    ArenaAllocatorRef(const ArenaAllocatorRef<U>& alloc);
    
    /**
     * Allocate the given number of items. Ought to be near the hint.
     */
    pointer allocate(size_type n, const_pointer hint = 0);
    
    /**
     * Deallocate the given number of items. Must be the same number as were allocated.
     */
    void deallocate(pointer p, size_type n);
    
protected:
    
    /// How much space is reserved at the start of the context for the
    /// allocator's data structures?
    static const size_t reserved_space = sizeof(body_t) + sizeof(ArenaAllocatorBlockRef::body_t); 
    
};

/**
 * Allocator specifically for types referenced by refs.
 * Allows connecting to the root object if present, and creating it if not.
 */
template<typename ref_t>
class ArenaRefAllocatorRef : public ArenaAllocatorRef<typename ref_t::body_t> {
public:
    // Inherit constructors
    using ArenaAllocatorRef<typename ref_t::body_t>::ArenaAllocatorRef;
    
    /// Boost has a whole system to connect to allocated objects; we have just
    /// a root object we can connect to, which is the first object allocated.
    /// We assume it is never freed.
    /// Connect to the existing root object, which is assumed to be of the
    /// appropriate type if memory exists for it in the context. Otherwise,
    /// creates it. Either eay, returns a ref to it.
    ref_t connect_or_create_root();
};

/**
 * Reference type for a vector of primitive items, directly stored in the mapping.
 * Note that destructors won't run when things are just unmapped, and
 * constructors won't run when things are mapped in.
 */
template<typename item_t>
class MappedVectorRef : public base_ref_t<MappedVectorRef<item_t>> {
public:
    struct body_t {
        big_endian<size_t> length;
        big_endian<size_t> reserved_length;
        offset_ptr<item_t> first;
    };
    
    using base_ref_t<MappedVectorRef<item_t>>::base_ref_t;
    
    size_t size() const;
    void resize(size_t new_size);
    item_t& at(size_t index);
    const item_t& at(size_t index) const;
    
    // TODO: reserve(), push_back()
};

/**
 * Reference type for a vector of reference type items, with bodies stored in the mapping.
 */
template<typename item_ref_t>
class MappedRefVectorRef : public base_ref_t<MappedRefVectorRef<item_ref_t>> {
public:
    struct body_t {
        big_endian<size_t> length = 0;
        offset_to<item_ref_t> first = nullptr;
    };
    
    using base_ref_t<MappedRefVectorRef<item_ref_t>>::base_ref_t;
    
    size_t size() const;
    void resize(size_t new_size);
    item_ref_t at(size_t index);
    const item_ref_t at(size_t index) const;
};

/**
 * A vector of primitive items, with guaranteed ABI compatibility across
 * compilers and platforms.
 */
template<typename T, typename Alloc = std::allocator<T>>
class CompatVector {
public:

    CompatVector() = default;
    
    // Because we contain a pointer, we need a destructor and copy and move
    // constructors and assignment operators.
    ~CompatVector();
    
    CompatVector(const CompatVector& other);
    CompatVector(CompatVector&& other);
    
    CompatVector& operator=(const CompatVector& other);
    CompatVector& operator=(CompatVector&& other);

    size_t size() const;
    void resize(size_t new_size);
    void reserve(size_t new_reserved_length);
    
    /**
     * Empty out the vector and free any allocated memory.
     */
    void clear();
    T& at(size_t index);
    const T& at(size_t index) const;
    
    // TODO: reserve(), push_back()
protected:
    // We keep the allocator in ourselves, so if working in a chain we allocate
    // in the right chain.
    Alloc alloc;
    big_endian<size_t> length = 0;
    big_endian<size_t> reserved_length = 0;
    typename std::allocator_traits<Alloc>::pointer first = nullptr;
    
    static const int RESIZE_FACTOR = 2;
};

class IntVectorRef : public base_ref_t<IntVectorRef> {
public:
    struct body_t {
    };
    // TODO
};

class PackedVectorRef : public base_ref_t<PackedVectorRef> {
public:
    struct body_t {
    };
    // TODO
};

class PagedVectorRef : public base_ref_t<PagedVectorRef> {
public:
    struct body_t {
    };
    // TODO
};


///////////////////////////////////////////////////////////////////////////////////


// Implementations

template<typename Derived>
base_ref_t<Derived>::base_ref_t(MappingContext* context, size_t position) : 
    context(context), position(position) {
    
    // Nothing to do
};

template<typename Derived>
base_ref_t<Derived>::base_ref_t(MappingContext* context) : context(context) {
    // Grab an allocator
    ArenaAllocatorRef<typename Derived::body_t> alloc(context);
    
    // Allocate one body
    position = alloc.allocate(1);
};

template<typename Derived>
base_ref_t<Derived>::base_ref_t() : context(nullptr), position(0) {
    // Nothing to do
}

template<typename Derived>
void* base_ref_t<Derived>::get_body() {
    if (!context) {
        throw std::runtime_error("Trying to follow a null reference");
    }
    return (void*)(context->base_address + position);
}

template<typename Derived>
const void* base_ref_t<Derived>::get_body() const {
    if (!context) {
        throw std::runtime_error("Trying to follow a null reference");
    }
    return (const void*)(context->base_address + position);
}

template<typename Derived>
base_ref_t<Derived>::operator bool () const {
    return (context != nullptr);
}

////////////////////

template<typename T>
big_endian<T>::big_endian() {
    for (auto& byte : storage) {
        byte = 0;
    }
#ifdef debug_big_endian
    std::cerr << "Zeroed " << sizeof(T) << " bytes for a big_endian at " << (intptr_t) this << std::endl;
#endif
    assert(*this == 0);
}

template<typename T>
big_endian<T>::big_endian(const T& value) {
    *this = value;
}

template<typename T>
big_endian<T>::operator T () const {
    
    static_assert(std::numeric_limits<T>::is_integer);

    // I had no luck partially specializing the conversion operator for all
    // integral types of a given width, so we switch and call only the
    // conversion that will work.
    // TODO: manually write all the specializations?
    // Note that signed types report 1 fewer bits (i.e. 63)
    switch (std::numeric_limits<T>::digits) {
    case 64:
    case 63:
        return (T)be64toh(*((const T*)storage));
        break;
    case 32:
    case 31:
        return (T)be32toh(*((const T*)storage));
        break;
    case 16:
    case 15:
        return (T)be16toh(*((const T*)storage));
        break;
    default:
        throw runtime_error("Unimplemented bit width: " + std::to_string(std::numeric_limits<T>::digits));
    }
}

template<typename T>
big_endian<T>& big_endian<T>::operator=(const T& x) {

    static_assert(std::numeric_limits<T>::is_integer);

    switch (std::numeric_limits<T>::digits) {
    case 64:
    case 63:
        *((T*)storage) = (T)htobe64(x);
        break;
    case 32:
    case 31:
        *((T*)storage) = (T)htobe32(x);
        break;
    case 16:
    case 15:
        *((T*)storage) = (T)htobe16(x);
        break;
    default:
        throw runtime_error("Unimplemented bit width: " + std::to_string(std::numeric_limits<T>::digits));
    }
    
#ifdef debug_big_endian
    std::cerr << "Set a big_endian at " << (intptr_t) this << " to " << x << std::endl;
#endif
    
    return *this;
}

////////////////////

template<typename T>
offset_ptr<T>::offset_ptr() : offset(std::numeric_limits<size_t>::max()) {
    // Nothing to do!
}

template<typename T>
offset_ptr<T>::operator bool () const {
    return (offset != std::numeric_limits<size_t>::max());
}

template<typename T>
T& offset_ptr<T>::operator*() {
    return *(this->operator->());
}

template<typename T>
const T& offset_ptr<T>::operator*() const {
    return *(this->operator->());
}

template<typename T>
T* offset_ptr<T>::operator->() {
    if (offset == std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("Null pointer dereference");
    }
    return (T*) (((char*) this) + offset);
}

template<typename T>
const T* offset_ptr<T>::operator->() const {
    if (offset == std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("Null pointer dereference");
    }
    return (const T*) (((const char*) this) + offset);
}

template<typename T>
offset_ptr<T>& offset_ptr<T>::operator=(const T* addr) {
    if (addr) {
        // Represent real value as as a difference
        offset = (size_t) (((const char*) addr) - ((const char*) this));
    } else {
        // Represent null specially
        offset = std::numeric_limits<size_t>::max();
    }
    
#ifdef debug_offset_ptr
    cerr << "Pointer to address " << addr << " is at offset " << offset
        << " from " << this << " producing " << this->operator->() << endl;
#endif
    
    return *this;
}

template<typename T>
T* offset_ptr<T>::operator+(size_t items) {
    return this->operator->() + items;
}

template<typename T>
const T* offset_ptr<T>::operator+(size_t items) const {
    return this->operator->() + items;
}

template<typename T>
offset_ptr<T>& offset_ptr<T>::operator=(const offset_ptr<T>& other) {
    // Delegate to assignment based on cuttent absolute memory address of the
    // destination.
    *this = other.operator->();
}

template<typename T>
offset_ptr<T>& offset_ptr<T>::operator=(offset_ptr<T>&& other) {
    *this = other.operator->();
}

template<typename T>
offset_ptr<T>::offset_ptr(const offset_ptr<T>& other) {
    *this = other.operator->();
}

template<typename T>
offset_ptr<T>::offset_ptr(offset_ptr<T>&& other) {
    *this = other.operator->();
}

////////////////////


template<typename ref_t>
ref_t offset_to<ref_t>::get(MappingContext* context) {
    if (*this) {
        // We aren't null.
        // Get our position in the context
        size_t our_position = ((char*) this) - context->base_address;
        // Apply our offset and create a reference in the context
        return ref_t(context, our_position + this->offset);
    } else {
        // Return a null ref. Null refs need no context.
        return ref_t();
    }
}

template<typename ref_t>
const ref_t offset_to<ref_t>::get(MappingContext* context) const {
    if (*this) {
        // We aren't null.
        // Get our position in the context
        size_t our_position = ((const char*) this) - context->base_address;
        // Apply our offset and create a reference in the context
        return ref_t(context, our_position + this->offset);
    } else {
        // Return a null ref. Null refs need no context.
        return ref_t();
    }
}

template<typename ref_t>
ref_t offset_to<ref_t>::get_at(MappingContext* context, size_t index) {
    if (*this) {
        // Get our position in the context
        size_t our_position = ((char*) this) - context->base_address;
        // Apply our offset and the index offset and create a reference in the context
        return ref_t(context, our_position + this->offset + index * sizeof(typename ref_t::body_t));
    } else {
        // Return a null ref. Null refs need no context.
        return ref_t();
    }
}

template<typename ref_t>
const ref_t offset_to<ref_t>::get_at(MappingContext* context, size_t index) const {
    if (*this) {
        // Get our position in the context
        size_t our_position = ((const char*) this) - context->base_address;
        // Apply our offset and the index offset and create a reference in the context
        return ref_t(context, our_position + this->offset + index * sizeof(typename ref_t::body_t));
    } else {
        // Return a null ref. Null refs need no context.
        return ref_t();
    }
}
    
template<typename ref_t>
offset_to<ref_t>& offset_to<ref_t>::operator=(const ref_t& other) {
    if (other) {
        // Not null
        size_t our_position = ((char*) this) - other.context->base_address;
        this->offset = other.position - our_position;
    } else {
        // Become null
        this->offset = std::numeric_limits<size_t>::max();
    }
    
    return *this;
}

template<typename ref_t>
bool offset_to<ref_t>::operator==(const ref_t& other) const {
    if (!*this || !other) {
        // If either is null, they're only equal if they're both null
        return !*this && !other;
    }
    
    // Otherwise, we're equal if we point at equal places.
    size_t dest_position = (((char*) this) - other.context->base_address) + this->offset;
    return dest_position == other.position;
}

template<typename ref_t>
bool offset_to<ref_t>::operator!=(const ref_t& other) const {
    return !(this == other);
}

////////////////////

// ArenaAllocatorBlockRef isn't a template so it lives in the CPP.

////////////////////

template<typename T>
ArenaAllocatorRef<T>::ArenaAllocatorRef(MappingContext* context) : base_ref_t<ArenaAllocatorRef<T>>(context, 0) {
    // We declared ourselves to be at 0.
    if (context->size < sizeof(body_t)) {
        // The body didn't exist yet.
        // Make room for the body
        context->base_address = context->resize(sizeof(body_t));
        context->size = sizeof(body_t);
        // Run the body's constructor.
        new ((void*) context->base_address) body_t;
    }
    // Otherwise we assume the allocator was already there.
    // We totally ignore the different template parameters.
    // TODO: be a base class of all the templated derived classes?
}

template<typename T>
template<typename U>
ArenaAllocatorRef<T>::ArenaAllocatorRef(const ArenaAllocatorRef<U>& alloc): base_ref_t<ArenaAllocatorRef<T>>(alloc.context, alloc.position) {
    // Nothing to do to change the type; just steal the state.
}

template<typename T>
auto ArenaAllocatorRef<T>::allocate(size_type n, const_pointer hint) -> pointer {
#ifdef debug_arena_allocator_ref
    cerr << "Allocate " << n << " items of size " << sizeof(T)
        << " from allocator at " << this->position
        << " in context " << this->context << endl;
#endif
    
    // Find our body. Note that it WILL MOVE when we reallocate.
    body_t* body = reinterpret_cast<body_t*>(this->get_body());
    
    // How much user space do we need?
    // TODO: alignment???
    size_t user_bytes = n * sizeof(T);
    // And how much space do we need with block overhead?
    size_t block_bytes = user_bytes + sizeof(ArenaAllocatorBlockRef::body_t);
    
    // This will hold a ref to the free block we found or made that is big enough to hold this item.
    // Starts null if there is no first_free.
    ArenaAllocatorBlockRef found = body->first_free.get(this->context);
    while (found && found.size() < user_bytes) {
        // Won't fit here. Try the next place.
        found = found.get_next();
    }
   
    if (!found) {
        // We have no free memory big enough.
        
        // Work out where new free memory will start
        size_t new_free = this->context->size;
        
        // Double the arena, or add space for the data we need to allocate, whichever is bigger.
        size_t new_bytes = max(this->context->size, block_bytes);
        this->context->base_address = this->context->resize(this->context->size + new_bytes);
        this->context->size += new_bytes;
        
        // Find our body again
        body = reinterpret_cast<body_t*>(this->get_body());
        
        // Create a new block
        new ((void*) (this->context->base_address + new_free)) ArenaAllocatorBlockRef::body_t;
        
        // Refer to it and say we found it.
        found = ArenaAllocatorBlockRef(this->context, new_free);
        
        // Initialize it. Leave prev and next pointers null because it is the only free block.
        found.size() = user_bytes;
        
        // Put it in the linked list as head and tail.
        body->first_free = found;
        body->last_free = found;
    }
    
    // Now we can allocate memory.
    
    if (found.size() > block_bytes) {
        // We could break the user data off of this block and have some space left over.
        // TODO: use a min block size here instead.
        
        // So split the block.
        ArenaAllocatorBlockRef second = found.split(user_bytes);
        if (body->last_free == found) {
            // And fix up the end of the linked list
            body->last_free = second;
        }
    }
    
    // Now we have a free block of the right size. Make it not free.
    auto connected = found.detach();
    if (body->first_free == found) {
        // This was the first free block
        body->first_free = connected.first;
    }
    if (body->last_free == found) {
        // This was the last free block 
        body->last_free = connected.second;
    }
    
#ifdef debug_arena_allocator_ref
    cerr << "Allocated at " << found.get_user_data() << " = "
        << (void*)(this->context->base_address + found.get_user_data()) << endl;
#endif
    
    // Give out the address of its data
    return found.get_user_data();
}


template<typename T>
void ArenaAllocatorRef<T>::deallocate(pointer p, size_type n) {
#ifdef debug_arena_allocator_ref
    cerr << "Deallocate " << n << " items of size " << sizeof(T)
        << " at " << p << " from allocator at " << this->position
        << " in context " << this->context << endl;
#endif
    
    // Find our body
    auto& body = *reinterpret_cast<body_t*>(this->get_body());
    
    // Find the block
    ArenaAllocatorBlockRef found = ArenaAllocatorBlockRef::get_from_data(this->context, p);
    
    // Find the block in the free list after it, if any
    ArenaAllocatorBlockRef right = body.first_free;
    while(right && right.position < found.position) {
        right = right.get_next();
    }
    ArenaAllocatorBlockRef left;
    if (!right) {
        // The new block should be the last block in the list.
        // So it comes after the existing last block, if any.
        left = body.last_free;
    } else {
        // The new block comes between right and its predecessor, if any
        left = right.get_prev();
    }
    
    // Wire in the block
    found.attach(left, right);
    
    // Update haed and tail
    if (body.last_free == left) {
        body.last_free = found;
    }
    if (body.first_free == right) {
        body.first_free = found;
    }
    
    // Defragment.
    auto bounds = found.coalesce();
    // We can't need to update the first free when defragmenting, but we may
    // need to update the last free.
    if (body.last_free == bounds.second) {
        body.last_free = bounds.first;
    }
}

////////////////////

template<typename ref_t>
ref_t ArenaRefAllocatorRef<ref_t>::connect_or_create_root() {
    // We expect to find the root right after the allocator's reserved space
    // (overall header and first block header)
    size_t root_position = ArenaAllocatorRef<typename ref_t::body_t>::reserved_space;

#ifdef debug_arena_allocator_ref
    cerr << "Looking for root at " << root_position 
        << " in context of size " << this->context->size << endl;
#endif

    if (this->context->size < root_position) {
        // Root object needs to be made.
#ifdef debug_arena_allocator_ref
        cerr << "Creating root..." << endl;
#endif
        size_t got_position = this->allocate(1);
        if (got_position != root_position) {
            throw std::runtime_error("Allocated root at " + std::to_string(got_position) +
                " instead of " + std::to_string(root_position));
        }
    }
    if (this->context->size >= root_position + sizeof(typename ref_t::body_t)) {
        // Root object (now) exists. Connect and return.
#ifdef debug_arena_allocator_ref
        cerr << "Found root at " << root_position 
            << " in context of size " << this->context->size << endl;
#endif
        return ref_t(this->context, root_position);
    }
    // Otherwise there's something else smaller here.
    throw std::runtime_error("Could not find root object of size " +
        std::to_string(sizeof(typename ref_t::body_t)) + " in context of size " +
        std::to_string(this->context->size) + " after header of size " +
        std::to_string(root_position));
}

////////////////////

template<typename item_t>
size_t MappedVectorRef<item_t>::size() const {
    auto& body = *reinterpret_cast<const body_t*>(this->get_body());
    return body.length;
}

template<typename item_t>
void MappedVectorRef<item_t>::resize(size_t new_size) {
    if (new_size == size()) {
        // Nothing to do!
        return;
    }

    // Get the body. Note that it WILL MOVE when we resize
    body_t* body = reinterpret_cast<body_t*>(this->get_body());
    
    // Where is the vector going?
    item_t* new_first = nullptr;
    
    if (new_size > body->reserved_length) {
        // Get the allocator
        // TODO: if we put a mutex in the ref, this won't work.
        ArenaAllocatorRef<item_t> alloc(this->context);

        // TODO: we can't deal with a simultaneous reallocation/move of the context
        // while we're working.

        // Allocate space for the new data, and get the position in the context
        size_t new_first_pos = alloc.allocate(new_size);
        
        // Re-find our body
        body = reinterpret_cast<body_t*>(this->get_body());
        
        // Convert to memory address
        new_first = (item_t*) (this->context->base_address + new_first_pos);
        
        if (size() > 0) {
            // Get memory address of old data
            item_t* old_first = &*body->first;
            
            for (size_t i = 0; i < size() && i < new_size; i++) {
                // Run move constructors
#ifdef debug_mapped_vector_ref
                cerr << "From index " << i << " at " << (old_first + i) << " move " << *(old_first + i);
#endif
                new (new_first + i) item_t(std::move(*(old_first + i)));
#ifdef debug_mapped_vector_ref
                cerr << " so it becomes " << *(new_first + i) << " at " << (new_first + i) << endl;
#endif
                // And destructors
                (old_first + i)->~item_t();
            }
        }
        
        // Record the new reserved length
        body->reserved_length = new_size;
    } else {
        // Just run in place
        new_first = &*body->first;
    }
    
    for (size_t i = size(); i < new_size; i++) {
        // For anything aded on, just run constructor
        new (new_first + i) item_t();
    }
    
    if (new_size < size()) {
        // We shrank, and we had at least one item.
        item_t* old_first = &*body->first;
        for (size_t i = new_size; i < size(); i++) {
            // For anything trimmed off, just run destructors.
            (old_first + i)->~item_t();
        }
    }
    
    // Record the new length
    body->length = new_size;
    // And position
    body->first = new_first;
}

template<typename item_t>
item_t& MappedVectorRef<item_t>::at(size_t index) {
    auto& body = *reinterpret_cast<body_t*>(this->get_body());
    
    if (index >= size()) {
        // TODO: throw the right type here.
        throw std::runtime_error("Cannot get " + std::to_string(index) +
                                 " in vector of length " + std::to_string(size()));
    }
    
    return *(body.first + index);
}

template<typename item_t>
const item_t& MappedVectorRef<item_t>::at(size_t index) const {
    // Just run non-const at and constify result
    return const_cast<MappedVectorRef<item_t>*>(this)->at(index);
}

////////////////////

template<typename T, typename Alloc>
CompatVector<T, Alloc>::~CompatVector() {
    if (first) {
        // We have some memory allocated to us.
        
        for (size_t i = 0; i < length; i++) {
            // Destroy everything in it.
            (first + i)->~T();
        }
        
        // Get rid of it.
        alloc.deallocate(first, reserved_length);
    }
}

template<typename T, typename Alloc>
CompatVector<T, Alloc>::CompatVector(const CompatVector& other) {
#ifdef debug_compat_vector
    std::cerr << "Copy-constructing a vector of size " << other.size() << " from " << (intptr_t)&other << " to " << (intptr_t)this << std::endl;
#endif
    if (other.size() != 0) {
        first = alloc.allocate(other.size());
        reserved_length = other.size();
        length = other.size();
        
        for (size_t i = 0; i < other.size(); i++) {
            // Copy each item
            new (first + i) T(other.at(i));
        }
    }
#ifdef debug_compat_vector
    std::cerr << "Result is of size " << size() << std::endl;
#endif
}

template<typename T, typename Alloc>
CompatVector<T, Alloc>::CompatVector(CompatVector&& other) :
    length(other.length), reserved_length(other.reserved_length), first(other.first) {
    
#ifdef debug_compat_vector
    std::cerr << "Move-constructing a vector of size " << other.size() << " from " << (intptr_t)&other << " to " << (intptr_t)this << std::endl;
#endif
    
    // And say they have no items or memory.
    other.length = 0;
    other.reserved_length = 0;
    other.first = nullptr;
#ifdef debug_compat_vector
    std::cerr << "Result is of size " << size() << std::endl;
#endif
}

template<typename T, typename Alloc>
CompatVector<T, Alloc>& CompatVector<T, Alloc>::operator=(const CompatVector& other) {
#ifdef debug_compat_vector
    std::cerr << "Copy-assigning a vector of size " << other.size() << " from " << (intptr_t)&other << " to " << (intptr_t)this << std::endl;
#endif
    if (this != &other) {
        // TODO: can we economize and use copy constructors instead of
        // assignment here sometimes?
        resize(other.size);
        for (size_t i = 0; i < other.size(); i++) {
            this.at(i) = other.at(i);
        }
    }
#ifdef debug_compat_vector
    std::cerr << "Result is of size " << size() << std::endl;
#endif
    return *this;
}

template<typename T, typename Alloc>
CompatVector<T, Alloc>& CompatVector<T, Alloc>::operator=(CompatVector&& other) {
#ifdef debug_compat_vector
    std::cerr << "Move-assigning a vector of size " << other.size() << " from " << (intptr_t)&other << " to " << (intptr_t)this << std::endl;
#endif
    if (this != &other) {
        // Get rid of our memory
        clear();
        
        // Steal their memory
        length = other.length;
        reserved_length = other.reserved_length;
        first = other.first;
        
        // And say they have no items or memory.
        other.length = 0;
        other.reserved_length = 0;
        other.first = nullptr;
    }
#ifdef debug_compat_vector
    std::cerr << "Result is of size " << size() << std::endl;
#endif
    return *this;
}

template<typename T, typename Alloc>
size_t CompatVector<T, Alloc>::size() const {
    return length;
}

template<typename T, typename Alloc>
void CompatVector<T, Alloc>::reserve(size_t new_reserved_length) {
    // Find where the data is. Note that this may be null.
    T* old_first = first;
    // And how much there is
    size_t old_reserved_length = reserved_length;
    
#ifdef debug_compat_vector
    std::cerr << "Reserving vector at " << (intptr_t)this
        << " with " << old_reserved_length << " spaces at "
        << (intptr_t) old_first << " to have " << new_reserved_length  << " spaces" << std::endl;
#endif

    if (new_reserved_length > old_reserved_length) {
        // Allocate space for the new data, and get the position in the context
        T* new_first  = alloc.allocate(new_reserved_length);
        
        // Record the new reserved length
        reserved_length = new_reserved_length;
        
#ifdef debug_compat_vector
        std::cerr << "Vector data moving from " << (intptr_t) old_first
            << " to " << (intptr_t) new_first << std::endl;
#endif
        for (size_t i = 0; i < size(); i++) {
            // Move over the preserved values.
            new (new_first + i) T(std::move(*(old_first + i)));
        }
        
        for (size_t i = 0; i < size(); i++) {
            // Destroy all the original values
            (old_first + i)->~T();
        }
        
        if (old_first) {
            // Free old memory if we moved
            alloc.deallocate(old_first, old_reserved_length);
        }
        first = new_first;
    }
    
    // If it isn't growing, ignore it.
}

template<typename T, typename Alloc>
void CompatVector<T, Alloc>::resize(size_t new_size) {
    // Find where the data is. Note that this may be null.
    T* old_first = first;
    // And how much there is
    size_t old_reserved_length = reserved_length;
    
#ifdef debug_compat_vector
    std::cerr << "Resizing vector at " << (intptr_t)this
        << " with " << size() << " items at "
        << (intptr_t) old_first << " to size " << new_size << std::endl;
#endif

    if (new_size == size()) {
        // Nothing to do!
        return;
    }

    if (new_size > reserved_length) {
        // We will need some more memory, and our data might move.
        // Make sure repeated calls to be bigger reallocate only a sensible number of times.
        reserve(std::max(new_size, size() * RESIZE_FACTOR));
    }
   
    // TODO: throw away excess memory when shrinking
   
    if (new_size < size()) {
        // We shrank, and we had at least one item.
#ifdef debug_compat_vector
        std::cerr << "Removing items between " << new_size << " and " << size() << std::endl;
#endif
        for (size_t i = new_size; i < size(); i++) {
            // For anything trimmed off, just run destructors.
            (first + i)->~T();
        }
    } else {
#ifdef debug_compat_vector
        std::cerr << "Adding items between " << size() << " and " << new_size << std::endl;
#endif
        for (size_t i = size(); i < new_size; i++) {
            // For anything aded on, just run constructor.
            // This will use value initialization
            // <https://stackoverflow.com/a/2418195> and zero everything if a
            // default constructor is not defined.
            new (first + i) T();
        }
    }
    
    // Record the new length
    length = new_size;
}

template<typename T, typename Alloc>
void CompatVector<T, Alloc>::clear() {
    // Deconstruct anything we have.
    resize(0);
    if (first) {
        // We have some memory allocated to us.
        // Get rid of it.
        alloc.deallocate(first, reserved_length);
    }
    reserved_length = 0;
}

template<typename T, typename Alloc>
T& CompatVector<T, Alloc>::at(size_t index) {
    if (index >= size()) {
        // TODO: throw the right type here.
        throw std::runtime_error("Cannot get " + std::to_string(index) +
                                 " in vector of length " + std::to_string(size()));
    }
    
    return *(first + index);
}

template<typename T, typename Alloc>
const T& CompatVector<T, Alloc>::at(size_t index) const {
    // Just run non-const at and constify result
    return const_cast<CompatVector<T, Alloc>*>(this)->at(index);
}


namespace yomo {

template<typename T>
Pointer<T>::Pointer(T* destination) {
    *this = destination;
}

template<typename T>
Pointer<T>::operator bool () const {
    return position != std::numeric_limits<size_t>::max();
}

template<typename T>
T& Pointer<T>::operator*() const {
    return *get();
}

template<typename T>
T* Pointer<T>::operator->() const {
    return get();
}

template<typename T>
Pointer<T>::operator T* () const {
    return get();
}

template<typename T>
Pointer<T>& Pointer<T>::operator=(T* addr) {
    if (addr == nullptr) {
        // Adopt our special null value
        position = std::numeric_limits<size_t>::max();
    } else {
        // Get the position, requiring that it is in the same chain as us.
        position = Manager::get_position_in_same_chain(this, addr);
    }
    return *this;
}

template<typename T>
T* Pointer<T>::operator+(size_t items) const {
    return get() + items;
}

template<typename T>
T* Pointer<T>::get() const {
    if (position == std::numeric_limits<size_t>::max()) {
        return nullptr;
    } else {
        return (T*) Manager::get_address_in_same_chain((const void*) this, position);
    }
}



template<typename T>
template<typename U>
Allocator<T>::Allocator(const Allocator<U>& alloc) {
    // Nothing to do!
}

template<typename T>
auto Allocator<T>::allocate(size_type n, const T* hint) -> T* {
    return (T*) Manager::allocate_from_same_chain((void*) this, n * sizeof(T));
}

template<typename T>
void Allocator<T>::deallocate(T* p, size_type n) {
    Manager::deallocate((void*) p);
}


template<typename T>
UniqueMappedPointer<T>::operator bool () const {
    return chain != Manager::NO_CHAIN;
}

template<typename T>
T& UniqueMappedPointer<T>::operator*() {
    return *get();
}

template<typename T>
const T& UniqueMappedPointer<T>::operator*() const {
    return *get();
}

template<typename T>
T* UniqueMappedPointer<T>::operator->() {
    return get();
}

template<typename T>
const T* UniqueMappedPointer<T>::operator->() const {
    return get();
}

template<typename T>
UniqueMappedPointer<T>::operator T*() {
    return get();
}

template<typename T>
UniqueMappedPointer<T>::operator const T*() const {
    return get();
}

template<typename T>
T* UniqueMappedPointer<T>::get() {
    if (chain == Manager::NO_CHAIN) {
        return nullptr;
    } else {
        return (T*) Manager::find_first_allocation(chain, sizeof(T));
    }
}

template<typename T>
const T* UniqueMappedPointer<T>::get() const {
    if (chain == Manager::NO_CHAIN) {
        return nullptr;
    } else {
        return (T*) Manager::find_first_allocation(chain, sizeof(T));
    }
}

template<typename T>
template<typename... Args>
void UniqueMappedPointer<T>::construct(const std::string& prefix, Args&&... constructor_args) {
    // Drop any existing chain.
    reset();
    
    // Make a new chain
    chain = Manager::create_chain(prefix);
    // Allocate space in the cahin for the item.
    // Can't use the Allocator because we don't have a place in the chain to
    // store one.
    T* item = (T*) Manager::allocate_from(chain, sizeof(T));
    // Run the constructor.
    new (item) T(std::forward<Args>(constructor_args)...);
}

template<typename T>
void UniqueMappedPointer<T>::load(int fd, const std::string& prefix) {
    // Drop any existing chain.
    reset();

    chain = Manager::create_chain(fd, prefix);
}

template<typename T>
void UniqueMappedPointer<T>::dissociate() {
    if (chain == Manager::NO_CHAIN) {
        throw runtime_error("Cannot dissociate a null object");
    }
    // Copy the chain
    Manager::chainid_t new_chain = Manager::get_dissociated_chain(chain);
    // Get rid of the old chain
    Manager::destroy_chain(chain);
    // Adopt the new chain
    chain = new_chain;
}

template<typename T>
void UniqueMappedPointer<T>::save(int fd) {
    if (chain == Manager::NO_CHAIN) {
        throw runtime_error("Cannot save a null object");
    }
    // Copy the chain
    Manager::chainid_t new_chain = Manager::get_associated_chain(chain, fd);
    // Get rid of the old chain
    Manager::destroy_chain(chain);
    // Adopt the new chain
    chain = new_chain;
}

template<typename T>
void UniqueMappedPointer<T>::reset() {
    if (chain != Manager::NO_CHAIN) {
        Manager::destroy_chain(chain);
        chain = Manager::NO_CHAIN;
    }
}

}


}

#endif