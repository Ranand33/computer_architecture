//Memory array allocation using first-fit algorithm  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Memory block structure for tracking allocations
typedef struct MemBlock {
    size_t start;          // Start index in memory array
    size_t size;           // Size of this block
    bool is_free;          // Whether this block is free or allocated
    struct MemBlock* next; // Pointer to next block in list
} MemBlock;

// Memory array structure
typedef struct {
    void* memory;          // The actual memory pool
    size_t total_size;     // Total size of memory pool
    size_t used;           // Amount of memory currently in use
    MemBlock* blocks;      // Linked list of memory blocks for tracking
} MemoryArray;

// Initialize a new memory array
MemoryArray* memory_array_init(size_t size) {
    MemoryArray* mem_array = (MemoryArray*)malloc(sizeof(MemoryArray));
    if (!mem_array) {
        perror("Failed to allocate memory for array structure");
        return NULL;
    }

    // Allocate the memory pool
    mem_array->memory = malloc(size);
    if (!mem_array->memory) {
        perror("Failed to allocate memory pool");
        free(mem_array);
        return NULL;
    }

    // Initialize properties
    mem_array->total_size = size;
    mem_array->used = 0;
    
    // Create initial block representing all memory (free)
    mem_array->blocks = (MemBlock*)malloc(sizeof(MemBlock));
    if (!mem_array->blocks) {
        perror("Failed to allocate initial memory block");
        free(mem_array->memory);
        free(mem_array);
        return NULL;
    }
    
    mem_array->blocks->start = 0;
    mem_array->blocks->size = size;
    mem_array->blocks->is_free = true;
    mem_array->blocks->next = NULL;
    
    printf("Memory array initialized with %zu bytes\n", size);
    return mem_array;
}

// Find first fit for requested size
MemBlock* find_free_block(MemoryArray* mem_array, size_t size) {
    MemBlock* current = mem_array->blocks;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

// Allocate memory from the memory array
void* memory_array_alloc(MemoryArray* mem_array, size_t size) {
    if (!mem_array || size == 0) {
        return NULL;
    }
    
    // Find a free block that can accommodate the request
    MemBlock* block = find_free_block(mem_array, size);
    if (!block) {
        printf("Memory allocation failed: no free block large enough\n");
        return NULL;
    }
    
    // If block is significantly larger than requested size, split it
    if (block->size >= size + sizeof(MemBlock) + 32) { // 32 is minimum block size
        MemBlock* new_block = (MemBlock*)malloc(sizeof(MemBlock));
        if (!new_block) {
            perror("Failed to allocate new block structure");
            return NULL;
        }
        
        // Set up the new block (remaining free space)
        new_block->start = block->start + size;
        new_block->size = block->size - size;
        new_block->is_free = true;
        new_block->next = block->next;
        
        // Update the current block
        block->size = size;
        block->next = new_block;
    }
    
    // Mark the block as allocated
    block->is_free = false;
    mem_array->used += block->size;
    
    // Return pointer to the actual memory in the array
    void* ptr = (char*)mem_array->memory + block->start;
    printf("Allocated %zu bytes at offset %zu\n", size, block->start);
    
    return ptr;
}

// Free memory back to the memory array
void memory_array_free(MemoryArray* mem_array, void* ptr) {
    if (!mem_array || !ptr) {
        return;
    }
    
    // Calculate offset from start of memory array
    size_t offset = (char*)ptr - (char*)mem_array->memory;
    
    // Find the block
    MemBlock* current = mem_array->blocks;
    while (current) {
        if (current->start == offset && !current->is_free) {
            // Found the block
            current->is_free = true;
            mem_array->used -= current->size;
            printf("Freed %zu bytes at offset %zu\n", current->size, offset);
            
            // Merge with next block if it's free
            if (current->next && current->next->is_free) {
                MemBlock* next_block = current->next;
                current->size += next_block->size;
                current->next = next_block->next;
                free(next_block);
            }
            
            // Look for previous block to merge with
            MemBlock* prev = mem_array->blocks;
            if (prev != current) {
                while (prev && prev->next != current) {
                    prev = prev->next;
                }
                
                if (prev && prev->is_free) {
                    prev->size += current->size;
                    prev->next = current->next;
                    free(current);
                }
            }
            
            return;
        }
        current = current->next;
    }
    
    printf("Error: pointer not found in memory array\n");
}

// Print the memory layout
void memory_array_print(MemoryArray* mem_array) {
    if (!mem_array) {
        return;
    }
    
    printf("\n=== Memory Array Status ===\n");
    printf("Total size: %zu bytes\n", mem_array->total_size);
    printf("Used: %zu bytes (%.2f%%)\n", mem_array->used, 
           (double)mem_array->used / mem_array->total_size * 100);
    printf("Free: %zu bytes (%.2f%%)\n", mem_array->total_size - mem_array->used,
           (double)(mem_array->total_size - mem_array->used) / mem_array->total_size * 100);
    
    printf("\nMemory blocks:\n");
    MemBlock* current = mem_array->blocks;
    int block_count = 0;
    
    while (current) {
        printf("Block %d: start=%zu, size=%zu, %s\n", 
               block_count++, current->start, current->size, 
               current->is_free ? "FREE" : "ALLOCATED");
        current = current->next;
    }
    printf("===========================\n\n");
}

// Clean up the memory array
void memory_array_destroy(MemoryArray* mem_array) {
    if (!mem_array) {
        return;
    }
    
    // Free all block structures
    MemBlock* current = mem_array->blocks;
    while (current) {
        MemBlock* next = current->next;
        free(current);
        current = next;
    }
    
    // Free memory pool and structure
    free(mem_array->memory);
    free(mem_array);
    
    printf("Memory array destroyed\n");
}

// Utility to write data to allocated memory
void memory_write(void* ptr, char value, size_t size) {
    memset(ptr, value, size);
}

// Utility to read and display memory contents
void memory_read(void* ptr, size_t size) {
    unsigned char* byte_ptr = (unsigned char*)ptr;
    printf("Memory contents at %p: ", ptr);
    
    for (size_t i = 0; i < size && i < 16; i++) {
        printf("%02X ", byte_ptr[i]);
    }
    
    if (size > 16) {
        printf("... (showing first 16 bytes only)");
    }
    
    printf("\n");
}

// Example usage
int main() {
    // Initialize a 1KB memory array
    MemoryArray* mem_array = memory_array_init(1024);
    if (!mem_array) {
        return 1;
    }
    
    // Print initial state
    memory_array_print(mem_array);
    
    // Allocate and use memory
    void* ptr1 = memory_array_alloc(mem_array, 128);
    memory_write(ptr1, 'A', 128);
    memory_read(ptr1, 128);
    
    void* ptr2 = memory_array_alloc(mem_array, 256);
    memory_write(ptr2, 'B', 256);
    memory_read(ptr2, 256);
    
    void* ptr3 = memory_array_alloc(mem_array, 64);
    memory_write(ptr3, 'C', 64);
    memory_read(ptr3, 64);
    
    // Print state after allocations
    memory_array_print(mem_array);
    
    // Free some memory
    memory_array_free(mem_array, ptr2);
    
    // Print state after free
    memory_array_print(mem_array);
    
    // Allocate again (should reuse freed space)
    void* ptr4 = memory_array_alloc(mem_array, 100);
    memory_write(ptr4, 'D', 100);
    memory_read(ptr4, 100);
    
    // Print final state
    memory_array_print(mem_array);
    
    // Clean up
    memory_array_destroy(mem_array);
    
    return 0;
}
