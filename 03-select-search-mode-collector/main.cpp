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
 * Mode for searching a free block.
 */
enum class SearchMode {
  FirstFit,
  NextFit,
  BestFit
};

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
static Block* top = heapStart;

/**
 * For next fit
*/

/**
 * Previously found block. Updated in `nextFit`.
 */
static Block* searchStart = heapStart;

/**
 * Current search mode.
 */
static SearchMode searchMode = SearchMode::FirstFit;

/**
 * Reset the heap to the original position.
 */
void resetHeap()
{
  // Already reset.
  if (heapStart == nullptr) {
    return;
  }
 
  // Roll back to the beginning.
  brk(heapStart);
 
  heapStart = nullptr;
  top = nullptr;
  searchStart = nullptr;
}

/**
 * Initializes the heap, and the search mode.
 */
void init(SearchMode mode) {
  searchMode = mode;
  resetHeap();
}

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
  Block* block = (Block*) sbrk(0);                // (1)

  // OOM. (Out Of Memory)
  size_t deltaIncrement = allocSize(size);
  // printf("%p == sbrk(0)\n", block);
  if (sbrk(deltaIncrement) == (void *)-1) {    // (2)
    return nullptr;
  }
  // printf("sbrk(%li)\n", deltaIncrement);
  // printf("%p == sbrk(0)\n", sbrk(0));
 
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
Block* firstFit(size_t alignedSize)
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
 * Next-fit algorithm.
 *
 * Returns the next free block which fits the size.
 * Updates the `searchStart` of success.
 */
Block* nextFit(size_t alignedSize)
{
  // The circular first fit
  // even if it’s much larger in size than requested.
  // We’ll fix this below with the next- and best-fit allocations.
  Block* block = searchStart == nullptr ? heapStart : searchStart;
  if (block == nullptr) return nullptr;

  while (true)
  {
    // If current block is not re-usable;
    // O(n) search
    if (block->used || block->size < alignedSize)
    {
      // Move to next or to heap start if already completed
      block = block->next;
      if (block == nullptr)
      {
        // If found nothing previously then we should stop here
        // otherwise it would cause an infinite loop
        if (searchStart == nullptr)
        {
          return nullptr;
        }

        block = heapStart;
      }

      // If next is search start then we already completed a circular iteration
      if (block == searchStart)
      {
        return nullptr;
      }

      // Continue if still valid
      continue;
    }

    searchStart = block; // Store the last found block to start from here later
    return block;
  }

  return nullptr;
}

/**
 * Best-fit algorithm.
 *
 * Returns a free block which size fits the best.
 */
Block* bestFit(size_t alignedSize) {
  // The first found block is returned,
  // even if it’s much larger in size than requested.
  // We’ll fix this below with the next- and best-fit allocations.
  Block* block = heapStart;
  Block* bestFitBlock = nullptr;

  while (block != nullptr)
  {
    // O(n) search
    if (block->used || block->size < alignedSize)
    {
      block = block->next;
      continue;
    }

    // If best fit return immediatly
    if (block->size == alignedSize)
    {
      return block;
    }

    // Search smaller fit
    if (bestFitBlock == nullptr || block->size < bestFitBlock->size)
    {
      bestFitBlock = block;
      block = block->next;
    }
  }
  
  return bestFitBlock;
}

/**
 * Tries to find a block of a needed size.
 */
Block* findBlock(size_t alignedSize)
{
  switch (searchMode)
  {
  case SearchMode::FirstFit:
    return firstFit(alignedSize);
  case SearchMode::NextFit:
    return nextFit(alignedSize);
  case SearchMode::BestFit:
    return bestFit(alignedSize);
  }

  return nullptr;
}

/**
 * Splits the block on two, returns the pointer to the smaller sub-block.
 */
Block* split(Block* block, size_t size) {
  size_t newBlockSize = block->size - allocSize(size);

  return nullptr;
}
 
/**
 * Whether this block can be split.
 */
inline bool canSplit(Block *block, size_t size) {
  return block->size > size;
}
 
/**
 * Allocates a block from the list, splitting if needed.
 */
Block* listAllocate(Block* block, size_t size) {
  // Split the larger block, reusing the free part.
  if (canSplit(block, size)) {
    block = split(block, size);
  }
 
  block->used = true;
  block->size = size;
 
  return block;
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

  // ---------------------------------------------------------
  // 1. Search for an available free block:

  if (Block* block = findBlock(alignedSize))
  {
    printf("Reused block at %p with size %li | req size %li and req aligned size %li \n", block, block->size, size, alignedSize);
    block->used = true;
    return block->data;
  }

  // ---------------------------------------------------------
  // 2. If block not found in the free list, request from OS:
  Block* block = requestFromOS(alignedSize);
  block->size = alignedSize;
  block->used = true;
  printf("Allocated block at %p with size %li | aligned size: %li\n", block, size, alignedSize);

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
  printf("freed block at %p with size %li\n", block, block->size);
}

// #define USE_NEXT_FIT
#define USE_BEST_FIT

int main()
{
#ifdef USE_NEXT_FIT
  // --------------------------------------
  // Test case 1: Next search start position
  //
  init(SearchMode::NextFit);
  
  // [[8, 1], [8, 1], [8, 1]]
  alloc(8);
  alloc(8);
  alloc(8);
  
  // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 1]]
  auto o1 = alloc(16);
  auto o2 = alloc(16);
  
  // [[8, 1], [8, 1], [8, 1], [16, 0], [16, 0]]
  free(o1);
  free(o2);
  
  // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 0]]
  auto o3 = alloc(16);
  
  // Start position from o3:
  assert(getHeader(o3)->used == true); // reused block should be marked as used
  assert(searchStart == getHeader(o3));
  
  // [[8, 1], [8, 1], [8, 1], [16, 1], [16, 1]]
  //                           ^ start here
  alloc(16);
#endif

#ifdef USE_BEST_FIT 
  // --------------------------------------
  // Test case 6: Best-fit search
  //
  init(SearchMode::BestFit);
  
  // [[8, 1], [64, 1], [8, 1], [16, 1]]
  alloc(8);
  auto z1 = alloc(64);
  alloc(8);
  auto z2 = alloc(16);
  
  // Free the last 16
  free(z2);
  
  // Free 64:
  free(z1);
  
  // [[8, 1], [64, 0], [8, 1], [16, 0]]
  
  // Reuse the last 16 block:
  auto z3 = alloc(16);
  assert(getHeader(z3) == getHeader(z2));
  
  // [[8, 1], [64, 0], [8, 1], [16, 1]]
  
  // Reuse 64, splitting it to 16, and 48
  z3 = alloc(16);
  assert(getHeader(z3) == getHeader(z1));
  
  // [[8, 1], [16, 1], [48, 0], [8, 1], [16, 1]]
#endif

  puts("\nAll assertions passed!\n");
  return 0;
}