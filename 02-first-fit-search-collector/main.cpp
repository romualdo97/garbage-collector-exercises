// Writing a Memory Allocator by Dmitry Soshnikov
// Our free function didn't actually return (unmap)
// the memory back to OS, it just sets the used flag to false.
// This means we can (read: should!) reuse the free
// blocks in future allocations.
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
 * First-fit algorithm.
 *
 * Returns the first free block which fits the size.
 */
Block* findBlock(size_t alignedSize)
{
  // The first found block is returned,
  // even if it’s much larger in size than requested.
  // We’ll fix this below with the next- and best-fit allocations.
  Block* block = heapStart;
  while (block != nullptr)
  {
    // O(n) search
    if (block->used || block->size < alignedSize)
    {
      block = block->next;
      continue;
    }

    return block;
  }

  return nullptr;
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

  // ---------------------------------------------------------
  // 1. Search for an available free block:

  if (Block* block = findBlock(alignedSize))
  {
    printf("Reused a block");
    return block->data;
  }

  // ---------------------------------------------------------
  // 2. If block not found in the free list, request from OS:

  Block* block = requestFromOS(alignedSize);
  block->size = alignedSize;
  block->used = true;

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
    // Test case 1: Memory re-use
    //
    // Check if the memory of freed object is reused
    //

    word_t* p1 = alloc(9);
    Block* p1b = getHeader(p1);
    assert(p1b->size == 16);

    free(p1);

    // p2b should reuse p1b
    word_t* p2 = alloc(8);
    Block* p2b = getHeader(p2);
    assert(p2b->size == 16); // 16 because first first fi is found even if it's much larger in size than requested
    assert(p2b == p1b);
  }

  puts("\nAll assertions passed!\n");
  return 0;
}