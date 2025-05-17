// LRU Cache

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define DEFAULT_CAPACITY 1024
#define HASH_TABLE_SIZE 1031  // A prime number for better hash distribution

// Cache entry structure (node in both hash table and doubly linked list)
typedef struct CacheEntry {
    char* key;                      // Key for lookup
    void* value;                    // Value stored in cache
    size_t value_size;              // Size of the value
    struct CacheEntry* hash_next;   // Next entry in hash table bucket
    struct CacheEntry* lru_prev;    // Previous entry in LRU list
    struct CacheEntry* lru_next;    // Next entry in LRU list
} CacheEntry;

// Cache structure
typedef struct {
    size_t capacity;                // Maximum capacity in bytes
    size_t size;                    // Current size in bytes
    size_t count;                   // Number of items in the cache
    CacheEntry* hash_table[HASH_TABLE_SIZE]; // Hash table buckets
    CacheEntry* lru_head;           // Most recently used item
    CacheEntry* lru_tail;           // Least recently used item
    void (*free_value)(void*);      // Custom function to free value
} Cache;

// Hash function for strings
unsigned int hash(const char* key) {
    unsigned int hash_value = 0;
    for (int i = 0; key[i] != '\0'; i++) {
        hash_value = hash_value * 31 + key[i];
    }
    return hash_value % HASH_TABLE_SIZE;
}

// Create a new cache with given capacity
Cache* cache_create(size_t capacity, void (*free_value)(void*)) {
    Cache* cache = (Cache*)malloc(sizeof(Cache));
    if (!cache) {
        return NULL;
    }

    cache->capacity = capacity > 0 ? capacity : DEFAULT_CAPACITY;
    cache->size = 0;
    cache->count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->free_value = free_value ? free_value : free; // Default to standard free

    // Initialize hash table buckets
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }

    return cache;
}

// Add entry to the front of LRU list (most recently used)
void move_to_front(Cache* cache, CacheEntry* entry) {
    if (!cache || !entry) {
        return;
    }

    // If already at the front, nothing to do
    if (entry == cache->lru_head) {
        return;
    }

    // Remove from current position in LRU list
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    }

    // If this was the tail, update tail
    if (cache->lru_tail == entry) {
        cache->lru_tail = entry->lru_prev;
    }

    // Put at the front of LRU list
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    
    if (cache->lru_head) {
        cache->lru_head->lru_prev = entry;
    }
    
    cache->lru_head = entry;

    // If this is the only entry, it's also the tail
    if (!cache->lru_tail) {
        cache->lru_tail = entry;
    }
}

// Remove an entry from the cache
void cache_remove_entry(Cache* cache, CacheEntry* entry) {
    if (!cache || !entry) {
        return;
    }

    // Remove from hash table
    unsigned int bucket = hash(entry->key);
    CacheEntry* current = cache->hash_table[bucket];
    CacheEntry* previous = NULL;

    while (current && current != entry) {
        previous = current;
        current = current->hash_next;
    }

    if (current) {
        if (previous) {
            previous->hash_next = current->hash_next;
        } else {
            cache->hash_table[bucket] = current->hash_next;
        }
    }

    // Remove from LRU list
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        cache->lru_head = entry->lru_next;
    }

    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        cache->lru_tail = entry->lru_prev;
    }

    // Update cache size and count
    cache->size -= entry->value_size;
    cache->count--;

    // Free resources
    cache->free_value(entry->value);
    free(entry->key);
    free(entry);
}

// Evict entries until we have enough space
void make_room(Cache* cache, size_t required_size) {
    while (cache->size + required_size > cache->capacity && cache->lru_tail) {
        CacheEntry* to_remove = cache->lru_tail;
        printf("Evicting: %s (LRU)\n", to_remove->key);
        cache_remove_entry(cache, to_remove);
    }
}

// Find an entry in the cache
CacheEntry* cache_find(Cache* cache, const char* key) {
    if (!cache || !key) {
        return NULL;
    }

    unsigned int bucket = hash(key);
    CacheEntry* entry = cache->hash_table[bucket];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
        entry = entry->hash_next;
    }

    return NULL;
}

// Get a value from the cache
void* cache_get(Cache* cache, const char* key, size_t* value_size) {
    if (!cache || !key) {
        return NULL;
    }

    CacheEntry* entry = cache_find(cache, key);
    if (!entry) {
        // Cache miss
        printf("Cache miss: %s\n", key);
        return NULL;
    }

    // Cache hit - move to front of LRU list
    printf("Cache hit: %s\n", key);
    move_to_front(cache, entry);

    if (value_size) {
        *value_size = entry->value_size;
    }

    return entry->value;
}

// Set a value in the cache
bool cache_set(Cache* cache, const char* key, void* value, size_t value_size) {
    if (!cache || !key || !value || value_size == 0) {
        return false;
    }

    // Check if this would exceed our total capacity
    if (value_size > cache->capacity) {
        printf("Value too large for cache\n");
        return false;
    }

    // Check if key already exists
    CacheEntry* entry = cache_find(cache, key);
    if (entry) {
        // Update existing entry
        printf("Updating: %s\n", key);
        
        // Remove old value size from cache size
        cache->size -= entry->value_size;
        
        // Free old value
        cache->free_value(entry->value);
        
        // Set new value
        entry->value = value;
        entry->value_size = value_size;
        
        // Update cache size
        cache->size += value_size;
        
        // Move to front of LRU list
        move_to_front(cache, entry);
    } else {
        // Make room if needed
        make_room(cache, value_size);
        
        // If still not enough room, value is too large
        if (cache->size + value_size > cache->capacity) {
            printf("Not enough space for value\n");
            return false;
        }
        
        // Create new entry
        printf("Adding: %s\n", key);
        entry = (CacheEntry*)malloc(sizeof(CacheEntry));
        if (!entry) {
            return false;
        }
        
        entry->key = strdup(key);
        entry->value = value;
        entry->value_size = value_size;
        
        // Add to hash table
        unsigned int bucket = hash(key);
        entry->hash_next = cache->hash_table[bucket];
        cache->hash_table[bucket] = entry;
        
        // Add to LRU list (at front)
        entry->lru_prev = NULL;
        entry->lru_next = cache->lru_head;
        
        if (cache->lru_head) {
            cache->lru_head->lru_prev = entry;
        } else {
            cache->lru_tail = entry;
        }
        
        cache->lru_head = entry;
        
        // Update cache size and count
        cache->size += value_size;
        cache->count++;
    }

    return true;
}

// Remove a key from the cache
bool cache_remove(Cache* cache, const char* key) {
    if (!cache || !key) {
        return false;
    }

    CacheEntry* entry = cache_find(cache, key);
    if (!entry) {
        return false;
    }

    printf("Removing: %s\n", key);
    cache_remove_entry(cache, entry);
    return true;
}

// Print cache statistics
void cache_stats(Cache* cache) {
    if (!cache) {
        return;
    }

    printf("\n===== Cache Statistics =====\n");
    printf("Capacity: %zu bytes\n", cache->capacity);
    printf("Current size: %zu bytes (%.2f%%)\n", cache->size, 
           (double)cache->size / cache->capacity * 100);
    printf("Items count: %zu\n", cache->count);
    printf("===========================\n\n");
}

// Clear all entries from the cache
void cache_clear(Cache* cache) {
    if (!cache) {
        return;
    }

    CacheEntry* current = cache->lru_head;
    while (current) {
        CacheEntry* next = current->lru_next;
        cache->free_value(current->value);
        free(current->key);
        free(current);
        current = next;
    }

    // Reset cache state
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        cache->hash_table[i] = NULL;
    }

    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->size = 0;
    cache->count = 0;

    printf("Cache cleared\n");
}

// Destroy the cache and free all resources
void cache_destroy(Cache* cache) {
    if (!cache) {
        return;
    }

    // Clear all entries
    cache_clear(cache);
    
    // Free the cache structure
    free(cache);
    
    printf("Cache destroyed\n");
}

// Custom value structure for demonstration
typedef struct {
    int id;
    char* data;
} CacheValue;

// Custom value free function
void free_cache_value(void* value) {
    if (!value) {
        return;
    }
    
    CacheValue* cache_value = (CacheValue*)value;
    free(cache_value->data);
    free(cache_value);
}

// Create a new cache value (for testing)
CacheValue* create_cache_value(int id, const char* data) {
    CacheValue* value = (CacheValue*)malloc(sizeof(CacheValue));
    if (!value) {
        return NULL;
    }
    
    value->id = id;
    value->data = strdup(data);
    
    return value;
}

// Example usage
int main() {
    // Create cache (1KB capacity)
    Cache* cache = cache_create(1024, free_cache_value);
    if (!cache) {
        printf("Failed to create cache\n");
        return 1;
    }
    
    // Insert some items
    for (int i = 0; i < 10; i++) {
        char key[16];
        char data[64];
        
        sprintf(key, "key%d", i);
        sprintf(data, "This is data for item %d", i);
        
        CacheValue* value = create_cache_value(i, data);
        if (!value) {
            printf("Failed to create value\n");
            continue;
        }
        
        size_t value_size = sizeof(CacheValue) + strlen(value->data) + 1;
        cache_set(cache, key, value, value_size);
    }
    
    // Check cache stats
    cache_stats(cache);
    
    // Retrieve some items
    for (int i = 5; i < 15; i++) {
        char key[16];
        sprintf(key, "key%d", i);
        
        size_t value_size;
        CacheValue* value = (CacheValue*)cache_get(cache, key, &value_size);
        
        if (value) {
            printf("Retrieved: id=%d, data=%s\n", value->id, value->data);
        }
    }
    
    // Add more items to trigger eviction
    for (int i = 10; i < 20; i++) {
        char key[16];
        char data[128];  // Larger data to trigger evictions
        
        sprintf(key, "key%d", i);
        sprintf(data, "This is a larger piece of data for item %d that should trigger evictions", i);
        
        CacheValue* value = create_cache_value(i, data);
        if (!value) {
            printf("Failed to create value\n");
            continue;
        }
        
        size_t value_size = sizeof(CacheValue) + strlen(value->data) + 1;
        cache_set(cache, key, value, value_size);
    }
    
    // Check cache stats after evictions
    cache_stats(cache);
    
    // Try to retrieve items that might have been evicted
    for (int i = 0; i < 10; i++) {
        char key[16];
        sprintf(key, "key%d", i);
        
        size_t value_size;
        CacheValue* value = (CacheValue*)cache_get(cache, key, &value_size);
        
        if (value) {
            printf("Retrieved: id=%d, data=%s\n", value->id, value->data);
        }
    }
    
    // Clean up
    cache_destroy(cache);
    
    return 0;
}
