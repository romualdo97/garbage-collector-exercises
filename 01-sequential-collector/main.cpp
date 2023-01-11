// Writing a Memory Allocator by Dmitry Soshnikov
// Sequential allocator (aka the “Bump”-allocator).
// Yes, it’s that trivial, and just constantly bumping
// the allocation pointer until it reaches the “end of the heap”,
// at which point a GC is called, which reclaims
// the allocation area, relocating the objects around.
// Below we implement a Free-list allocator, which can reuse the
// blocks right away.
// http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <stdint.h>
#include <unistd.h> // for sbrk
#include <utility> // for std::declval

/**
 * Machine word size. Depending on the architecture,
 * can be 4 or 8 bytes.
 */
using word_t = intptr_t;

/**
 * Allocated block of memory. Contains the object header structure,
 * and the actual payload pointer.
 * 
 * Note that this header object is not mmeory aligned
 */
struct Block
{

    // -------------------------------------
    // 1. Object header

    /**
     * Block size.
     */
    size_t size; // 8bytes

    /**
     * Whether this block is currently used.
     */
    bool used; // 1byte + 7bytes for padding

    /**
     * Next block in the list.
     */
    Block* next; // 8bytes

    // -------------------------------------
    // 2. User data

    /**
     * Payload pointer.
     */
    word_t data[1]; // 8bytes
};

/**
 * Heap start. Initialized on first allocation.
 */
static Block* heapStart = nullptr;

/**
 * Current top. Updated on each allocation.
 */
static auto top = heapStart;

/**
 * Aligns the size by the machine word.
 */
inline size_t align(size_t n)
{
    return (n + sizeof(word_t) - 1) & ~(sizeof(word_t) - 1);
}

/**
 * Returns total allocation size, reserving in addition the space for
 * the Block structure (object header + first data word).
 *
 * Since the `word_t data[1]` already allocates one word inside the Block
 * structure, we decrease it from the size request: if a user allocates
 * only one word, it's fully in the Block struct.
 */
inline size_t allocSize(size_t size) {
  return size + sizeof(Block) - sizeof(std::declval<Block>().data);
}
 
/**
 * Requests (maps) memory from OS.
 */
Block* requestFromOS(size_t size) {
  // Current heap break.
  auto block = (Block*) sbrk(0);                // (1)
 
  // OOM. (Out Of Memory)
  if (sbrk(allocSize(size)) == (void *)-1) {    // (2)
    return nullptr;
  }
 
  return block;
}

/**
 * Returns the object header.
 */
Block* getHeader(word_t* data) {
  return (Block*)
    (
    (char*)data + sizeof(std::declval<Block>().data)
                - sizeof(Block)
    );
}

/**
 * Mimicking the malloc function, we have the following
 * interface (except we’re using typed word_t* instead
 * of void* for the return type):
 */

/**
 * Allocates a block of memory of (at least) `size` bytes.
 * Why is it “at least” of size bytes?
 * Because of the padding or alignment
 */
word_t* alloc(size_t size)
{
  size_t alignedSize = align(size);
  printf("requested size %li | aligned size: %li\n", size, alignedSize);

  Block* block = requestFromOS(alignedSize);
  block->size = alignedSize;
  //block->used = true;

  // Init heap
  if (heapStart == nullptr)
  {
    heapStart = block;
  }

  // Chain the blocks
  if (top != nullptr)
  {
    top->next = block;
  }

  top = block;

  // User payload
  return block->data;
}

/**
 * Frees a previously allocated block.
 */
void free(word_t* data)
{
  Block* block = getHeader(data);
  block->used = false;
}

int main()
{
  {
    // --------------------------------------
    // Test case 1: Alignment
    //
    // A request for 3 bytes is aligned to 8.
    //
  
    auto p1 = alloc(3);                        // (1)
    auto p1b = getHeader(p1);
    assert(p1b->size == sizeof(word_t));
  }
  
  {
    // --------------------------------------
    // Test case 2: Exact amount of aligned bytes 64bit machine
    //
  
    auto p1 = alloc(8);                        // (2)
    auto p1b = getHeader(p1);
    assert(p1b->size == 8);
  }
  
  {
    // --------------------------------------
    // Test case 3: Alignment 2
    //
    // A request for 9 bytes is aligned to 16. 64bit machine
    //

    auto p1 = alloc(9);                        // (1)
    auto p1b = getHeader(p1);
    assert(p1b->size == 16);
  }

  {
    // --------------------------------------
    // Test case 4: Header size because of members padding
    //
    // Check that Header type is padded as expected
    //

    word_t* p1 = alloc(9);
    Block* p1b = getHeader(p1);

    // https://learn.microsoft.com/en-us/cpp/cpp/alignment-cpp-declarations?view=msvc-170&viewFallbackFrom=vs-2019
    // data structure alignment
    assert(sizeof(*p1b) == 32);

    // being lucky here, we hope sbrk returns an adress aligned to word_t:
    // check this to understand how to create your own malloc
    // that returns an aligned address:
    // http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/#allocator-interface
    // address aligned to word_t
    assert(((word_t)p1b % sizeof(word_t)) == 0);
  }

  
  {
    // --------------------------------------
    // Test case 5: Free the memory
    //
    // Check if the memory is marked as free
    //

    word_t* p1 = alloc(8);
    free(p1);

    assert(getHeader(p1)->used == false);
  }

  puts("\nAll assertions passed!\n");
  return 0;
}